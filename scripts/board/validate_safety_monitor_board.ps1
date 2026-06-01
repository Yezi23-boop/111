param(
    [string]$Port = "",
    [int]$DurationSeconds = 240,
    [switch]$NoFlash,
    [switch]$ListPorts,
    [string]$IdfExportBat = "D:\esp-idf\v5.5.3\esp-idf\export.bat",
    [string]$LogDir = "board_logs"
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

function Write-CheckResult {
    param(
        [string]$Name,
        [string]$Pattern,
        [bool]$Required,
        [string]$Text
    )

    $matched = $Text -match $Pattern
    $status = if ($matched) { "PASS" } elseif ($Required) { "FAIL" } else { "MISSING" }
    Write-Host ("[{0}] {1}" -f $status, $Name)
    return ($matched -or -not $Required)
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
$resolvedLogDir = if ([System.IO.Path]::IsPathRooted($LogDir)) {
    $LogDir
} else {
    Join-Path $repoRoot $LogDir
}

if (-not (Test-Path $IdfExportBat)) {
    throw "ESP-IDF export.bat not found: $IdfExportBat"
}

$boardPort = Resolve-BoardPort -RequestedPort $Port
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
New-Item -ItemType Directory -Force -Path $resolvedLogDir | Out-Null

$logPath = Join-Path $resolvedLogDir "safety_monitor_board_validation_$stamp.log"
$stderrPath = Join-Path $resolvedLogDir "safety_monitor_board_validation_$stamp.err.log"
$idfAction = if ($NoFlash) { "monitor" } else { "flash monitor" }
$cmd = "cd /d `"$repoRoot`" && call `"$IdfExportBat`" >nul && idf.py -p $boardPort $idfAction"

Write-Host "Safety Monitor board validation"
Write-Host "Repo: $repoRoot"
Write-Host "Port: $boardPort"
Write-Host "Action: idf.py -p $boardPort $idfAction"
Write-Host "Duration: $DurationSeconds seconds"
Write-Host "Log: $logPath"
Write-Host ""
Write-Host "Manual actions during monitor:"
Write-Host "1. Open Danger Detection and enable Safety Monitor."
Write-Host "2. Leave the Danger Detection page and confirm inference logs continue."
Write-Host "3. Enter AI chat and confirm Safety Monitor pauses or mic is blocked."
Write-Host "4. Leave AI chat and confirm Safety Monitor resumes."
Write-Host "5. Try normal speech, then horn/siren/alarm samples if available."
Write-Host ""

$process = Start-Process -FilePath "cmd.exe" `
    -ArgumentList @("/c", $cmd) `
    -NoNewWindow `
    -RedirectStandardOutput $logPath `
    -RedirectStandardError $stderrPath `
    -PassThru

try {
    Start-Sleep -Seconds $DurationSeconds
} finally {
    if (-not $process.HasExited) {
        & taskkill.exe /T /F /PID $process.Id | Out-Null
    }
}

if (Test-Path $stderrPath) {
    $stderrInfo = Get-Item $stderrPath
    if ($stderrInfo.Length -gt 0) {
        Add-Content -Path $logPath -Value ""
        Add-Content -Path $logPath -Value "--- stderr ---"
        Get-Content -Path $stderrPath | Add-Content -Path $logPath
    }
}

$logText = if (Test-Path $logPath) {
    Get-Content -Path $logPath -Raw
} else {
    ""
}

Write-Host ""
Write-Host "Log evidence summary:"
$allRequiredOk = $true
$allRequiredOk = (Write-CheckResult "boot completed" "boot_stage: startup_sequence_done" $true $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "UI first frame ready" "boot_stage: ui_first_frame_ready" $true $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "Safety Monitor started" "background danger detection started" $false $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "3s inference heartbeat" "INFERENCE #" $false $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "AI foreground declared" "foreground_audio_active: active=1 reason=official_chat" $false $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "mic blocked while AI foreground" "resource_blocked_change: resource=mic danger=1" $false $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "AI foreground cleared" "foreground_audio_active: active=0 reason=official_chat" $false $logText) -and $allRequiredOk
$allRequiredOk = (Write-CheckResult "danger alerting transition" "danger risk: .* -> ALERTING" $false $logText) -and $allRequiredOk

Write-Host ""
if ($allRequiredOk) {
    Write-Host "Required boot evidence is present. Review MISSING optional items against the manual actions performed."
    exit 0
}

Write-Host "Required boot evidence is missing. Check the full log before accepting this run."
exit 2
