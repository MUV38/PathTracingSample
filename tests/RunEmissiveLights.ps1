param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/emissive-" + $Configuration)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$testExe = Join-Path $repository ("build/x64/" + $Configuration + "/MeshLightTests/MeshLightTests.exe")
$viewerExe = Join-Path $repository ("build/x64/" + $Configuration + "/UsdKitchen/UsdKitchen.exe")
& $testExe $outputPath
if ($LASTEXITCODE -ne 0)
{
	throw "Mesh light CPU tests failed."
}
$cases = @(
	@{ Name="mis"; Model="single"; Options="--samples 1024" },
	@{ Name="bsdf"; Model="single"; Options="--samples 1024 --no-emissive-mis" },
	@{ Name="shadow"; Model="shadow"; Options="--samples 256" },
	@{ Name="multiple"; Model="multiple"; Options="--samples 256" },
	@{ Name="multiple-bsdf"; Model="multiple"; Options="--samples 256 --no-emissive-mis" },
	@{ Name="mirror"; Model="mirror"; Options="--samples 128" },
	@{ Name="mirror-bsdf"; Model="mirror"; Options="--samples 128 --no-emissive-mis" },
	@{ Name="dark"; Model="dark"; Options="--samples 128" }
)
Add-Type -AssemblyName System.Drawing
$summary = @()
foreach ($case in $cases)
{
	$model = Join-Path $outputPath ($case.Model + ".ptscene")
	$png = Join-Path $outputPath ($case.Name + ".png")
	$stderr = Join-Path $outputPath ($case.Name + ".stderr.txt")
	$arguments = '--spp 4 --bounces 1 --light-strength 0 --ambient 0 --ibl-intensity 0 --model "{0}" {1} --screenshot "{2}"' -f $model, $case.Options, $png
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
		$sum = 0.0
		$count = 0
		$litFloor = 0
		$brightFloor = 0
		$redFloor = 0
		$blueFloor = 0
		$maximum = 0
		for ($y = 0; $y -lt $bitmap.Height; $y += 2)
		{
			for ($x = [int]($bitmap.Width * 0.6); $x -lt $bitmap.Width; $x += 2)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				$maximum = [Math]::Max($maximum, [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B)))
				if ($y -ge $bitmap.Height * 0.55)
				{
					$sum += $pixel.R
					$count++
					if ($pixel.R -gt 10 -or $pixel.B -gt 10)
					{
						$litFloor++
					}
					if ($pixel.R -gt 220)
					{
						$brightFloor++
					}
					if ($pixel.R -gt 30 -and $pixel.R -gt $pixel.B * 1.1)
					{
						$redFloor++
					}
					if ($pixel.B -gt 30 -and $pixel.B -gt $pixel.R * 1.1)
					{
						$blueFloor++
					}
				}
			}
		}
		if ($case.Name -eq "dark")
		{
			if ($maximum -ne 0)
			{
				throw "An all-dark mesh must not illuminate the scene."
			}
		}
		elseif ($litFloor -lt 200)
		{
			throw ("Missing illumination/reflection: " + $case.Name)
		}
		if ($case.Name.StartsWith("mirror") -and $brightFloor -lt 100)
		{
			throw "The mirror must reflect the emitter without MIS attenuation."
		}
		if ($case.Name.StartsWith("multiple") -and ($redFloor -lt 100 -or $blueFloor -lt 100))
		{
			throw "Both colored emitters must illuminate the floor."
		}
		$summary += [pscustomobject]@{ Name=$case.Name; FloorMean=$sum/$count; LitFloor=$litFloor; BrightFloor=$brightFloor; RedFloor=$redFloor; BlueFloor=$blueFloor; Maximum=$maximum; Png=$png }
		Write-Output ($case.Name + ": exit 0, image passed")
	}
	finally
	{
		$bitmap.Dispose()
	}
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath "images.json") -Encoding utf8
$mis = ($summary | Where-Object Name -eq "mis").FloorMean
$bsdf = ($summary | Where-Object Name -eq "bsdf").FloorMean
if ([Math]::Abs($mis - $bsdf) -gt 1.0)
{
	throw "Mesh light MIS and BSDF-only brightness disagree."
}
Write-Output "Emissive mesh light checks passed. Open the PNGs to inspect shadows and reflection."
