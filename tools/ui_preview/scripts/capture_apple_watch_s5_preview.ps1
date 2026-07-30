param(
    [string]$ExePath = "D:\esp32S3\111\tools\ui_preview\host_runner\build\agent_preview_host.exe",
    [string]$OutputPath = "D:\esp32S3\111\tools\ui_preview\artifacts\wifi-management-image-to-code.png",
    [switch]$OpenHermes,
    [switch]$OpenHermesInbox,
    [switch]$OpenHermesDetail,
    [switch]$OpenAi,
    [switch]$OpenCalendar,
    [switch]$OpenDanger,
    [switch]$OpenGames,
    [switch]$OpenGame2048,
    [switch]$OpenGameFlappy,
    [switch]$OpenGameDino
)

$ErrorActionPreference = "Stop"

# 显式加入 MSYS2 MinGW64 DLL 搜索路径，确保模拟器运行时不丢失动态链接库。
$env:PATH = "D:\MSYS2\mingw64\bin;D:\MSYS2\usr\bin;" + $env:PATH

if (-not (Test-Path $ExePath)) {
    $buildScript = Join-Path $PSScriptRoot "build_apple_watch_s5_preview.ps1"
    if (-not (Test-Path $buildScript)) {
        throw "Preview executable not found and build script is missing: $ExePath"
    }
    & powershell -ExecutionPolicy Bypass -File $buildScript | Out-Null
}

$outDir = Split-Path -Parent $OutputPath
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$captureArgs = @("--capture", $OutputPath)
if ($OpenHermes) {
    $captureArgs = @("--open-hermes") + $captureArgs
} elseif ($OpenHermesInbox) {
    $captureArgs = @("--open-hermes-inbox") + $captureArgs
} elseif ($OpenHermesDetail) {
    $captureArgs = @("--open-hermes-detail") + $captureArgs
} elseif ($OpenAi) {
    $captureArgs = @("--open-ai") + $captureArgs
} elseif ($OpenCalendar) {
    $captureArgs = @("--open-calendar") + $captureArgs
} elseif ($OpenDanger) {
    $captureArgs = @("--open-danger") + $captureArgs
} elseif ($OpenGames) {
    $captureArgs = @("--open-games") + $captureArgs
} elseif ($OpenGame2048) {
    $captureArgs = @("--open-game-2048") + $captureArgs
} elseif ($OpenGameFlappy) {
    $captureArgs = @("--open-game-flappy") + $captureArgs
} elseif ($OpenGameDino) {
    $captureArgs = @("--open-game-dino") + $captureArgs
}

& $ExePath @captureArgs
if ($LASTEXITCODE -ne 0) {
    throw "Preview capture failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $OutputPath)) {
    throw "Preview capture did not create output file: $OutputPath"
}

# 截图进程会自行退出；重新拉起一个独立预览窗口，避免 exe 被截图进程占用。
$runArgs = @()
if ($OpenHermes) {
    $runArgs += "--open-hermes"
} elseif ($OpenHermesInbox) {
    $runArgs += "--open-hermes-inbox"
} elseif ($OpenHermesDetail) {
    $runArgs += "--open-hermes-detail"
} elseif ($OpenAi) {
    $runArgs += "--open-ai"
} elseif ($OpenCalendar) {
    $runArgs += "--open-calendar"
} elseif ($OpenDanger) {
    $runArgs += "--open-danger"
} elseif ($OpenGames) {
    $runArgs += "--open-games"
} elseif ($OpenGame2048) {
    $runArgs += "--open-game-2048"
} elseif ($OpenGameFlappy) {
    $runArgs += "--open-game-flappy"
} elseif ($OpenGameDino) {
    $runArgs += "--open-game-dino"
}
$commandLine = "`"$ExePath`""
if ($runArgs.Count -gt 0) {
    $commandLine += " " + ($runArgs -join " ")
}
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine } | Out-Null

Write-Output $OutputPath
