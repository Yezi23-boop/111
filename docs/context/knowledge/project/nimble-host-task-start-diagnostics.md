---
id: nimble-host-task-start-diagnostics
tags: [project, ble, provisioning, nimble, freertos, esp32-s3]
summary: 记录在 NimBLE HCI 初始化已恢复后，板端仍未出现 `BLE host task started` / advertising 日志时，当前仓库采用的最小诊断探针与判断依据。
last_reviewed: 2026-04-03
---

# NimBLE Host Task 启动诊断

## 现象

- 单击按键后，板端已经进入 BLE 启动路径。
- `hci inits failed / nimble host init failed` 已经消失。
- 但串口仍未出现：
  - `ble_prov: BLE host task started`
  - `ble_prov: BLE provisioning advertising: ESP32S3-xxxx`

## 当前判断

- 当前阻塞点已经从 `esp_nimble_hci_init()` 前移到更后面的阶段。
- `components/wifi_provision/src/ble_server/ble_provision_transport.c` 中，
  `BLE host task started` 日志位于 `ble_provision_host_task()` 的第一行。
- 如果这条日志完全没有出现，优先怀疑：
  - `nimble_port_freertos_init()` 内部没有真正创建出 `nimble_host`
  - 或者 host task 在运行前就没有被调度起来

## 关键证据

- ESP-IDF 5.5.3 的 `nimble_port_freertos_init()` 位于：
  - `D:/esp32/v5.5.3/esp-idf/components/bt/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c`
- 该实现内部直接调用 `xTaskCreatePinnedToCore(...)`，但**没有检查返回值**。
- 因此在任务创建失败时，默认日志里可能没有明确错误，而上层仍误以为 BLE host 已成功拉起。

## 当前仓库中的诊断探针

- `ble_provision_transport_start()` 在第一次启动 NimBLE 时会额外打印：
  - 启动前的 `MALLOC_CAP_INTERNAL` 剩余堆
  - `nimble_port_freertos_init()` 后是否能通过 `xTaskGetHandle("nimble_host")` 查到任务
- 如果查不到 `nimble_host`，当前固件会：
  - 打印 `nimble host task missing after init`
  - 连同内部堆余量一起输出
  - 调用 `nimble_port_deinit()` 回滚
  - 向上层返回失败，避免把“静默失败”误判成“BLE 已启动”

## 下一轮板端验证应该看什么

- 如果日志出现：
  - `ble_prov: nimble host task created, internal_heap=...`
  - 但仍没有 `BLE host task started`
  - 说明问题更偏向调度/运行时异常，而不是任务创建失败
- 如果日志出现：
  - `ble_prov: nimble host task missing after init, internal_heap=...`
  - 说明当前问题已经收敛到 host task 创建失败，可优先从内部堆、任务栈和 pinned core 继续排查

## 对后续 agent 的建议

- 不要再把这类现象直接归因为 advertising payload 或微信小程序。
- 先看是否已有 `nimble host task created/missing after init` 证据，再决定下一步是查 FreeRTOS 资源，还是继续追 NimBLE 同步/广告阶段。
