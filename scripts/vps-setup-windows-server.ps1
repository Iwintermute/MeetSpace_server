[CmdletBinding()]
param(
    [string]$ServerRoot = "C:\diplom",
    [Parameter(Mandatory = $true)]
    [string]$PublicIp,
    [Parameter(Mandatory = $true)]
    [string]$PostgresConninfo,
    [string]$SupabaseUrl = "",
    [string]$SupabaseAnonKey = "",
    [string]$TaskName = "EduSpaceServer",
    [switch]$SkipScheduledTask
)

$ErrorActionPreference = "Stop"

function Resolve-ServerExe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $candidates = @(
        (Join-Path $Root "out\x64-release\EDS_serverNew\eds_server_new_mediasoup_app.exe"),
        (Join-Path $Root "out\build\x64-release-mediasoup\EDS_serverNew\eds_server_new_mediasoup_app.exe"),
        (Join-Path $Root "out\build\x64-debug-mediasoup\EDS_serverNew\eds_server_new_mediasoup_app.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Server executable not found. Checked paths: $($candidates -join '; ')"
}

function Set-MachineEnv {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    [Environment]::SetEnvironmentVariable($Name, $Value, "Machine")
}

function Ensure-FirewallRule {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DisplayName,
        [Parameter(Mandatory = $true)]
        [string]$Protocol,
        [Parameter(Mandatory = $true)]
        [string]$LocalPort
    )

    $existing = Get-NetFirewallRule -DisplayName $DisplayName -ErrorAction SilentlyContinue
    if ($existing) {
        $existing | Remove-NetFirewallRule -ErrorAction SilentlyContinue | Out-Null
    }

    New-NetFirewallRule `
        -DisplayName $DisplayName `
        -Direction Inbound `
        -Action Allow `
        -Protocol $Protocol `
        -LocalPort $LocalPort | Out-Null
}

$resolvedRoot = (Resolve-Path $ServerRoot).Path
$serverExe = Resolve-ServerExe -Root $resolvedRoot
$backendCmd = Join-Path $resolvedRoot "apps\mediasoup-server\run-backend.cmd"
if (!(Test-Path $backendCmd)) {
    throw "Mediasoup backend runner not found: $backendCmd"
}
$backendCmd = (Resolve-Path $backendCmd).Path

Set-MachineEnv -Name "EDUSPACE_POSTGRES_CONNINFO" -Value $PostgresConninfo
Set-MachineEnv -Name "POSTGRES_CONNINFO" -Value $PostgresConninfo
Set-MachineEnv -Name "EDUSPACE_MEDIASOUP_BACKEND_URL" -Value "ws://127.0.0.1:5001/ws"
Set-MachineEnv -Name "EDUSPACE_MEDIASOUP_BACKEND_CMD" -Value $backendCmd
Set-MachineEnv -Name "MEDIASOUP_BACKEND_HOST" -Value "127.0.0.1"
Set-MachineEnv -Name "MEDIASOUP_BACKEND_PORT" -Value "5001"
Set-MachineEnv -Name "MEDIASOUP_BACKEND_PATH" -Value "/ws"
Set-MachineEnv -Name "MEDIASOUP_RTC_LISTEN_IP" -Value "0.0.0.0"
Set-MachineEnv -Name "MEDIASOUP_ANNOUNCED_IP" -Value $PublicIp
Set-MachineEnv -Name "MEDIASOUP_RTC_MIN_PORT" -Value "40000"
Set-MachineEnv -Name "MEDIASOUP_RTC_MAX_PORT" -Value "49999"

if (-not [string]::IsNullOrWhiteSpace($SupabaseUrl)) {
    Set-MachineEnv -Name "SUPABASE_URL" -Value $SupabaseUrl
}
if (-not [string]::IsNullOrWhiteSpace($SupabaseAnonKey)) {
    Set-MachineEnv -Name "SUPABASE_ANON_KEY" -Value $SupabaseAnonKey
}

$startupCmd = Join-Path $resolvedRoot "start-eds-server.cmd"
$startupCmdContent = @"
@echo off
setlocal
cd /d "$resolvedRoot"
"$serverExe" --server --mediasoup-backend-url ws://127.0.0.1:5001/ws --mediasoup-backend-cmd "$backendCmd"
"@
Set-Content -Path $startupCmd -Value $startupCmdContent -Encoding Ascii

Ensure-FirewallRule -DisplayName "EduSpace Signaling TCP 9002" -Protocol "TCP" -LocalPort "9002"
Ensure-FirewallRule -DisplayName "EduSpace WebRTC UDP 40000-49999" -Protocol "UDP" -LocalPort "40000-49999"
Ensure-FirewallRule -DisplayName "EduSpace WebRTC TCP 40000-49999" -Protocol "TCP" -LocalPort "40000-49999"

if (-not $SkipScheduledTask) {
    $action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c `"$startupCmd`""
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
}

Write-Host "VPS setup completed."
Write-Host "Server exe: $serverExe"
Write-Host "Startup script: $startupCmd"
Write-Host "MEDIASOUP_ANNOUNCED_IP: $PublicIp"
Write-Host "Reminder: open the same ports in your VPS provider network/security group."
