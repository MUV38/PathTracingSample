param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[switch]$FullBsdf
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$namePrefix = if ($FullBsdf) { 'pt-spatial' } else { 'gi-spatial' }
$output = Join-Path $repository "build/checks/$namePrefix-$Configuration"
[IO.Directory]::CreateDirectory($output) | Out-Null
$binary = Join-Path $repository "build/x64/$Configuration"
$viewer = Join-Path $binary "UsdKitchen/UsdKitchen.exe"
$fixtures = Join-Path $output 'MeshLightTests'
& (Join-Path $binary 'MeshLightTests/MeshLightTests.exe') $fixtures
if ($LASTEXITCODE -ne 0)
{
	throw 'MeshLightTests failed.'
}
. (Join-Path $PSScriptRoot 'GiImageMetrics.ps1')

function Capture([string]$name, [string]$options, [string]$model = (Join-Path $fixtures 'shadow.ptscene'))
{
	$png = Join-Path $output "$name.png"
	$stderr = Join-Path $output "$name.stderr.txt"
	$arguments = '--no-pix --light-strength 0 --ambient 0 --ibl-intensity 0 --model "{0}" {1} --screenshot "{2}"' -f $model,$options,$png
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -PassThru -RedirectStandardError $stderr
	if (!$process.WaitForExit(120000))
	{
		Stop-Process -Id $process.Id
		throw "Spatial GI capture timed out: $name"
	}
	if ($process.ExitCode -ne 0)
	{
		throw "Capture failed: $name. $(Get-Content -LiteralPath $stderr -Raw)"
	}
	Write-Output "$name : exit 0"
}

