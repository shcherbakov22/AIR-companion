param(
    [string]$InstallDirectory = "$env:ProgramFiles\AIR Companion",
    [string]$UserDataDirectory = "$env:APPDATA\AIRCompanion",
    [switch]$SystemRepair
)

$ErrorActionPreference = 'Stop'
$serviceName = 'AIRCompanion'
$programDataRoot = Join-Path $env:ProgramData 'AIRCompanion'
$publicRoot = Join-Path $env:PUBLIC 'AIRCompanion'
$systemProfileDataRoot = Join-Path $env:windir 'System32\config\systemprofile\AppData\Roaming\AIRCompanion'
$logDirectory = Join-Path $programDataRoot 'Logs'
$logPath = Join-Path $logDirectory 'permission-repair.log'
$bootstrapDirectory = Join-Path $env:windir 'Temp\AIRCompanion'
$resultPath = Join-Path $bootstrapDirectory 'permission-repair-result.txt'
$elevatedWrapperPath = Join-Path $bootstrapDirectory 'permission-repair-elevated.ps1'
$systemScriptPath = Join-Path $bootstrapDirectory 'permission-repair-system.ps1'

function Ensure-Elevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ($identity.User.Value -eq 'S-1-5-18') {
        return
    }

    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        return
    }

    New-Item -ItemType Directory -Force -Path $bootstrapDirectory | Out-Null
    @'
param(
    [string]$ScriptPath,
    [string]$ResultPath,
    [string]$InstallDirectory,
    [string]$UserDataDirectory
)

$ErrorActionPreference = 'Stop'

try {
    & "$ScriptPath" -InstallDirectory $InstallDirectory -UserDataDirectory $UserDataDirectory
    $exitCode = if ($LASTEXITCODE -ne $null) { $LASTEXITCODE } else { 0 }
    if ($exitCode -ne 0 -and -not (Test-Path $ResultPath)) {
        Set-Content -Path $ResultPath -Value "Elevated repair exited with code $exitCode."
    }
    exit $exitCode
} catch {
    Set-Content -Path $ResultPath -Value $_.Exception.Message
    exit 1
}
'@ | Set-Content -Path $elevatedWrapperPath

    $argumentList = ('-NoProfile -ExecutionPolicy Bypass -File "{0}" -ScriptPath "{1}" -ResultPath "{2}" -InstallDirectory "{3}" -UserDataDirectory "{4}"' -f `
        $elevatedWrapperPath, $PSCommandPath, $resultPath, $InstallDirectory, $UserDataDirectory)
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argumentList -PassThru -Wait

    if ($process.ExitCode -ne 0) {
        if (Test-Path $resultPath) {
            throw (Get-Content $resultPath -Raw).Trim()
        }

        throw "Elevated repair exited with code $($process.ExitCode)."
    }

    exit 0
}

function Invoke-SystemRepair {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ($SystemRepair -or $identity.User.Value -eq 'S-1-5-18') {
        return
    }

    New-Item -ItemType Directory -Force -Path $bootstrapDirectory | Out-Null
    Copy-Item -Path $PSCommandPath -Destination $systemScriptPath -Force
    if (Test-Path $resultPath) {
        Remove-Item -Force $resultPath
    }

    $taskName = 'AIR Companion Permission Repair'
    $taskArgument = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -InstallDirectory "{1}" -UserDataDirectory "{2}" -SystemRepair' -f `
        $systemScriptPath, $InstallDirectory, $UserDataDirectory

    try {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $taskArgument
        $trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1)
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-CompanionScheduledTaskSettings -ExecutionMinutes 5
        Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    } catch {
        throw "Failed to create SYSTEM permission repair task. $($_.Exception.Message)"
    }

    try {
        Start-ScheduledTask -TaskName $taskName -ErrorAction Stop
    } catch {
        throw "Failed to start SYSTEM permission repair task. $($_.Exception.Message)"
    }

    for ($attempt = 0; $attempt -lt 180; $attempt++) {
        if (Test-Path $resultPath) {
            $result = Get-Content $resultPath -Raw
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue

            if ($result -match '^OK') {
                Write-Host $result.Trim()
                exit 0
            }

            throw $result.Trim()
        }

        Start-Sleep -Seconds 1
    }

    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    throw 'SYSTEM permission repair task timed out.'
}

