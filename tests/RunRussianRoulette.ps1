param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/roulette-" + $Configuration)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$testExe = Join-Path $repository ("build/x64/" + $Configuration + "/WhiteFurnaceTests/WhiteFurnaceTests.exe")
$viewerExe = Join-Path $repository ("build/x64/" + $Configuration + "/UsdKitchen/UsdKitchen.exe")
& $testExe $outputPath
if ($LASTEXITCODE -ne 0)
{
	throw "White furnace / Russian roulette numeric checks failed."
}
Add-Type -AssemblyName System.Drawing
$summary = @()
$cases = @(
	@{ Name="short"; Options="--bounces 2 --samples 128"; Exact=$true },
	@{ Name="preview"; Options="--bounces 8 --samples 512"; Exact=$false },
	@{ Name="rough-metal"; Options="--bounces 8 --samples 512 --metallic 1 --roughness 0.6"; Exact=$false }
)
foreach ($case in $cases)
{
	$paths = @()
	foreach ($mode in @("on", "off"))
	{
		$name = $case.Name + "-" + $mode
		$png = Join-Path $outputPath ($name + ".png")
		$stderr = Join-Path $outputPath ($name + ".stderr.txt")
		$preview = Join-Path $repository "assets/PreviewRoom.ptscene"
		$arguments = '--model "{0}" --spp 4 --ambient 0 {1} --screenshot "{2}"' -f $preview, $case.Options, $png
		if ($mode -eq "off")
		{
			$arguments += ' --no-russian-roulette'
		}
		$process = Start-Process -FilePath $viewerExe -ArgumentList $arguments -WorkingDirectory $repository -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
		if (!$process.WaitForExit(55000))
		{
			Stop-Process -Id $process.Id
			throw "Screenshot timed out: $name"
		}
		if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $png))
		{
			throw ("Viewer failed: " + $name + ": " + (Get-Content -LiteralPath $stderr -Raw))
		}
		$paths += $png
		Write-Output "$name : exit 0, PNG saved"
	}
	$on = [Drawing.Bitmap]::new($paths[0])
	$off = [Drawing.Bitmap]::new($paths[1])
	try
	{
		if ($on.Width -ne $off.Width -or $on.Height -ne $off.Height)
		{
			throw "Screenshot dimensions differ."
		}
		$sums = @(0.0,0.0,0.0)
		$squares = @(0.0,0.0,0.0)
		$count = 0
		for ($y = 0; $y -lt $on.Height; $y += 2)
		{
			for ($x = [int]($on.Width * 0.6); $x -lt $on.Width; $x += 2)
			{
				$a = $on.GetPixel($x, $y)
				$b = $off.GetPixel($x, $y)
				$delta = @(([double]$a.R - $b.R), ([double]$a.G - $b.G), ([double]$a.B - $b.B))
				for ($axis = 0; $axis -lt 3; ++$axis)
				{
					$sums[$axis] += $delta[$axis]
					$squares[$axis] += $delta[$axis] * $delta[$axis]
				}
				$count++
			}
		}
		$mean = @()
		$rmse = @()
		for ($axis = 0; $axis -lt 3; ++$axis)
		{
			$mean += $sums[$axis] / $count
			$rmse += [Math]::Sqrt($squares[$axis] / $count)
			if (($case.Exact -and $rmse[$axis] -ne 0) -or [Math]::Abs($mean[$axis]) -gt 1 -or $rmse[$axis] -gt 8)
			{
				throw ("Russian roulette image comparison failed: " + $case.Name + " channel=" + $axis + " mean=" + $mean[$axis] + " RMSE=" + $rmse[$axis])
			}
		}
		$summary += [pscustomobject]@{ Case=$case.Name; MeanDifference=$mean; Rmse=$rmse; Png=$paths }
		Write-Output ($case.Name + ": image comparison passed")
	}
	finally
	{
		$on.Dispose()
		$off.Dispose()
	}
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath "images.json") -Encoding utf8
Write-Output "Russian roulette checks passed. Open the PNGs for visual inspection."
