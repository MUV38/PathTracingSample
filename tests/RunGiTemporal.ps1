param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$output = Join-Path $repository "build/checks/gi-temporal-$Configuration"
[IO.Directory]::CreateDirectory($output) | Out-Null
$binary = Join-Path $repository "build/x64/$Configuration"
$viewer = Join-Path $binary "UsdKitchen/UsdKitchen.exe"
foreach ($test in @("WhiteFurnaceTests", "MeshLightTests"))
{
	& (Join-Path $binary "$test/$test.exe") (Join-Path $output $test)
	if ($LASTEXITCODE -ne 0)
	{
		throw "$test failed."
	}
}
Add-Type -AssemblyName System.Drawing
if (!("GiTemporalMetrics" -as [type]))
{
	$drawingAssemblies = @('System.Drawing')
	if ($PSVersionTable.PSEdition -eq 'Core')
	{
		$drawingAssemblies += 'System.Drawing.Primitives'
		$drawingAssemblies += [Drawing.Bitmap].Assembly.Location
		$drawingAssemblies += @(Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows*.dll' | Select-Object -ExpandProperty FullName)
	}
	Add-Type -ReferencedAssemblies $drawingAssemblies -TypeDefinition @'
using System;
using System.Drawing;
public static class GiTemporalMetrics
{
	static double Linear(byte value)
	{
		double s = value / 255.0;
		double mapped = s <= 0.04045 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4);
		return mapped / Math.Max(1e-6, 1 - mapped);
	}
	public static double[] Read(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			var result = new double[8];
			int count = 0;
			for (int y = 0; y < bitmap.Height; y += 4)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; x += 4)
				{
					Color p = bitmap.GetPixel(x, y);
					result[0] += Linear(p.R);
					result[1] += Linear(p.G);
					result[2] += Linear(p.B);
					if (p.G > 150 && p.R < 10)
					{
						result[4]++;
					}
					if (p.B > 200 && p.G > 150 && p.R < 10)
					{
						result[5]++;
					}
					if (p.R > 100 && p.G > 30 && p.G < 100 && p.B < 10)
					{
						result[6]++;
					}
					if (Math.Max(p.R, Math.Max(p.G, p.B)) > 3)
					{
						result[7]++;
					}
					count++;
				}
			}
			for (int channel = 0; channel < 3; channel++)
			{
				result[channel] /= count;
			}
			result[3] = .2126 * result[0] + .7152 * result[1] + .0722 * result[2];
			return result;
		}
	}
	public static double Mse(string referencePath, string samplePath)
	{
		using (var reference = new Bitmap(referencePath))
		using (var sample = new Bitmap(samplePath))
		{
			double error = 0;
			int count = 0;
			for (int y = 0; y < reference.Height; y += 4)
			{
				for (int x = (int)(reference.Width * .65); x < reference.Width; x += 4)
				{
					Color r = reference.GetPixel(x,y), s = sample.GetPixel(x,y);
					double a = .2126 * Linear(r.R) + .7152 * Linear(r.G) + .0722 * Linear(r.B);
					double b = .2126 * Linear(s.R) + .7152 * Linear(s.G) + .0722 * Linear(s.B);
					if (a < .0003)
					{
						continue;
					}
					error += (a-b)*(a-b);
					count++;
				}
			}
			if (count < 100)
			{
				throw new Exception("Empty diffuse-GI reference");
			}
			return error / count;
		}
	}
}
'@
}
function Capture([string]$name, [string]$options, [string]$model = "")
{
	if (!$model)
	{
		$model = Join-Path $output "MeshLightTests/shadow.ptscene"
	}
	$arguments = '--no-pix --restir-gi temporal --samples 512 --spp 4 --light-strength 0 --ambient 0 --ibl-intensity 0 --model "{0}" {1} --screenshot "{2}/{3}.png"' -f $model, $options, $output, $name
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $output "$name.stderr.txt")
	if (!$process.WaitForExit(90000))
	{
		Stop-Process -Id $process.Id
		throw "Temporal GI capture timed out: $name"
	}
	if ($process.ExitCode -ne 0)
	{
		throw "Capture failed: $name. $(Get-Content (Join-Path $output "$name.stderr.txt") -Raw)"
	}
	Write-Output "$name : exit 0"
}
Capture 'baseline' '--no-gi-history'
Capture 'temporal' ''
Capture 'di-baseline' '--no-gi-history --restir spatial'
Capture 'di-temporal' '--restir spatial'
Capture 'rr-baseline' '--no-gi-history --rr'
Capture 'rr-temporal' '--rr'
Capture 'diffuse-reference' '--no-gi-history --gi-view diffuse --samples 2048 --spp 16'
Capture 'diffuse-temporal' '--gi-view diffuse --samples 2048 --spp 16'
Capture 'colored-baseline' '--no-gi-history --gi-view diffuse --samples 2048 --spp 16' (Join-Path $output 'MeshLightTests/gi-colored.ptscene')
Capture 'colored-temporal' '--gi-view diffuse --samples 2048 --spp 16' (Join-Path $output 'MeshLightTests/gi-colored.ptscene')
Capture 'history' '--gi-view history --samples 16 --spp 1'
Capture 'history-disabled' '--gi-view history --no-gi-history --samples 16 --spp 1'
Capture 'glossy-baseline' '--no-gi-history --specular 0.5'
Capture 'glossy-temporal' '--specular 0.5'
Capture 'glossy-diffuse' '--gi-view diffuse --specular 0.5'
Capture 'black' '' (Join-Path $output 'MeshLightTests/dark.ptscene')
Capture 'mirror-diffuse' '--gi-view diffuse' (Join-Path $output 'WhiteFurnaceTests/mirror.ptscene')
$summary = @()
foreach ($pair in @(@('baseline','temporal'), @('di-baseline','di-temporal'), @('rr-baseline','rr-temporal'), @('diffuse-reference','diffuse-temporal'), @('colored-baseline','colored-temporal'), @('glossy-baseline','glossy-temporal')))
{
	$a = [GiTemporalMetrics]::Read((Join-Path $output ($pair[0]+'.png')))
	$b = [GiTemporalMetrics]::Read((Join-Path $output ($pair[1]+'.png')))
	for ($channel = 0; $channel -lt 3; $channel++)
	{
		$tolerance = 0.0003 + 0.04 * $a[$channel]
		if ($pair[0] -eq 'rr-baseline')
		{
			$tolerance = 0.001 + 0.08 * $a[$channel]
		}
		if ([Math]::Abs($a[$channel] - $b[$channel]) -gt $tolerance)
		{
			throw "Temporal GI energy mismatch: $($pair[0]), channel=$channel baseline=$($a[$channel]) temporal=$($b[$channel])"
		}
	}
	$summary += [pscustomobject]@{Name=$pair[1]; Reference=$a[3]; Temporal=$b[3]}
}
$history = [GiTemporalMetrics]::Read((Join-Path $output 'history.png'))
$disabled = [GiTemporalMetrics]::Read((Join-Path $output 'history-disabled.png'))
if ($history[4] -lt 500 -or $history[5] -lt 20 -or $disabled[4] -ne 0 -or $disabled[6] -lt 500)
{
	throw 'History acceptance / sample selection / disabled-state diagnostics failed.'
}
foreach ($name in @('black','mirror-diffuse','glossy-diffuse'))
{
	if (([GiTemporalMetrics]::Read((Join-Path $output "$name.png")))[7] -ne 0)
	{
		throw "Expected zero GI contribution: $name"
	}
}
foreach ($mode in @('fresh','reused'))
{
	$options = '--samples 16 --spp 1 --gi-view diffuse-noisy --screenshot-count 16'
	if ($mode -eq 'fresh')
	{
		$options += ' --no-gi-history'
	}
	Capture $mode $options
}
$mse = @{}
foreach ($mode in @('fresh','reused'))
{
	$total = 0.0
	for ($frame=0; $frame -lt 16; $frame++)
	{
		$path = Join-Path $output ('{0}-{1:D4}.png' -f $mode,$frame)
		$total += [GiTemporalMetrics]::Mse((Join-Path $output 'diffuse-reference.png'), $path)
	}
	$mse[$mode] = $total / 16
}
if ($mse.reused -ge $mse.fresh)
{
	throw "Single-frame diffuse GI noise did not decrease: fresh=$($mse.fresh) reused=$($mse.reused)"
}
Write-Output "Diffuse frame MSE: fresh=$($mse.fresh), reused=$($mse.reused)"
$csv = Join-Path $output 'sequence.csv'
Capture 'sequence' ('--samples 1 --spp 1 --gi-view history --screenshot-count 96 --gi-validation "{0}"' -f $csv)
$rows = @(Import-Csv -LiteralPath $csv)
$captureOffset = $rows.Count - 96
foreach ($event in @('cut','reset','light','material'))
{
	$row = $rows | Where-Object event -eq $event | Select-Object -First 1
	if (!$row -or $row.history -ne '0')
	{
		throw "History did not reset for $event"
	}
	$frameIndex = [int]$row.frame - $captureOffset
	$image = Join-Path $output ('sequence-{0:D4}.png' -f $frameIndex)
	$metrics = [GiTemporalMetrics]::Read($image)
	if ($metrics[4] -ne 0 -or $metrics[6] -lt 100)
	{
		throw "GPU history diagnostic did not reset for $event"
	}
	$next = $rows | Where-Object { [int]$_.frame -eq [int]$row.frame + 1 } | Select-Object -First 1
	if (!$next -or $next.history -ne '1')
	{
		throw "History did not resume after $event"
	}
}
$pan = @($rows | Where-Object { $_.event -eq 'pan' -and $_.history -eq '1' })
if ($pan.Count -lt 6)
{
	throw 'Slow camera motion discarded the entire GI history.'
}
$resized = $rows | Where-Object { $_.width -ne $rows[0].width -or $_.height -ne $rows[0].height } | Select-Object -First 1
if (!$resized -or $resized.history -ne '0')
{
	throw 'Resize did not invalidate GI history.'
}
$summary | Export-Csv -LiteralPath (Join-Path $output 'energy.csv') -NoTypeInformation
$mse | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'noise.json') -Encoding utf8
Write-Output "Temporal GI energy, noise, reprojection and reset checks passed. Open PNGs: $output"
