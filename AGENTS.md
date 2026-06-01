<INSTRUCTIONS>
## 规则优先级与作用域

1. `CLAUDE.md` 内联行为准则是本仓库本地 agent 规则的最高优先级。
2. 默认启用本文件基础规则与“当前仓库规则（上下文库）”。
3. 仅当满足“当前项目专项规则触发条件”时，才启用当前项目专项规则。
4. 其余仓库规则冲突时，优先遵循：最小可运行改动 + 可验证 + 可回退。

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
- 新增/修改公开接口或非显然函数时补中文 Doxygen；关键变量、共享状态和魔法数字解释用途、单位或边界。

## ESP-IDF / 构建 / 硬件红线

- 默认使用 `FreeRTOS` 思路组织任务、同步和资源访问；详细 IDF 环境、build/flash/monitor 流程见 `docs/context/knowledge/project/agent-operational-rules.md`。
- 新功能或整体系统骨架设计时，优先用 `FreeRTOS` 搭出清晰的运行时结构：用 task 表达长期执行单元，用 queue / task notification 表达事件流，用 event group 表达 readiness 或组合状态，用 software timer 表达周期性调度，用 mutex / semaphore / critical section 表达共享资源保护；这样便于观察、调度和解释整个系统的运行。
- 用户正在学习 `FreeRTOS`：涉及任务通信、跨上下文事件、超时等待、互斥保护、状态通知或资源仲裁时，默认把合适的 `FreeRTOS` 原语作为第一选择（如 queue、event group、task notification、mutex、semaphore、critical section），避免用裸 `volatile`、临时轮询或自造 flag 协议替代同步语义。
- 如果某处没有使用 `FreeRTOS` 原语，必须能说清楚原因，例如同一线程内纯局部状态、已有 owner 提供更高层同步 API、或第三方组件已有固定同步模型；不能只是为了少写代码而绕开 `FreeRTOS`。
- 新增或重构相关代码时，默认用简短说明解释选择该 `FreeRTOS` 原语的原因、它解决的并发问题，以及对应的操作系统概念，帮助用户把代码和 `FreeRTOS` 学习对应起来。
- 只有确认 `export.ps1` 可用后，才执行 `idf.py build` 或其他 `idf.py` 构建动作；修改过 `sdkconfig` 时必须先 `idf.py fullclean` 再 `idf.py build`。
- 常规 C/C++ 代码、UI 逻辑或业务 service 改动在 `idf.py build` 通过后，默认使用 `idf.py -p <PORT> app-flash`；不得把 `idf.py flash` 作为默认烧录命令，因为它会按 `build/flasher_args.json` 写入多个分区。
- 如需串口验证，优先 `app-flash` 后再限时采集 `monitor` / 串口日志，避免默认 `idf.py flash monitor`。
- 修改 `GPIO` / `I2C` / `SPI` / `UART` / `I2S` / `LCD` / `Touch` / `Wi-Fi` / `BLE` 前，必须先确认引脚定义、初始化顺序、时钟或带宽约束，以及错误恢复路径。
- 测试 `Light Sleep` / `Deep Sleep` / 外部唤醒源前，必须先写清楚唤醒源、观测口、兜底唤醒和恢复步骤；测试代码默认关闭，禁止随开机自动进入 sleep。依赖 USB 串口/JTAG 观测时，不得把测试结果当作可靠闭环，应优先准备外部 UART 日志或手动 BOOT/RST 恢复路径。
- 不要在没有证据的情况下删除已有 `reset`、`delay`、power-on sequence 或初始化命令序列。
- 不要随意修改 `partition table`、boot 流程、`OTA` 逻辑、`NVS` 关键结构、`Wi-Fi/BLE` 初始化主流程；若必须修改，要显式说明原因、影响范围和验证方法。

## 当前仓库规则（默认生效）

将 `docs/context` 作为项目长期上下文库，但默认低 token 使用：

- 首读只看 `docs/context/INDEX.agent.md` 与 `docs/context/knowledge/project/project-profile.md`；不要默认全量打开 `README.md`、`knowledge-map.md`、`repo-overview.md` 或 `knowledge/**`。
- 非简单任务默认用 `uv run python scripts/context/validate_context.py --level light --q "<任务关键词/文件/错误码>" --brief`，避免重复尝试并只生成低 token brief。
- 新增功能、跨模块改动、后台能力、低功耗、OTA、音频/网络/危险识别协作类任务，默认先按 `docs/context/knowledge/project/runtime-owner-contract.md` 判断启动阶段、资源 owner、调用方向和禁止加层边界。
- 出现可复用知识、流程、决策、attempt 或交接状态时，按 `docs/context/procedures/context-garden-policy.md` 写入对应层，并更新 `docs/context/CHANGELOG.md`。
- 上下文验证按影响范围分级执行：普通任务只用 `uv run python scripts/context/validate_context.py --level light --q "<任务关键词>" --brief`；只改 context 文档用 `--level standard`；改入口或检索基准用 `--level routing`；改 `scripts/context` 或记忆/晋升/归档机制才用 `--level full`。

