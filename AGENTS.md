<INSTRUCTIONS>
## 规则优先级与作用域

1. 默认只使用“当前仓库规则（上下文库）”。
2. 仅当满足“当前项目专项规则触发条件”时，才启用当前项目专项规则。
3. 若规则冲突，优先遵循：最小可运行改动 + 可验证 + 可回退。

## CLAUDE.md（最高优先级，已内联）

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" -> "Write tests for invalid inputs, then make them pass"
- "Fix the bug" -> "Write a test that reproduces it, then make it pass"
- "Refactor X" -> "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] -> verify: [check]
2. [Step] -> verify: [check]
3. [Step] -> verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

默认补充约束：

- 遇到底层代码、复杂问题或证据不足时，可以联网查阅 `ESP-IDF` 官方资料，优先使用官方文档与一手资料。
- 默认直接在当前仓库内修改，不强制使用 `worktree`。
- 写代码时要优先保证用户可读性；默认遵循下方“代码注释规范（默认生效）”。
- 每一个新增或修改的函数，都必须补中文注释；关键变量、关键常量也要补用途说明。

## ESP-IDF Rules
- 默认使用 `FreeRTOS` 思路组织任务、同步和资源访问。
- 需要拉起 `IDF 5.5` 环境时，必须按以下顺序查找并使用 `export.ps1`：
  1. 先尝试 `D:\esp-idf\v5.5.3\esp-idf\export.ps1`。
  2. 若不存在，再尝试 `D:\esp32\v5.5.3\esp-idf\export.ps1`。
  3. 若仍不存在，再检查 `$env:IDF_PATH`，或在本机搜索 `export.ps1`。
- 只有确认 `export.ps1` 可用后，才执行 `idf.py build` 或其他 `idf.py` 构建动作。
- 项目配置基线优先参考 `sdkconfig.defaults`；不要在没有说明的情况下大范围修改生成出来的 `sdkconfig`。
- 只要修改过 `sdkconfig`，就必须先执行 `idf.py fullclean`，再执行 `idf.py build`，避免配置变更没有进入最终产物。
- 默认不走 `test app` 路线；除非任务明确要求，否则直接修改主工程并完成验证。

## Monitor And Flash Rules

- `idf.py monitor` 没有自然返回值；自动化验证时默认采集窗口为 `30` 秒，最长不超过 `1` 分钟。
- Windows Codex 桌面环境下，如需让 agent 直接读取 `idf.py monitor`，可临时设置 `ESP_IDF_MONITOR_TEST=1` 绕过 `TTY` 检查；该变量只用于 host 侧采集，不属于固件配置。
- 只要使用过 `ESP_IDF_MONITOR_TEST=1`，就必须清理残留的 `idf_monitor` / `idf.py monitor` 进程，并确认串口已释放，再进行下一轮 `flash`、`monitor` 或串口验证。
- 如果 `monitor` 日志提示 built/flashed checksum mismatch，不得把板端日志直接当作当前本地 `build` 产物的验证结论；必须单独标注镜像漂移风险，必要时重做一致的 `build` / `flash` / `monitor` 闭环。

## Hardware And Safety Rules

- 修改 `GPIO` / `I2C` / `SPI` / `UART` / `I2S` / `LCD` / `Touch` / `Wi-Fi` / `BLE` 前，必须先确认引脚定义、初始化顺序、时钟或带宽约束，以及错误恢复路径。
- 不要在没有证据的情况下删除已有 `reset`、`delay`、power-on sequence 或初始化命令序列。
- 只要涉及 `DMA`、双缓冲、`PSRAM`、cache、共享总线、共享状态机，就必须说明数据生命周期、资源归属和性能影响。
- 不要随意修改 `partition table`、boot 流程、`OTA` 逻辑、`NVS` 关键结构、`Wi-Fi/BLE` 初始化主流程；若必须修改，要显式说明原因、影响范围和验证方法。

## 当前仓库规则（默认生效）

将 `docs/context` 作为项目长期上下文库，但默认低 token 使用：

- 首读只看 `docs/context/INDEX.agent.md` 与 `docs/context/knowledge/project/project-profile.md`；不要默认全量打开 `README.md`、`knowledge-map.md`、`repo-overview.md` 或 `knowledge/**`。
- 非简单任务先查历史尝试：`uv run python scripts/context/query.py --scope runs --q "<任务关键词/文件/错误码>" --top 8`，避免重复已失败或已完成路径。
- 再查稳定知识并打 brief：`uv run python scripts/context/query.py --q "<任务关键词>" --top 5`，必要时 `uv run python scripts/context/pack_context.py --q "<任务关键词>" --mode brief --top 5 --print`。
- 出现可复用知识、流程、决策、attempt 或交接状态时，按 `docs/context/procedures/context-garden-policy.md` 写入对应层，并更新 `docs/context/CHANGELOG.md`。
- 修改上下文系统后运行 `uv run python scripts/context/build_index.py`、`uv run python scripts/context/check.py`、`uv run python scripts/context/garden.py --verbose`、`uv run python scripts/context/eval_query.py`。

