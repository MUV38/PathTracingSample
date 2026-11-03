param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$output = Join-Path $repository "build/checks/gi-spatial-$Configuration"
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
