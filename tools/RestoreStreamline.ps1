param(
	# Supply only after personally reviewing and accepting bin/x64/nvngx_dlss.license.txt.
	[switch]$AcceptNvidiaLicense
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$version = "2.14.16"
$checksum = "32041E51DB0CAB709A271B72A352281B690C2ABBD669A827809401A5DD23956B"
$archive = Join-Path $repository "packages/downloads/streamline-sdk-v$version.zip"
$destination = Join-Path $repository "packages/Streamline-$version"
[IO.Directory]::CreateDirectory((Split-Path $archive)) | Out-Null
if (!(Test-Path -LiteralPath $archive))
{
	Invoke-WebRequest -Uri "https://github.com/NVIDIA-RTX/Streamline/releases/download/v$version/streamline-sdk-v$version.zip" -OutFile $archive -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $checksum)
{
	throw "Streamline archive SHA256 mismatch. Remove the archive and retry: $archive"
}
if (!(Test-Path -LiteralPath (Join-Path $destination "include/sl.h")))
{
	Expand-Archive -LiteralPath $archive -DestinationPath $destination
}
if ($AcceptNvidiaLicense)
{
	$properties = Join-Path $repository "build_support/Streamline.local.props"
	[IO.File]::WriteAllText($properties, "<Project xmlns=`"http://schemas.microsoft.com/developer/msbuild/2003`">`n`t<PropertyGroup><StreamlineRuntimeEnabled>true</StreamlineRuntimeEnabled></PropertyGroup>`n</Project>`n")
	Write-Host "Runtime copying enabled for future builds. Existing consent is preserved when rerunning without this switch."
}
Write-Host "Streamline $version restored: $destination"
Write-Host "Review license.txt and bin/x64/nvngx_dlss.license.txt. Use -AcceptNvidiaLicense after personal acceptance to enable runtime copying."