function New-CompanionScheduledTaskSettings {
    param([int]$ExecutionMinutes = 0)

    try {
        if ($ExecutionMinutes -gt 0) {
            return New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes $ExecutionMinutes)
        }

        return New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries
    } catch {
        return New-ScheduledTaskSettingsSet
    }
}

function New-CompanionRepeatingMinuteTrigger {
    try {
        return New-ScheduledTaskTrigger -Once -At (Get-Date).Date -RepetitionInterval (New-TimeSpan -Minutes 1)
    } catch {
        $trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1)
        try {
            $trigger.Repetition.Interval = 'PT1M'
            $trigger.Repetition.Duration = 'P1D'
        } catch {
        }

        return $trigger
    }
}

function Invoke-NativeCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$FailureMessage,
        [switch]$IgnoreFailure
    )

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'

    try {
        $output = & $FilePath @Arguments 2>&1
        $exitCode = if ($LASTEXITCODE -ne $null) { $LASTEXITCODE } else { 0 }
    } catch {
        if ($IgnoreFailure) {
            return
        }

        throw "$FailureMessage`n$($_.Exception.Message)"
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    if ($exitCode -ne 0 -and -not $IgnoreFailure) {
        $details = ($output | ForEach-Object { "$_" }) -join [Environment]::NewLine
        if ([string]::IsNullOrWhiteSpace($details)) {
            throw $FailureMessage
        }

        throw "$FailureMessage`n$details"
    }
}

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Take-PathOwnership {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return
    }

    Invoke-NativeCommand -FilePath 'takeown.exe' -Arguments @('/F', $Path, '/A', '/R', '/D', 'Y') -FailureMessage "Failed to take ownership of $Path." -IgnoreFailure
    Invoke-NativeCommand -FilePath 'icacls.exe' -Arguments @($Path, '/setowner', '*S-1-5-32-544', '/T', '/C') -FailureMessage "Failed to set owner for $Path." -IgnoreFailure
}

function Set-CompanionAcl {
    param(
        [string]$Path,
        [ValidateSet('None', 'ReadExecute', 'Modify')]
        [string]$UsersAccess = 'None',
        [switch]$AllowPartial
    )

    Ensure-Directory $Path
    Take-PathOwnership $Path

    $grants = @(
        '*S-1-5-18:(OI)(CI)F',
        '*S-1-5-32-544:(OI)(CI)F'
    )

    if ($UsersAccess -eq 'ReadExecute') {
        $grants += '*S-1-5-32-545:(OI)(CI)RX'
    } elseif ($UsersAccess -eq 'Modify') {
        $grants += '*S-1-5-32-545:(OI)(CI)M'
    }

    Invoke-NativeCommand -FilePath 'icacls.exe' -Arguments (@($Path, '/inheritance:e', '/grant:r') + $grants) -FailureMessage "Failed to repair root ACLs for $Path."
    Invoke-NativeCommand -FilePath 'icacls.exe' -Arguments (@($Path, '/inheritance:e', '/grant:r') + $grants + @('/T', '/C')) -FailureMessage "Failed to repair ACLs for $Path." -IgnoreFailure:$AllowPartial
}

function Assert-PathWritable {
    param(
        [string]$Path,
        [string]$Name
    )

    Ensure-Directory $Path
    $testPath = Join-Path $Path ('permission-repair-test-{0}.tmp' -f ([Guid]::NewGuid().ToString('N')))

    try {
        Set-Content -Path $testPath -Value 'ok' -ErrorAction Stop
        Remove-Item -Force $testPath -ErrorAction SilentlyContinue
    } catch {
        throw "$Name is not writable after repair: $Path. $($_.Exception.Message)"
    }
}

function Assert-PathReadable {
    param(
        [string]$Path,
        [string]$Name
    )

    Ensure-Directory $Path

    try {
        Get-ChildItem -LiteralPath $Path -Force -ErrorAction Stop | Select-Object -First 1 | Out-Null
    } catch {
        throw "$Name is not readable after repair: $Path. $($_.Exception.Message)"
    }
}

