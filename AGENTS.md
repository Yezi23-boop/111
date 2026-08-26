<INSTRUCTIONS>
## 规则优先级与作用域

1. “本地最高优先级行为准则”特指下方 `Think Before Coding`、`Simplicity First`、`Surgical Changes`、`Goal-Driven Execution` 四节正文；这四节是本仓库本地 agent 规则中的最高优先级，优先于本文件后续所有仓库规则。
2. 默认启用本文件基础规则与“当前仓库规则（上下文库）”。
3. 仅当满足“当前项目专项规则触发条件”时，才启用当前项目专项规则。
4. 其余仓库规则冲突时，优先遵循：最小可运行改动 + 可验证 + 可回退。

## 本地最高优先级行为准则

下方 `Think Before Coding`、`Simplicity First`、`Surgical Changes`、`Goal-Driven Execution` 四节正文优先于本文件后续所有仓库规则；当后续规则与这四节冲突时，优先选择更简单、更小、更可验证、影响范围更窄的方案。

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

## Agent 操作节流原则

- 默认按“最小实现 -> 聚焦验证 -> 批量收尾”的节奏工作，不要每个小修改都立即跑全量测试、build、板测、写 run 或更新 CHANGELOG。
- 同一任务内的多个小修复应合并到一个验证批次：先用最便宜的检查确认方向，例如编译局部、聚焦 source test 或静态检查；等代码形态稳定后，再统一执行 `idf.py build`、必要的全量测试和板端验证。
- `uv run python -m pytest tests`、`idf.py build`、`app-flash-monitor` 属于高成本验证。只有当改动已经收敛、准备结案，或前一层验证无法覆盖风险时才运行；不要因为每次补一行测试或改一个分支就重复执行。
- active plan、CHANGELOG 和 `runs/` 默认在一个可说明的小闭环结束后统一更新一次。同一阶段内的连续小修可以追加到同一条记录，不要每个微修复都新开记录或反复改文档。
- 板端验证要验证本轮真正改变的行为。若只是证明冷启动无 panic，通常一次最终 `app-flash-monitor` 足够；除非修改涉及启动路径、硬件初始化、真实交互流程或前一次板测暴露异常。

**活跃计划同步规则：不要在未更新活跃计划的情况下结束计划内任务。**
如果你正在执行的任务属于 `docs/context/plans/active/` 中的某个活跃项目计划，在宣布任务或代码修改完成之前，你**必须**使用工具更新该计划的“进度 (Progress)”部分（例如，将 `[ ]` 勾选为 `[x]`）。严禁在不更新活文档的情况下为计划内任务修改代码。
*（注意：对于不属于活跃计划的琐碎任务、简单 Bug 修复或日常沟通，此规则不适用，你也不需要做任何明确声明。）*

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

默认补充约束：

- 遇到底层代码、复杂问题或证据不足时，可以联网查阅 `ESP-IDF` 官方资料，优先使用官方文档与一手资料。
- 默认直接在当前仓库内修改，不强制使用 `worktree`。
- 写代码时要优先保证用户可读性；默认遵循下方“代码注释规范（默认生效）”。
- 向用户讲解技术概念时，专业英文术语默认后跟中文注释，格式如 `ring buffer（环形缓冲区）`、`notification（任务通知）`、`jitter（抖动）`、`I2S underrun（下溢）`，帮助用户建立中英文对应；代码注释中已有中文说明的除外。
- 新增/修改公开接口或非显然函数时补中文 Doxygen；关键变量、共享状态和魔法数字解释用途、单位或边界。
- 内存分配应尽量使用外部 PSRAM（如使用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 等 API），避免挤压 internal RAM；只把必须快速响应、DMA 传输或无法放在外部 RAM 的关键数据留给 internal RAM。

## ESP-IDF / 构建 / 硬件红线