## 嵌入式 C/C++ 代码生成默认规范

本仓库默认按 ESP32 / MCU 平台嵌入式 C/C++ 工程规范生成、评审和重构代码，适用于显示/UI、触摸输入、音频播放、Wi-Fi 配网、板级驱动、模块拆分和架构建议任务。

- 新增或修改代码应优先满足模块化、分层、单一职责、明确接口边界和可验证错误路径。
- 新增或修改代码默认按 Google Code Style 靠拢，但不因引入新规则而大面积重排无关旧代码。
- 资源受限路径优先使用静态分配或受控分配，避免在高频路径中频繁申请和释放内存。
- 输入、状态、长度、返回值、超时和降级路径必须显式处理，不能静默失败。
- 算法或 AI 相关实现默认拆分为预处理、推理、后处理和模型配置模块，避免把阈值、量化参数和业务逻辑混写。
- 涉及新增模块、跨文件改动或明显重构时，输出代码前，应先给出文件划分方案和模块职责说明；若发现明显的高耦合、大函数、深层嵌套或不安全路径，应先重构方案再输出。
- 详细条款见 `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md`。

## 状态发布与 UI 读取原则（默认生效）

- `UI/LVGL` 高频路径默认只读快照，不在 `poll/timer/getter` 中顺手推进状态，也不做重 `IO`、网络启停、阻塞等待或重锁路径。
- 真实状态推进、重试、超时、错误恢复和硬件交互应由后台任务或 owner 模块负责；`getter` 默认无副作用。
- 若出现 `UI` 卡顿、刷新抖动、锁竞争或“读取状态导致行为变化”，优先按本原则重构，而不是继续堆 `delay`、`flag` 或日志绕过。

## 代码注释规范（默认生效）

- 适用于所有新增或修改的 `C/C++` 代码。
- 注释优先解释“为什么这样做”“受什么约束”“改掉会有什么风险”，不要逐行翻译代码。
- 所有新增或修改的函数，必须补中文 `Doxygen` 风格函数头；至少写清 `@brief`、关键参数、返回值；有时序、并发、上下文约束时要额外说明。
- 实现内部普通说明注释优先使用 `//`；多行原因说明可使用 `/* ... */`；`/** ... */` 仅用于 `Doxygen` 文档注释。
- 关键变量、共享变量、`volatile` 变量、状态位、哨兵值、长度/偏移/单位变量必须解释用途和边界。
- 协议常量、寄存器位、超时、重试次数、阈值、缓冲区大小、采样率、分包长度等魔法数字必须解释来源、单位和含义。
- 复杂代码块必须补“原因说明”注释，重点关注：延时、重试、上电顺序、状态机分支、错误恢复、协议拆包组包、共享状态同步、DMA/Cache 处理、兼容性绕行、降级路径、回滚路径。
- 结构体成员、枚举值、协议字段、状态位等行尾文档可使用 `/**< ... */`。
- 禁止无信息量注释，如“设置标志位”“调用初始化函数”“计数器加一”“进入循环”。
- 禁止无说明保留被注释掉的旧代码、临时 workaround 或特殊初始化顺序。
- 当任务目标是补注释或仅提升代码可读性时，默认只处理注释；若注释仍无法解决可读性问题，必须先获得用户明确允许，才可做最小重构。

兼容边界：

- 该规范不覆盖更高优先级的 system / developer 指令。
- 该规范不会替代现有上下文库流程。
- 该规范主要约束后续新增或修改代码，不要求一次性重构全部历史代码。

## 当前项目专项规则触发条件（限定作用域）

仅当满足任一条件时，启用“当前项目专项规则”：

- 用户明确要求参考或对齐当前仓库的显示/UI、触摸、音频播放、配网、时间天气或手表路线协作规范。
- 任务明确涉及 `LVGL`、`CO5300`、`FT5x06`、`TE` 同步、`main/ui`、`components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06`、`components/audio_codec`、`components/mp3_player`、`main/features/audio/audio_app.c`、`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`components/network_manager`、`main/app/hardware_init.c` 这些关键词或路径。
- 用户明确要求执行手表/屏幕路线，并提到表盘、多页面导航、触摸交互、传感器页面、BLE/WiFi 同步、时间天气、低功耗或电池续航。

未命中以上条件时，不启用当前项目专项规则，避免放大约束。

## 当前项目专项规则（仅在触发时生效）

- 显示/触摸、音频/存储、配网/联网、手表/屏幕路线的详细默认实践，统一以 `docs/context/knowledge/project/agent-operational-rules.md` 为准。
- 先稳定再优化，先可观测再调优；先输出证据（日志、编译结果、关键指标）再下结论。`git` 提交信息和新增/修改代码注释优先使用中文，接口名、协议字段名、库名等不可翻译标识符保留英文。
  </INSTRUCTIONS>
