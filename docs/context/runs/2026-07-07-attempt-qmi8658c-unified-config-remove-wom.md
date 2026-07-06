---
id: run-2026-07-07-qmi8658c-unified-config-remove-wom
tags: imu, qmi8658c, unified-config, wom-removal, int-disabled, esp-idf
summary: QMI8658C driver 收口到统一 config/read 契约，移除 WoM public API 与 service 运行路径，并预留 disabled-only 芯片 INT 字段。
date: 2026-07-07
last_reviewed: 2026-07-07
memory_type: episodic
scope: imu
owners: components/qmi8658c, main/services/imu_service.c, main/app/board_imu.c, docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md
status: completed
evidence_level: build-and-flash
---

# QMI8658C 统一配置 + 移除 WoM

## 背景

用户明确要求“不需要 WoM，统一配置就行”，并确认第一版只需要 QMI8658C driver 对外输出物理量，后续采样、跌倒检测或 data-ready 中断再由 service/BSP 层扩展。此前 WoM/INT1 已做过 COM3/COM7 板测闭环，但不再作为当前固件运行主线。

## 本轮改动

- `components/qmi8658c` public API 保留 `init/init_bus/probe/config/read`，删除 public `enable_wom/disable_wom/read_wom/read_int` 与相关 WoM/status 类型。
- `qmi8658c_config_t` 新增 `int1_source/int2_source` 预留字段；第一版唯一支持 `QMI8658C_INT_SOURCE_DISABLED`，非 disabled 返回 `ESP_ERR_NOT_SUPPORTED`。
- `qmi8658c_config()` 统一负责 `CTRL2/CTRL3/CTRL7`，即加速度/陀螺仪量程、ODR 与 sensor enable；不写 INT1/INT2 事件源寄存器。
- `qmi8658c_read()` 在未成功 `qmi8658c_config()` 前返回 `ESP_ERR_INVALID_STATE`，避免物理量换算使用默认或未知量程。
- `imu_service` 删除 GPIO21 ISR、task notification、WoM poll fallback 和 motion window 路径；当前 task 只做 `probe -> qmi8658c_config()`，配置 `accel_fs=3`（±16g）、`gyro_fs=7`（±2048 dps）、`int1_source/int2_source=DISABLED` 后发布 snapshot。
- `board_imu` 继续保留 `qmi_int1_gpio` 作为板级硬件事实，但当前 service 不消费该 GPIO。

## 关键判断

- ESP32 GPIO ISR 不属于 QMI8658C driver 职责；后续若启用 data-ready/FIFO 中断，应由 service 读取 `board_imu` 事实后安装 GPIO ISR。
- 芯片侧 INT 字段只是配置结构语义预留，不代表本轮启用任何芯片中断事件源。
- WoM/INT1 历史证据仍保留在 knowledge/run 中，但后续实现不应从旧 WoM fallback 代码路径继续补丁式扩展。

## 验证

- `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`：22 tests passed。
- `git diff --check -- . ':!managed_components'`：通过，仅输出 Git LF/CRLF 工作区提示，无 whitespace error。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过；`111.bin` 大小 `0xac3230`，最小 app 分区剩余 `0x33cdd0`（23%）。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py -p COM7 app-flash`：通过；写入 `11285040` bytes，hash verified，hard reset 完成。
- `uv run python scripts/context/validate_context.py --level standard --q "qmi8658c unified config remove wom imu service int disabled" --brief`：通过；错误 0、警告 0。

## 下一步

- 如果后续要做跌倒检测连续采样，应在 `imu_service` 新增 50Hz 采样 owner 与窗口出口，复用当前 `qmi8658c_config()/qmi8658c_read()` 契约，不恢复 WoM。
