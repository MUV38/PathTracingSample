param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = Join-Path $repository "build/checks/ray-reconstruction-$Configuration"
$binaryPath = Join-Path $repository "build/x64/$Configuration"
$viewer = Join-Path $binaryPath "UsdKitchen/UsdKitchen.exe"
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
foreach ($test in @("WhiteFurnaceTests", "MeshLightTests"))
{
	& (Join-Path $binaryPath "$test/$test.exe") (Join-Path $outputPath $test)
	if ($LASTEXITCODE -ne 0)
	{
		throw "$test failed."
	}
}
$cases = @(
	@{Name="preview-off"; Model="assets/PreviewRoom.ptscene"; Options="--no-rr"},
	@{Name="preview-rr"; Model="assets/PreviewRoom.ptscene"; Options="--rr"},
	@{Name="preview-restir"; Model="assets/PreviewRoom.ptscene"; Options="--rr --restir spatial"},
	@{Name="emissive-restir"; Model=(Join-Path $outputPath "MeshLightTests/multiple.ptscene"); Options="--rr --restir spatial --light-strength 0 --ambient 0 --ibl-intensity 0"},
	@{Name="gbuffer-bypass"; Model="assets/PreviewRoom.ptscene"; Options="--rr --restir-debug normal"}
)
foreach ($view in @("noisy", "normal", "roughness", "diffuse", "specular", "motion", "depth", "hit-distance"))
{
	$cases += @{Name="guide-$view"; Model="assets/PreviewRoom.ptscene"; Options="--rr-view $view"}
}
foreach ($model in @("mirror", "rough-metal", "diffuse", "absorber"))
{
	$environment = Join-Path $outputPath "WhiteFurnaceTests/white.hdr"
	$cases += @{Name="furnace-$model"; Model=(Join-Path $outputPath "WhiteFurnaceTests/$model.ptscene"); Options="--rr --ibl `"$environment`" --light-strength 0 --ambient 0 --ibl-intensity 1 --bounces 1"}
}
if (Test-Path -LiteralPath (Join-Path $repository "assets/local/Kitchen_set.ptscene"))
{
	$cases += @{Name="kitchen-rr"; Model="assets/local/Kitchen_set.ptscene"; Options="--rr --restir spatial"}
}
Add-Type -AssemblyName System.Drawing
$summary = @()
foreach ($case in $cases)
{
	$png = Join-Path $outputPath ($case.Name + ".png")
	$stderr = Join-Path $outputPath ($case.Name + ".stderr.txt")
	$stdout = Join-Path $outputPath ($case.Name + ".stdout.txt")
	$arguments = '--model "{0}" --samples 128 --spp 1 {1} --screenshot "{2}"' -f $case.Model, $case.Options, $png
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -RedirectStandardError $stderr -RedirectStandardOutput $stdout -WindowStyle Hidden -PassThru
	if (!$process.WaitForExit(60000))
	{
		Stop-Process -Id $process.Id
		throw "RR screenshot timed out: $($case.Name)"
	}
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $png))
	{
		throw "RR screenshot failed: $($case.Name). $(Get-Content -LiteralPath $stderr -Raw)"
	}
	$log = Join-Path $binaryPath "UsdKitchen/Streamline/logs/sl.log"
	if (Test-Path -LiteralPath $log)
	{
		Copy-Item -LiteralPath $log -Destination (Join-Path $outputPath ($case.Name + ".sl.log"))
		if (Select-String -LiteralPath $log -Pattern '\[error\]|NGX evaluate feature failed' -Quiet)
		{
			throw "Streamline logged an error: $($case.Name)"
		}
	}
	$bitmap = [Drawing.Bitmap]::new($png)
	try
	{
		$sum = 0.0
		$squaredError = 0.0
		$count = 0
		$black = 0
		for ($y = 0; $y -lt $bitmap.Height; $y += 4)
		{
			for ($x = 700; $x -lt $bitmap.Width; $x += 4)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				if ($pixel.R -eq 0 -and $pixel.G -eq 0 -and $pixel.B -eq 0)
				{
					$black++
				}
				foreach ($channel in @($pixel.R, $pixel.G, $pixel.B))
				{
					$sum += $channel
					$squaredError += ([double]$channel - 188) * ([double]$channel - 188)
					$count++
				}
			}
		}
		$mean = $sum / $count
		$rmse = [Math]::Sqrt($squaredError / $count)
		if ($case.Name.StartsWith("furnace-") -and $case.Name -ne "furnace-absorber" -and ([Math]::Abs($mean - 188) -gt 2 -or $rmse -gt 8))
		{
			throw "RR white furnace changed energy: $($case.Name), mean=$mean RMSE=$rmse"
		}
		if ($mean -lt 1 -or ($case.Name -eq "furnace-absorber" -and $black -lt 100))
		{
			throw "RR black output / absorber check failed: $($case.Name)"
		}
		$summary += [pscustomobject]@{Name=$case.Name; Mean=$mean; WhiteRmse=$rmse; BlackPixels=$black; ExitCode=$process.ExitCode; Png=$png}
		Write-Output "$($case.Name): exit 0, mean=$mean"
	}
	finally
	{
		$bitmap.Dispose()
	}
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath "images.json") -Encoding utf8
Write-Output "RR checks passed. Open the saved PNGs for visual inspection."
