param(
    [string]$Port = "",
    [int]$DurationSeconds = 60,
    [int]$FlashTimeoutSeconds = 180,
    [ValidateSet("monitor", "app-flash-monitor")]
    [string]$Action = "monitor",
    [string]$Tag = "serial",
    [string]$LogDir = "board_logs",
    [string]$Python = "python",
    [string]$IdfExportPs1 = "D:\esp-idf\v5.5.3\esp-idf\export.ps1",
    [switch]$NoReset,
    [switch]$QuietConsole,
    [switch]$StreamConsole,
    [switch]$AutoDetectEsp,
    [switch]$ListPorts
)

$ErrorActionPreference = "Stop"

function Get-SerialPortRows {
    Get-CimInstance Win32_SerialPort |
        Select-Object DeviceID, Description, PNPDeviceID
}

function Resolve-BoardPort {
    param(
        [string]$RequestedPort,
        [string]$Action,
        [switch]$AutoDetectEsp
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPort)) {
        return $RequestedPort
    }

    $ports = @(Get-SerialPortRows)
    $candidates = @($ports | Where-Object { $_.DeviceID -ne "COM1" })
    $useEspDetect = $AutoDetectEsp -or ($Action -eq "app-flash-monitor")

    if ($candidates.Count -eq 0) {
        $knownPorts = if ($ports.Count -gt 0) {
            ($ports | ForEach-Object { "$($_.DeviceID) $($_.Description)" }) -join "; "
        } else {
            "<none>"
        }
        throw "No ESP32-S3 serial port candidate found. Known ports: $knownPorts"
    }

    if ($useEspDetect) {
        $probeResults = @()
        foreach ($candidate in $candidates) {
            Write-Host "Probing ESP32-S3 on $($candidate.DeviceID) with esptool..."
            $probeResults += Invoke-Esp32S3Probe -CandidatePort $candidate.DeviceID
        }

        $detected = @($probeResults | Where-Object { $_.Success })
        if ($detected.Count -eq 1) {
            Write-Host "Selected ESP32-S3 port: $($detected[0].Port)"
            return $detected[0].Port
        }

        if ($detected.Count -eq 0) {
            $candidateText = ($candidates | ForEach-Object {
                "$($_.DeviceID) $($_.Description)"
            }) -join "; "
            throw "No ESP32-S3 port detected by esptool. Pass -Port explicitly or check BOOT/USB. Candidates: $candidateText"
        }

        $detectedText = ($detected | ForEach-Object { $_.Port }) -join "; "
        throw "Multiple ESP32-S3 ports detected by esptool. Pass -Port explicitly. Detected: $detectedText"
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0].DeviceID
    }

    $candidateText = ($candidates | ForEach-Object {
        "$($_.DeviceID) $($_.Description)"
    }) -join "; "
    throw "Multiple serial port candidates found. Pass -Port explicitly. Candidates: $candidateText"
}

function Invoke-Esp32S3Probe {
    param([string]$CandidatePort)

    $job = Start-Job -ScriptBlock {
        param(
            [string]$Python,
            [string]$IdfExportPs1,
            [string]$CandidatePort
        )

        try {
            . $IdfExportPs1 *> $null
            $probeOutput = & $Python -m esptool --chip esp32s3 -p $CandidatePort --baud 115200 read_mac 2>&1
            $exitCode = $LASTEXITCODE
            $probeOutput
            "AGENT_ESP_PROBE_EXIT_CODE=$exitCode"
        } catch {
            $_.Exception.Message
            "AGENT_ESP_PROBE_EXIT_CODE=1"
        }
    } -ArgumentList $Python, $IdfExportPs1, $CandidatePort

    try {
        if (-not (Wait-Job -Job $job -Timeout 10)) {
            Stop-Job -Job $job | Out-Null
            return [pscustomobject]@{
                Port = $CandidatePort
                Success = $false
                Output = "timeout"
            }
        }

        $output = (Receive-Job -Job $job | Out-String).Trim()
        return [pscustomobject]@{
            Port = $CandidatePort
            Success = ($output -match "AGENT_ESP_PROBE_EXIT_CODE=0")
            Output = $output
        }
    } finally {
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
}

if ($ListPorts) {
    $ports = @(Get-SerialPortRows)
    if ($ports.Count -eq 0) {
        Write-Host "No serial ports found."
    } else {
        $ports | Format-Table -AutoSize
    }
    exit 0
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$scriptPath = Join-Path $PSScriptRoot "agent_serial_monitor.py"
$boardPort = Resolve-BoardPort -RequestedPort $Port -Action $Action -AutoDetectEsp:$AutoDetectEsp

$arguments = @(
    $scriptPath,
    "--port", $boardPort,
    "--duration-seconds", $DurationSeconds.ToString(),
    "--flash-timeout-seconds", $FlashTimeoutSeconds.ToString(),
    "--action", $Action,
    "--tag", $Tag,
    "--log-dir", $LogDir,
    "--idf-export-ps1", $IdfExportPs1
)

if ($NoReset) {
    $arguments += "--no-reset"
}

if ($QuietConsole) {
    $arguments += "--quiet-console"
}

if ($StreamConsole) {
    $arguments += "--stream-console"
}

& $Python @arguments
