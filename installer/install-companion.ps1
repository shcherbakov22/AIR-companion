param(
    [string]$InstallDirectory = "$env:ProgramFiles\\AIR Companion",
    [string]$EnrollmentUrl = "https://192.168.11.228/student/companion/enroll"
)

$ErrorActionPreference = 'Stop'
$logDirectory = Join-Path $env:ProgramData 'AIRCompanion\Logs'
$logPath = Join-Path $logDirectory 'install.log'

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

try {
    $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentIdentity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $argumentList = @(
            '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f $PSCommandPath),
            '-InstallDirectory', ('"{0}"' -f $InstallDirectory),
            '-EnrollmentUrl', ('"{0}"' -f $EnrollmentUrl)
        )
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argumentList -PassThru -Wait
        if ($process.ExitCode -ne 0) {
            Show-FailureAndPause -Message "Elevated installer exited with code $($process.ExitCode)." -LogPath $logPath
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
        sc.exe config $serviceName binPath= "\"$installedServiceBinary\"" start= auto DisplayName= "\"AIR Companion\"" | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to update AIR Companion service."
        }
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