function Repair-CompanionBinaryAcls {
    param([string]$InstallDirectory)

    foreach ($fileName in @('air_companion_service.exe', 'air_companion_tray.exe', 'air_companion_helper.exe', 'air_companion_updater.exe')) {
        $path = Join-Path $InstallDirectory $fileName
        if (-not (Test-Path $path)) {
            continue
        }

        Invoke-NativeCommand -FilePath 'takeown.exe' -Arguments @('/F', $path, '/A') -FailureMessage "Failed to take ownership of $path." -IgnoreFailure
        Invoke-NativeCommand -FilePath 'icacls.exe' -Arguments @($path, '/setowner', '*S-1-5-32-544', '/C') -FailureMessage "Failed to set owner for $path." -IgnoreFailure
        Invoke-NativeCommand -FilePath 'icacls.exe' -Arguments @($path, '/inheritance:e', '/grant:r', '*S-1-5-18:F', '*S-1-5-32-544:F', '*S-1-5-32-545:RX', '/C') -FailureMessage "Failed to repair ACLs for $path." -IgnoreFailure
    }
}

function Repair-CompanionDataAcls {
    Set-CompanionAcl -Path $programDataRoot -UsersAccess Modify -AllowPartial
    Set-CompanionAcl -Path (Join-Path $programDataRoot 'Service') -UsersAccess Modify
    Set-CompanionAcl -Path (Join-Path $programDataRoot 'Internal') -UsersAccess Modify
    Set-CompanionAcl -Path (Join-Path $programDataRoot 'Captures') -UsersAccess Modify
    Set-CompanionAcl -Path $logDirectory -UsersAccess Modify
    Set-CompanionAcl -Path $systemProfileDataRoot -UsersAccess Modify

    if (-not [string]::IsNullOrWhiteSpace($UserDataDirectory)) {
        Set-CompanionAcl -Path $UserDataDirectory -UsersAccess Modify -AllowPartial
    }

    Set-CompanionAcl -Path $publicRoot -UsersAccess Modify -AllowPartial
    Set-CompanionAcl -Path (Join-Path $publicRoot 'InteractiveCapture') -UsersAccess Modify
}

function Assert-CompanionDataAcls {
    Assert-PathReadable -Path $programDataRoot -Name 'ProgramData AIRCompanion root'
    Assert-PathReadable -Path (Join-Path $programDataRoot 'Service') -Name 'Service folder'
    Assert-PathReadable -Path (Join-Path $programDataRoot 'Internal') -Name 'Internal enrollment folder'
    Assert-PathWritable -Path (Join-Path $programDataRoot 'Captures') -Name 'Captures folder'
    Assert-PathWritable -Path $logDirectory -Name 'Logs folder'
    Assert-PathWritable -Path $systemProfileDataRoot -Name 'System profile AIRCompanion folder'

    if (-not [string]::IsNullOrWhiteSpace($UserDataDirectory)) {
        Assert-PathWritable -Path $UserDataDirectory -Name 'User AIRCompanion folder'
    }

    Assert-PathWritable -Path $publicRoot -Name 'Public AIRCompanion folder'
    Assert-PathWritable -Path (Join-Path $publicRoot 'InteractiveCapture') -Name 'Interactive capture folder'
}

function Stop-CompanionRuntime {
    param([string]$ServiceName)

    Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
    foreach ($processName in @('air_companion_tray.exe', 'air_companion_helper.exe', 'air_companion_updater.exe')) {
        Invoke-NativeCommand -FilePath 'taskkill.exe' -Arguments @('/IM', $processName, '/F') -FailureMessage "Failed to stop $processName." -IgnoreFailure
    }
}

