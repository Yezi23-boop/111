---
id: 2026-03-31-i2c-manager-new-driver-migration-design
tags: spec, i2c, esp32-s3, esp-idf, audio, touch
summary: 将当前仓库 i2c_manager 迁移到 idf-xiaozhi 新版 I2C master bus 接口，并同步调整调用方与测试契约。
last_reviewed: 2026-03-31
---

# i2c_manager 新版驱动迁移设计

## 1. 目标

把当前仓库的 `components/i2c_manager` 从旧版 legacy `driver/i2c.h` 统一管理方式，迁移为对齐 `C:\Users\ye\Desktop\idf-xiaozhi\components\i2c_manager` 的新版实现：

- `i2c_manager` 公开 `i2c_manager_get_bus_handle()`
- 在 `ESP-IDF >= 5.3` 下优先使用 `driver/i2c_master.h` 的 master bus API
- `CMakeLists.txt` 增加 `esp_driver_i2c` 依赖
- `audio_codec` 等调用方按新版接口获取共享总线句柄
- 同步更新源码契约测试，允许并要求新版接口存在

本次迁移目标是“严格替换成新版接口”，不保留“测试仍禁止 `i2c_manager_get_bus_handle`”这一旧约束。

## 2. 背景与证据

- 当前项目共享 I2C 总线位于 `GPIO14/GPIO15`，触摸、音频控制面共用一条总线。
  证据：`docs/context/knowledge/project/display-touch-audio-bus-map.md`
- 当前 `i2c_manager` 仅暴露 legacy `port` 接口，`scan()` 也基于 `i2c_master_cmd_begin()` 手工探测。
  证据：`components/i2c_manager/include/i2c_manager.h`、`components/i2c_manager/i2c_manager.c`
- `idf-xiaozhi` 的新版 `i2c_manager` 已切换到 master bus 语义，并新增 `i2c_manager_get_bus_handle()`。
  证据：`C:\Users\ye\Desktop\idf-xiaozhi\components\i2c_manager\include\i2c_manager.h`、`C:\Users\ye\Desktop\idf-xiaozhi\components\i2c_manager\i2c_manager.c`
- 当前仓库已有测试显式禁止 `i2c_manager_get_bus_handle`，与目标方案冲突，必须同步修改。
  证据：`tests/test_audio_codec_port_source.py`

## 3. 约束

- 不覆盖当前工作区里与本任务无关的用户改动，尤其是 `components/audio_codec/*`、`main/audio_app.c`、`components/mp3_player/mp3_player.c` 上已有未提交修改。
- 迁移必须优先保证共享 I2C 总线稳定性，不能只替换 `i2c_manager` 而忽略触摸或音频控制面调用方式。
- 改动优先最小可运行闭环，不顺手扩展到 IMU、RTC 等尚未接入的从设备。
- 验证至少覆盖：
  - `i2c_manager` 组件接口和依赖变化
  - `audio_codec` 对新 bus handle 的接入
  - 现有源码契约测试更新后可通过

## 4. 方案比较

### 方案 A：仅替换 `i2c_manager` 本体，外部继续只用 `get_port`

优点：
- 改动最少

缺点：
- 不能实现“严格替换成新版接口”
- `audio_codec` 仍停留在旧接法，和 `idf-xiaozhi` 不一致
- 在 `ESP-IDF 5.5.x` 下继续维持新旧 I2C 驱动混合状态，收益有限

结论：
- 不采用

### 方案 B：替换 `i2c_manager`，并只让 `audio_codec` 切到 bus handle

优点：
- 能打通当前最核心的新接口使用路径
- 改动相对可控

缺点：
- `touch_ft5x06` 仍是 legacy command-link 风格，项目内 I2C 使用方式会暂时并存
- 后续仍需确认 touch 侧是否与新版总线管理兼容

结论：
- 可作为最小落地路径

