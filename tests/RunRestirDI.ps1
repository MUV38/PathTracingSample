param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/restir-" + $Configuration)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$viewer = Join-Path $repository ("build/x64/" + $Configuration + "/UsdKitchen/UsdKitchen.exe")
& (Join-Path $repository ("build/x64/" + $Configuration + "/WhiteFurnaceTests/WhiteFurnaceTests.exe")) $outputPath
if ($LASTEXITCODE -ne 0)
{
	throw "White furnace / reservoir numeric checks failed."
}
& (Join-Path $repository ("build/x64/" + $Configuration + "/MeshLightTests/MeshLightTests.exe")) $outputPath
if ($LASTEXITCODE -ne 0)
{
	throw "Mesh light fixtures failed."
}
$cases = @()
foreach ($mode in @("off", "ris", "temporal", "spatial"))
{
	$cases += @{ Name="multiple-" + $mode; Model="multiple"; Mode=$mode; Samples=1024; Options="" }
}
foreach ($model in @("shadow", "mirror"))
{
	foreach ($mode in @("off", "spatial"))
	{
		$cases += @{ Name=$model + "-" + $mode; Model=$model; Mode=$mode; Samples=512; Options="" }
	}
}
$cases += @{ Name="dark"; Model="dark"; Mode="spatial"; Samples=128; Options="" }
foreach ($debug in @("normal", "depth", "motion", "reservoir"))
{
	$cases += @{ Name=$debug; Model="multiple"; Mode="spatial"; Samples=128; Options="--restir-debug " + $debug }
}
Add-Type -AssemblyName System.Drawing
$summary = @()
foreach ($case in $cases)
{
	$png = Join-Path $outputPath ($case.Name + ".png")
	$stderr = Join-Path $outputPath ($case.Name + ".stderr.txt")
	$model = Join-Path $outputPath ($case.Model + ".ptscene")
	$arguments = '--model "{0}" --restir {1} --samples {2} --spp 4 --bounces 3 --light-strength 0 --ambient 0 --ibl-intensity 0 {3} --screenshot "{4}"' -f $model, $case.Mode, $case.Samples, $case.Options, $png
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
	if (!$process.WaitForExit(55000))
	{
		Stop-Process -Id $process.Id
		throw ("Timed out: " + $case.Name)
	}
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $png))
	{
		throw ("Viewer failed: " + $case.Name + ": " + (Get-Content -LiteralPath $stderr -Raw))
	}
	$bitmap = [Drawing.Bitmap]::new($png)
	try
	{
		$sum = @(0.0,0.0,0.0)
		$count = 0
		$maximum = 0
		$normalPixels = 0
		$motionPixels = 0
		$historyPixels = 0
		foreach ($y in 0..([int]($bitmap.Height / 2) - 1))
		{
			for ($x = [int]($bitmap.Width * 0.6); $x -lt $bitmap.Width; $x += 2)
			{
				$pixel = $bitmap.GetPixel($x, $y * 2)
				$maximum = [Math]::Max($maximum, [Math]::Max($pixel.R, [Math]::Max($pixel.G,$pixel.B)))
				if ($y * 2 -ge $bitmap.Height * 0.55)
				{
					$sum[0] += $pixel.R
					$sum[1] += $pixel.G
					$sum[2] += $pixel.B
					$count++
					if ($pixel.G -gt 250 -and [Math]::Abs($pixel.R - 128) -le 1 -and [Math]::Abs($pixel.B - 128) -le 1)
					{
						$normalPixels++
					}
					if ([Math]::Abs($pixel.R - 128) -le 1 -and [Math]::Abs($pixel.G - 128) -le 1 -and [Math]::Abs($pixel.B - 128) -le 1)
					{
						$motionPixels++
					}
					if ($pixel.G -gt 250 -and $pixel.R -gt 20 -and $pixel.B -eq 0)
					{
						$historyPixels++
					}
				}
			}
		}
		if (($case.Name -eq "dark" -and $maximum -ne 0) -or
			($case.Name -eq "normal" -and $normalPixels -lt 1000) -or
			($case.Name -eq "motion" -and $motionPixels -lt 1000) -or
			($case.Name -eq "reservoir" -and $historyPixels -lt 1000))
		{
			throw ("G-buffer/history/dark image check failed: " + $case.Name)
		}
		$mean = @(($sum[0]/$count), ($sum[1]/$count), ($sum[2]/$count))
		$summary += [pscustomobject]@{ Name=$case.Name; Mean=$mean; NormalPixels=$normalPixels; MotionPixels=$motionPixels; HistoryPixels=$historyPixels; Png=$png }
		Write-Output ($case.Name + ": exit 0, image saved")
	}
	finally
	{
		$bitmap.Dispose()
	}
}
foreach ($model in @("multiple", "shadow", "mirror"))
{
	$baseline = $summary | Where-Object Name -eq ($model + "-off")
	foreach ($result in ($summary | Where-Object { $_.Name.StartsWith($model + "-") -and $_.Name -ne ($model + "-off") }))
	{
		for ($axis = 0; $axis -lt 3; ++$axis)
		{
			if ([Math]::Abs($baseline.Mean[$axis] - $result.Mean[$axis]) -gt 1.0)
			{
				throw ("Mean differs from NEE baseline: " + $result.Name + " channel=" + $axis)
			}
		}
	}
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath "images.json") -Encoding utf8
Write-Output "ReSTIR DI checks passed. Open all saved PNGs for visual inspection."