- 默认用 `FreeRTOS` 原语（task / queue / task notification / event group / software timer / mutex / semaphore / critical section）组织任务、同步与资源访问；不用时必须能说清原因，新增代码时用简短说明解释所选原语解决的并发问题。用户正在学习 FreeRTOS，涉及并发/同步的教学场景默认以这些原语为第一选择。
- IDF 环境、build/flash/monitor 完整流程与引脚约束见 `docs/context/knowledge/project/agent-operational-rules.md`。三条硬底线：① `idf.py build` 通过后默认 `idf.py -p <PORT> app-flash`，禁止默认 `idf.py flash`；② 改过 `sdkconfig` 必须先 `idf.py fullclean` 再 build；③ 串口验证用 `scripts/board/agent_serial_monitor.ps1|.py`，禁止裸 `idf.py monitor`；板端烧录至少预留 2 分钟。
- 修改 `GPIO` / `I2C` / `SPI` / `UART` / `I2S` / `LCD` / `Touch` / `Wi-Fi` / `BLE` 前，必须先确认引脚定义、初始化顺序、时钟或带宽约束，以及错误恢复路径。
- 测试 `Light Sleep` / `Deep Sleep` / 外部唤醒源前，必须先写清楚唤醒源、观测口、兜底唤醒和恢复步骤；测试代码默认关闭，禁止随开机自动进入 sleep。依赖 USB 串口/JTAG 观测时，不得把测试结果当作可靠闭环，应优先准备外部 UART 日志或手动 BOOT/RST 恢复路径。
- 不要在没有证据的情况下删除已有 `reset`、`delay`、power-on sequence 或初始化命令序列。
- 不要随意修改 `partition table`、boot 流程、`OTA` 逻辑、`NVS` 关键结构、`Wi-Fi/BLE` 初始化主流程；若必须修改，要显式说明原因、影响范围和验证方法。

## 当前仓库规则（默认生效）

将 `docs/context` 作为项目长期上下文库，但默认低 token 使用：

### 服务器代码部署闭环

- 修改明确会部署到生产环境的 `server/**` 服务代码后，不能只完成本地测试或提交代码；测试通过后必须将对应代码上传到目标服务器。
- 上传后使用目标服务对应的 Compose 配置执行 `docker compose up -d --build`，并检查服务健康状态或运行对应 smoke test。
- 不要在规则、日志或提交中写入 SSH 地址、账号、密钥或其他部署凭据；如果缺少远程权限、凭据或部署目标不明确，必须明确报告“未完成部署”，不能把本地测试结果当作部署成功。

- 首读只看 `docs/context/INDEX.agent.md` 与 `docs/context/knowledge/project/project-profile.md`；不要默认全量打开 `README.md`、`knowledge-map.md`、`repo-overview.md` 或 `knowledge/**`。
- 遇到项目内不明确的问题，若可能由既有规划、稳定约束、owner 边界、历史决策或板端证据回答，先用上下文库索引按关键词检索，并只打开命中的相关文档；索引未提供充分证据时，再核对代码或官方资料。若仍存在会影响实现范围或行为的歧义，明确说明后向用户澄清，不得臆测。
- 涉及“当前 / 最新 / 生产 / 现网 / 现在”的事实判断，禁止直接依赖压缩摘要、上轮对话或旧记忆；必须先运行本轮 `validate_context.py --level light --q "<当前性关键词>" --brief`，并以本轮检索结果中的稳定知识卡和明确生命周期状态为准。
- 非简单任务默认用 `uv run python scripts/context/validate_context.py --level light --q "<任务关键词/文件/错误码>" --brief`，避免重复尝试并只生成低 token brief。（**降级策略**：如果 Python 脚本执行失败，必须立即改用 `grep_search` 工具直接搜索 `docs/context/` 目录，严禁因此放弃查阅上下文）。
- 新增功能、跨模块改动、后台能力、低功耗、OTA、音频/网络/危险识别协作类任务，默认先按 `docs/context/knowledge/project/runtime-owner-contract.md` 判断启动阶段、资源 owner、调用方向和禁止加层边界。
- 出现可复用知识、流程、决策、attempt 或跨会话接手状态时，按 `docs/context/procedures/context-garden-policy.md` 写入对应层；`CHANGELOG.md` 只记录后续有检索价值的摘要，不记录普通执行流水。
- `docs/context/handoffs/` 已退场，不再作为当前任务接力层。跨会话接手状态优先写到对应 `plans/active/` 的 Progress/Next Step；失败路线、特殊证据和可复用排查结论写入 `runs/`；稳定事实进入 `knowledge/`。
- `docs/context/runs/` 是反重复踩坑证据库，不是每次任务记录本。只有出现大问题错误、路线取舍、被证伪尝试、板端特殊证据、跨 owner 决策或高成本验证结论，并且这些信息未来可能防止重复误判/重复排查时，才写入 `docs/context/runs/`；大问题错误使用 `error-signature`，路线选择/放弃使用 `route-choice`，必要时补 `evidence`。普通成功改动、常规 build 通过、小修小补和没有复用价值的过程不写 `runs/`。
- 如果一个问题经历了长时间排查、多次失败路线、非显然关键发现、远端/板端/高成本验证，或未来 agent 很可能重复踩坑，结案前必须固化一条经验记录。记录应包含：症状/上下文 -> 失败路线 -> 关键发现 -> 最小解法 -> 验证证据 -> 后续不要重复的做法。普通小修、单次 build 失败、显然拼写/参数调整不写。
- 上下文验证按影响范围分级执行：普通任务只用 `uv run python scripts/context/validate_context.py --level light --q "<任务关键词>" --brief`；只改 context 文档用 `--level standard`；改入口或检索基准用 `--level routing`；改 `scripts/context` 或记忆/晋升/归档机制才用 `--level full`。
- 涉及 `FreeRTOS`、RAM/PSRAM、硬件驱动初始化、分区表或 `sdkconfig` 的改动，不因为触碰底层就默认新建 run；按上面的反重复踩坑标准判断。若任务属于 active plan，优先更新对应计划的 Progress/Validation/Next Step；只有 context 文档实际变更时，才按影响范围运行对应级别的 `validate_context.py`。

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