### 方案 C：替换 `i2c_manager`，并把触摸和音频都切到新版设备句柄模式

优点：
- 最彻底，项目内 I2C 访问模型统一

缺点：
- 牵涉更大，触摸读寄存器路径需要额外重构
- 本轮风险高于“最小可运行改动”

结论：
- 本轮不作为首选

## 5. 选定方案

采用“方案 B”作为本轮执行方案：

- 严格替换 `i2c_manager` 为新版实现
- `audio_codec` 对齐 `idf-xiaozhi`，接入 `i2c_manager_get_bus_handle()`
- `touch_ft5x06` 先保持现有读写模式，但要确认其在新版 `i2c_manager` 下仍可编译和运行
- 更新测试契约，使其与新版接口一致

这样可以先把共享 I2C 总线的管理入口切到新版，同时把最依赖 codec 控制接口的模块迁移过去，再决定是否继续把触摸侧完全设备句柄化。

## 6. 影响范围

预计直接改动文件：

- `components/i2c_manager/CMakeLists.txt`
- `components/i2c_manager/include/i2c_manager.h`
- `components/i2c_manager/i2c_manager.c`
- `components/audio_codec/audio_codec.c`
- `tests/test_audio_codec_port_source.py`

按编译结果可能补充检查的文件：

- `components/touch_ft5x06/touch_ft5x06.c`

## 7. 执行步骤

1. 用 `idf-xiaozhi` 新版内容替换当前 `i2c_manager` 的头文件、实现和 `CMakeLists.txt`。
2. 在 `audio_codec.c` 中对齐 `idf-xiaozhi` 写法：
   - `audio_codec_i2c_cfg_t` 保留 `.port`
   - 在 `ESP-IDF >= 5.3` 下填充 `.bus_handle = i2c_manager_get_bus_handle()`
   - 空句柄时给出显式错误日志并返回失败
3. 更新 `tests/test_audio_codec_port_source.py`：
   - 头文件中应存在 `i2c_manager_get_bus_handle`
   - `audio_codec.c` 中应使用 `i2c_manager_get_bus_handle()`
4. 运行最小验证命令，检查测试和编译结果。
5. 若发现 `touch_ft5x06` 因新版依赖或头文件变化产生编译问题，再做最小兼容修正。

## 8. 验证计划

优先验证命令：

```powershell
python -m unittest tests.test_audio_codec_port_source
```

如需进一步确认组件能编译，再执行：

```powershell
idf.py build
```

验证关注点：

- `i2c_manager.h` 已暴露 `i2c_manager_get_bus_handle()`
- `audio_codec.c` 在新接口下可通过源码契约检查
- `i2c_manager` 的 `REQUIRES` 已包含 `esp_driver_i2c`
- 构建期间无新旧 I2C 头文件/类型冲突

## 9. 风险与回滚

主要风险：

- `touch_ft5x06` 仍使用 legacy command-link，若新版总线实现与其组合存在兼容问题，可能导致触摸初始化或读点异常
- 当前工作区 `audio_codec` 已有未提交改动，迁移时若直接覆盖整文件，容易误伤用户已有更改
- 若测试只改断言但未同步实际调用方，可能出现“契约通过但运行未接新接口”的假阳性

回滚策略：

- 仅回退本轮改动的 `i2c_manager`、`audio_codec.c`、`tests/test_audio_codec_port_source.py`
- 若 `touch_ft5x06` 被迫做兼容改动，也只回退该最小修补
- 回滚标准：出现编译失败、I2C 类型冲突、或共享总线初始化链路被破坏时，优先恢复到当前项目旧版 `i2c_manager`

## 10. 完成判据

满足以下条件才算本轮迁移完成：

- 当前仓库 `i2c_manager` 已切到 `idf-xiaozhi` 新版接口
- `audio_codec` 已使用 `i2c_manager_get_bus_handle()`
- 测试契约已更新并通过
- 未引入与当前工作区已有改动的冲突
