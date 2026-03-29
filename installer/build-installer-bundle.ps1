param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$presetDirectory = if ($Configuration -eq 'Release') { 'windows-release' } else { 'windows-debug' }
$sourceDirectory = Join-Path $repoRoot "build\\$presetDirectory\\$Configuration"
$bundleDirectory = Join-Path $repoRoot "dist\\air-companion-windows-installer-0.1.10"
$bundleZip = "$bundleDirectory.zip"

foreach ($requiredFile in @('air_companion_service.exe', 'air_companion_tray.exe', 'air_companion_updater.exe')) {
    $path = Join-Path $sourceDirectory $requiredFile
    if (-not (Test-Path $path)) {
        throw "Missing built payload file: $path"
    }
}

if (Test-Path $bundleDirectory) {
    Remove-Item -Recurse -Force $bundleDirectory
}

New-Item -ItemType Directory -Force -Path $bundleDirectory | Out-Null
Copy-Item -Path (Join-Path $sourceDirectory 'air_companion_service.exe') -Destination $bundleDirectory -Force
Copy-Item -Path (Join-Path $sourceDirectory 'air_companion_tray.exe') -Destination $bundleDirectory -Force
Copy-Item -Path (Join-Path $sourceDirectory 'air_companion_updater.exe') -Destination $bundleDirectory -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'install-companion.ps1') -Destination (Join-Path $bundleDirectory 'install-companion.ps1') -Force

if (Test-Path $bundleZip) {
    Remove-Item -Force $bundleZip
}

Compress-Archive -Path (Join-Path $bundleDirectory '*') -DestinationPath $bundleZip -Force
Write-Host $bundleZip
