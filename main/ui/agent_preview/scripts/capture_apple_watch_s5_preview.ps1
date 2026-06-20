param(
    [string]$ExePath = "D:\esp32S3\111\main\ui\agent_preview\host_runner\build\agent_preview_host.exe",
    [string]$OutputPath = "D:\esp32S3\111\main\ui\agent_preview\artifacts\apple-watch-s5-preview.png"
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class User32Capture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
}
"@

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

$proc = Start-Process -FilePath $ExePath -PassThru
try {
    $deadline = (Get-Date).AddSeconds(12)
    $hwnd = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 250
        $proc.Refresh()
        $hwnd = $proc.MainWindowHandle
        if ($hwnd -eq [IntPtr]::Zero) {
            # Fallback: Find window by title
            $hwnd = [User32Capture]::FindWindow($null, "Agent Preview - Apple Watch S5 Style")
        }
    } while ($hwnd -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline)

    if ($hwnd -eq [IntPtr]::Zero) {
        throw "Preview window did not appear before timeout."
    }

    [User32Capture]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 800

    $rect = New-Object User32Capture+RECT
    if (-not [User32Capture]::GetWindowRect($hwnd, [ref]$rect)) {
        throw "Failed to read preview window rectangle."
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        throw "Invalid preview window rectangle: ${width}x${height}."
    }

    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }

    Write-Output $OutputPath
}
finally {
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