if ($FullBsdf)
{
	$results = foreach ($fixture in @('gi-colored','gi-rough-metal'))
	{
		$model = Join-Path $fixtures "$fixture.ptscene"
		$reference = Join-Path $output "$fixture-reference.png"
		Capture "$fixture-reference" '--restir-pt temporal --no-gi-history --gi-view diffuse --samples 2048 --spp 16 --seed 101' $model | Write-Host
		Capture "$fixture-ordinary" '--restir-pt off --samples 512 --spp 4' $model | Write-Host
		Capture "$fixture-spatial-total" '--restir-pt spatial --samples 512 --spp 4' $model | Write-Host
		Capture "$fixture-spatiotemporal-total" '--restir-pt spatiotemporal --samples 512 --spp 4' $model | Write-Host
		$ordinary = [GiImageMetrics]::Read((Join-Path $output "$fixture-ordinary.png"))
		foreach ($mode in @('spatial','spatiotemporal'))
		{
			$actual = [GiImageMetrics]::Read((Join-Path $output "$fixture-$mode-total.png"))
			for ($channel = 0; $channel -lt 3; ++$channel)
			{
				if ([Math]::Abs($ordinary[$channel] - $actual[$channel]) -gt 0.0003 + 0.04 * $ordinary[$channel])
				{
					throw "PT $mode energy differs from ordinary PT: $fixture, channel $channel."
				}
			}
		}
		foreach ($view in @('spatial','history'))
		{
			$name = "$fixture-selected-$view"
			Capture $name "--restir-pt spatiotemporal --gi-view $view --samples 16 --spp 1" $model | Write-Host
			$guide = [GiImageMetrics]::Read((Join-Path $output "$name.png"))
			if ($guide[4] -lt 500 -or $guide[5] -lt 20)
			{
				throw "PT spatiotemporal did not accept and select the $view proposal: $fixture."
			}
		}
		$fixedCount = 0
		foreach ($test in @('fixed','budget'))
		{
			$target = if ($test -eq 'fixed') { '--samples 128' } else { '--samples 1 --gpu-budget-ms 1000' }
			foreach ($mode in @('fresh','temporal','spatial','spatiotemporal'))
			{
				$options = if ($mode -eq 'fresh') { '--restir-pt temporal --no-gi-history' } else { "--restir-pt $mode" }
				$name = "$fixture-$test-$mode"
				$csv = Join-Path $output "$name.csv"
				Capture $name ('{0} {1} --gi-view diffuse --spp 1 --gpu-timing "{2}"' -f $options,$target,$csv) $model | Write-Host
				$rows = @(Import-Csv -LiteralPath $csv)
				$stats = $rows | ForEach-Object { [double]$_.gpu_ms } | Measure-Object -Sum -Maximum
				$count = [int]$rows[-1].samples
				if ($test -eq 'fixed')
				{
					if ($fixedCount -eq 0)
					{
						$fixedCount = $count
					}
					if ($count -lt 128 -or $count -ne $fixedCount)
					{
						throw "Sample count differs: $name"
					}
				}
				elseif ($stats.Sum -lt 1000 -or $stats.Sum -gt 1000 + 2 * $stats.Maximum)
				{
					throw "Unexpected GPU budget: $name"
				}
				$png = Join-Path $output "$name.png"
				[pscustomobject]@{Fixture=$fixture; Test=$test; Mode=$mode; Samples=$count; GpuMs=$stats.Sum; Mse=[GiImageMetrics]::Mse($reference,$png)}
			}
		}
		foreach ($ablation in @(
			@{Name='fresh'; Options='--no-gi-spatial --no-gi-history'},
			@{Name='spatial'; Options='--no-gi-history'},
			@{Name='temporal'; Options='--no-gi-spatial'}
		))
		{
			$name = "$fixture-ablation-$($ablation.Name)"
			Capture $name "--restir-pt spatiotemporal $($ablation.Options) --gi-view diffuse --samples 128 --spp 1" $model | Write-Host
			$expected = Join-Path $output "$fixture-fixed-$($ablation.Name).png"
			if ([GiImageMetrics]::MaxDifference($expected, (Join-Path $output "$name.png")) -gt 1)
			{
				throw "PT spatiotemporal ablation differs from $($ablation.Name): $fixture."
			}
		}
	}
	$csv = Join-Path $output 'camera.csv'
	Capture 'motion' ('--restir-pt spatiotemporal --gi-view history --samples 1 --spp 1 --screenshot-count 32 --gi-validation "{0}"' -f $csv)
	$rows = @(Import-Csv -LiteralPath $csv)
	$cut = $rows | Where-Object event -eq 'cut' | Select-Object -First 1
	$next = $rows | Where-Object { [int]$_.frame -eq [int]$cut.frame + 1 } | Select-Object -First 1
	if (!$cut -or $cut.history -ne '0' -or !$next -or $next.history -ne '1')
	{
		throw 'PT spatiotemporal history did not reset and resume after a camera cut.'
	}
	$index = [int]$cut.frame - ($rows.Count - 32)
	$cutImage = Join-Path $output ('motion-{0:D4}.png' -f $index)
	if ([GiImageMetrics]::Read($cutImage)[5] -ne 0)
	{
		throw 'PT spatiotemporal selected history on the camera cut.'
	}
	$results | Export-Csv -LiteralPath (Join-Path $output 'evaluation.csv') -NoTypeInformation
	$results | Format-Table Fixture, Test, Mode, Samples, GpuMs, @{Name='Mse'; Expression={ '{0:E4}' -f $_.Mse }}
	return
}

# Compare total energy on the blocker scene and RGB of the replaceable diffuse
# component on the colored fixture with a mixed GGX/diffuse primary.
$colored = Join-Path $fixtures 'gi-colored.ptscene'
foreach ($mode in @('off','temporal','spatial'))
{
	Capture "total-$mode" "--restir-gi $mode --samples 512 --spp 4"
}
Capture 'colored-reference' '--restir-gi temporal --no-gi-history --gi-view diffuse --samples 2048 --spp 16' $colored
Capture 'colored-spatial' '--restir-gi spatial --gi-view diffuse --samples 2048 --spp 16' $colored
Capture 'glossy-off' '--restir-gi off --specular 0.5 --samples 128 --spp 4'
Capture 'glossy-spatial' '--restir-gi spatial --specular 0.5 --samples 128 --spp 4'
Capture 'di-rr-temporal' '--restir-gi temporal --restir spatial --rr --samples 128 --spp 4'
Capture 'di-rr-spatial' '--restir-gi spatial --restir spatial --rr --samples 128 --spp 4'

