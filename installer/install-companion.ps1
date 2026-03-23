param(
    [string]$InstallDirectory = "$env:ProgramFiles\\AIR Companion",
    [string]$EnrollmentUrl = "https://192.168.11.228/student/companion/enroll"
)

$ErrorActionPreference = 'Stop'
$logDirectory = Join-Path $env:ProgramData 'AIRCompanion\Logs'
$logPath = Join-Path $logDirectory 'install.log'
$bootstrapDirectory = Join-Path ([System.IO.Path]::GetTempPath()) 'AIRCompanion'
$resultPath = Join-Path $bootstrapDirectory 'install-result.txt'
$elevatedWrapperPath = Join-Path $bootstrapDirectory 'install-elevated.ps1'

function Show-FailureAndPause {
    param(
        [string]$Message,
        [string]$LogPath
    )

    Write-Host ''
    Write-Host 'AIR Companion install failed.' -ForegroundColor Red
    Write-Host $Message -ForegroundColor Red
    if ($LogPath) {
        Write-Host "Log: $LogPath" -ForegroundColor Yellow
    }
    Write-Host ''
    Read-Host 'Press Enter to close'
}

function Update-ServiceConfiguration {
    param(
        [string]$ServiceName,
        [string]$BinaryPath
    )

    $quotedBinaryPath = '"' + $BinaryPath + '"'
    $serviceInstance = Get-CimInstance -ClassName Win32_Service -Filter "Name='$ServiceName'" -ErrorAction Stop
    $changeResult = Invoke-CimMethod -InputObject $serviceInstance -MethodName Change -Arguments @{
        PathName = $quotedBinaryPath
        StartMode = 'Automatic'
        DisplayName = 'AIR Companion'
    } -ErrorAction Stop

    if ($null -eq $changeResult -or $changeResult.ReturnValue -ne 0) {
        $returnValue = if ($null -eq $changeResult) { 'unknown' } else { $changeResult.ReturnValue }
        throw "Failed to update AIR Companion service. Win32_Service.Change returned $returnValue."
    }
}

try {
    $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentIdentity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    New-Item -ItemType Directory -Force -Path $bootstrapDirectory | Out-Null
    if (Test-Path $resultPath) {
        Remove-Item -Force $resultPath
    }

    @'
param(
    [string]$ScriptPath,
    [string]$ResultPath,
    [string]$InstallDirectory,
    [string]$EnrollmentUrl
)

$ErrorActionPreference = 'Stop'

try {
    & "$ScriptPath" -InstallDirectory $InstallDirectory -EnrollmentUrl $EnrollmentUrl
    $exitCode = if ($LASTEXITCODE -ne $null) { $LASTEXITCODE } else { 0 }
    if ($exitCode -ne 0 -and -not (Test-Path $ResultPath)) {
        Set-Content -Path $ResultPath -Value "Elevated installer exited with code $exitCode."
    }
    exit $exitCode
} catch {
    Set-Content -Path $ResultPath -Value $_.Exception.Message
    exit 1
}
'@ | Set-Content -Path $elevatedWrapperPath

    $argumentList = ('-NoProfile -ExecutionPolicy Bypass -File "{0}" -ScriptPath "{1}" -ResultPath "{2}" -InstallDirectory "{3}" -EnrollmentUrl "{4}"' -f `
        $elevatedWrapperPath, $PSCommandPath, $resultPath, $InstallDirectory, $EnrollmentUrl)
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argumentList -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        $childMessage = if (Test-Path $resultPath) {
            Get-Content $resultPath -Raw
        } else {
            "Elevated installer exited with code $($process.ExitCode)."
        }

        Show-FailureAndPause -Message $childMessage.Trim() -LogPath $logPath
    }
        exit $process.ExitCode
    }

    $bundleDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $serviceBinary = Join-Path $bundleDirectory 'air_companion_service.exe'
    $utilityBinary = Join-Path $bundleDirectory 'air_companion_tray.exe'
    $updaterBinary = Join-Path $bundleDirectory 'air_companion_updater.exe'
    $serviceName = 'AIRCompanion'

    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    Start-Transcript -Path $logPath -Append | Out-Null

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
        New-Service -Name $serviceName -BinaryPathName $installedServiceBinary -DisplayName 'AIR Companion' -StartupType Automatic | Out-Null
    } else {
        Update-ServiceConfiguration -ServiceName $serviceName -BinaryPath $installedServiceBinary
    }

    sc.exe failure $serviceName reset= 86400 actions= restart/5000/restart/15000/restart/30000 | Out-Null
    reg add "HKLM\\SYSTEM\\CurrentControlSet\\Services\\$serviceName" /v DelayedAutostart /t REG_DWORD /d 1 /f | Out-Null

    Start-Service -Name $serviceName -ErrorAction SilentlyContinue | Out-Null
    if ((Get-Service -Name $serviceName).Status -ne 'Running') {
        sc.exe start $serviceName | Out-Null
    }

    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        $service = Get-Service -Name $serviceName -ErrorAction Stop
        if ($service.Status -eq 'Running') {
            Start-Process $EnrollmentUrl | Out-Null
            Write-Host "AIR Companion installed to $InstallDirectory"
            Write-Host "Opened enrollment page: $EnrollmentUrl"
            Write-Host "Install log: $logPath"
            Stop-Transcript | Out-Null
            exit 0
        }

        Start-Sleep -Seconds 1
    }

    throw "AIR Companion service failed to start. See $logPath"
} catch {
    try {
        Stop-Transcript | Out-Null
    } catch {
    }
    Show-FailureAndPause -Message $_.Exception.Message -LogPath $logPath
    exit 1
}
