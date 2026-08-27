param(
    [string]$ExePath = "D:\esp32S3\111\tools\ui_preview\host_runner\build\agent_preview_host.exe",
    [string]$OutputPath = "D:\esp32S3\111\tools\ui_preview\artifacts\wifi-management-image-to-code.png",
    [switch]$OpenHermes,
    [switch]$OpenHermesInbox,
    [switch]$OpenHermesDetail,
    [switch]$OpenAi,
    [switch]$OpenMusic,
    [switch]$OpenMusicCatalog,
    [switch]$OpenMusicAccount,
    [switch]$OpenDropdown,
    [switch]$OpenWifi,
    [switch]$OpenCalendar,
    [switch]$OpenWallpaper,
    [switch]$OpenOta,
    [switch]$OpenFunction,
    [switch]$OpenFunctionInitial,
    [switch]$OpenDanger,
    [switch]$OpenGames,
    [switch]$OpenGame2048,
    [switch]$OpenGameFlappy,
    [switch]$OpenGameDino,
    [switch]$OpenNotificationBubble
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
} elseif ($OpenMusicCatalog) {
    $captureArgs = @("--open-music-catalog") + $captureArgs
} elseif ($OpenMusicAccount) {
    $captureArgs = @("--open-music-account") + $captureArgs
} elseif ($OpenMusic) {
    $captureArgs = @("--open-music") + $captureArgs
} elseif ($OpenDropdown) {
    $captureArgs = @("--open-dropdown") + $captureArgs
    } elseif ($OpenWifi) {
        $captureArgs = @("--open-wifi") + $captureArgs
    } elseif ($OpenCalendar) {
        $captureArgs = @("--open-calendar") + $captureArgs
    } elseif ($OpenWallpaper) {
        $captureArgs = @("--open-wallpaper") + $captureArgs
    } elseif ($OpenOta) {
        $captureArgs = @("--open-ota") + $captureArgs
    } elseif ($OpenFunctionInitial) {
        $captureArgs = @("--open-function-initial") + $captureArgs
    } elseif ($OpenFunction) {
        $captureArgs = @("--open-function") + $captureArgs
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
} elseif ($OpenNotificationBubble) {
    $captureArgs = @("--open-notification-bubble") + $captureArgs
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
} elseif ($OpenMusicCatalog) {
    $runArgs += "--open-music-catalog"
} elseif ($OpenMusicAccount) {
    $runArgs += "--open-music-account"
} elseif ($OpenMusic) {
    $runArgs += "--open-music"
} elseif ($OpenDropdown) {
    $runArgs += "--open-dropdown"
    } elseif ($OpenWifi) {
        $runArgs += "--open-wifi"
    } elseif ($OpenCalendar) {
        $runArgs += "--open-calendar"
    } elseif ($OpenWallpaper) {
        $runArgs += "--open-wallpaper"
    } elseif ($OpenOta) {
        $runArgs += "--open-ota"
    } elseif ($OpenFunctionInitial) {
        $runArgs += "--open-function-initial"
    } elseif ($OpenFunction) {
        $runArgs += "--open-function"
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
} elseif ($OpenNotificationBubble) {
    $runArgs += "--open-notification-bubble"
}
$commandLine = "`"$ExePath`""
if ($runArgs.Count -gt 0) {
    $commandLine += " " + ($runArgs -join " ")
}
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine } | Out-Null

Write-Output $OutputPath
