param(
	[ValidateSet('Debug', 'Release')]
	[string]$Configuration = 'Debug',
	[switch]$CompareCost
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testName = 'pt-hybrid'
if ($CompareCost)
{
	$testName = 'pt-hybrid-cost'
}
$output = Join-Path $repository "build/checks/$testName-$Configuration"
[IO.Directory]::CreateDirectory($output) | Out-Null
$bin = Join-Path $repository "build/x64/$Configuration"
$fixtures = Join-Path $output 'fixtures'
& (Join-Path $bin 'MeshLightTests/MeshLightTests.exe') $fixtures
if ($LASTEXITCODE -ne 0)
{
	throw 'MeshLightTests failed.'
}
. (Join-Path $PSScriptRoot 'GiImageMetrics.ps1')

function Capture([string]$name, [string]$fixture, [string]$options)
{
	$png = Join-Path $output "$name.png"
	$stderr = Join-Path $output "$name.stderr.txt"
	$model = Join-Path $fixtures "pt-hybrid-$fixture.ptscene"
	$arguments = '--no-pix --no-rr --light-strength 0 --ambient 0 --ibl-intensity 0 --model "{0}" {1} --screenshot "{2}"' -f $model,$options,$png
	$process = Start-Process -FilePath (Join-Path $bin 'UsdKitchen/UsdKitchen.exe') -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -PassThru -RedirectStandardError $stderr
	if (!$process.WaitForExit(120000))
	{
		Stop-Process -Id $process.Id
		throw "Capture timed out: $name"
	}
	if ($process.ExitCode -ne 0)
	{
		throw "Capture failed: $name. $(Get-Content -LiteralPath $stderr -Raw)"
	}
	Write-Host "$name : exit 0"
	return $png
}

if ($CompareCost)
{
	$work = foreach ($fixture in @('mirror','glossy','black-suffix'))
	{
		$model = $fixture
		$options = '--restir-pt hybrid'
		if ($fixture -eq 'black-suffix')
		{
			$model = 'mirror'
			$options += ' --bounces 2 --no-emissive-mis'
		}
		$before = Capture "$fixture-unoptimized" $model "$options --hybrid-unoptimized --samples 64 --spp 4"
		$after = Capture "$fixture-optimized" $model "$options --samples 64 --spp 4"
		if ([GiImageMetrics]::MaxDifference($before, $after) -ne 0)
		{
			throw "RGB differs with the optimization: $fixture"
		}
		$before = Capture "$fixture-work-before" $model "$options --hybrid-unoptimized --gi-view hybrid-cost --samples 16"
		$after = Capture "$fixture-work-after" $model "$options --gi-view hybrid-cost --samples 16"
		$a = [GiImageMetrics]::HybridWork($before)
		$b = [GiImageMetrics]::HybridWork($after)
		if ($a[0] -eq 0 -or $b[0] -gt $a[0] -or $b[1] -gt $a[1] -or $a[2] -ne $b[2])
		{
			throw "Unexpected work or selected history count: $fixture"
		}
		if ($fixture -eq 'black-suffix' -and ($b[0] -ne 0 -or $a[1] -eq 0 -or $b[1] -ne 0))
		{
			throw 'Expected to skip all replay and visibility work for black suffixes.'
		}
		[pscustomobject]@{Fixture=$fixture; ReplayBefore=$a[0]; ReplayAfter=$b[0]; VisibilityBefore=$a[1]; VisibilityAfter=$b[1]; Selected=$b[2]}
	}
	$work | Export-Csv -LiteralPath (Join-Path $output 'work.csv') -NoTypeInformation
	$work | Format-Table
	$null = Capture 'motion-before' 'mirror' ('--restir-pt hybrid --hybrid-unoptimized --samples 1 --screenshot-count 32 --gi-validation "{0}/motion-before.csv"' -f $output)
	$null = Capture 'motion-after' 'mirror' ('--restir-pt hybrid --samples 1 --screenshot-count 32 --gi-validation "{0}/motion-after.csv"' -f $output)
	for ($frame = 0; $frame -lt 32; ++$frame)
	{
		$before = Join-Path $output ('motion-before-{0:D4}.png' -f $frame)
		$after = Join-Path $output ('motion-after-{0:D4}.png' -f $frame)
		if ([GiImageMetrics]::MaxDifference($before, $after) -ne 0)
		{
			throw "RGB differs during camera motion at frame $frame"
		}
	}
	Write-Host 'All 32 camera-motion frames match in the RGB ROI.'

	$comparison = foreach ($fixture in @('mirror','glossy'))
	{
		$reference = Capture "$fixture-reference" $fixture '--samples 2048 --spp 16'
		foreach ($mode in @('off','unoptimized','optimized'))
		{
			$csv = Join-Path $output "$fixture-budget-$mode.csv"
			$options = '--restir-pt hybrid'
			if ($mode -eq 'off')
			{
				$options = '--restir-pt off'
			}
			elseif ($mode -eq 'unoptimized')
			{
				$options += ' --hybrid-unoptimized'
			}
			$png = Capture "$fixture-budget-$mode" $fixture ('{0} --samples 1 --gpu-budget-ms 1000 --gpu-timing "{1}"' -f $options,$csv)
			$rows = @(Import-Csv -LiteralPath $csv)
			$stats = $rows | ForEach-Object { [double]$_.gpu_ms } | Measure-Object -Sum -Maximum
			if ($rows.Count -lt 8 -or $stats.Sum -lt 1000 -or $stats.Sum -gt 1000 + 2 * $stats.Maximum)
			{
				throw "Unexpected GPU budget for $fixture / $mode"
			}
			[pscustomobject]@{Fixture=$fixture; Mode=$mode; GpuMilliseconds=$stats.Sum; Samples=[int]$rows[-1].samples; LuminanceMse=[GiImageMetrics]::Mse($reference,$png)}
		}
	}
	$comparison | Export-Csv -LiteralPath (Join-Path $output 'budget.csv') -NoTypeInformation
	$comparison | Format-Table Fixture, Mode, GpuMilliseconds, Samples, @{Name='LuminanceMse'; Expression={ '{0:E4}' -f $_.LuminanceMse }}
	return
}

$energy = foreach ($fixture in @('mirror','glossy'))
{
	$options = '--samples 2048 --spp 16'
	$ordinary = Capture "$fixture-ordinary" $fixture $options
	$reference = [GiImageMetrics]::Read($ordinary)
	foreach ($mode in @('fresh','hybrid'))
	{
		$history = ''
		if ($mode -eq 'fresh')
		{
			$history = '--no-gi-history'
		}
		$png = Capture "$fixture-$mode" $fixture "$options --restir-pt hybrid $history"
		$actual = [GiImageMetrics]::Read($png)
		for ($channel = 0; $channel -lt 3; $channel++)
		{
			if ([Math]::Abs($actual[$channel] - $reference[$channel]) -gt (0.0003 + .04 * $reference[$channel]))
			{
				throw "$fixture $mode differs from PT on RGB channel $channel"
			}
		}
		[pscustomobject]@{Fixture=$fixture; Mode=$mode; Reference=$reference[3]; Luminance=$actual[3]}
	}
	$guide = Capture "$fixture-history" $fixture '--restir-pt hybrid --gi-view history --samples 16'
	if (([GiImageMetrics]::Read($guide))[5] -lt 20)
	{
		throw "$fixture did not select enough replayed paths"
	}
}
$energy | Export-Csv -LiteralPath (Join-Path $output 'energy.csv') -NoTypeInformation

$motion = foreach ($mode in @('off','fresh','hybrid'))
{
	$csv = Join-Path $output "motion-$mode.csv"
	$modeOptions = "--restir-pt $mode"
	if ($mode -eq 'fresh')
	{
		$modeOptions = '--restir-pt hybrid --no-gi-history'
	}
	$null = Capture "motion-$mode" 'mirror' ('{0} --samples 1 --screenshot-count 32 --gi-validation "{1}"' -f $modeOptions,$csv)
	$rows = @(Import-Csv -LiteralPath $csv)
	$offset = $rows.Count - 32
	$cut = $rows | Where-Object event -eq 'cut' | Select-Object -First 1
	$next = $rows | Where-Object { [int]$_.frame -eq [int]$cut.frame + 1 } | Select-Object -First 1
	if (!$cut -or $cut.history -ne '0' -or ($mode -eq 'hybrid' -and (!$next -or $next.history -ne '1')))
	{
		throw "Unexpected history state across the camera cut: $mode"
	}
	$pan = @($rows | Where-Object event -eq 'pan')
	if ($pan.Count -lt 8 -or ($mode -eq 'hybrid' -and @($pan | Where-Object history -eq '1').Count -lt 6))
	{
		throw "Unexpected history state during the pan: $mode"
	}
	$delta = for ($i = 1; $i -lt $pan.Count; $i++)
	{
		$a = Join-Path $output ('motion-{0}-{1:D4}.png' -f $mode,([int]$pan[$i-1].frame - $offset))
		$b = Join-Path $output ('motion-{0}-{1:D4}.png' -f $mode,([int]$pan[$i].frame - $offset))
		[GiImageMetrics]::Mse($a,$b)
	}
	[pscustomobject]@{Mode=$mode; PanFrameDifferenceMse=($delta | Measure-Object -Average).Average}
}
$motion | Export-Csv -LiteralPath (Join-Path $output 'motion.csv') -NoTypeInformation

$reference = Join-Path $output 'mirror-ordinary.png'
$budget = foreach ($mode in @('off','hybrid'))
{
	$csv = Join-Path $output "budget-$mode.csv"
	$png = Capture "budget-$mode" 'mirror' ('--restir-pt {0} --samples 1 --gpu-budget-ms 1000 --gpu-timing "{1}"' -f $mode,$csv)
	$rows = @(Import-Csv -LiteralPath $csv)
	$stats = $rows | ForEach-Object { [double]$_.gpu_ms } | Measure-Object -Sum -Maximum
	if ($rows.Count -lt 8 -or $stats.Sum -lt 1000 -or $stats.Sum -gt 1000 + 2 * $stats.Maximum)
	{
		throw "Unexpected GPU budget for $mode"
	}
	[pscustomobject]@{Mode=$mode; GpuMilliseconds=$stats.Sum; Samples=[int]$rows[-1].samples; LuminanceMse=[GiImageMetrics]::Mse($reference,$png)}
}
$budget | Export-Csv -LiteralPath (Join-Path $output 'budget.csv') -NoTypeInformation
$budget | Format-Table Mode, GpuMilliseconds, Samples, @{Name='LuminanceMse'; Expression={ '{0:E4}' -f $_.LuminanceMse }}