本仓库默认按 ESP32 / MCU 平台嵌入式 C/C++ 工程规范生成、评审和重构代码，完整条款见 `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md`。默认基线：

- 按 `App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK` 判断 owner 与调用方向；新能力优先落到现有 owner，必要时新增窄 service/session，不新增大而全管理器。
- 跨任务命令、状态、等待和资源仲裁优先 FreeRTOS 原语（queue、task notification、event group、mutex/critical section）；避免裸 `volatile`、临时轮询或自造 flag 协议。
- 板级事实由 `main/app/board_*` 或现有 board owner 持有；GPIO、总线地址、片选、传感器轴向、硬件阈值不得长期散落在 service 或 driver 中。
- 资源受限路径优先静态分配或受控分配；区分 task stack、internal RAM、PSRAM、DMA-capable memory 和长期缓存，不把大对象默认放到任务栈。
- 防护代码只覆盖真实可能发生、代价明确且当前层负责处理的失败；Build 是代码改动的默认验证，测试只保护 build 看不出的重要约定。
- 算法或 AI 实现默认拆分为预处理、推理、后处理和模型配置，避免阈值、量化参数、模型 I/O 和业务逻辑混写。
- 新增模块、跨 owner 改动或明显重构前，先给出简短文件划分和模块职责；代码风格向 Google Code Style 靠拢，但不为新规则大面积重排无关旧代码。

## 状态发布与 UI 读取原则（默认生效）

- `UI/LVGL` 高频路径默认只读快照，不在 `poll/timer/getter` 中顺手推进状态，也不做重 `IO`、网络启停、阻塞等待或重锁路径。
- 真实状态推进、重试、超时、错误恢复和硬件交互应由后台任务或 owner 模块负责；`getter` 默认无副作用。
- 若出现 `UI` 卡顿、刷新抖动、锁竞争或“读取状态导致行为变化”，优先按本原则重构，而不是继续堆 `delay`、`flag` 或日志绕过。

## 模拟器预览与截图交付原则（默认生效）

- 新页面、明显视觉重构、参考图复刻或“像素级复刻”任务，必须走仓库 Skill `vue-lvgl-pixel-ui`（Vue 定稿 → LVGL 复刻 → 分层截图对比 → host 验收 → 真机验收）；Vue 是视觉基准，不得为迁就 LVGL 实现静默修改 Vue 基准。窄范围逻辑修复、纯文案修改和不影响布局的 Bugfix 不强制重走完整流程。
- Host 预览工具位于 `tools/ui_preview/`；host 只是中间验证环境，host 未过验收且未经用户确认前不得上板，最终交付以真实手表验收为准。
- 修改了 UI 布局/样式/交互并在后台编译运行了 host 模拟器时，**必须**执行截图脚本（如 `capture_apple_watch_s5_preview.ps1`），并把截图作为绝对路径图片（`![预览图](/绝对路径/截图.png)`）附在当次回答中；严禁静默启动模拟器不提供截图。

