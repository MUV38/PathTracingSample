param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = Join-Path $repository "build/checks/restir-gi-$Configuration"
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$binary = Join-Path $repository "build/x64/$Configuration"
$viewer = Join-Path $binary "UsdKitchen/UsdKitchen.exe"
foreach ($test in @("WhiteFurnaceTests", "MeshLightTests"))
{
	& (Join-Path $binary "$test/$test.exe") (Join-Path $outputPath $test)
	if ($LASTEXITCODE -ne 0)
	{
		throw "$test failed."
	}
}
Add-Type -AssemblyName System.Drawing
$cases = @(
	@{Name="pt"; Model="assets/PreviewRoom.ptscene"; Options="--restir-gi off"},
	@{Name="candidates"; Model="assets/PreviewRoom.ptscene"; Options="--restir-gi candidates"},
	@{Name="di-pt"; Model="assets/PreviewRoom.ptscene"; Options="--restir spatial"},
	@{Name="di-candidates"; Model="assets/PreviewRoom.ptscene"; Options="--restir spatial --restir-gi candidates"},
	@{Name="rr-pt"; Model="assets/PreviewRoom.ptscene"; Options="--rr"},
	@{Name="rr-candidates"; Model="assets/PreviewRoom.ptscene"; Options="--rr --restir-gi candidates"},
	@{Name="emissive-pt"; Model=(Join-Path $outputPath "MeshLightTests/multiple.ptscene"); Options="--ibl-intensity 0 --light-strength 0 --ambient 0 --bounces 6"},
	@{Name="emissive-candidates"; Model=(Join-Path $outputPath "MeshLightTests/multiple.ptscene"); Options="--ibl-intensity 0 --light-strength 0 --ambient 0 --bounces 6 --restir-gi candidates"},
	@{Name="mirror-valid"; Model=(Join-Path $outputPath "WhiteFurnaceTests/mirror.ptscene"); Options="--gi-view valid"},
	@{Name="zero-bounces"; Model="assets/PreviewRoom.ptscene"; Options="--bounces 0 --gi-view diffuse"}
)
foreach ($view in @("diffuse", "remainder", "radiance", "normal", "pdf", "valid", "error"))
{
	$cases += @{Name=$view; Model="assets/PreviewRoom.ptscene"; Options="--gi-view $view"}
}
$summary = @()
foreach ($case in $cases)
{
	$prefix = Join-Path $outputPath $case.Name
	$arguments = '--no-pix --model "{0}" --samples 128 --spp 4 {1} --screenshot "{2}.png"' -f $case.Model, $case.Options, $prefix
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -PassThru -RedirectStandardError "$prefix.stderr.txt"
	if (!$process.WaitForExit(60000))
	{
		Stop-Process -Id $process.Id
		throw "GI capture timed out: $($case.Name)"
	}
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath "$prefix.png"))
	{
		throw "GI capture failed: $($case.Name). $(Get-Content -LiteralPath "$prefix.stderr.txt" -Raw)"
	}
	$bitmap = [Drawing.Bitmap]::new("$prefix.png")
	try
	{
		$sum = 0.0
		$nonzero = 0
		$count = 0
		for ($y = 0; $y -lt $bitmap.Height; $y += 4)
		{
			for ($x = [int]($bitmap.Width * 0.65); $x -lt $bitmap.Width; $x += 4)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				$value = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
				$sum += $value
				if ($value -gt 3)
				{
					$nonzero++
				}
				$count++
			}
		}
		$mean = $sum / $count
		$summary += [pscustomobject]@{Name=$case.Name; Mean=$mean; Nonzero=$nonzero}
		if (($case.Name -eq "mirror-valid" -or $case.Name -eq "zero-bounces") -and $nonzero -ne 0)
		{
			throw "Invalid GI eligibility / stale candidate: $($case.Name)"
		}
		if ($case.Name -eq "error" -and $mean -gt 1.0)
		{
			throw "GI reconstruction error exceeds the x1000 display tolerance: $mean"
		}
		if ($case.Name -in @("diffuse", "radiance", "normal", "pdf", "valid") -and $nonzero -lt 50)
		{
			throw "GI diagnostic unexpectedly empty: $($case.Name)"
		}
	}
	finally
	{
		$bitmap.Dispose()
	}
}
foreach ($pair in @(@("pt", "candidates"), @("di-pt", "di-candidates"), @("rr-pt", "rr-candidates"), @("emissive-pt", "emissive-candidates")))
{
	$a = [Drawing.Bitmap]::new((Join-Path $outputPath ($pair[0] + ".png")))
	$b = [Drawing.Bitmap]::new((Join-Path $outputPath ($pair[1] + ".png")))
	try
	{
		$errorSum = 0.0
		$maximum = 0
		$count = 0
		for ($y = 0; $y -lt $a.Height; $y += 4)
		{
			for ($x = [int]($a.Width * 0.65); $x -lt $a.Width; $x += 4)
			{
				$pa = $a.GetPixel($x, $y)
				$pb = $b.GetPixel($x, $y)
				foreach ($delta in @([Math]::Abs([int]$pa.R - $pb.R), [Math]::Abs([int]$pa.G - $pb.G), [Math]::Abs([int]$pa.B - $pb.B)))
				{
					$errorSum += $delta
					$maximum = [Math]::Max($maximum, $delta)
					$count++
				}
			}
		}
		$mean = $errorSum / $count
		# The paired paths use identical RNGs. Allow 8-bit quantization and RR sensitivity.
		if ($mean -gt 0.2 -or $maximum -gt 4)
		{
			throw "GI changed total lighting: $($pair[0]), MAE=$mean, max=$maximum"
		}
		Write-Output "$($pair[0]): candidate reconstruction MAE=$mean, max=$maximum"
	}
	finally
	{
		$a.Dispose()
		$b.Dispose()
	}
}
function Get-LinearRadiance([double]$encoded)
{
	$s = [Math]::Max(0.0, [Math]::Min(254.999, $encoded)) / 255.0
	if ($s -le 0.04045)
	{
		$mapped = $s / 12.92
	}
	else
	{
		$mapped = [Math]::Pow(($s + 0.055) / 1.055, 2.4)
	}
	return $mapped / (1.0 - $mapped)
}
$full = [Drawing.Bitmap]::new((Join-Path $outputPath "candidates.png"))
$diffuse = [Drawing.Bitmap]::new((Join-Path $outputPath "diffuse.png"))
$remainder = [Drawing.Bitmap]::new((Join-Path $outputPath "remainder.png"))
try
{
	$violations = 0
	for ($y = 0; $y -lt $full.Height; $y += 4)
	{
		for ($x = [int]($full.Width * 0.65); $x -lt $full.Width; $x += 4)
		{
			$f = $full.GetPixel($x, $y)
			$d = $diffuse.GetPixel($x, $y)
			$r = $remainder.GetPixel($x, $y)
			foreach ($channel in @('R','G','B'))
			{
				# Invert sRGB + tone mapping, allowing each 8-bit quantization interval.
				$sumLow = (Get-LinearRadiance ($d.$channel - 0.5)) + (Get-LinearRadiance ($r.$channel - 0.5))
				$sumHigh = (Get-LinearRadiance ($d.$channel + 0.5)) + (Get-LinearRadiance ($r.$channel + 0.5))
				$fullLow = Get-LinearRadiance ($f.$channel - 0.5)
				$fullHigh = Get-LinearRadiance ($f.$channel + 0.5)
				if ($sumLow -gt $fullHigh + 0.0001 -or $sumHigh -lt $fullLow - 0.0001)
				{
					$violations++
				}
			}
		}
	}
	if ($violations -ne 0)
	{
		throw "Diffuse + remainder do not reconstruct total linear radiance: $violations channels"
	}
	Write-Output 'Diffuse + remainder reconstruct total radiance within quantization intervals.'
}
finally
{
	$full.Dispose()
	$diffuse.Dispose()
	$remainder.Dispose()
}
$summary | Export-Csv -LiteralPath (Join-Path $outputPath "summary.csv") -NoTypeInformation
Write-Output "GI candidate checks passed. Open PNGs: $outputPath"
