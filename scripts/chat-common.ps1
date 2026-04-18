if ($null -eq $script:CliIoBuffers) {
    $script:CliIoBuffers = [hashtable]::Synchronized(@{})
}
function Get-ChatRepoRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptPath
    )

    $scriptDir = Split-Path -Parent $ScriptPath
    return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function Resolve-ChatArtifacts {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [string]$BuildDir = ""
    )

    if ([string]::IsNullOrWhiteSpace($BuildDir)) {
        $BuildDir = Join-Path $RepoRoot "out/build/x64-debug-mediasoup"
    } else {
        $BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
    }

    $serverExe = Join-Path $BuildDir "EDS_serverNew/eds_server_new_mediasoup_app.exe"
    $cliExe = Join-Path $BuildDir "Cli/Cli.exe"

    if (!(Test-Path $serverExe)) {
        throw "Server executable not found: $serverExe"
    }
    if (!(Test-Path $cliExe)) {
        throw "Cli executable not found: $cliExe"
    }

    return [pscustomobject]@{
        RepoRoot = $RepoRoot
        BuildDir = $BuildDir
        ServerExe = $serverExe
        CliExe = $cliExe
    }
}

function Start-ChatServer {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ServerExe,
        [switch]$EnableDirectMediasoupTestMode
    )

    $args = @("--server")
    if ($EnableDirectMediasoupTestMode) {
        $args += "--allow-direct-mediasoup"
    }

    return Start-Process -FilePath $ServerExe -ArgumentList $args -PassThru -WindowStyle Hidden
}

function Start-CliClient {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CliExe,
        [string]$ServerHost = "127.0.0.1",
        [string]$Port = "9002"
    )

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $process.StartInfo.FileName = $CliExe
    try {
        if ($null -ne $process.StartInfo.ArgumentList) {
            $process.StartInfo.ArgumentList.Add($ServerHost)
            $process.StartInfo.ArgumentList.Add($Port)
        } else {
            throw "ArgumentList is not available."
        }
    } catch {
        $process.StartInfo.Arguments = "$ServerHost $Port"
    }
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    [void]$process.Start()

    $script:CliIoBuffers[$process.Id] = [pscustomobject]@{
        StdOutTask = $process.StandardOutput.ReadToEndAsync()
        StdErrTask = $process.StandardError.ReadToEndAsync()
    }

    return $process
}


function Start-SleepWithCliDrain {
    param(
        [int]$DelayMs,
        [System.Diagnostics.Process[]]$Processes
    )
    Start-Sleep -Milliseconds $DelayMs
}

function Send-CliCommand {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$Command
    )

    if ($Process.HasExited) {
        return $false
    }

    try {
        $Process.StandardInput.WriteLine($Command)
        return $true
    } catch {
        return $false
    }
}

function Wait-CliExit {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutMs = 8000
    )

    $waitResult = $Process.WaitForExit($TimeoutMs)
    if (-not $waitResult -and -not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit(2000) | Out-Null
    }

    if ($Process.HasExited) {
        $Process.WaitForExit()
    }
    return $Process.HasExited
}

function Read-CliOutput {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    $entry = $script:CliIoBuffers[$Process.Id]
    if ($null -ne $entry) {
        $stdoutText = ""
        $stderrText = ""
        try {
            $stdoutText = $entry.StdOutTask.GetAwaiter().GetResult()
        } catch {
            $stdoutText = ""
        }
        try {
            $stderrText = $entry.StdErrTask.GetAwaiter().GetResult()
        } catch {
            $stderrText = ""
        }
        return ($stdoutText + "`n" + $stderrText)
    }

    return ($Process.StandardOutput.ReadToEnd() + "`n" + $Process.StandardError.ReadToEnd())
}

function Stop-ProcessSafe {
    param(
        [Parameter(Mandatory = $false)]
        [System.Diagnostics.Process]$Process
    )

    if ($null -eq $Process) {
        return
    }
    if (-not $Process.HasExited) {
        try {
            $Process.Kill()
            $Process.WaitForExit(2000) | Out-Null
        } catch {
        }
    }

    if ($null -ne $script:CliIoBuffers) {
        [void]$script:CliIoBuffers.Remove($Process.Id)
    }
}

function Test-HasAbort {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    return $Text.Contains("Assertion failed") -or $Text.Contains("abort")
}