## 规划/框架类任务强制路由

当用户说“规划、框架、整体方案、路线、架构、先搭起来、按之前方案、上下文库里有”等关键词时：

1. 不要先读代码实现文件。
2. 先运行：
   `uv run python scripts/context/validate_context.py --level light --q "<任务关键词>" --brief`
3. 如果命中 `docs/context/knowledge/project/*.md`、`docs/context/plans/**/*.md` 或 `docs/context/runs/**/*.md`：
   - 先打开命中的规划/架构文档；
   - 先总结文档里的目标、owner、边界、禁止项；
   - 再决定代码落点。
4. 只有在规划文档无法回答“改哪一层/哪个 owner”时，才继续读代码。
5. 禁止直接从当前代码现状反推产品框架。

## 嵌入式 C/C++ 代码生成默认规范

本仓库默认按 ESP32 / MCU 平台嵌入式 C/C++ 工程规范生成、评审和重构代码，适用于显示/UI、触摸输入、音频播放、Wi-Fi 配网、板级驱动、模块拆分和架构建议任务。

- 新增或修改代码应优先满足模块化、分层、单一职责、明确接口边界和可验证错误路径。
- 后续新增功能、跨文件改动和重构默认按 `App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK` 判断 owner 与调用方向；详细边界见 `docs/context/knowledge/project/layering-boundary-map.md`。
- 运行时骨架默认按 `docs/context/knowledge/project/runtime-owner-contract.md` 执行：不新增大而全 `ResourceManager`、`resource_policy`、`session_router` 或默认 `ui_manager`；新能力优先落到现有 owner，必要时新增窄 service/session。
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
- 新增/修改公开接口或非显然函数时补中文 `Doxygen`，实现内部说明优先使用 `//`。
- 关键变量、共享状态、协议常量、超时、阈值、缓冲区大小等必须解释用途、单位、来源或边界。
- 禁止无信息量注释，如“设置标志位”“调用初始化函数”“计数器加一”“进入循环”。
- 禁止无说明保留被注释掉的旧代码、临时 workaround 或特殊初始化顺序。
- 当任务目标是补注释或仅提升代码可读性时，默认只处理注释；若注释仍无法解决可读性问题，必须先获得用户明确允许，才可做最小重构。
- 详细注释风格见 `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md`。

兼容边界：

- 该规范不覆盖更高优先级的 system / developer 指令。
- 该规范不会替代现有上下文库流程。
- 该规范主要约束后续新增或修改代码，不要求一次性重构全部历史代码。

## 当前项目专项规则触发条件（限定作用域）

仅当满足任一条件时，启用“当前项目专项规则”：

- 用户明确要求参考或对齐当前仓库的显示/UI、触摸、音频播放、配网、时间天气或手表路线协作规范。
- 任务明确涉及 `LVGL`、`CO5300`、`FT5x06`、`TE` 同步、`main/ui`、`main/ui/custom`、`main/ui/custom/fonts`、`ai_chat_view`、`ui_runtime_fonts`、`ui_chinese_fonts`、`lv_font_`、`components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06`、`components/audio_codec`、`components/mp3_player`、`main/features/audio/audio_app.c`、`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`components/network_manager`、`main/app/hardware_init.c` 这些关键词或路径。
- 用户明确要求执行手表/屏幕路线，并提到表盘、多页面导航、触摸交互、传感器页面、BLE/WiFi 同步、时间天气、低功耗或电池续航。

未命中以上条件时，不启用当前项目专项规则，避免放大约束。

## 当前项目专项规则（仅在触发时生效）

- 显示/触摸、音频/存储、配网/联网、手表/屏幕路线的详细默认实践，统一以 `docs/context/knowledge/project/agent-operational-rules.md` 为准。
- 命中 hearing-assist / danger reminder / ESP-DL 危险提醒任务时，产品边界、状态机、参数口径和当前固件归属统一以这 4 张卡为准：
  - `docs/context/knowledge/project/hearing-assist-danger-alert-system-architecture.md`
  - `docs/context/knowledge/project/hearing-assist-danger-alert-state-machine-and-notification-policy.md`
  - `docs/context/knowledge/project/hearing-assist-danger-alert-parameter-defaults-table.md`
  - `docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`
- 该产品线 active danger 默认只认 `siren / horn / alarm`；`glass_break / crash / impact` 默认只保留在 challenger / 扩展实验，不得静默并入 active 主线。
- 涉及该产品线实现时，默认由 `danger_detection_service` 负责公共状态机与连续证据融合，`app_alert_manager` 负责提醒编排，`danger_detection_controller` 只负责页面展示；不要再把专页生命周期当作长期功能 owner。
- 先稳定再优化，先可观测再调优；先输出证据（日志、编译结果、关键指标）再下结论。`git` 提交信息和新增/修改代码注释优先使用中文，接口名、协议字段名、库名等不可翻译标识符保留英文。
  </INSTRUCTIONS>