function Ensure-ServiceWatchdogTasks {
    param([string]$ServiceName)

    $taskDefinitions = @(
        @{ Name = 'AIR Companion Service (Boot)'; Trigger = New-ScheduledTaskTrigger -AtStartup },
        @{ Name = 'AIR Companion Service (Logon)'; Trigger = New-ScheduledTaskTrigger -AtLogOn },
        @{ Name = 'AIR Companion Service (Watchdog)'; Trigger = New-CompanionRepeatingMinuteTrigger }
    )

    foreach ($task in $taskDefinitions) {
        try {
            Unregister-ScheduledTask -TaskName $task.Name -Confirm:$false -ErrorAction SilentlyContinue
            $action = New-ScheduledTaskAction -Execute 'sc.exe' -Argument "start $ServiceName"
            $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
            $settings = New-CompanionScheduledTaskSettings
            Register-ScheduledTask -TaskName $task.Name -Action $action -Trigger $task.Trigger -Principal $principal -Settings $settings -Force | Out-Null
        } catch {
            throw "Failed to repair watchdog task '$($task.Name)'. $($_.Exception.Message)"
        }
    }
}

function Repair-Service {
    param(
        [string]$ServiceName,
        [string]$InstallDirectory
    )

    $serviceBinary = Join-Path $InstallDirectory 'air_companion_service.exe'
    if (-not (Test-Path $serviceBinary)) {
        throw "Missing service binary: $serviceBinary"
    }

    $quotedBinary = '"' + $serviceBinary + '"'
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue

    if ($null -eq $service) {
        New-Service -Name $ServiceName -BinaryPathName $quotedBinary -DisplayName 'AIR Companion' -StartupType Automatic | Out-Null
    } else {
        Invoke-NativeCommand -FilePath 'sc.exe' -Arguments @('config', $ServiceName, 'binPath=', $quotedBinary, 'start=', 'delayed-auto', 'obj=', 'LocalSystem') -FailureMessage 'Failed to repair AIR Companion service configuration.'
    }

    Invoke-NativeCommand -FilePath 'sc.exe' -Arguments @('failure', $ServiceName, 'reset=', '86400', 'actions=', 'restart/5000/restart/15000/restart/30000') -FailureMessage 'Failed to repair AIR Companion service restart policy.'
    Invoke-NativeCommand -FilePath 'reg.exe' -Arguments @('add', "HKLM\SYSTEM\CurrentControlSet\Services\$ServiceName", '/v', 'DelayedAutostart', '/t', 'REG_DWORD', '/d', '1', '/f') -FailureMessage 'Failed to enable delayed autostart for AIR Companion service.'
    Ensure-ServiceWatchdogTasks -ServiceName $ServiceName
}

try {
    Ensure-Elevated
    Invoke-SystemRepair

    try {
        Ensure-Directory $logDirectory
        Start-Transcript -Path $logPath -Append | Out-Null
    } catch {
    }

    Stop-CompanionRuntime -ServiceName $serviceName
    Start-Sleep -Seconds 2

    Set-CompanionAcl -Path $InstallDirectory -UsersAccess ReadExecute -AllowPartial
    Repair-CompanionBinaryAcls -InstallDirectory $InstallDirectory
    Repair-CompanionDataAcls
    Assert-CompanionDataAcls

    Repair-Service -ServiceName $serviceName -InstallDirectory $InstallDirectory

    Start-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ((Get-Service -Name $serviceName -ErrorAction Stop).Status -ne 'Running') {
        Invoke-NativeCommand -FilePath 'sc.exe' -Arguments @('start', $serviceName) -FailureMessage 'Failed to start AIR Companion service.'
    }

    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        $service = Get-Service -Name $serviceName -ErrorAction Stop
        if ($service.Status -eq 'Running') {
            Set-Content -Path $resultPath -Value 'OK: AIR Companion permissions repaired and service is running.'
            Write-Host "AIR Companion permissions repaired and service is running."
            Write-Host "Repair log: $logPath"
            try {
                Stop-Transcript | Out-Null
            } catch {
            }
            exit 0
        }

        Start-Sleep -Seconds 1
    }

    throw "AIR Companion service did not reach Running state after permission repair."
} catch {
    if ($SystemRepair) {
        try {
            Set-Content -Path $resultPath -Value $_.Exception.Message
        } catch {
        }
        exit 1
    }

    try {
        Stop-Transcript | Out-Null
    } catch {
    }

    Write-Host ''
    Write-Host 'AIR Companion permission repair failed.' -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "Log: $logPath" -ForegroundColor Yellow
    Write-Host ''
    Read-Host 'Press Enter to close'
    try {
        Set-Content -Path $resultPath -Value $_.Exception.Message
    } catch {
    }
    exit 1
}
