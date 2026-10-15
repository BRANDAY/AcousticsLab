<#
Installs the Steam Audio Unreal plugin (Valve, Apache 2.0) into <project>/Plugins/SteamAudio (M9).

The plugin is not committed: the release ships ~300 MB of SDK libraries for every platform. This script reproduces the
installation: download steamaudio_unreal_<version>.zip from the GitHub release (or use -Zip with a file already on disk),
check its SHA-256, extract unreal/SteamAudio without the prebuilt Binaries/ and Intermediate/ (they target an older
engine; UBT rebuilds the plugin from its sources for UE 5.8 with the project).

Usage (PowerShell, from the project root):
  powershell -ExecutionPolicy Bypass -File Tools/Plugins/install_steam_audio.ps1
  powershell -ExecutionPolicy Bypass -File Tools/Plugins/install_steam_audio.ps1 -Zip C:\Downloads\steamaudio_unreal_4.8.1.zip
#>
param(
    [string]$Version = "4.8.1",
    [string]$Sha256 = "bab5119fcd27630f8356682fafe83d96adf2a998bcbf0fcb2351245dac7e8f6b",
    [string]$Zip = ""
)
$ErrorActionPreference = "Stop"
$Project = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Target = Join-Path $Project "Plugins\SteamAudio"
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("steamaudio_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $Work | Out-Null

if (-not $Zip) {
    $Zip = Join-Path $Work "steamaudio_unreal_$Version.zip"
    $Url = "https://github.com/ValveSoftware/steam-audio/releases/download/v$Version/steamaudio_unreal_$Version.zip"
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Zip -UseBasicParsing
}
$Hash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()
if ($Hash -ne $Sha256) { throw "SHA-256 mismatch: $Hash (expected $Sha256)" }
Write-Host "SHA-256 ok"

$Extract = Join-Path $Work "x"
Expand-Archive -Path $Zip -DestinationPath $Extract
$Source = Join-Path $Extract "steamaudio_unreal\unreal\SteamAudio"
if (-not (Test-Path (Join-Path $Source "SteamAudio.uplugin"))) { throw "unreal/SteamAudio/SteamAudio.uplugin not found in the archive" }

if (Test-Path $Target) {
    $Backup = "$Target.old_" + (Get-Date -Format "yyyyMMdd_HHmmss")
    Write-Host "Existing plugin moved to $Backup"
    Move-Item $Target $Backup
}
New-Item -ItemType Directory -Force $Target | Out-Null
foreach ($Item in Get-ChildItem $Source) {
    if ($Item.Name -in @("Binaries", "Intermediate")) { continue }
    Copy-Item $Item.FullName -Destination $Target -Recurse
}
Remove-Item $Work -Recurse -Force
Write-Host "Steam Audio $Version installed into $Target (rebuild the editor target with Build.bat)"
