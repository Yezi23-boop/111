param(
    [string]$SourceDir = "D:\esp32S3\111\tools\ui_preview\host_runner",
    [string]$BuildDir = "D:\esp32S3\111\tools\ui_preview\host_runner\build"
)

$ErrorActionPreference = "Stop"

# 如果前一次留下的预览窗口还在运行，先杀掉它以释放文件锁确保编译通过
Get-Process -Name "agent_preview_host" -ErrorAction SilentlyContinue | Stop-Process -Force

$env:PATH = "D:\MSYS2\mingw64\bin;D:\MSYS2\usr\bin;" + $env:PATH

cmake -S $SourceDir -B $BuildDir -G "MinGW Makefiles" -DCMAKE_C_COMPILER="D:/MSYS2/mingw64/bin/gcc.exe"
if ($LASTEXITCODE -ne 0) {
    throw "Preview CMake configure failed with exit code $LASTEXITCODE."
}

cmake --build $BuildDir -j 2
if ($LASTEXITCODE -ne 0) {
    throw "Preview build failed with exit code $LASTEXITCODE."
}

Write-Output (Join-Path $BuildDir "agent_preview_host.exe")
