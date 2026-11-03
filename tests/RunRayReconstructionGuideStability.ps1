param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = Join-Path $repository "build/checks/rr-guides-$Configuration"
$viewer = Join-Path $repository "build/x64/$Configuration/UsdKitchen/UsdKitchen.exe"
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
Add-Type -AssemblyName System.Drawing
if (!("RRGuidePixels" -as [type]))
{
	Add-Type -TypeDefinition @'
using System;
public static class RRGuidePixels
{
	public static int changed(byte[] reference, byte[] current, int width, int height, int stride)
	{
		int count = 0;
		for (int y = 0; y < height; ++y)
		{
			for (int x = 600; x < width; ++x)
			{
				int offset = y * stride + x * 4;
				if (reference[offset] != current[offset] || reference[offset + 1] != current[offset + 1] || reference[offset + 2] != current[offset + 2])
				{
					++count;
				}
			}
		}
		return count;
	}
}
'@
}
function readPixels([string]$path)
{
	$bitmap = [Drawing.Bitmap]::new($path)
	try
	{
		if ($bitmap.Width -ne 1280 -or $bitmap.Height -ne 720)
		{
			throw "Expected a 1280x720 capture."
		}
		$rectangle = [Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
		$bits = $bitmap.LockBits($rectangle, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
		try
		{
			if ($bits.Stride -ne 1280 * 4)
			{
				throw "Unexpected capture stride."
			}
			$pixels = New-Object byte[] ($bits.Stride * $bitmap.Height)
			[Runtime.InteropServices.Marshal]::Copy($bits.Scan0, $pixels, 0, $pixels.Length)
			return ,$pixels
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
}
$cases = @()
foreach ($view in @("normal", "roughness", "diffuse", "specular", "motion", "depth", "hit-distance"))
{
	$cases += @{ Name=$view; View=$view; Model="assets/PreviewRoom.ptscene" }
}
if (Test-Path -LiteralPath (Join-Path $repository "assets/local/Kitchen_set.ptscene"))
{
	$cases += @{ Name="kitchen-normal"; View="normal"; Model="assets/local/Kitchen_set.ptscene" }
}
$results = @()
foreach ($case in $cases)
{
	$directory = Join-Path $outputPath $case.Name
	[IO.Directory]::CreateDirectory($directory) | Out-Null
	$png = Join-Path $directory "frame.png"
	$stderr = Join-Path $directory "capture.stderr.txt"
	$log = Join-Path $repository "build/x64/$Configuration/UsdKitchen/Streamline/logs/sl.log"
	$arguments = '--model "{0}" --rr-view {1} --samples 8 --spp 1 --screenshot-count 16 --screenshot "{2}"' -f $case.Model, $case.View, $png
	$captureStarted = [DateTime]::UtcNow
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -RedirectStandardError $stderr -PassThru
	if (!$process.WaitForExit(60000))
	{
		Stop-Process -Id $process.Id
		throw "Guide capture timed out: $($case.Name)"
	}
	if ($process.ExitCode -ne 0)
	{
		throw "Guide capture failed: $($case.Name). $(Get-Content -LiteralPath $stderr -Raw)"
	}
	# The SDK may leave the previous log untouched when RR is not evaluated.
	if ((Test-Path -LiteralPath $log) -and (Get-Item -LiteralPath $log).LastWriteTimeUtc -ge $captureStarted)
	{
		Copy-Item -LiteralPath $log -Destination (Join-Path $directory "sl.log")
		if (Select-String -LiteralPath $log -Pattern '\[error\]|NGX evaluate feature failed|Created DLSSDContext feature' -Quiet)
		{
			throw "Guide rendering must succeed without evaluating RR: $($case.Name)"
		}
	}
	else
	{
		"No fresh Streamline file log was emitted for this guide capture." | Set-Content -LiteralPath (Join-Path $directory "sl.log")
	}
	$reference = readPixels (Join-Path $directory "frame-0000.png")
	$maximumChanged = 0
	for ($index = 1; $index -lt 16; ++$index)
	{
		$pixels = readPixels (Join-Path $directory ("frame-{0:D4}.png" -f $index))
		$maximumChanged = [Math]::Max($maximumChanged, [RRGuidePixels]::changed($reference, $pixels, 1280, 720, 1280 * 4))
	}
	$results += [pscustomobject]@{ View=$case.Name; MaximumChangedPixels=$maximumChanged; Directory=$directory }
	if ($maximumChanged -ne 0)
	{
		throw "Static guide changed between frames: $($case.Name), pixels=$maximumChanged"
	}
	Write-Output "$($case.Name): 16 frames, changed pixels=0, no RR evaluation."
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath "stability.json") -Encoding utf8
