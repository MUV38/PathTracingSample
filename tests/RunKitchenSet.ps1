param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$SourceDirectory = "",
	[string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($SourceDirectory))
{
	$SourceDirectory = Join-Path $repository "assets/Source/Kitchen_set"
}
if ([string]::IsNullOrEmpty($OutputDirectory))
{
	$OutputDirectory = Join-Path $repository ("build/checks/kitchen-" + $Configuration)
}
$sourcePath = [IO.Path]::GetFullPath($SourceDirectory)
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$converter = Join-Path $repository ("build/x64/" + $Configuration + "/UsdConverter/UsdConverter.exe")
$viewer = Join-Path $repository ("build/x64/" + $Configuration + "/UsdKitchen/UsdKitchen.exe")
if (!(Test-Path -LiteralPath $converter) -or !(Test-Path -LiteralPath $viewer))
{
	throw "Build the main solution and tools/UsdConverter/UsdConverter.slnx in $Configuration first."
}
if (!(Test-Path -LiteralPath (Join-Path $sourcePath "Kitchen_set.usd")))
{
	throw "Place the locally obtained Kitchen Set (with all references) in SourceDirectory first."
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$summary = @()
foreach ($name in @("Kitchen_set", "Kitchen_set_instanced"))
{
	$source = Join-Path $sourcePath ($name + ".usd")
	if (!(Test-Path -LiteralPath $source))
	{
		Write-Output "Optional instanced stage is absent: $source"
		continue
	}
	$binary = Join-Path $outputPath ($name + ".ptscene")
	$stdout = Join-Path $outputPath ($name + ".converter.stdout.txt")
	$stderr = Join-Path $outputPath ($name + ".converter.stderr.txt")
	& $converter $source $binary > $stdout 2> $stderr
	if ($LASTEXITCODE -ne 0)
	{
		throw ("USD conversion failed: " + (Get-Content -LiteralPath $stderr -Raw))
	}
	# Inspect header metadata for the report; the viewer validates the full payload.
	$reader = [IO.BinaryReader]::new([IO.File]::OpenRead($binary))
	try
	{
		$magic = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(8))
		$version = $reader.ReadUInt32()
		$stride = $reader.ReadUInt32()
		if ($magic -ne "PTUSDM03" -or $version -ne 3 -or $stride -ne 40)
		{
			throw "Converted scene has an unsupported format."
		}
		$vertices = $reader.ReadUInt64()
		$meshes = $reader.ReadUInt64()
		$bounds = @()
		for ($axis = 0; $axis -lt 6; $axis++)
		{
			$bounds += $reader.ReadSingle()
		}
		$reader.ReadUInt64() | Out-Null
		$meshlets = $reader.ReadUInt64()
		$reader.ReadUInt64() | Out-Null
		$triangles = $reader.ReadUInt64() / 3
		$materials = $reader.ReadUInt64()
	}
	finally
	{
		$reader.Dispose()
	}
	$png = Join-Path $outputPath ($name + ".png")
	$viewStderr = Join-Path $outputPath ($name + ".viewer.stderr.txt")
	$arguments = '--model "{0}" --spp 4 --samples 128 --screenshot "{1}"' -f $binary, $png
	$process = Start-Process -FilePath $viewer -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -RedirectStandardError $viewStderr -PassThru
	if (!$process.WaitForExit(55000))
	{
		Stop-Process -Id $process.Id
		throw "Kitchen Set screenshot timed out: $name"
	}
	if ($process.ExitCode -ne 0)
	{
		throw ("Kitchen Set viewer failed: " + (Get-Content -LiteralPath $viewStderr -Raw))
	}
	if (!(Test-Path -LiteralPath $png) -or (Get-Item -LiteralPath $png).Length -eq 0)
	{
		throw "The viewer did not save a screenshot."
	}
	$hash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
	$summary += [pscustomobject]@{ Stage=$name; Configuration=$Configuration; MeshOccurrences=$meshes; Triangles=$triangles; Vertices=$vertices; Meshlets=$meshlets; Materials=$materials; BoundsMeters=$bounds; Sha256=$hash; Binary=$binary; Png=$png }
	Write-Output "$name : conversion and screenshot exited 0; triangles=$triangles meshlets=$meshlets"
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath "summary.json") -Encoding utf8
if ($summary.Count -eq 2 -and $summary[0].Sha256 -ne $summary[1].Sha256)
{
	throw "Kitchen Set regular and instanced stages produced different scenes; inspect the conversion logs."
}
Write-Output "Kitchen Set conversion/load checks passed. Open the saved PNGs to inspect the rendering."
