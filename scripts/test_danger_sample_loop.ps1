#!/usr/bin/env pwsh
<#
.SYNOPSIS
    危险样本 SD 卡闭环测试监控脚本。
.DESCRIPTION
    监控串口日志，捕获危险检测和样本录制相关的关键事件。
    同时检查 SD 卡上的样本文件。
.PARAMETER Port
    串口号，默认 COM3。
.PARAMETER Duration
    监控时长（秒），默认 120。
#>

param(
    [string]$Port = "COM3",
    [int]$Duration = 120
)

$ErrorActionPreference = "Stop"

# 设置 ESP-IDF 环境
Write-Host "=== 危险样本 SD 卡闭环测试 ===" -ForegroundColor Cyan
Write-Host "串口: $Port, 监控时长: ${Duration}s" -ForegroundColor Cyan
Write-Host ""

# 关键日志关键词
$keywords = @(
    "danger_recorder",
    "danger_detection",
    "espdl",
    "audio_runtime",
    "safety_monitor",
    "background_service",
    "PCM tap",
    "capture",
    "sample saved",
    "WAV",
    "JSON",
    "alert",
    "ALERTING"
)

# 预期事件序列
$expectedEvents = @(
    "危险样本录制器已初始化",
    "PCM tap 回调注册",
    "ESP-DL 单模型运行时已启动",
    "录制请求已提交",
    "已写入 WAV 文件",
    "已写入 JSON 文件",
    "样本录制完成"
)

$foundEvents = @()
$startTime = Get-Date
$endTime = $startTime.AddSeconds($Duration)

Write-Host "开始监控..." -ForegroundColor Green
Write-Host "请执行以下操作：" -ForegroundColor Yellow
Write-Host "1. 触摸屏幕唤醒设备" -ForegroundColor Yellow
Write-Host "2. 进入危险检测页面" -ForegroundColor Yellow
Write-Host "3. 启用危险识别服务" -ForegroundColor Yellow
Write-Host "4. 播放警笛/喇叭声（靠近麦克风）" -ForegroundColor Yellow
Write-Host "5. 等待检测触发和样本保存" -ForegroundColor Yellow
Write-Host ""
Write-Host "按 Ctrl+C 提前结束监控" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Cyan

# 使用 Python 进行串口监控
$pythonScript = @"
import serial
import time
import sys
import re

port = '$Port'
duration = $Duration
keywords = $(ConvertTo-Json $keywords -Compress)

try:
    s = serial.Serial(port, 115200, timeout=0.5)
    end_time = time.time() + duration
    events = []
    
    while time.time() < end_time:
        line = s.readline()
        if line:
            text = line.decode('utf-8', 'replace').rstrip()
            # 过滤关键日志
            if any(k.lower() in text.lower() for k in keywords):
                print(text, flush=True)
                events.append(text)
    
    s.close()
    
    # 分析事件序列
    print('\n=== 事件分析 ===')
    expected = [
        '危险样本录制器已初始化',
        'PCM tap',
        'ESP-DL',
        '录制请求已提交',
        '已写入 WAV',
        '已写入 JSON',
        '样本录制完成'
    ]
    
    found = []
    for exp in expected:
        for event in events:
            if exp in event:
                found.append(exp)
                break
    
    print(f'找到 {len(found)}/{len(expected)} 个预期事件')
    for f in found:
        print(f'  ✓ {f}')
    
    missing = [e for e in expected if e not in found]
    for m in missing:
        print(f'  ✗ {m} (未找到)')
    
except Exception as e:
    print(f'错误: {e}', file=sys.stderr)
    sys.exit(1)
"@

# 保存 Python 脚本
$scriptPath = Join-Path $env:TEMP "danger_test_monitor.py"
$pythonScript | Out-File -FilePath $scriptPath -Encoding UTF8

# 运行监控
try {
    python $scriptPath
} finally {
    # 清理临时文件
    if (Test-Path $scriptPath) {
        Remove-Item $scriptPath -Force
    }
}

Write-Host ""
Write-Host "=== 监控结束 ===" -ForegroundColor Cyan

# 检查 SD 卡文件
Write-Host ""
Write-Host "检查 SD 卡样本文件..." -ForegroundColor Green
Write-Host "请将 SD 卡插入电脑，检查以下目录：" -ForegroundColor Yellow
Write-Host "  \sdcard\danger_samples\" -ForegroundColor Yellow
Write-Host "预期文件：" -ForegroundColor Yellow
Write-Host "  - WAV 文件（约 2 秒，16kHz 单声道）" -ForegroundColor Yellow
Write-Host "  - JSON 元数据文件" -ForegroundColor Yellow
