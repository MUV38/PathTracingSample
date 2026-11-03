param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[string]$PixTool = ""
)
$ErrorActionPreference = "Stop"
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = Join-Path $repository "build/checks/pix-$Configuration"
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
if ([string]::IsNullOrEmpty($PixTool))
{
	$installation = Get-ChildItem -LiteralPath (Join-Path $env:ProgramFiles "Microsoft PIX") -Directory |
		Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "pixtool.exe") } |
		Sort-Object Name -Descending | Select-Object -First 1
	if (!$installation)
	{
		throw "PIX for Windows is required. Specify -PixTool <pixtool.exe>."
	}
	$PixTool = Join-Path $installation.FullName "pixtool.exe"
}
$commonMarkers = @("Frame / GPU", "Clear back buffer", "ImGui", "Screenshot / GPU readback")
$cases = @(
	@{Name="clear"; Project="PathTracingSample"; Options=""; Markers=@()},
	@{Name="cube"; Project="DxrCube"; Options=""; Markers=@("DXR Cube", "Trace cube", "Copy to back buffer")},
	@{Name="nee"; Project="UsdKitchen"; Options="--model assets/PreviewRoom.ptscene --no-rr --samples 128"; Markers=@("UsdKitchen", "Path tracing", "Copy to back buffer")},
	@{Name="restir-rr"; Project="UsdKitchen"; Options="--model assets/PreviewRoom.ptscene --rr --restir spatial --samples 128"; Markers=@("ReSTIR DI / candidates + temporal", "ReSTIR DI / spatial reuse", "Path tracing", "DLSS Ray Reconstruction", "Tone mapping / RR view")},
	@{Name="normal"; Project="UsdKitchen"; Options="--model assets/PreviewRoom.ptscene --rr-view normal --samples 128"; Markers=@("RR diagnostic guides", "Tone mapping / RR view")}
)
foreach ($case in $cases)
{
	$exe = Join-Path $repository "build/x64/$Configuration/$($case.Project)/$($case.Project).exe"
	$capture = Join-Path $outputPath ($case.Name + ".wpix")
	$png = Join-Path $outputPath ($case.Name + ".png")
	$stderr = Join-Path $outputPath ($case.Name + ".stderr.txt")
	$stdout = Join-Path $outputPath ($case.Name + ".stdout.txt")
	$events = Join-Path $outputPath ($case.Name + "-events.csv")
	$arguments = '{0} --pix-capture "{1}" --screenshot "{2}"' -f $case.Options, $capture, $png
	$process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $repository -WindowStyle Hidden -RedirectStandardError $stderr -RedirectStandardOutput $stdout -PassThru
	if (!$process.WaitForExit(60000))
	{
		Stop-Process -Id $process.Id
		throw "PIX capture timed out: $($case.Name)"
	}
	if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $capture) -or !(Test-Path -LiteralPath $png))
	{
		throw "PIX capture failed: $($case.Name). $(Get-Content -LiteralPath $stderr -Raw)"
	}
	& $PixTool open-capture $capture save-event-list $events run-debug-layer > (Join-Path $outputPath ($case.Name + "-analysis.log"))
	if ($LASTEXITCODE -ne 0)
	{
		throw "PIX analysis / replay failed: $($case.Name)"
	}
	$content = Get-Content -LiteralPath $events -Raw
	foreach ($marker in ($commonMarkers + $case.Markers))
	{
		if (!$content.Contains($marker))
		{
			throw "Missing PIX marker '$marker': $($case.Name)"
		}
	}
	if ($case.Name -eq "normal" -and $content.Contains("DLSS Ray Reconstruction"))
	{
		throw "Normal view must skip RR evaluation."
	}
	Write-Output "$($case.Name): capture saved, marker names verified, debug-layer replay succeeded."
}
Write-Output "Open the PNGs to verify rendering: $outputPath"
