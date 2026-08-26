---
id: hermes-music-control-mcp
tags: context, plans, ai-memory-watch, hermes, music, mcp, watch-control, search, remote-command
summary: 为 Hermes 增加受限音乐 MCP，通过搜索点歌和播放控制生成手表远程命令；手表继续由 music_service owner 执行真实播放。
last_reviewed: 2026-08-05
memory_type: task
scope: task
owners: server/music_service, main/services/music, server/deploy/openresty, server/deploy/1panel/hermes-agent
triggers: Hermes 音乐 MCP, Hermes 点歌, 手表远程音乐控制, music MCP, music remote command
evidence_level: design
status: active
---

# Hermes 音乐控制 MCP 执行计划

## 目标与边界

目标：让 Hermes 能理解“播放周杰伦的《晴天》”等自然语言音乐意图，搜索网易云候选歌曲，并控制 `watch-001` 手表上的现有音乐播放器。

用户可见能力：

- 搜索歌曲并返回最多 5 个候选，包含 `track_id`、歌名、歌手和来源。
- 用户直接点歌时，服务端搜索并自动选择第一条可播放结果，立即下发播放命令；来源型请求也直接播放来源第一首可播放歌曲，仍可单独搜索获取候选。
- 查询当前播放状态。
- 修改音量、暂停、恢复、上一首、下一首、停止和切换播放模式。
- 播放模式覆盖网易云的 4 种常规顺序模式，以及需要服务端生成动态队列的心动/智能播放。
- MCP 调用等待手表 ACK 最多 5 秒；确认成功返回 `executed`，超时返回 `accepted`，不把服务器收件误报为手表已执行。

明确不做：

- 不让 Hermes 创建二维码、读取二维码、登录或退出网易云。
- 不让 Hermes 直接写音频硬件或 NVS；音量修改必须通过手表 `audio_codec` owner 的远程命令。
- 不把网易云 Cookie、上游播放 URL、设备长期 token 或 stream capability token 暴露给 Hermes。
- 不让 MCP 直接操作 ESP32、I2S、audio codec 或 LVGL。
- 不新增第二个音乐 owner；真实播放仍由 `main/services/music/music_service.c` 执行。
- 不做多设备；本轮固定 `watch-001`。

## 参考 NetEaseMusic-MCP 的取舍

