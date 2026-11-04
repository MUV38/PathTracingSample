param(
	[ValidateSet('Debug', 'Release')]
	[string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = Join-Path $repository "build/checks/path-replay-$Configuration"
[IO.Directory]::CreateDirectory($output) | Out-Null
$bin = Join-Path $repository "build/x64/$Configuration"
$fixtures = Join-Path $output 'fixtures'
& (Join-Path $bin 'MeshLightTests/MeshLightTests.exe') $fixtures
if ($LASTEXITCODE -ne 0)
{
	throw 'MeshLightTests failed.'
}
. (Join-Path $PSScriptRoot 'GiImageMetrics.ps1')

function Capture([string]$name, [string]$model, [string]$options)
{
	$png = Join-Path $output "$name.png"
	$stderr = Join-Path $output "$name.stderr.txt"
	$arguments = '--no-pix --no-rr --model "{0}" {1} --screenshot "{2}"' -f $model,$options,$png
	$process = Start-Process -FilePath (Join-Path $bin 'UsdKitchen/UsdKitchen.exe') -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -PassThru -RedirectStandardError $stderr
	if (!$process.WaitForExit(120000))
	{
		Stop-Process -Id $process.Id
		throw "Capture timed out: $name"
	}
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $png))
	{
		throw "Capture failed: $name. $(Get-Content -LiteralPath $stderr -Raw)"
	}
	return $png
}

$preview = Join-Path $repository 'assets/PreviewRoom.ptscene'
$shadow = Join-Path $fixtures 'shadow.ptscene'
$cases = @(
	@{Name='mixed'; Model=$preview; Options='--bounces 8 --rect-light --spp 4 --samples 32'},
	@{Name='emissive'; Model=$shadow; Options='--bounces 3 --ibl-intensity 0 --light-strength 0 --ambient 0 --spp 1 --samples 16'},
	@{Name='mirror'; Model=$preview; Options='--bounces 4 --roughness 0 --metallic 1 --spp 2 --samples 16'}
)
foreach ($case in $cases)
{
	$ordinary = Capture "$($case.Name)-ordinary" $case.Model $case.Options
	$replay = Capture "$($case.Name)-replay" $case.Model "$($case.Options) --path-replay"
	$diagnostic = Capture "$($case.Name)-check" $case.Model "$($case.Options) --path-replay-check"
	$maximum = [GiImageMetrics]::MaxDifference($ordinary, $replay)
	$continued = [GiImageMetrics]::CheckReplay($diagnostic)
	if ($maximum -gt 1 -or $continued -lt 100)
	{
		throw "Replay failed for $($case.Name): max RGB difference=$maximum, multi-ray pixels=$continued"
	}
	Write-Output "$($case.Name): max RGB difference=$maximum, multi-ray pixels=$continued"
}

# Use ordinary PT as a high-sample reference. Run GPU budgets sequentially.
$options = '--ibl-intensity 0 --light-strength 0 --ambient 0'
$reference = Capture 'budget-reference' $shadow "$options --samples 4096 --spp 16"
$summary = foreach ($mode in @('off', 'temporal', 'spatial'))
{
	$csv = Join-Path $output "budget-$mode.csv"
	$png = Capture "budget-$mode" $shadow ('{0} --restir-gi {1} --samples 1 --spp 1 --gpu-budget-ms 1000 --gpu-timing "{2}"' -f $options,$mode,$csv)
	$rows = @(Import-Csv -LiteralPath $csv)
	$stats = $rows | ForEach-Object { [double]$_.gpu_ms } | Measure-Object -Sum -Maximum
	if ($rows.Count -lt 8 -or $stats.Sum -lt 1000 -or $stats.Sum -gt 1000 + 2 * $stats.Maximum)
	{
		throw "Unexpected GPU budget for $mode : $($stats.Sum) ms"
	}
	[pscustomobject]@{Mode=$mode; GpuMilliseconds=$stats.Sum; Samples=[int]$rows[-1].samples; LuminanceMse=[GiImageMetrics]::Mse($reference,$png)}
}
$summary | Export-Csv -LiteralPath (Join-Path $output 'budget.csv') -NoTypeInformation
$summary | Format-Table Mode, GpuMilliseconds, Samples, @{Name='LuminanceMse'; Expression={ '{0:E4}' -f $_.LuminanceMse }}
