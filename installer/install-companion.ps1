param(
    [string]$InstallDirectory = "$env:ProgramFiles\\AIR Companion",
    [string]$EnrollmentUrl = "https://192.168.11.228/student/companion/enroll"
)

$ErrorActionPreference = 'Stop'
$logDirectory = Join-Path $env:ProgramData 'AIRCompanion\Logs'
$logPath = Join-Path $logDirectory 'install.log'
$bootstrapDirectory = Join-Path ([System.IO.Path]::GetTempPath()) 'AIRCompanion'
$stagedBundleDirectory = Join-Path $bootstrapDirectory 'installer-bundle'
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

function Get-ServiceDiagnostics {
    param(
        [string]$ServiceName
    )

    $statusLine = ''
    $win32ExitLine = ''
    $serviceExitLine = ''
    $binaryPathLine = ''

    try {
        $queryOutput = sc.exe queryex $ServiceName 2>&1
        foreach ($line in $queryOutput) {
            if ($line -match 'STATE\s*:\s*\d+\s+(.+)$') {
                $statusLine = $Matches[1].Trim()
            } elseif ($line -match 'WIN32_EXIT_CODE\s*:\s*\d+\s+\((.+)\)$') {
                $win32ExitLine = $Matches[1].Trim()
            } elseif ($line -match 'SERVICE_EXIT_CODE\s*:\s*\d+\s+\((.+)\)$') {
                $serviceExitLine = $Matches[1].Trim()
            }
        }
    } catch {
    }

    try {
        $serviceInstance = Get-CimInstance -ClassName Win32_Service -Filter "Name='$ServiceName'" -ErrorAction Stop
        $binaryPathLine = $serviceInstance.PathName
    } catch {
    }

    $parts = @()
    if ($statusLine) {
        $parts += "state=$statusLine"
    }
    if ($win32ExitLine) {
        $parts += "win32_exit=$win32ExitLine"
    }
    if ($serviceExitLine) {
        $parts += "service_exit=$serviceExitLine"
    }
    if ($binaryPathLine) {
        $parts += "path=$binaryPathLine"
    }

    return ($parts -join '; ')
}

function Ensure-ServiceWatchdogTasks {
    param(
        [string]$ServiceName
    )

    $taskDefinitions = @(
        @{
            Name = 'AIR Companion Service (Boot)'
            Schedule = '/SC ONSTART'
        },
        @{
            Name = 'AIR Companion Service (Logon)'
            Schedule = '/SC ONLOGON'
        },
        @{
            Name = 'AIR Companion Service (Watchdog)'
            Schedule = '/SC MINUTE /MO 1'
        }
    )

    foreach ($task in $taskDefinitions) {
        $command = 'schtasks.exe /Create /TN "{0}" {1} /RU SYSTEM /RL HIGHEST /TR "cmd.exe /c sc start {2}" /F' -f `
            $task.Name, $task.Schedule, $ServiceName
        $null = cmd.exe /c $command
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create watchdog task '$($task.Name)'."
        }
    }
}

function Protect-CompanionPath {
    param(
        [string]$Path,
        [switch]$AllowUsersReadExecute
    )

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }

    $grants = @(
        '*S-1-5-18:(OI)(CI)F',      # LocalSystem
        '*S-1-5-32-544:(OI)(CI)F'   # Administrators
    )

    if ($AllowUsersReadExecute) {
        $grants += '*S-1-5-32-545:(OI)(CI)RX' # Users
    }

    & icacls.exe $Path /inheritance:r /grant:r $grants /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to protect ACLs for $Path."
    }
}

function Protect-CompanionStorage {
    param(
        [string]$InstallDirectory
    )

    Protect-CompanionPath -Path $InstallDirectory -AllowUsersReadExecute
    Protect-CompanionPath -Path (Join-Path $env:ProgramData 'AIRCompanion\Service')
    Protect-CompanionPath -Path (Join-Path $env:ProgramData 'AIRCompanion\Internal')
}

function Ensure-RemoteControlFirewallRule {
    $ruleName = 'AIR Companion Remote Control'
    $existingRule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue

    if ($null -eq $existingRule) {
        New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow -Protocol TCP -LocalPort 5905 -Profile Any | Out-Null
        return
    }

    Set-NetFirewallRule -DisplayName $ruleName -Enabled True -Action Allow -Profile Any | Out-Null
}

try {
    $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentIdentity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    New-Item -ItemType Directory -Force -Path $bootstrapDirectory | Out-Null
    if (Test-Path $stagedBundleDirectory) {
        Remove-Item -Recurse -Force $stagedBundleDirectory
    }
    New-Item -ItemType Directory -Force -Path $stagedBundleDirectory | Out-Null

    $sourceBundleDirectory = Split-Path -Parent $PSCommandPath
    foreach ($payloadName in @('install-companion.ps1', 'air_companion_service.exe', 'air_companion_tray.exe', 'air_companion_helper.exe', 'air_companion_updater.exe')) {
        $sourcePath = Join-Path $sourceBundleDirectory $payloadName
        if (-not (Test-Path $sourcePath)) {
            throw "Missing installer payload file: $sourcePath"
        }

        Copy-Item -Path $sourcePath -Destination (Join-Path $stagedBundleDirectory $payloadName) -Force
    }

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

    $stagedScriptPath = Join-Path $stagedBundleDirectory 'install-companion.ps1'
    $argumentList = ('-NoProfile -ExecutionPolicy Bypass -File "{0}" -ScriptPath "{1}" -ResultPath "{2}" -InstallDirectory "{3}" -EnrollmentUrl "{4}"' -f `
        $elevatedWrapperPath, $stagedScriptPath, $resultPath, $InstallDirectory, $EnrollmentUrl)
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
    $helperBinary = Join-Path $bundleDirectory 'air_companion_helper.exe'
    $updaterBinary = Join-Path $bundleDirectory 'air_companion_updater.exe'
    $serviceName = 'AIRCompanion'

    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    Start-Transcript -Path $logPath -Append | Out-Null

    foreach ($path in @($serviceBinary, $utilityBinary, $helperBinary, $updaterBinary)) {
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
    Copy-Item -Path $helperBinary -Destination (Join-Path $InstallDirectory 'air_companion_helper.exe') -Force
    Copy-Item -Path $updaterBinary -Destination (Join-Path $InstallDirectory 'air_companion_updater.exe') -Force

    $installedServiceBinary = Join-Path $InstallDirectory 'air_companion_service.exe'
    $quotedInstalledServiceBinary = '"' + $installedServiceBinary + '"'
    $serviceExists = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -eq $serviceExists) {
        New-Service -Name $serviceName -BinaryPathName $quotedInstalledServiceBinary -DisplayName 'AIR Companion' -StartupType Automatic | Out-Null
    } else {
        Update-ServiceConfiguration -ServiceName $serviceName -BinaryPath $installedServiceBinary
    }

    sc.exe failure $serviceName reset= 86400 actions= restart/5000/restart/15000/restart/30000 | Out-Null
    reg add "HKLM\\SYSTEM\\CurrentControlSet\\Services\\$serviceName" /v DelayedAutostart /t REG_DWORD /d 1 /f | Out-Null
    Ensure-ServiceWatchdogTasks -ServiceName $serviceName
    Protect-CompanionStorage -InstallDirectory $InstallDirectory
    Ensure-RemoteControlFirewallRule

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

    $diagnostics = Get-ServiceDiagnostics -ServiceName $serviceName
    if ($diagnostics) {
        throw "AIR Companion service failed to start. $diagnostics. See $logPath"
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
