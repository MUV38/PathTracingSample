param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = "",
	[ValidateRange(100, 5000)]
	[double]$GpuBudgetMs = 500
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/multiple-rectangles-" + $Configuration)
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$fixtures = Join-Path $output "fixtures"
[IO.Directory]::CreateDirectory($output) | Out-Null
$bin = Join-Path $repository ("build/x64/" + $Configuration)
& (Join-Path $bin "WhiteFurnaceTests/WhiteFurnaceTests.exe") $fixtures
if ($LASTEXITCODE -ne 0)
{
	throw "Numeric rectangle checks failed."
}
$viewer = Join-Path $bin "UsdKitchen/UsdKitchen.exe"
function LightOptions([int]$count)
{
	$options = "--rect-light"
	$columns = if ($count -eq 32)
	{
		8
	}
	elseif ($count -eq 8)
	{
		4
	}
	else
	{
		1
	}
	$rows = $count / $columns
	for ($i = 0; $i -lt $count; ++$i)
	{
		if ($i -gt 0)
		{
			$options += " --add-rect-light"
		}
		$x = if ($columns -eq 1)
		{
			0.0
		}
		else
		{
			-1.6 + 3.2 * ($i % $columns) / ($columns - 1)
		}
		$z = if ($rows -eq 1)
		{
			0.0
		}
		else
		{
			-1.6 + 3.2 * [Math]::Floor($i / $columns) / ($rows - 1)
		}
		$color = if ($i % 2 -eq 0)
		{
			"1 0.45 0.2"
		}
		else
		{
			"0.2 0.5 1"
		}
		$options += [string]::Format([Globalization.CultureInfo]::InvariantCulture,
			" --rect-position {0} 2.2 {1} --rect-size 0.25 0.25 --rect-color {2} --rect-radiance {3}", $x, $z, $color, (64.0 / $count))
	}
	return $options
}
function Capture([string]$name, [string]$options, [string]$mode, [int]$samples, [int]$spp = 4, [switch]$Mask, [double]$BudgetMs = 0)
{
	$png = Join-Path $output ($name + ".png")
	$timing = Join-Path $output ($name + ".gpu.csv")
	$stderr = Join-Path $output ($name + ".stderr.txt")
	$model = if ($Mask)
	{
		"rectangle-shadow-mask"
	}
	else
	{
		"rectangle-shadow"
	}
	$maskOptions = if ($Mask)
	{
		"--ibl-intensity 1 --bounces 0"
	}
	else
	{
		""
	}
	if ($BudgetMs -gt 0)
	{
		$maskOptions += " --gpu-budget-ms " + $BudgetMs.ToString([Globalization.CultureInfo]::InvariantCulture)
	}
	$arguments = '--model "{0}" --ibl "{1}" --ibl-intensity 0 --bounces 1 --light-strength 0 --ambient 0 --restir {2} --samples {3} --spp {4} --gpu-timing "{5}" {6} {7} --screenshot "{8}"' -f (Join-Path $fixtures ($model + ".ptscene")),(Join-Path $fixtures "white.hdr"),$mode,$samples,$spp,$timing,$options,$maskOptions,$png
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
	if (!$process.WaitForExit(55000))
	{
		Stop-Process -Id $process.Id
		throw "Screenshot timed out: $name"
	}
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $png))
	{
		throw ("Capture failed: " + $name + ": " + (Get-Content -LiteralPath $stderr -Raw))
	}
	$rows = @(Import-Csv -LiteralPath $timing)
	$values = @($rows | ForEach-Object { [double]$_.gpu_ms })
	if ($values.Count -eq 0 -or ($values | Where-Object { [double]::IsNaN($_) -or [double]::IsInfinity($_) -or $_ -le 0 }))
	{
		throw "Missing / invalid GPU timestamps: $name"
	}
	$total = ($values | Measure-Object -Sum).Sum
	if ([int]$rows[-1].samples -lt $samples -or ($BudgetMs -gt 0 -and
		($total -lt $BudgetMs -or $total -gt $BudgetMs + 2 * ($values | Measure-Object -Maximum).Maximum + 1)))
	{
		throw "GPU budget / accumulated sample count differs: $name"
	}
	$steady = if ($values.Count -gt 10)
	{
		@($values | Select-Object -Skip 10)
	}
	else
	{
		$values
	}
	Write-Host "$name : exit 0, PNG saved"
	return [pscustomobject]@{ Name=$name; Png=$png; Samples=[int]$rows[-1].samples; RequestedSamples=$samples; GpuFrames=$values.Count; MeanGpuMs=($steady | Measure-Object -Average).Average; TotalGpuMs=$total }
}
$results = @()
foreach ($count in @(1,8,32))
{
	$options = LightOptions $count
	$null = Capture "$count-mask" $options "off" 16 4 -Mask
	$null = Capture "$count-reference" $options "off" 8192 16
	foreach ($mode in @("off", "ris", "temporal", "spatial"))
	{
		$fixed = Capture "$count-$mode-fixed" $options $mode 512
		$fixed | Add-Member -NotePropertyName Lights -NotePropertyValue $count
		$fixed | Add-Member -NotePropertyName Batch -NotePropertyValue "fixed"
		$results += $fixed
		$matched = Capture "$count-$mode-matched" $options $mode 1 4 -BudgetMs $GpuBudgetMs
		$matched | Add-Member -NotePropertyName Lights -NotePropertyValue $count
		$matched | Add-Member -NotePropertyName Batch -NotePropertyValue "matched"
		$matched | Add-Member -NotePropertyName BudgetErrorPercent -NotePropertyValue (100 * ($matched.TotalGpuMs / $GpuBudgetMs - 1))
		$results += $matched
	}
}
Add-Type -AssemblyName System.Drawing
function Linear([byte]$channel)
{
	$value = [double]$channel / 255
	$value = if ($value -le 0.04045)
	{
		$value / 12.92
	}
	else
	{
		[Math]::Pow(($value + 0.055) / 1.055, 2.4)
	}
	return $value / [Math]::Max(1e-6, 1 - $value)
}
$linearValues = New-Object double[] 256
for ($channel = 0; $channel -lt 256; ++$channel)
{
	$linearValues[$channel] = Linear $channel
}
foreach ($count in @(1,8,32))
{
	$mask = [Drawing.Bitmap]::new((Join-Path $output "$count-mask.png"))
	$reference = [Drawing.Bitmap]::new((Join-Path $output "$count-reference.png"))
	try
	{
		foreach ($result in ($results | Where-Object Lights -eq $count))
		{
			$bitmap = [Drawing.Bitmap]::new($result.Png)
			try
			{
				$sum = @(0.0,0.0,0.0)
				$referenceSum = @(0.0,0.0,0.0)
				$squared = 0.0
				$pixels = 0
				for ($y = [int]($bitmap.Height * 0.55); $y -lt $bitmap.Height - 2; $y += 2)
				{
					for ($x = [int]($bitmap.Width * 0.6); $x -lt $bitmap.Width - 2; $x += 2)
					{
						if ($mask.GetPixel($x,$y).R -ne 0 -or $mask.GetPixel($x+2,$y).R -ne 0 -or $mask.GetPixel($x-2,$y).R -ne 0 -or $mask.GetPixel($x,$y+2).R -ne 0 -or $mask.GetPixel($x,$y-2).R -ne 0)
						{
							continue
						}
						$pixel = $bitmap.GetPixel($x,$y)
						$expected = $reference.GetPixel($x,$y)
						$rgb = @($pixel.R,$pixel.G,$pixel.B)
						$ref = @($expected.R,$expected.G,$expected.B)
						for ($axis = 0; $axis -lt 3; ++$axis)
						{
							$sum[$axis] += $rgb[$axis]
							$referenceSum[$axis] += $ref[$axis]
							$delta = $linearValues[$rgb[$axis]] - $linearValues[$ref[$axis]]
							$squared += $delta * $delta
						}
						$pixels++
					}
				}
				if ($pixels -lt 1000)
				{
					throw "Invalid surface mask: $count"
				}
				$means = @($sum | ForEach-Object { $_ / $pixels })
				for ($axis = 0; $axis -lt 3; ++$axis)
				{
					if ([Math]::Abs($means[$axis] - $referenceSum[$axis] / $pixels) -gt 1)
					{
						throw ("Mean differs from reference: " + $result.Name)
					}
				}
				$result | Add-Member -NotePropertyName MeanRGB -NotePropertyValue $means
				$result | Add-Member -NotePropertyName LinearRmse -NotePropertyValue ([Math]::Sqrt($squared / (3 * $pixels)))
				$result | Add-Member -NotePropertyName Pixels -NotePropertyValue $pixels
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
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output "images.json") -Encoding utf8
$results | Format-Table Name,Samples,MeanGpuMs,TotalGpuMs,LinearRmse,BudgetErrorPercent
Write-Output "Multiple rectangle checks passed. Open all saved PNGs for visual inspection."
