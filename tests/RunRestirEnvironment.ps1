param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/restir-ibl-" + $Configuration)
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($output) | Out-Null
$bin = Join-Path $repository ("build/x64/" + $Configuration)
$furnace = Join-Path $output "furnace"
$mesh = Join-Path $output "mesh"
& (Join-Path $bin "WhiteFurnaceTests/WhiteFurnaceTests.exe") $furnace
if ($LASTEXITCODE -ne 0)
{
	throw "Numeric integration failed."
}
& (Join-Path $bin "EnvironmentMapTests/EnvironmentMapTests.exe") $output
if ($LASTEXITCODE -ne 0)
{
	throw "Environment distribution tests failed."
}
& (Join-Path $bin "MeshLightTests/MeshLightTests.exe") $mesh
if ($LASTEXITCODE -ne 0)
{
	throw "Mesh light fixtures failed."
}
foreach ($name in @("bright", "black"))
{
	$stream = [IO.File]::Create((Join-Path $output ($name + ".hdr")))
	try
	{
		$header = [Text.Encoding]::ASCII.GetBytes("#?RADIANCE`nFORMAT=32-bit_rle_rgbe`n`n-Y 32 +X 64`n")
		$stream.Write($header, 0, $header.Length)
		for ($y = 0; $y -lt 32; ++$y)
		{
			for ($x = 0; $x -lt 64; ++$x)
			{
				$pixel = [byte[]]@(0,0,0,0)
				if ($name -eq "bright")
				{
					$pixel = [byte[]]@(164,164,164,123)
					if ($x -ge 30 -and $x -le 32 -and $y -ge 7 -and $y -le 9)
					{
						$pixel = [byte[]]@(128,128,128,134)
					}
				}
				$stream.Write($pixel, 0, 4)
			}
		}
	}
	finally
	{
		$stream.Dispose()
	}
}
$viewer = Join-Path $bin "UsdKitchen/UsdKitchen.exe"
$captures = @()
function Capture([string]$name, [string]$model, [string]$environment, [string]$mode, [int]$samples, [string]$options = "")
{
	$png = Join-Path $output ($name + ".png")
	$timing = Join-Path $output ($name + ".gpu.csv")
	$stderr = Join-Path $output ($name + ".stderr.txt")
	$arguments = '--model "{0}" --ibl "{1}" --restir {2} --samples {3} --spp 4 --bounces 1 --light-strength 0 --ambient 0 --gpu-timing "{4}" {5} --screenshot "{6}"' -f $model,$environment,$mode,$samples,$timing,$options,$png
	$watch = [Diagnostics.Stopwatch]::StartNew()
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
	if (!$process.WaitForExit(55000))
	{
		Stop-Process -Id $process.Id
		throw "Screenshot timed out: $name"
	}
	$watch.Stop()
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $png))
	{
		throw ("Viewer failed: " + $name + ": " + (Get-Content -LiteralPath $stderr -Raw))
	}
	$values = @(Import-Csv -LiteralPath $timing | Select-Object -Skip 10 | ForEach-Object { [double]$_.gpu_ms })
	if ($values.Count -eq 0 -or ($values | Where-Object { [double]::IsNaN($_) -or [double]::IsInfinity($_) -or $_ -le 0 }))
	{
		throw "Missing / invalid GPU timing: $name"
	}
	Write-Host "$name : exit 0, PNG saved"
	return [pscustomobject]@{ Name=$name; Png=$png; GpuMeanMs=($values | Measure-Object -Average).Average; GpuFrames=$values.Count; ProcessSeconds=$watch.Elapsed.TotalSeconds }
}
$plane = Join-Path $furnace "diffuse.ptscene"
$bright = Join-Path $output "bright.hdr"
$white = Join-Path $furnace "white.hdr"
$captures += Capture "mask" (Join-Path $furnace "absorber.ptscene") $bright "off" 128
foreach ($mode in @("off", "ris", "temporal", "spatial"))
{
	$captures += Capture ("bright-" + $mode) $plane $bright $mode 512
}
$captures += Capture "bright-bsdf" $plane $bright "off" 512 "--no-ibl-mis"
$captures += Capture "reference" $plane $bright "off" 4096
foreach ($model in @("diffuse", "rough-metal", "mirror"))
{
	$captures += Capture ("white-" + $model) (Join-Path $furnace ($model + ".ptscene")) $white "spatial" 512
}
$captures += Capture "black" $plane (Join-Path $output "black.hdr") "spatial" 128
foreach ($mode in @("off", "spatial"))
{
	$captures += Capture ("mixed-" + $mode) (Join-Path $mesh "multiple.ptscene") $white $mode 512
	$captures += Capture ("shadow-" + $mode) (Join-Path $mesh "shadow.ptscene") $bright $mode 512
	$captures += Capture ("rotated-" + $mode) $plane $bright $mode 512 "--ibl-rotation 75"
}
Add-Type -AssemblyName System.Drawing
function LinearRadiance([byte]$channel)
{
	$value = [double]$channel / 255
	if ($value -le 0.04045)
	{
		$value /= 12.92
	}
	else
	{
		$value = [Math]::Pow(($value + 0.055) / 1.055, 2.4)
	}
	return $value / [Math]::Max(1e-6, 1 - $value)
}
$mask = [Drawing.Bitmap]::new((Join-Path $output "mask.png"))
$reference = [Drawing.Bitmap]::new((Join-Path $output "reference.png"))
try
{
	foreach ($capture in $captures)
	{
		$bitmap = [Drawing.Bitmap]::new($capture.Png)
		try
		{
			$sum = 0.0
			$errorSquared = 0.0
			$count = 0
			$maximum = 0
			for ($y = 2; $y -lt $bitmap.Height - 2; $y += 2)
			{
				for ($x = [int]($bitmap.Width * 0.6); $x -lt $bitmap.Width - 2; $x += 2)
				{
					$pixel = $bitmap.GetPixel($x,$y)
					$maximum = [Math]::Max($maximum, $pixel.R)
					if ($mask.GetPixel($x,$y).R -ne 0 -or $mask.GetPixel($x+2,$y).R -ne 0 -or $mask.GetPixel($x-2,$y).R -ne 0 -or $mask.GetPixel($x,$y+2).R -ne 0 -or $mask.GetPixel($x,$y-2).R -ne 0)
					{
						continue
					}
					$sum += $pixel.R
					if ($capture.Name.StartsWith("bright-"))
					{
						$delta = (LinearRadiance $pixel.R) - (LinearRadiance ($reference.GetPixel($x,$y).R))
						$errorSquared += $delta * $delta
					}
					if ($capture.Name.StartsWith("white-") -and [Math]::Abs($pixel.R - 188) -gt 20)
					{
						throw ("White furnace pixel diverged: " + $capture.Name)
					}
					$count++
				}
			}
			if ($count -lt 1000 -or ($capture.Name -eq "black" -and $maximum -ne 0))
			{
				throw ("Invalid mask / black image: " + $capture.Name)
			}
			$mean = $sum / $count
			if ($capture.Name.StartsWith("white-") -and [Math]::Abs($mean - 188) -gt 1)
			{
				throw ("White furnace mean differs: " + $capture.Name)
			}
			$capture | Add-Member -NotePropertyName MeanRed -NotePropertyValue $mean
			$capture | Add-Member -NotePropertyName LinearRmse -NotePropertyValue ([Math]::Sqrt($errorSquared / $count))
		}
		finally
		{
			$bitmap.Dispose()
		}
	}
}
finally
{
	$mask.Dispose()
	$reference.Dispose()
}
foreach ($prefix in @("mixed", "shadow", "rotated"))
{
	$baseline = $captures | Where-Object Name -eq ($prefix + "-off")
	$result = $captures | Where-Object Name -eq ($prefix + "-spatial")
	if ([Math]::Abs($baseline.MeanRed - $result.MeanRed) -gt 1)
	{
		throw ("Mean differs from IBL MIS: " + $prefix)
	}
}
$baseline = $captures | Where-Object Name -eq "bright-off"
foreach ($capture in ($captures | Where-Object { $_.Name -in @("bright-ris", "bright-temporal", "bright-spatial") }))
{
	if ([Math]::Abs($baseline.MeanRed - $capture.MeanRed) -gt 1)
	{
		throw ("Environment mean differs from MIS: " + $capture.Name)
	}
}
$captures | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output "images.json") -Encoding utf8
$captures | Where-Object { $_.Name.StartsWith("bright-") } | Format-Table Name,GpuMeanMs,MeanRed,LinearRmse
Write-Output "IBL ReSTIR checks passed. Open saved PNGs for visual inspection."
