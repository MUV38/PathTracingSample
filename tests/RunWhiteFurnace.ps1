param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = "",
	[switch]$Warp
)

$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/white-furnace-" + $Configuration)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$testExe = Join-Path $repository ("build/x64/" + $Configuration + "/WhiteFurnaceTests/WhiteFurnaceTests.exe")
$viewerExe = Join-Path $repository ("build/x64/" + $Configuration + "/UsdKitchen/UsdKitchen.exe")
if (!(Test-Path -LiteralPath $testExe) -or !(Test-Path -LiteralPath $viewerExe))
{
	throw "Build PathTracingSample.slnx in $Configuration first."
}
if ($Warp)
{
	& $testExe $outputPath --warp
}
else
{
	& $testExe $outputPath
}
if ($LASTEXITCODE -ne 0)
{
	throw "White furnace BRDF integration failed. See furnace.csv."
}

Add-Type -AssemblyName System.Drawing
$summary = @()
foreach ($fixture in @("mirror", "diffuse", "absorber"))
{
	$model = Join-Path $outputPath ($fixture + ".ptscene")
	$environment = Join-Path $outputPath "white.hdr"
	$png = Join-Path $outputPath ($fixture + ".png")
	$stderr = Join-Path $outputPath ($fixture + ".stderr.txt")
	$arguments = '--model "{0}" --ibl "{1}" --light-strength 0 --ambient 0 --ibl-intensity 1 --bounces 1 --spp 4 --samples 128 --screenshot "{2}"' -f $model, $environment, $png
	$process = Start-Process -FilePath $viewerExe -ArgumentList $arguments -WorkingDirectory $repository -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
	if (!$process.WaitForExit(45000))
	{
		Stop-Process -Id $process.Id
		throw "White furnace screenshot timed out: $fixture"
	}
	if ($process.ExitCode -ne 0)
	{
		throw "White furnace viewer failed: $fixture. $(Get-Content -LiteralPath $stderr -Raw)"
	}
	$bitmap = [Drawing.Bitmap]::new($png)
	try
	{
		$white = 0
		$black = 0
		$wrong = 0
		$errorSum = 0.0
		$squaredError = 0.0
		$channels = 0
		$startX = [Math]::Max(700, [int]($bitmap.Width * 0.6))
		for ($y = 0; $y -lt $bitmap.Height; $y += 2)
		{
			for ($x = $startX; $x -lt $bitmap.Width; $x += 2)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				foreach ($channel in @($pixel.R, $pixel.G, $pixel.B))
				{
					$delta = [double]$channel - 188
					$errorSum += $delta
					$squaredError += $delta * $delta
					$channels++
				}
				if ([Math]::Abs([int]$pixel.R - 188) -le 1 -and [Math]::Abs([int]$pixel.G - 188) -le 1 -and [Math]::Abs([int]$pixel.B - 188) -le 1)
				{
					$white++
				}
				else
				{
					$wrong++
				}
				if ($pixel.R -eq 0 -and $pixel.G -eq 0 -and $pixel.B -eq 0)
				{
					$black++
				}
			}
		}
		$meanError = $errorSum / $channels
		$rmse = [Math]::Sqrt($squaredError / $channels)
		if ($fixture -eq "absorber")
		{
			if ($black -lt 1000 -or $white -lt 1000)
			{
				throw "Absorber fixture must show a black surface against the white environment."
			}
		}
		elseif ($white -lt 1000 -or [Math]::Abs($meanError) -gt 1.0 -or $rmse -gt 5.0)
		{
			throw "$fixture must match the unit-radiance environment: mean error=$meanError, RMSE=$rmse."
		}
		$summary += [pscustomobject]@{ Fixture = $fixture; WhiteSamples = $white; BlackSamples = $black; OtherSamples = $wrong - $black; MeanError = $meanError; Rmse = $rmse; Png = $png }
		Write-Output "$fixture image passed: white=$white black=$black"
	}
	finally
	{
		$bitmap.Dispose()
	}
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath "images.json") -Encoding utf8
Write-Output "White furnace integration and image checks passed: $outputPath"
