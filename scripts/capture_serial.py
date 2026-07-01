"""采集 COM3 串口日志，DTR 复位触发冷启动，保存到文件。

本轮用于冷启动基线：采集 65 秒，覆盖冷启动 8 秒延迟后的
cold_boot_resource_snapshot_done 采样点（含 all task stack high-water mark）。
"""
import serial
import time
import sys

PORT = "COM3"
BAUD = 115200
DURATION_SEC = 65
OUT = r"D:\esp32S3\111\board_logs\2026-06-28-owner-init-stack-sampling.log"

# DTR 复位触发干净冷启动，确保采样点来自完整 boot 流程
ser = serial.Serial(PORT, BAUD, timeout=1)
ser.dtr = False
ser.dtr = True
time.sleep(0.5)

lines = []
t0 = time.monotonic()
print(f"[capture] started at {time.strftime('%H:%M:%S')}, writing to {OUT}", flush=True)

# 实时打印关键行，便于观察采样是否命中
KEYWORDS = (
    "STACK:",              # 栈高水位数据行（printf_esp32_all_task_stack_stats 输出）
    "cold_boot_resource_snapshot_done",  # 冷启动采样完成标志
    "internal_free",       # 内存快照汇总行
    "memory_watch",        # 内存快照
    "boot_stage",          # boot 阶段标志
    "network_service_ready",
    "Guru",
    "panic",
    "PSRAM:",
    "RAM:",
    "IRAM:",
    "high_water",
)

try:
    while time.monotonic() - t0 < DURATION_SEC:
        try:
            raw = ser.readline()
            if raw:
                try:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                except Exception:
                    line = str(raw)
                lines.append(line)
                if any(kw in line for kw in KEYWORDS):
                    print(line, flush=True)
        except serial.SerialTimeoutException:
            pass
except KeyboardInterrupt:
    pass

ser.close()
elapsed = time.monotonic() - t0
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print(f"[capture] done: {len(lines)} lines in {elapsed:.0f}s -> {OUT}", flush=True)
