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
    [string[]]$Pattern = @(),
    [string[]]$LiteralPattern = @(),
    [int]$TailLines = 120,
    [switch]$NoReset,
    [switch]$QuietConsole,
    [switch]$ListPorts
)

$ErrorActionPreference = "Stop"

function Get-SerialPortRows {
    Get-CimInstance Win32_SerialPort |
        Select-Object DeviceID, Description, PNPDeviceID
}

function Resolve-BoardPort {
    param([string]$RequestedPort)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPort)) {
        return $RequestedPort
    }

    $ports = @(Get-SerialPortRows)
    $candidates = @($ports | Where-Object { $_.DeviceID -ne "COM1" })

    if ($candidates.Count -eq 1) {
        return $candidates[0].DeviceID
    }

    if ($candidates.Count -eq 0) {
        $knownPorts = if ($ports.Count -gt 0) {
            ($ports | ForEach-Object { "$($_.DeviceID) $($_.Description)" }) -join "; "
        } else {
            "<none>"
        }
        throw "No ESP32-S3 serial port candidate found. Known ports: $knownPorts"
    }

    $candidateText = ($candidates | ForEach-Object {
        "$($_.DeviceID) $($_.Description)"
    }) -join "; "
    throw "Multiple serial port candidates found. Pass -Port explicitly. Candidates: $candidateText"
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
$boardPort = Resolve-BoardPort -RequestedPort $Port

$arguments = @(
    $scriptPath,
    "--port", $boardPort,
    "--duration-seconds", $DurationSeconds.ToString(),
    "--flash-timeout-seconds", $FlashTimeoutSeconds.ToString(),
    "--action", $Action,
    "--tag", $Tag,
    "--log-dir", $LogDir,
    "--idf-export-ps1", $IdfExportPs1,
    "--tail-lines", $TailLines.ToString()
)

foreach ($item in $Pattern) {
    $arguments += @("--pattern", $item)
}

foreach ($item in $LiteralPattern) {
    $arguments += @("--literal-pattern", $item)
}

if ($NoReset) {
    $arguments += "--no-reset"
}

if ($QuietConsole) {
    $arguments += "--quiet-console"
}

& $Python @arguments