参考项目提供了播放/暂停、上一首/下一首、音量、搜索、每日推荐和当前播放状态等用户能力；这些能力集合与本计划一致。[参考 README](https://github.com/Ocrosoft/NetEaseMusic-MCP/blob/main/README.md)

本项目采纳：

- 自然语言点歌后直接播放并回报实际歌曲。
- 当前播放歌曲/歌手/状态和系统音量可查询。
- 每日推荐通过现有 `source_id=today` 入口播放，不另造一套推荐接口。
- 搜索只返回受限歌曲元数据，播放 URL 继续由服务端私有解析。

本项目不采纳：

- 依赖 Windows 网易云客户端、窗口自动化、ChromeDriver 或本地动态端口。
- 让 MCP 直接控制第三方播放器进程。
- 把 Cookie、私有播放地址或客户端控制协议暴露给 Hermes。

## 网易云播放模式清单与协议映射

网易云常规播放顺序可以归纳为 4 种：顺序播放、列表循环、单曲循环、随机播放；部分第三方客户端把“顺序播放”称为“单曲结束”或 `single_stop`，本项目协议统一使用更直观的 `order`。网易云生态另外提供“心动/智能播放”，它不是对当前队列重新排序，而是根据种子歌曲和歌单向网易云请求新的推荐队列，因此单独作为 `smart`，不能伪装成普通 `shuffle`。

| 协议值 | 用户说法 | 所属层 | 行为 |
| --- | --- | --- | --- |
| `order` | 顺序播放 | 手表播放队列 | 按来源队列顺序播放一遍，末曲结束后停止，不回到第一首 |
| `repeat_all` | 列表循环 | 手表播放队列 | 按来源队列顺序播放，末曲后回到第一首 |
| `repeat_one` | 单曲循环 | 手表播放队列 | 当前歌曲结束后重新播放当前歌曲 |
| `shuffle` | 随机播放 | 手表播放队列 | 在当前来源队列中随机选择下一首，避免固定首曲 |
| `smart` | 心动模式/智能播放 | 网易云服务端 | 以当前歌曲和具体歌单为种子请求动态推荐队列，再继续播放 |

`today`（今日推荐）、`liked`（我喜欢）、`recent`（最近播放）和 `playlist:<id>` 是内容来源，不是播放模式；“私人 FM”也是网易云推荐内容入口，不把它混成这 5 个模式。`smart` 需要网易云返回可用的歌单 ID 与种子歌曲：`playlist:<id>` 直接满足，`liked` 需要先解析账号的“我喜欢”歌单 ID；如果 `today` 没有可用歌单 ID，服务端应返回稳定的 `smart_mode_requires_playlist`，而不是悄悄降级为随机播放。来源型“随机播放今日推荐”仍使用 `mode=shuffle`。

参考依据：[NetEase-CMD 的播放模式常量](https://pkg.go.dev/github.com/CxZMoE/NetEase-CMD/control)、[NeteaseCloudMusicApi 的智能播放接口说明](https://gitlab.com/m6365/NeteaseCloudMusicApi/-/tree/master/docs)。这些是生态实现/API 文档参考，不把第三方客户端协议直接复制到手表。

## 当前基线与关键缺口

当前 music-service API 是手表主动调用的控制面：手表创建会话、领取 `stream_id` 并建立媒体连接。Hermes 直接调用现有 `/v1/music/sessions` 只会改变服务器会话，不会通知位于 NAT 后的手表。

因此本轮必须补一条窄的下行命令链：

```text
Hermes
  -> 远程 Streamable HTTP MCP
  -> music-service 搜索/写入最新远程命令
  -> 手表控制 HTTP 轮询领取命令
  -> music_service owner 进入现有 command queue
  -> 手表执行并 ACK
  -> MCP 等待 ACK 或返回 accepted
```

## Hermes-only 闭环尝试（不接 ESP32）

在刷写固件前，先做一次只验证 Hermes/MCP/服务端的闭环。测试用的 `watch-001` mock client 通过 HTTP 轮询领取远程命令并回传 ACK，替代真实 ESP32；本阶段不启动真实媒体流、不操作 I2S、不要求 COM7，也不把 mock 结果当作真机播放验收。

闭环路径固定为：

```text
Hermes runtime / hermes mcp test
  -> Streamable HTTP MCP tools/call
  -> music-service pending command
  -> mock watch-001 poll/claim
  -> mock ACK with music snapshot
  -> MCP returns executed
```

该尝试至少覆盖：工具发现与 Bearer 认证、自然语言点歌对应的 `music_search` + `music_play`、暂停/恢复/上一首/下一首/停止、音量 `0/35/100`、5 种模式，以及命令超时返回 `accepted`。它只证明 Hermes 能通过 MCP 控制服务端命令链；真实设备执行仍留到后续固件阶段验证。

## 固定架构决策

### MCP 传输与认证

- MCP 运行在现有 `music-service` 进程内，提供独立 HTTP 路径，例如 `/v1/music/mcp`。
- Hermes 使用远程 Streamable HTTP MCP 配置访问，不把 music-service 加入 Hermes 或 `watch-relay-private` 网络。
- MCP 使用独立 `MUSIC_MCP_TOKEN`，通过 `Authorization: Bearer` 认证；不复用 ESP32 `device_token`。
- MCP 服务端固定绑定 `watch-001`，从环境变量读取设备 ID；请求参数不能切换到其他设备。
- OpenResty 只公开 MCP 路径和必要的 `POST/GET/HEAD` 方法；禁止暴露上游网易云 URL 和私有数据目录。
- Hermes 配置只启用本 MCP 的明确工具白名单，关闭 resources/prompts，避免扩展工具面。

### MCP 工具面

首版只暴露以下工具：

| 工具 | 作用 | 是否写命令 |
| --- | --- | --- |
| `music_search` | 按关键词搜索歌曲，最多 5 个候选 | 否 |
| `music_status` | 获取当前歌曲、播放状态、系统音量和设备最后 ACK 状态 | 否 |
| `music_play` | 用歌名/歌手关键词直接搜索并播放第一条可播放结果；也接受 `source_id`（来源第一首或随机首曲）或 `source_id + track_id`，可带播放模式 | 是 |
| `music_pause` | 暂停 | 是 |
| `music_resume` | 恢复 | 是 |
| `music_previous` | 上一首 | 是 |
| `music_next` | 下一首 | 是 |
| `music_stop` | 停止并销毁当前会话 | 是 |
| `music_set_mode` | 对当前会话切换 `order`、`repeat_all`、`repeat_one`、`shuffle`、`smart`；普通模式原地切换，`smart` 必要时由服务端刷新动态队列 | 是 |
| `music_set_volume` | 设置扬声器音量 `0-100` | 是 |

`music_search` 返回的结果只含歌曲元数据和可再次提交的 `source_id`、`track_id`；MCP 不直接播放搜索结果的上游 URL。

### 远程命令语义

- 每个设备只保留一个 `pending` 命令；新命令到达时将旧命令标记为 `superseded`。
- 每个命令都有服务端生成的 `command_id`、动作参数、创建时间、过期时间和状态。
- 默认 TTL 为 30 秒；过期命令不执行并返回明确的 `expired`。
- 手表通过现有控制 HTTP 长连接低频轮询命令；轮询不在 UI 线程执行。
- 手表领取后立即标记 `claimed`，随后由 `music_service` owner 将动作放入已有 command queue。
- owner 执行结果由手表通过 ACK 请求回传；ACK 必须包含 `command_id`、最终状态和窄的音乐 snapshot。
- 重复领取同一 `command_id` 不重复执行；服务器返回已知 ACK。
- MCP 等待 ACK 最多 5 秒，超时只返回 `accepted`，让 Hermes 可以继续对话而不会伪报失败。
- 远程命令不绕过本地已有的 `music_service_stop_player()`、会话销毁和设备鉴权边界。
- 音量命令由手表 `music_service` owner 调用 `audio_codec_set_volume_preference()`，实时应用并持久化到现有 NVS；MCP 不直接写 NVS。手表 ACK 和状态轮询回报当前音量。
- 播放模式命令复用现有 `music_service_handle_mode()`：`order`、`repeat_all`、`repeat_one`、`shuffle` 只更新当前会话模式，不停止播放器、不重新申请媒体 stream；`smart` 由服务端调用网易云智能播放接口更新队列，不能在手表端用本地随机逻辑冒充。
- `order` 到达队列末曲后将会话置为停止；`repeat_all`、`repeat_one` 和 `shuffle` 保持现有下一首/上一首控制语义。协议解析可兼容旧客户端传来的 `single_stop`，但对外状态统一返回 `order`。
- `smart` 只对具备歌单 ID 和种子歌曲的会话开放；服务端无法取得这两项时返回 `smart_mode_requires_playlist`，不改变当前播放会话。

### 点歌搜索

- 在 `NeteaseProvider` 增加窄的歌曲搜索适配，只请求网易云单曲类型。
- 搜索默认 `limit=5`，上限不超过 10；关键词长度和结果字段均限长。
- 搜索异常只返回 `music_search_unavailable` 或 `music_auth_required` 等稳定错误码。
- `music_play` 传入关键词时由服务端搜索并选择第一条可播放歌曲；传入 `track_id` 时由服务端重新解析播放地址；两种路径都不信任 Hermes 提交的 URL。
- 自动点歌响应必须返回实际选择的 `track_id`、歌名、歌手和播放状态，避免 Hermes 无法说明播放了哪一首。

## Owner 与修改范围

Server：

- `server/music_service/src/app.js`：MCP HTTP 路由、远程命令领取/ACK 路由和鉴权分流。
- `server/music_service/src/mcp_server.js`：MCP JSON-RPC/Streamable HTTP 会话、工具 schema 和调用分发。
- `server/music_service/src/store.js`：远程命令 pending/claimed/ACK/superseded 持久化。
- `server/music_service/src/netease_provider.js`：歌曲搜索适配。
- `server/music_service/src/config.js`、`env.example`、Compose/部署模板：MCP token、设备 ID、TTL、ACK 等待配置。
- `server/music_service/test/`：MCP 协议、鉴权、搜索、最新覆盖、ACK/超时和私密字段测试。

Firmware：

- `main/services/music/music_http_client.h/.c`：远程命令轮询和 ACK 的控制面 HTTP 接口。
- `main/services/music/music_protocol.h`：窄命令/结果结构和最大字段边界；扩展 `music_service_mode_t` 覆盖 `order`、`repeat_all`、`repeat_one`、`shuffle`、`smart`。
- `main/services/music/music_service.c/.h`：owner task 接收远程命令并复用现有 command queue；不在 HTTP 回调中直接执行播放；解析和发布全部 5 种模式，收到服务端末曲停止结果时不重新启动播放器。
- `main/ui/custom/music_controller.c` 及现有音乐页面：模式选择、按钮文案和状态快照显示必须覆盖全部 5 种模式；`smart` 的动态队列由服务端提供，手表不自行伪造推荐。
- `tests/test_music_service_source.py`：锁定轮询不在 UI、最新命令、ACK、FreeRTOS owner queue 和资源边界。

部署：

- `server/deploy/openresty/ai-memory-watch.conf`：只添加 MCP 路由和必要的 body/header 限制。
- `server/deploy/1panel/hermes-agent/docker-compose.yml` 或独立 Hermes secret 模板：只记录非秘密 MCP URL 与 `${MUSIC_MCP_TOKEN}` 引用，真实 token 留在 secrets/.env。
- 不修改 Hermes 核心镜像源码；通过官方 `mcp_servers` 配置接入远程服务器。

## 分阶段执行

### 阶段 0：协议与基线

- `[x]` 冻结 MCP 工具 schema、错误码、远程命令状态和 ACK payload。
- `[x]` 记录当前音乐服务测试基线、固件音乐 source test 和 active plan 状态。
- `[x]` 确认 OpenResty 当前生产配置路径与 Hermes MCP 配置落点，不输出秘密。

### 阶段 1：服务端 MCP 与搜索

- `[x]` 实现 Streamable HTTP MCP initialize/tools/list/tools/call。
- `[x]` 实现独立 MCP Bearer token 和固定 `watch-001` 绑定。
- `[x]` 增加 `music_search`、`music_status` 的只读工具。
- `[x]` 增加服务端 `search` 适配和结果边界。

### 阶段 2：远程命令与手表执行

- `[x]` 增加最新命令覆盖的 SQLite 表/状态迁移和 ACK 持久化。
- `[x]` 增加 MCP 写工具到远程命令的映射。
- `[x]` 增加手表 owner 轮询、领取、复用现有 command queue、ACK 和重复命令保护；手表端解析/发布 5 种模式并处理 `order` 末曲停止与 `smart` 动态队列结果。
- `[x]` 增加 5 秒等待与 `accepted` 超时返回。

### 阶段 3：Hermes/OpenResty 接入

- `[x]` 写入 OpenResty 精确 MCP 路径并限制 POST 方法、64 KiB 请求体、认证头和 15 秒上游等待；香港 1Panel 生产 OpenResty 已通过语法检查并 reload。
- `[x]` 写 Hermes `mcp_servers` 配置模板，工具白名单只包含本计划工具；真实 token 仍只留在 secrets/.env。
- `[x]` 云端 `hermes mcp test watch_music_canary` 已验证 initialize、Bearer 认证和 10 个工具发现；临时 Hermes 配置已在测试后移除。

### 阶段 4：验证与短时真机闭环

- `[x]` 服务端 MCP/搜索/命令测试通过。
- `[x]` **Hermes-only 闭环尝试**：云端 `hermes mcp test` 和 Hermes 实际自然语言工具调用均已完成；mock `watch-001` 验证了 `tools/call -> claim -> ACK -> executed`，本项未刷 ESP、未接 COM7。
- `[x]` 固件音乐 source tests、`git diff --check`、完整 ESP-IDF build 通过。
- `[x]` 云端 music-service 已部署并健康；本阶段未刷 COM7 app 固件。
- `[ ]` Hermes 直接说歌名/歌手后自动点播，验证手表实际开始播放并返回选中歌曲。
- `[ ]` 验证音量 `0%/35%/100%`、暂停/恢复/上一首/下一首/停止/模式、重复命令、最新覆盖和 5 秒超时。
- `[ ]` 验证 MCP 不可读取 Cookie、设备 token、stream URL，公网私有路径不暴露。
- `[ ]` 不执行 30 分钟持续测试。

## 测试计划

服务端：

- MCP initialize/tools/list/tools/call 的 JSON-RPC 正常和错误响应。
- 缺失/错误 MCP token、错误 device scope、过大关键词、未知工具和非法参数。
- 搜索结果最多 5 条，结果不含 Cookie、上游播放 URL 或 stream capability。
- 关键词点歌自动选择第一条可播放结果并立即产生播放命令；响应包含实际选择的歌曲。
- `source_id=today` 对应“播放今日推荐”，`source_id=liked` 对应“播放我喜欢歌单里的歌”；未给 `track_id` 时由服务端选择来源第一首。
- 来源型请求带 `mode=shuffle` 时，服务端先从来源候选中随机选择首曲，再创建 `shuffle` 会话；因此“随机播放今日推荐的歌”不会先固定播放第一首。
- 覆盖 4 种常规模式的边界：`order` 播放完停止、`repeat_all` 回到首曲、`repeat_one` 保持当前曲、`shuffle` 不固定首曲且随机切换。
- `mode=smart` 使用网易云 `playmode_intelligence_list` 所需的歌单 ID 与种子歌曲，验证动态队列确实来自服务端；缺少歌单 ID 时返回 `smart_mode_requires_playlist`，不得静默降级。
- `music_play` 重新解析 track，不接受客户端 URL。
- `music_set_volume` 只接受整数 `0-100`，0% 静音，且通过设备 ACK 证明已持久化。
- 固件协议、快照和音乐页面均能读写 5 种模式：`order`、`repeat_all`、`repeat_one`、`shuffle`、`smart`；切换后状态回报不能被默认分支改写为 `repeat_all`。
- 新命令覆盖旧 pending，claimed/ACK 后不被覆盖。
- 重复 command_id 不重复执行；过期命令明确拒绝。
- ACK 在 5 秒内返回 `executed`，超时返回 `accepted`，后续状态可查询。
- 同时存在手表 UI 控制和 MCP 命令时，服务端状态不产生双重播放进程。

固件 source test：

- 远程轮询属于 `music_service` owner task，不在 UI/timer/getter 中做网络。
- 远程动作只进入现有 command queue，不新增播放器或 audio owner。
- 手表端模式枚举、字符串映射和 UI 状态覆盖 5 种模式；`smart` 只转发服务端动态队列结果，不在设备端调用网易云。
- 播放结束收到服务端 `order` 的停止结果时，释放播放器并发布 `STOPPED`，不再次请求/启动媒体流。
- claimed/ACK 使用 `command_id`，重复命令不会重复 start/next/pause。
- HTTP 错误、过期命令和 ACK 失败不会释放错误的媒体/控制句柄。
- 轮询和 ACK 不携带设备 token 以外的秘密，不记录 stream URL/Cookie。

部署验收：

- Hermes-only：不接 ESP32 时，MCP 工具发现、认证、命令领取、ACK 和 `executed/accepted` 语义闭环通过；不把该结果记为真机播放通过。
- music-service health、MCP `hermes mcp test`、OpenResty private exposure gate。
- Hermes 只看到 allowlist 工具，MCP 无 resources/prompts。
- 真机日志无 panic、stack overflow、WDT、I2S 写错误或 music_service stop timeout。

## 安全与回退

- MCP token 与 ESP32 device token 分离；token 只放 Hermes/music-service secrets，不进入仓库、固件、日志或 MCP 返回值。
- MCP 路由失败时，现有手表音乐 API、媒体流和二维码登录不受影响。
- 远程命令表为 additive schema；MCP 关闭后手表继续使用本地 UI 控制。
- 部署前备份 music-service SQLite 和当前容器 image digest；失败时仅回退 music-service/MCP 路径，不回退固件已验证的 Opus/48 kHz 播放链路。
- Hermes 配置默认 `enabled: false` 或只在部署验收阶段启用；工具白名单不扩张到账号管理。

## 进度

- `[x]` 完成现有 music-service、Hermes MCP 能力和手表 owner 边界审查。
- `[x]` 确认只做播放控制和点歌；不做登录/退出，音量作为已确认的播放控制能力。
- `[x]` 确认自然语言点歌由服务端搜索并自动选择第一条可播放歌曲后立即播放。
- `[x]` 确认 Hermes 可设置 `0-100` 系统音量，复用手表现有持久化音量 owner。
- `[x]` 对照 NetEaseMusic-MCP 能力集合，采纳搜索/推荐/当前状态/音量控制，排除本地客户端自动化。
- `[x]` 核对网易云播放模式：纳入 `order`、`repeat_all`、`repeat_one`、`shuffle` 与服务端 `smart`，并区分内容来源。
- `[x]` 明确手表端也必须支持 5 种模式：协议枚举、服务端模式字符串、快照/UI 显示和末曲停止终态均纳入实现范围。
- `[x]` 确认先做 Hermes-only MCP 闭环尝试；使用 mock `watch-001` 领取/ACK，不接 ESP32，不与当前固件任务冲突。
- `[x]` 确认最新远程命令覆盖旧 pending，MCP 等 ACK 最多 5 秒。
- `[x]` 确认远程 Streamable HTTP MCP、独立 token、固定 `watch-001` 和 OpenResty 隔离。
- `[x]` 阶段 0：冻结协议、基线和部署落点。
- `[x]` 阶段 1：服务端 MCP 与搜索。
- `[x]` 阶段 2：远程命令、手表轮询和 ACK（服务端与固件 owner 接入已完成；云端 Hermes/mock ACK 闭环已完成，真实设备仍待验收）。
- `[x]` 阶段 3：Hermes/OpenResty 接入（云端生产 reload、Bearer 鉴权和工具发现已完成；临时配置已回收）。
- `[ ]` 阶段 4：服务端、固件、部署和短时真机闭环（Hermes-only 云端闭环已完成，真实手表与音频流待完成）。
- `[x]` 2026-08-05：定向审计并迁移 Hermes/WSS 发送期可安全外置的长期业务缓存；internal `.bss` 从 `48,984 B` 降至 `42,176 B`，COM7 实测 `internal_free=9027B`、`largest=8704B`，WSS 语音上传完成且无 `esp-aes` 分配失败；未迁移 DMA、ISR、Wi-Fi/NimBLE、ESP-DL 或 cache-freeze 路径对象。
- `[x]` 2026-08-05：音乐来源目录页已按 Vue 定稿推进到 LVGL host；歌单首屏、动态歌名/歌手、滚动容器和顶部返回来源路径完成验证，未宣称真机通过。
- `[x]` 2026-08-05：Vue 音乐原型新增 `?fx=1` 视觉演示态；已捕获全屏实时模糊弹层与滚动歌单多层阴影两张 410×502 截图，仅用于确认效果与性能代价，未作为正式 LVGL 设计基准。
- `[x]` 2026-08-05：Vue `?polish=1` 展示候选正式层级：播放中卡片轻投影、主播放键薄荷光晕、Orbit Playback（轨道播放）与来源区局部磨砂播放模式弹层；歌单行维持无投影，等待用户确认作为 Vue 基准。
- `[x]` 2026-08-05：Vue 音乐主页候选方案已收敛为 Pulse Dial（脉冲唱盘）+ Sonic Rail（声波轨道）；删除常驻模式弹层并完成 410×502 截图，等待用户确认是否冻结为 Vue 基准。
- `[x]` 2026-08-05：27px 固定中文使用点已改为按需子集，删除旧 3500 全库 27px 字体；固件 build 通过，最小应用分区余量恢复到 12%。
- `[x]` 2026-08-05：开放动态中文的 16/22px 已统一迁移到仓库内 5500 通用字库，移除无当前使用点的 24px 全库；最终固件 build 通过，最小应用分区余量为 7%。
- `[x]` 2026-08-05：LVGL 中文字体规则已固化为项目级 Skill；`AGENTS.md` 仅保留触发条件与 MUST，生成命令和验证闭环集中在 `.agents/skills/lvgl-chinese-ui-fonts/SKILL.md`。
- `[x]` 2026-08-05：修复音乐目录页当前歌名误绑定 `music_ui_20_4` 页面子集导致的动态文字方框，改用 `common_5500_22_4`；音乐 UI/字体聚焦测试 21 项通过，固件 build 通过。
- `[x]` 2026-08-05：清理已替代的 3500 字符集源文件；当前保留字体均有生产代码引用或明确的运行时资源 owner。
- `[x]` 2026-08-05：完成官方 `xiaozhi-fonts 2.0.0` / LVGL 9.5 双字号 Noto raw-assets 迁移；AI common20 与 Hermes common16 共用一次 assets mmap，移除 AI 内置 basic20、Hermes LittleFS 字体和动态 fallback；独立全量构建通过。
- `[x]` 2026-08-05：已对 COM7 执行完整 `idf.py flash`，重写分区表、应用、assets 和 resources；随后 `app-flash-monitor` 冷启动验证 `LVGL 9.5`、`Noto font assets ready: bundle=noto-v1 text=common20 hermes=common16 image=2173924 pages=34`，启动到首帧且 `panic_log_seen=0`，未见旧 `A:/fonts` Hermes 路径（证据：`build-font-migration/board_logs/2026-08-05-06-26-06-noto-assets-lvgl95.log`）。
- `[ ]` 2026-08-05：仍待在真实 AI/Hermes 页面输入长中文动态文本，并用未同步 assets 的专用测试镜像验证 `FONT ASSET ERROR` 状态。
- `[x]` 2026-08-05：音乐动态歌手通用字库补入 `雲`，重新生成 common_5500 16/22px；保持 5500 个唯一字符，音乐 host 中 `黄霄雲` 不再显示方框，中文字体测试 16 项通过。
- `[x]` 2026-08-05：音乐主页按 Vue Orbit 基准迁移到 LVGL：播放控制下移、模式控制舱居中、双 `lv_arc` 与轨道点静态结构完成；完成同一 410×502 数据的 Vue/LVGL 截图及分层差异报告，未宣称整页自动像素阈值通过。
- `[x]` 2026-08-05：Orbit 两条轨道已加入 `lv_anim_t` 反向旋转；当前播放卡增加 host/LVGL 点击入口，弹出四来源歌单选择层并复用原来源回调，host 合成点击与截图验证通过，未上板。
- `[x]` 2026-08-05：修正音乐选择歌单层的 Vue/LVGL 交互结构：移除 Vue 遗留关闭叉号，顶部返回统一关闭选择层；LVGL 歌单按钮装饰线、箭头位置与 Vue 基准对齐。Vue build、LVGL host build、ESP-IDF build 和聚焦测试通过，未上板；浏览器/LVGL 中文栅格差异仍未宣称自动像素阈值通过。
- `[x]` 2026-08-26：音乐 UI 回退到干净静态主页；移除 Orbit/选择歌单覆盖层相关的曲目卡入口、LVGL 对象、Vue 状态和 host 预览参数，保留四个来源按钮、目录、播放控制、模式切换与账号页。Vue、LVGL host、音乐聚焦测试和 ESP-IDF build 通过，未烧录。

### 2026-08-04 实施证据

- `server/music_service` Node tests：16/16 通过，覆盖 MCP Bearer 隔离、工具发现、搜索/点歌、mock watch poll/ACK、`executed`、latest-wins、过期命令、网易云 smart 动态队列和敏感字段不外泄。
- `git diff --check` 通过；固件最终增量 ESP-IDF build 通过，`111.bin` 为 `0xb6bb20`，最小应用分区余量约 5%。本轮没有刷 COM7，也没有启动真实音频流。
- 固件 owner 已接入 `music_service` 轮询/ACK：远程动作进入现有 owner queue，音量复用 `audio_codec` 持久化接口，5 种模式和 `order` 末曲停止均有源代码断言；音乐 source test 9/9 通过。
- 香港 1Panel 云端已上传 music-service 源码并执行 `docker compose up -d --build --no-deps ai-memory-watch-music-service`；容器健康检查为 `healthy`，OpenResty `-t` 和 reload 均通过。公网边界验证为 `/health=404`、MCP GET=403、MCP 缺失 Bearer=401、设备控制面缺失 Bearer=401。
- 云端 Hermes `hermes mcp test watch_music_canary` 已连接并发现 10 个白名单工具；临时 MCP 配置及 Hermes 临时 token 已移除，原 Hermes 无 MCP 配置保持不变。
- 云端直接 MCP canary 已完成 `initialize -> tools/list -> music_set_volume -> mock watch claim -> ACK -> executed -> music_status`，ACK 快照读回音量 35%；未启动真实媒体流。
- 首次 Hermes 自然语言 canary 因模型账户余额不足返回 `HTTP 402: Insufficient account balance`，未产生工具调用；充值后已重试成功，详见下一条证据。
- 充值后 Hermes 自然语言调用已成功：音量请求实际调用 `music_set_volume`，mock ACK 为 `executed`；“播放周杰伦的《晴天》”实际调用点歌工具，返回晴天/周杰伦并产生 `play` 命令，mock ACK 为 `executed`。随后发送 `music_stop` 清理 canary 会话；未建立真实媒体连接。
- 2026-08-04：修复 `music_service` 启动竞态；远程命令轮询现在先检查已有 `network_service_is_service_ready()`，Wi-Fi/IP/DNS 未就绪时不创建控制 TLS 连接，避免启动阶段重复的 `getaddrinfo() returns 202` / `ESP_ERR_HTTP_CONNECT`。音乐 source tests `9 passed`，完整 ESP-IDF build 通过；尝试刷 COM7 时系统已不再枚举 COM7，未强行重试。
- 2026-08-05：修复 `official_ssl` 接收任务创建失败的 internal RAM 峰值问题；`oc_ssl_rx` 改用 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`，退出改用匹配的 `vTaskDeleteWithCaps()`，失败日志增加 internal/PSRAM 空闲与最大连续块。官方聊天/Memory Watch 聚焦 source tests `36 passed`，完整 ESP-IDF build 通过（`111.bin` 余量约 15%）。
- 2026-08-05：COM7 `app-flash-monitor` 60 秒已验证上述修复：`memory_watch` WSS 在 18.7 秒完成连接并复用录音上传路径，无 panic、Guru 或 stack overflow。该轮同时发现独立的发送期 internal RAM 峰值：`internal_free=2327B`、`largest=1152B` 时，`esp-aes` 无法分配 DMA 临时块，WSS 发送失败；已记录到 `docs/context/runs/2026-08-05-attempt-hermes-wss-psram-aes.md`，不把此板测记为 Hermes 真实点歌验收。
- 2026-08-05：针对该峰值完成静态/长期对象审计：仅将 official chat 文本/消息历史和 Memory Watch owner/worker scratch、inbox pending-read 集合迁到显式 PSRAM；保持 `official_chat_service` internal 栈、FreeRTOS 控制块、DMA、Wi-Fi/NimBLE、ESP-DL 不变。聚焦 source tests `50 passed`、完整 build 通过；COM7 短测显示 `internal_free=9027B`、`largest=8704B`，WSS 已连接、录音上传到 `voice-ws-done` 并得到 `conversation_reply`，无 `esp-aes`、panic、Guru、WDT 或 stack overflow（`board_logs/2026-08-05-07-30-03-psram-static-audit.log`）。

## 当前下一步

按当前要求暂不做 Hermes 真实点歌。音乐目录页当前只完成 LVGL host 验证；字体迁移已完成分区刷写和启动期 mmap 验证，下一步只剩真实 AI/Hermes 页面长中文与故障注入验收。当前云端 MCP 配置已回收，音乐服务和独立 MCP 鉴权仍部署，未把 mock 结果记为真机播放通过。