$energy = foreach ($pair in @(@('total-off','total-temporal'), @('total-off','total-spatial'), @('colored-reference','colored-spatial'), @('glossy-off','glossy-spatial'), @('di-rr-temporal','di-rr-spatial')))
{
	$a = [GiImageMetrics]::Read((Join-Path $output ($pair[0]+'.png')))
	$b = [GiImageMetrics]::Read((Join-Path $output ($pair[1]+'.png')))
	for ($channel = 0; $channel -lt 3; $channel++)
	{
		$tolerance = 0.0003 + 0.04 * $a[$channel]
		if ($pair[0] -eq 'di-rr-temporal')
		{
			$tolerance = 0.001 + 0.08 * $a[$channel]
		}
		if ([Math]::Abs($a[$channel] - $b[$channel]) -gt $tolerance)
		{
			throw "Spatial GI energy mismatch: $($pair[0]) vs $($pair[1]), channel=$channel reference=$($a[$channel]) actual=$($b[$channel])"
		}
	}
	[pscustomobject]@{Name=$pair[1]; Reference=$a[3]; Actual=$b[3]}
}
$energy | Export-Csv -LiteralPath (Join-Path $output 'energy.csv') -NoTypeInformation

Capture 'accepted' '--restir-gi spatial --no-gi-history --gi-view spatial --samples 16'
Capture 'disabled' '--restir-gi spatial --no-gi-spatial --gi-view spatial --samples 16'
$accepted = [GiImageMetrics]::Read((Join-Path $output 'accepted.png'))
$disabled = [GiImageMetrics]::Read((Join-Path $output 'disabled.png'))
if ($accepted[4] -lt 500 -or $accepted[5] -lt 20 -or $disabled[7] -ne 0)
{
	throw 'Spatial proposal acceptance, sample selection, or disabled-state diagnostics failed.'
}

# All noise variants use the same path seeds and Lambertian component domain.
Capture 'diffuse-reference' '--restir-gi temporal --no-gi-history --gi-view diffuse --samples 2048 --spp 16'
$mse = @{}
foreach ($mode in @('fresh','temporal','spatial'))
{
	$options = '--restir-gi temporal'
	if ($mode -eq 'fresh')
	{
		$options += ' --no-gi-history'
	}
	elseif ($mode -eq 'spatial')
	{
		$options = '--restir-gi spatial'
	}
	Capture "noisy-$mode" "$options --samples 16 --spp 1 --gi-view diffuse-noisy --screenshot-count 16"
	$total = 0.0
	for ($frame = 0; $frame -lt 16; $frame++)
	{
		$path = Join-Path $output ('noisy-{0}-{1:D4}.png' -f $mode,$frame)
		$total += [GiImageMetrics]::Mse((Join-Path $output 'diffuse-reference.png'), $path)
	}
	$mse[$mode] = $total / 16
}
$mse | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'noise.json') -Encoding utf8
if ($mse.spatial -ge $mse.temporal -or $mse.temporal -ge $mse.fresh)
{
	throw "Expected lower diffuse noise at each stage: fresh=$($mse.fresh) temporal=$($mse.temporal) spatial=$($mse.spatial)"
}
Write-Output "Diffuse frame MSE: fresh=$($mse.fresh), temporal=$($mse.temporal), spatial=$($mse.spatial)"

# Run timings sequentially and omit startup samples. These are scene GPU times,
# excluding presentation, screenshot readback, and ImGui.
$timings = foreach ($mode in @('off','temporal','spatial'))
{
	$path = Join-Path $output "time-$mode.csv"
	Capture "time-$mode" ('--restir-gi {0} --samples 64 --spp 1 --gpu-timing "{1}"' -f $mode,$path) | Write-Host
	$rows = @(Import-Csv -LiteralPath $path | Select-Object -Skip 10)
	$values = @($rows | ForEach-Object { [double]$_.gpu_ms } | Sort-Object)
	if ($values.Count -lt 32 -or ($values | Where-Object { [double]::IsNaN($_) -or [double]::IsInfinity($_) -or $_ -le 0 }))
	{
		throw "Missing or invalid GPU timings: $mode"
	}
	[pscustomobject]@{Mode=$mode; Frames=$values.Count; MedianMilliseconds=$values[[int][Math]::Floor($values.Count / 2)]; MeanMilliseconds=($values | Measure-Object -Average).Average}
}
$timings | Export-Csv -LiteralPath (Join-Path $output 'timing.csv') -NoTypeInformation
$timings | Format-Table
Write-Output "Spatial GI energy, selection and noise checks passed. Open PNGs: $output"
