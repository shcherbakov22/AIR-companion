param(
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $repoRoot "dist\\air-companion-windows-0.1.1"
$bundleDirectory = Join-Path $repoRoot "dist\\air-companion-windows-installer-0.1.1"
$bundleZip = "$bundleDirectory.zip"

if (-not (Test-Path $sourceDirectory)) {
    throw "Missing source directory: $sourceDirectory"
}

if (Test-Path $bundleDirectory) {
    Remove-Item -Recurse -Force $bundleDirectory
}

New-Item -ItemType Directory -Force -Path $bundleDirectory | Out-Null
Copy-Item -Path (Join-Path $sourceDirectory '*') -Destination $bundleDirectory -Recurse -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'install-companion.ps1') -Destination (Join-Path $bundleDirectory 'install-companion.ps1') -Force

if (Test-Path $bundleZip) {
    Remove-Item -Force $bundleZip
}

Compress-Archive -Path (Join-Path $bundleDirectory '*') -DestinationPath $bundleZip -Force
Write-Host $bundleZip
