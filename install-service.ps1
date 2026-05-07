[CmdletBinding()]
param(
    [string]$ServiceName = "MeetSpaceServer",
    [string]$ServerRoot = "C:\diplom",
    [string]$PublicIp = "31.177.83.146"
)

$ErrorActionPreference = "Stop"

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Run this script from an elevated PowerShell (Run as Administrator)."
    }
}

function Stop-MeetSpaceProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    Get-Process -Name "eds_server_new_mediasoup_app" -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    $serverJsMarker = [System.IO.Path]::Combine($Root, "apps", "mediasoup-server", "src", "server.js")
    $nodeProcesses = Get-CimInstance Win32_Process -Filter "Name='node.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($serverJsMarker, [StringComparison]::OrdinalIgnoreCase) -ge 0 }

    foreach ($nodeProcess in $nodeProcesses) {
        Stop-Process -Id $nodeProcess.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

Assert-Administrator

$runScript = Join-Path $ServerRoot "run-all.ps1"
if (-not (Test-Path -LiteralPath $runScript)) {
    throw "run-all.ps1 not found: $runScript"
}

# Remove broken classic service if it was created earlier with the same name.
$existingService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($null -ne $existingService) {
    try {
        Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
    }
    catch {
    }
    sc.exe delete $ServiceName | Out-Null
}

$taskName = $ServiceName
$taskPath = "\MeetSpace\"
$fullTaskName = "$taskPath$taskName"
$taskArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$runScript`" -ServerRoot `"$ServerRoot`" -PublicIp $PublicIp"

Stop-MeetSpaceProcesses -Root $ServerRoot

$action = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument $taskArgs `
    -WorkingDirectory $ServerRoot
$trigger = New-ScheduledTaskTrigger -AtStartup
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -RunLevel Highest -LogonType ServiceAccount
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit (New-TimeSpan -Hours 0) `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew

Register-ScheduledTask `
    -TaskName $taskName `
    -TaskPath $taskPath `
    -Action $action `
    -Trigger $trigger `
    -Principal $principal `
    -Settings $settings `
    -Description "MeetSpace signaling/media stack startup task." `
    -Force | Out-Null

Start-ScheduledTask -TaskName $taskName -TaskPath $taskPath
Start-Sleep -Seconds 2

$task = Get-ScheduledTask -TaskName $taskName -TaskPath $taskPath
$taskInfo = Get-ScheduledTaskInfo -TaskName $taskName -TaskPath $taskPath

Write-Host "[startup-task] Registered: $fullTaskName"
Write-Host "[startup-task] State: $($task.State)"
Write-Host "[startup-task] LastRunTime: $($taskInfo.LastRunTime)"
Write-Host "[startup-task] LastTaskResult: $($taskInfo.LastTaskResult)"
Write-Host "[startup-task] To restart manually:"
Write-Host "  Stop-Process -Name eds_server_new_mediasoup_app -Force -ErrorAction SilentlyContinue"
Write-Host "  Start-ScheduledTask -TaskPath '$taskPath' -TaskName '$taskName'"
