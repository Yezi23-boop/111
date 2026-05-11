param(
    [string]$SourceDir = "D:\esp32S3\111\main\ui\agent_preview\host_runner",
    [string]$BuildDir = "D:\esp32S3\111\main\ui\agent_preview\host_runner\build"
)

$ErrorActionPreference = "Stop"

$env:PATH = "D:\MSYS2\mingw64\bin;D:\MSYS2\usr\bin;" + $env:PATH

cmake -S $SourceDir -B $BuildDir -G "MinGW Makefiles" -DCMAKE_C_COMPILER="D:/MSYS2/mingw64/bin/gcc.exe"
cmake --build $BuildDir -j 2

Write-Output (Join-Path $BuildDir "agent_preview_host.exe")
