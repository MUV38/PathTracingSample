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
& (Join-Path $repository ("build/x64/" + $Configuration + "/MeshLightTests/MeshLightTests.exe")) (Join-Path $outputPath "mesh")
if ($LASTEXITCODE -ne 0)
{
	throw "Mesh light fixtures failed."
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
foreach ($mode in @("ris", "temporal", "spatial"))
{
	$cases += @{ Name="mis-" + $mode; Model="rectangle-floor"; Options="--restir $mode --rect-size 2 2 --rect-radiance 4 --samples 1024" }
}
foreach ($name in @("small", "large", "mirror", "mirror-bsdf", "front", "back", "back-ibl"))
{
	$source = $cases | Where-Object Name -eq $name
	$cases += @{ Name=$name + "-spatial"; Model=$source.Model; Options=$source.Options + " --restir spatial" }
}
foreach ($mode in @("off", "spatial"))
{
	$cases += @{ Name="ibl-" + $mode; Model="rectangle-floor"; Options="--restir $mode --rect-size 2 2 --rect-radiance 4 --ibl-intensity 1 --samples 1024" }
	$cases += @{ Name="mesh-" + $mode; Model="mesh/multiple"; Options="--restir $mode --rect-position 0 2.8 0 --rect-size 1 1 --rect-radiance 4 --samples 1024" }
	$cases += @{ Name="all-" + $mode; Model="mesh/multiple"; Options="--restir $mode --rect-position 0 2.8 0 --rect-size 1 1 --rect-radiance 4 --ibl-intensity 1 --samples 1024" }
}
$multiple = "--rect-position -0.8 2.2 0 --rect-size 0.5 1 --rect-color 1 0.2 0.1 --rect-radiance 12 --add-rect-light --rect-position 0.8 1.8 0 --rect-size 1 0.5 --rect-color 0.1 0.4 1 --rect-radiance 5 --add-rect-light --no-rect-light --rect-position 0 0.5 0 --rect-radiance 10000 --rect-index 0"
$cases += @{ Name="multi-mis"; Model="rectangle-shadow"; Options=$multiple + " --spp 16 --samples 8192" }
$cases += @{ Name="multi-bsdf"; Model="rectangle-shadow"; Options=$multiple + " --spp 16 --samples 8192 --no-rect-mis" }
$cases += @{ Name="multi-spatial"; Model="rectangle-shadow"; Options=$multiple + " --restir spatial --samples 1024" }
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
		$sumGreen = 0.0
		$sumBlue = 0.0
		$count = 0
		for ($y = 0; $y -lt $bitmap.Height; $y += 2)
		{
			for ($x = [int]($bitmap.Width * 0.6); $x -lt $bitmap.Width; $x += 2)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				$sum += $pixel.R
				$sumGreen += $pixel.G
				$sumBlue += $pixel.B
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
		$summary += [pscustomobject]@{ Name=$case.Name; Mean=$sum/$count; MeanGreen=$sumGreen/$count; MeanBlue=$sumBlue/$count; LitSamples=$lit; BrightSamples=$bright; Png=$png }
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
foreach ($name in @("small", "large", "mirror", "mirror-bsdf", "front", "back", "back-ibl"))
{
	$baseline = ($summary | Where-Object Name -eq $name).Mean
	$result = ($summary | Where-Object Name -eq ($name + "-spatial")).Mean
	if ([Math]::Abs($baseline - $result) -gt 1.0)
	{
		throw ("ReSTIR rectangle mean differs: " + $name)
	}
}
foreach ($mode in @("ris", "temporal", "spatial"))
{
	$result = ($summary | Where-Object Name -eq ("mis-" + $mode)).Mean
	if ([Math]::Abs($mis - $result) -gt 1.0)
	{
		throw ("ReSTIR rectangle mean differs: " + $mode)
	}
}
foreach ($name in @("ibl", "mesh", "all"))
{
	$baseline = ($summary | Where-Object Name -eq ($name + "-off")).Mean
	$result = ($summary | Where-Object Name -eq ($name + "-spatial")).Mean
	if ([Math]::Abs($baseline - $result) -gt 1.0)
	{
		throw ("Mixed light mean differs: " + $name)
	}
}
Write-Output "ReSTIR rectangle and mixed light checks passed."
foreach ($name in @("multi-bsdf", "multi-spatial"))
{
	$baseline = $summary | Where-Object Name -eq "multi-mis"
	$result = $summary | Where-Object Name -eq $name
	foreach ($channel in @("Mean", "MeanGreen", "MeanBlue"))
	{
		if ([Math]::Abs($baseline.$channel - $result.$channel) -gt 1)
		{
			throw ("Multiple rectangle MIS / BSDF mean differs: " + $name + " " + $channel)
		}
	}
}
Write-Output "Multiple rectangle MIS / BSDF / ReSTIR and disabled-slot checks passed."
