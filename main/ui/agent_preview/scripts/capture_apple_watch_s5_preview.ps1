param(
    [string]$ExePath = "D:\esp32S3\111\main\ui\agent_preview\host_runner\build\agent_preview_host.exe",
    [string]$OutputPath = "D:\esp32S3\111\main\ui\agent_preview\artifacts\wifi-management-image-to-code.png"
)

$ErrorActionPreference = "Stop"

# 显式加入 MSYS2 MinGW64 DLL 搜索路径，确保模拟器运行时不丢失动态链接库
$env:PATH = "D:\MSYS2\mingw64\bin;D:\MSYS2\usr\bin;" + $env:PATH

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies "System.Drawing", "System.Drawing.Common", "System.Drawing.Primitives", "System.Private.Windows.GdiPlus", "System.Private.Windows.Core" -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;

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

    public delegate bool EnumWindowsDelegate(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsDelegate lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);

    private static IntPtr matchedWindow = IntPtr.Zero;

    public static IntPtr FindWindowForProcess(int processId, string title) {
        matchedWindow = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint windowProcessId;
            GetWindowThreadProcessId(hWnd, out windowProcessId);
            if (windowProcessId != (uint)processId) {
                return true;
            }

            StringBuilder sb = new StringBuilder(256);
            GetWindowText(hWnd, sb, 256);
            if (sb.ToString() == title) {
                matchedWindow = hWnd;
                return false;
            }

            return true;
        }, IntPtr.Zero);
        return matchedWindow;
    }

    public static bool CaptureWindow(IntPtr hwnd, string outputPath) {
        RECT rect;
        if (!GetWindowRect(hwnd, out rect)) return false;
        
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width <= 0 || height <= 0) return false;
        
        using (Bitmap bmp = new Bitmap(width, height)) {
            using (Graphics g = Graphics.FromImage(bmp)) {
                IntPtr hdc = g.GetHdc();
                try {
                    // PW_RENDERFULLCONTENT = 2
                    bool success = PrintWindow(hwnd, hdc, 2);
                    if (!success) {
                        success = PrintWindow(hwnd, hdc, 0);
                    }
                }
                finally {
                    g.ReleaseHdc(hdc);
                }
            }
            bmp.Save(outputPath, ImageFormat.Png);
        }
        return true;
    }
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
$deadline = (Get-Date).AddSeconds(12)
$hwnd = [IntPtr]::Zero
while ($hwnd -eq [IntPtr]::Zero) {
    $now = Get-Date
    if ($now -ge $deadline) {
        break
    }
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    
    # Find window by process ID and title
    if (-not $proc.HasExited) {
        $hwnd = [User32Capture]::FindWindowForProcess($proc.Id, "Agent Preview - GUI Guider Generated")
    }
}

if ($hwnd -eq [IntPtr]::Zero) {
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
    throw "Preview window did not appear before timeout."
}

[User32Capture]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 1200 # 留出足够的时间进行渲染和呼吸灯动画开始

# 调用强健的 GDI 内存 PrintWindow 截图方法
$res = [User32Capture]::CaptureWindow($hwnd, $OutputPath)
if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}

if (-not $res) {
    throw "GDI PrintWindow capture failed."
}

Write-Output $OutputPath
