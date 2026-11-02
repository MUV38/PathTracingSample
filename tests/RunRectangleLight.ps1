param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/rectangle-" + $Configuration)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$testExe = Join-Path $repository ("build/x64/" + $Configuration + "/WhiteFurnaceTests/WhiteFurnaceTests.exe")
$viewerExe = Join-Path $repository ("build/x64/" + $Configuration + "/UsdKitchen/UsdKitchen.exe")
& $testExe $outputPath
if ($LASTEXITCODE -ne 0)
{
	throw "Rectangle integration failed. See furnace.csv."
}
$cases = @(
	@{ Name="small"; Model="rectangle-shadow"; Options="--rect-size 0.2 0.2 --rect-radiance 400 --samples 256" },
	@{ Name="large"; Model="rectangle-shadow"; Options="--rect-size 2 2 --rect-radiance 4 --samples 256" },
	@{ Name="mis"; Model="rectangle-floor"; Options="--rect-size 2 2 --rect-radiance 4 --samples 1024" },
	@{ Name="bsdf"; Model="rectangle-floor"; Options="--rect-size 2 2 --rect-radiance 4 --samples 1024 --no-rect-mis" },
	@{ Name="mirror"; Model="rectangle-mirror"; Options="--rect-size 2 2 --rect-radiance 4 --samples 128" },
	@{ Name="mirror-bsdf"; Model="rectangle-mirror"; Options="--rect-size 2 2 --rect-radiance 4 --samples 128 --no-rect-mis" },
	@{ Name="front"; Model="rectangle-floor"; Options="--rect-size 1 1 --rect-radiance 4 --rect-orientation 0 90 --samples 128" },
	@{ Name="back"; Model="rectangle-floor"; Options="--rect-size 1 1 --rect-radiance 4 --samples 128" },
	@{ Name="back-ibl"; Model="rectangle-floor"; Options="--rect-size 1 1 --rect-radiance 0 --ibl-intensity 1 --bounces 0 --samples 128" }
)
Add-Type -AssemblyName System.Drawing
$summary = @()
foreach ($case in $cases)
{
	$model = Join-Path $outputPath ($case.Model + ".ptscene")
	$png = Join-Path $outputPath ($case.Name + ".png")
	$stderr = Join-Path $outputPath ($case.Name + ".stderr.txt")
	$environment = Join-Path $outputPath "white.hdr"
	$arguments = '--rect-light --rect-position 0 2 0 --rect-color 1 1 1 --spp 4 --bounces 1 --light-strength 0 --ambient 0 --ibl-intensity 0 --model "{0}" --ibl "{1}" {2} --screenshot "{3}"' -f $model, $environment, $case.Options, $png
	$process = Start-Process -FilePath $viewerExe -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -RedirectStandardError $stderr -PassThru
	if (!$process.WaitForExit(45000))
	{
		Stop-Process -Id $process.Id
		throw ("Screenshot timed out: " + $case.Name)
	}
	if ($process.ExitCode -ne 0)
	{
		throw ("Viewer failed: " + $case.Name + ": " + (Get-Content -LiteralPath $stderr -Raw))
	}
	$bitmap = [Drawing.Bitmap]::new($png)
	try
	{
		$bright = 0
		$lit = 0
		$sum = 0.0
		$count = 0
		for ($y = 0; $y -lt $bitmap.Height; $y += 2)
		{
			for ($x = [int]($bitmap.Width * 0.6); $x -lt $bitmap.Width; $x += 2)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				$sum += $pixel.R
				$count++
				if ($pixel.R -gt 10)
				{
					$lit++
				}
				if ($pixel.R -gt 220)
				{
					$bright++
				}
			}
		}
		if ($lit -lt 500)
		{
			throw ("No visible result: " + $case.Name)
		}
		if (($case.Name -eq "front" -or $case.Name.StartsWith("mirror")) -and $bright -lt 200)
		{
			throw "Emitter or its mirror reflection is missing."
		}
		if ($case.Name -eq "back" -and $bright -ne 0)
		{
			throw "The back of the rectangle must not emit."
		}
		$summary += [pscustomobject]@{ Name=$case.Name; Mean=$sum/$count; LitSamples=$lit; BrightSamples=$bright; Png=$png }
		Write-Output ($case.Name + ": exit 0, image passed")
	}
	finally
	{
		$bitmap.Dispose()
	}
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath "images.json") -Encoding utf8
$mis = ($summary | Where-Object Name -eq "mis").Mean
$bsdf = ($summary | Where-Object Name -eq "bsdf").Mean
if ([Math]::Abs($mis - $bsdf) -gt 1.0)
{
	throw "MIS and BSDF-only images disagree in mean brightness."
}
Write-Output "Rectangle light checks passed. Open the PNGs to inspect soft shadows and occlusion."
