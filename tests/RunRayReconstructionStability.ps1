param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = Join-Path $repository "build/checks/rr-stability-$Configuration"
$binaryPath = Join-Path $repository "build/x64/$Configuration"
$fixtures = Join-Path $outputPath "fixtures"
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
& (Join-Path $binaryPath "WhiteFurnaceTests/WhiteFurnaceTests.exe") $fixtures
if ($LASTEXITCODE -ne 0)
{
	throw "White furnace fixture generation failed."
}
$model = Join-Path $fixtures "absorber.ptscene"
$environment = Join-Path $fixtures "white.hdr"
$png = Join-Path $outputPath "frame.png"
$stderr = Join-Path $outputPath "capture.stderr.txt"
$arguments = '--rr --model "{0}" --ibl "{1}" --light-strength 0 --ambient 0 --bounces 1 --samples 128 --spp 1 --screenshot-count 16 --screenshot "{2}"' -f $model, $environment, $png
$process = Start-Process -FilePath (Join-Path $binaryPath "UsdKitchen/UsdKitchen.exe") -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -RedirectStandardError $stderr -PassThru
if (!$process.WaitForExit(60000))
{
	Stop-Process -Id $process.Id
	throw "RR stability capture timed out."
}
if ($process.ExitCode -ne 0)
{
	throw "RR stability capture failed: $(Get-Content -LiteralPath $stderr -Raw)"
}
$log = Join-Path $binaryPath "UsdKitchen/Streamline/logs/sl.log"
Copy-Item -LiteralPath $log -Destination (Join-Path $outputPath "sl.log")
if (Select-String -LiteralPath $log -Pattern '\[error\]|NGX evaluate feature failed' -Quiet)
{
	throw "Streamline logged an error."
}
Add-Type -AssemblyName System.Drawing
if (!("RRStability.CentroidPixels" -as [type]))
{
	Add-Type -TypeDefinition @'
using System;
namespace RRStability
{
	public static class CentroidPixels
	{
		public static double[] measure(byte[] pixels, int width, int height, int stride)
		{
			if (width != 1280 || height != 720)
			{
				throw new Exception("Expected a 1280x720 capture.");
			}
			{
				double sum = 0, xsum = 0, ysum = 0;
				// Fixed 1280x720 capture; exclude ImGui and measure the black plane.
				for (int y = 0; y < height; ++y)
				{
					int row = stride >= 0 ? y * stride : (height - 1 - y) * -stride;
					for (int x = 600; x < width; ++x)
					{
						byte red = pixels[row + x * 4 + 2];
						if (red < 180)
						{
							double weight = (188.0 - red) / 188.0;
							sum += weight;
							xsum += weight * (x + 0.5);
							ysum += weight * (y + 0.5);
						}
					}
				}
				if (sum < 10000)
				{
					throw new Exception("Missing absorber silhouette.");
				}
				return new double[] {xsum / sum, ysum / sum, sum};
			}
		}
	}
}
'@
}
$frames = @()
for ($index = 0; $index -lt 16; ++$index)
{
	$path = Join-Path $outputPath ("frame-{0:D4}.png" -f $index)
	$bitmap = [Drawing.Bitmap]::new($path)
	try
	{
		$rectangle = [Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
		$bits = $bitmap.LockBits($rectangle, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
		try
		{
			$pixels = New-Object byte[] ([Math]::Abs($bits.Stride) * $bitmap.Height)
			$origin = $bits.Scan0
			if ($bits.Stride -lt 0)
			{
				$origin = [IntPtr]::Add($origin, $bits.Stride * ($bitmap.Height - 1))
			}
			[Runtime.InteropServices.Marshal]::Copy($origin, $pixels, 0, $pixels.Length)
			$value = [RRStability.CentroidPixels]::measure($pixels, $bitmap.Width, $bitmap.Height, $bits.Stride)
		}
		finally
		{
			$bitmap.UnlockBits($bits)
		}
	}
	finally
	{
		$bitmap.Dispose()
	}
	$frames += [pscustomobject]@{ Frame=$index; X=$value[0]; Y=$value[1]; Area=$value[2]; Png=$path }
}
$horizontal = $frames | Measure-Object X -Minimum -Maximum
$vertical = $frames | Measure-Object Y -Minimum -Maximum
$driftX = $horizontal.Maximum - $horizontal.Minimum
$driftY = $vertical.Maximum - $vertical.Minimum
[pscustomobject]@{ DriftX=$driftX; DriftY=$driftY; Frames=$frames } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath "stability.json") -Encoding utf8
if ($driftX -gt 0.15 -or $driftY -gt 0.15)
{
	throw "Static RR silhouette drifts with jitter: x=$driftX, y=$driftY pixels."
}
Write-Output "RR stability passed: x=$driftX, y=$driftY pixels. Open the 16 PNGs for visual inspection."