## 代码注释规范（默认生效）

- 适用于所有新增或修改的 `C/C++` 代码；注释解释“为什么这样做/受什么约束/改动风险”，不逐行翻译代码。
- 新增/修改公开接口或非显然函数补中文 `Doxygen`，实现内部优先 `//`；关键变量、共享状态、协议常量、超时、阈值、缓冲区大小解释用途、单位、来源或边界。
- 禁止无信息量注释（如“设置标志位”）、禁止无说明保留被注释掉的旧代码或临时 workaround。
- 只补注释的任务默认只动注释；需动代码先获用户明确允许。详细风格见 `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md` 与 `.agents/skills/embedded-c-cpp-comment-style`。
- 兼容边界：不覆盖更高优先级的 system / developer 指令，不替代上下文库流程，不要求一次性重构全部历史代码。

## 当前项目专项规则触发条件（限定作用域）

仅当满足任一条件时，启用“当前项目专项规则”：

- 用户明确要求参考或对齐当前仓库的显示/UI、触摸、音频播放、配网、时间天气或手表路线协作规范。
- 任务明确涉及 `LVGL`、`CO5300`、`FT5x06`、`TE` 同步、`main/ui`、`main/ui/custom`、`main/ui/custom/fonts`、`ai_chat_view`、`ui_runtime_fonts`、`ui_chinese_fonts`、`lv_font_`、`components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06`、`components/audio_codec`、`components/mp3_player`、`main/features/audio/audio_app.c`、`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`components/network_manager`、`main/app/hardware_init.c` 这些关键词或路径。
- 用户明确要求执行手表/屏幕路线，并提到表盘、多页面导航、触摸交互、传感器页面、BLE/WiFi 同步、时间天气、低功耗或电池续航。

未命中以上条件时，不启用当前项目专项规则，避免放大约束。

## 当前项目专项规则（仅在触发时生效）

- 显示/触摸、音频/存储、配网/联网、手表/屏幕路线的详细默认实践，统一以 `docs/context/knowledge/project/agent-operational-rules.md` 为准。
- **CO5300 屏幕安全区（LVGL 布局硬性约束）**：新增或修改任何 `lv_obj_set_pos` / `lv_obj_set_size` 必须满足：左 `x ≥ 40`、右 `x + width ≤ 370`、上 `y ≥ 20`、下 `y + height ≤ 480`。例外：全屏背景图（410×502）、全宽居中文本（width=410 + text_align=center）、游戏引擎 stage 局部坐标。两列卡片每列 160px：左 x=40、右 x=210。详见 `docs/context/knowledge/project/co5300-screen-layout-safe-zone.md`。
- **LVGL 中文字体规则（项目级）**：涉及 `lv_font_*`、`ui_chinese_fonts`、`main/ui/custom/fonts`、中文 UI 文案或动态中文文本时，必须使用项目级 Skill `.agents/skills/lvgl-chinese-ui-fonts/SKILL.md`，不得修改或依赖用户级同名 Skill。动态中文文本用通用 `common_5500` 16/22px，缺字扩充通用字符集；固定文案大字号按使用点生成页面子集；普通 UI 不新增字体 fallback 链。
- **危险识别产品线**：产品边界、状态机、参数口径、固件归属统一以 4 张卡为准：`hearing-assist-danger-alert-system-architecture` / `state-machine-and-notification-policy` / `parameter-defaults-table` / `firmware-mapping`（均在 `docs/context/knowledge/project/`）。active danger 只认 `siren / horn / alarm`；`glass_break / crash / impact` 只留在 challenger，不得静默并入主线。`danger_detection_service` 管状态机与连续证据融合，`app_alert_manager` 管提醒编排，`danger_detection_controller` 只管页面展示。
- 先稳定再优化，先可观测再调优；先输出证据（日志、编译结果、关键指标）再下结论。`git` 提交信息和新增/修改代码注释优先使用中文，接口名、协议字段名、库名等不可翻译标识符保留英文。
  </INSTRUCTIONS>
