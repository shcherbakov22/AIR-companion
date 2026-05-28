param(
    [string]$Configuration = 'Release',
    [string]$Version = '0.1.29'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectoryCandidates = @(
    (Join-Path $repoRoot 'build\mingw-w64-release'),
    (Join-Path $repoRoot "build\windows-release\$Configuration"),
    (Join-Path $repoRoot "build\windows-debug\$Configuration")
)
$sourceDirectory = $sourceDirectoryCandidates | Where-Object { Test-Path (Join-Path $_ 'air_companion_service.exe') } | Select-Object -First 1
if (-not $sourceDirectory) {
    throw "Could not find built companion binaries. Tried: $($sourceDirectoryCandidates -join ', ')"
}

$bundleDirectory = Join-Path $repoRoot "dist\\air-companion-windows-installer-$Version"
$bundleZip = "$bundleDirectory.zip"

foreach ($requiredFile in @('air_companion_service.exe', 'air_companion_tray.exe', 'air_companion_helper.exe', 'air_companion_updater.exe')) {
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
Copy-Item -Path (Join-Path $sourceDirectory 'air_companion_helper.exe') -Destination $bundleDirectory -Force
Copy-Item -Path (Join-Path $sourceDirectory 'air_companion_updater.exe') -Destination $bundleDirectory -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'install-companion.ps1') -Destination (Join-Path $bundleDirectory 'install-companion.ps1') -Force
if (Test-Path (Join-Path $PSScriptRoot 'repair-companion-permissions.ps1')) {
    Copy-Item -Path (Join-Path $PSScriptRoot 'repair-companion-permissions.ps1') -Destination (Join-Path $bundleDirectory 'repair-companion-permissions.ps1') -Force
}

if (Test-Path $bundleZip) {
    Remove-Item -Force $bundleZip
}

Compress-Archive -Path (Join-Path $bundleDirectory '*') -DestinationPath $bundleZip -Force
Write-Host $bundleZip
