param(
    [string]$InstallDirectory = "$env:ProgramFiles\\AIR Companion",
    [string]$EnrollmentUrl = "https://192.168.11.228/student/companion/enroll"
)

$ErrorActionPreference = 'Stop'

$bundleDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$serviceBinary = Join-Path $bundleDirectory 'air_companion_service.exe'
$utilityBinary = Join-Path $bundleDirectory 'air_companion_tray.exe'
$updaterBinary = Join-Path $bundleDirectory 'air_companion_updater.exe'
$serviceName = 'AIRCompanion'

foreach ($path in @($serviceBinary, $utilityBinary, $updaterBinary)) {
    if (-not (Test-Path $path)) {
        throw "Missing installer payload file: $path"
    }
}

New-Item -ItemType Directory -Force -Path $InstallDirectory | Out-Null

try {
    Stop-Service -Name $serviceName -ErrorAction Stop | Out-Null
    Start-Sleep -Seconds 2
} catch {
}

Copy-Item -Path $serviceBinary -Destination (Join-Path $InstallDirectory 'air_companion_service.exe') -Force
Copy-Item -Path $utilityBinary -Destination (Join-Path $InstallDirectory 'air_companion_tray.exe') -Force
Copy-Item -Path $updaterBinary -Destination (Join-Path $InstallDirectory 'air_companion_updater.exe') -Force

$installedServiceBinary = Join-Path $InstallDirectory 'air_companion_service.exe'
$serviceExists = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -eq $serviceExists) {
    sc.exe create $serviceName binPath= "\"$installedServiceBinary\"" start= auto DisplayName= "\"AIR Companion\"" | Out-Null
} else {
    sc.exe config $serviceName binPath= "\"$installedServiceBinary\"" start= auto DisplayName= "\"AIR Companion\"" | Out-Null
}

sc.exe failure $serviceName reset= 86400 actions= restart/5000/restart/15000/restart/30000 | Out-Null
reg add "HKLM\\SYSTEM\\CurrentControlSet\\Services\\$serviceName" /v DelayedAutostart /t REG_DWORD /d 1 /f | Out-Null

try {
    Start-Service -Name $serviceName -ErrorAction Stop | Out-Null
} catch {
}

Start-Process $EnrollmentUrl | Out-Null
Write-Host "AIR Companion installed to $InstallDirectory"
Write-Host "Opened enrollment page: $EnrollmentUrl"
