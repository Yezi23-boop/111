---
id: 2026-06-25-hermes-inbox-global-notification-plan
tags: plan, active, ai-memory-watch, hermes, inbox, notification, global-bubble, polling
summary: Hermes 收件箱与系统级气泡通知计划：V1 用 HTTP 轮询打通业务和 UI，后续升级为 MQTT 推送；收件箱只放 Hermes 主动下发提示，不混入普通对话回复。
created: 2026-06-25
last_updated: 2026-06-25
last_reviewed: 2026-06-25
status: active
memory_type: project_plan
scope: repo
owners: docs/context/plans/active/2026-06-25-hermes-inbox-global-notification-plan.md, main/services/memory_watch_service.c, main/services/memory_watch_voice_client.c, main/ui/custom/memory_watch_controller.c, main/ui/custom/memory_watch_view.c, server/watch_voice_endpoint/app.py
triggers: Hermes 收件箱, inbox, notification, 全局气泡, 后台回复, memory_watch_service, MQTT, HTTP polling
evidence_level: design
route_area: "AI Memory Watch / Hermes inbox and notification"
---

# Hermes 收件箱与全局通知计划

## 目标与全局

- 任务目标：把 Hermes 主动下发提示做成手表系统级消息能力，V1 先用 HTTP 轮询打通服务器、固件 service、收件箱 UI 和全局气泡通知。
- 为什么现在做：当前 Hermes 语音页已经能完成按住说话和回执展示，但用户退出 Hermes 页面后无法知道后台回复或主动提示；收件箱也还停留在页面内 mock/预览形态。
- 完成后用户会看到什么变化：Hermes 主动提示会进入收件箱；无论用户当前在哪个普通页面，新的 Hermes 主动提示都会以轻量气泡出现；点气泡可进入对应内容。

## 已敲定产品决策

- 消息中心定位：允许多个消息来源，V1 只接 Hermes；未来可接 weather、safety、system、reminders 或其他 app events。
- V1 消息类型：只读通知，不做确认、取消、回复、删除或继续任务。
- 收件箱内容边界：收件箱主要放 Hermes 主动下发的提示，不把每一条普通 Hermes 对话回复都塞进收件箱。
- Hermes 后台回复：退出 Hermes 页面后，已经发送的请求默认继续等待，不弹确认框；后台回复到达时可弹气泡，点气泡回到 Hermes 对话页并滚到底部。
- 退出语义：
  - `recording / encoding`：退出页面取消录音，因为还没有形成有效请求。
  - `uploading / thinking`：退出页面后台继续。
  - `needs_clarification`：退出页面保留状态，后续可回到 Hermes。
  - `done / error / idle`：正常退出。
- 主动提示通知：V1 拉到新的 Hermes 主动提示时默认全局弹气泡，但在录音/编码、高优先级安全告警、OTA/配网关键流程、熄屏/低功耗时暂缓。
- 传输路线：V1 用 HTTP 轮询打通业务和 UI；V2 再重构升级为 MQTT，解决延迟和功耗。

## 范围与非目标

本轮明确要做：

- 定义 inbox 数据模型、轮询节奏、全局气泡行为、页面跳转规则和 owner 边界。
- 后续实现时把收件箱真实数据 owner 放在 `memory_watch_service`，UI 只消费 snapshot。
- 后续实现时增加 server 侧 inbox API，固件侧增加 `memory_watch_voice_client` 的窄 HTTP client 解析。
- 后续实现时增加全局 notification controller，挂在 `lv_layer_top()`，而不是绑在某个页面里。

本轮明确不做：

- 不在 V1 做 MQTT、WebSocket、SSE 或常驻推送连接。
- 不让 Hermes 普通聊天回复默认进入收件箱。
- 不做 inbox 消息的确认、回复、删除、继续任务或服务器任务状态机。
- 不让 UI 页面直接执行 HTTP、轮询调度或后台状态推进。
- 不把收件箱做成手机通知聚合器；V1 仍围绕 Hermes 主动提示。

## 运行时设计

```text
Hermes notification producer
  -> 根据提醒、后台任务结果等业务事件生成 notification_id/kind/title/preview/body
  -> 决定何时创建通知，不把业务语义下放给 inbox 存储层
  -> V1 分容器部署时调用 inbox HTTP 写入接口
  -> 后续与 inbox 合并到同一进程时可直接调用 inbox_create()

server/watch_voice_endpoint
  -> 使用 SQLite 持久化每个 device 的 Hermes 主动提示 inbox
  -> 作为 V1 inbox repository/API，负责校验、幂等去重、保留最近 20 条和向手表提供读取
  -> Hermes 写入、手表读取和标记已读统一使用目标 device 的现有 watch device token
  -> 只接收已经到达通知时机的即时消息，不承担提醒调度
  -> 提供 POST /v1/watch/inbox?device_id={device_id}，V1 服务端固定写入 source=hermes
  -> 提供 GET /v1/watch/inbox
  -> 提供 POST /v1/watch/inbox/{notification_id}/read?device_id={device_id}

memory_watch_voice_client
  -> 窄 HTTP client，只负责请求和 JSON 解析

memory_watch_service
  -> inbox store owner
  -> 按网络、电源、前台活跃状态决定下一次 poll
  -> 缓存最近消息、有效未读数和待同步已读集合
  -> 发布只读 snapshot

watch_notification_center / controller
  -> 消费 service 发布的新通知事件或 snapshot diff
  -> 独占 surfaced ledger、当前气泡和待展示通知状态
  -> 在 lv_layer_top() 显示全局气泡
  -> 决定立即弹出、暂缓或合并

memory_watch_view / controller
  -> 展示 Hermes 对话、收件箱列表和消息详情
  -> 用户点击气泡后跳转到 Hermes 对话或收件箱详情
```

部署方式不改变逻辑 owner：

- Hermes 始终是通知内容和业务触发的 owner；Inbox 模块不替 Hermes 生成标题、正文、类型或业务 ID。
- Inbox 模块始终是通知记录的持久化 owner，负责输入校验、幂等、不可变约束、排序、最近 20 条保留和已读状态。
- V1 如果 Hermes 与 `watch_voice_endpoint` 分容器运行，二者通过内部 HTTP 写入。
- 后续把服务合并进 Hermes 容器或同一进程时，可把内部 HTTP 调用替换为 `inbox_create()` 或 repository 直调；手表使用的 GET/read API 和消息契约保持不变。
- `created_at` 仍由 Inbox 存储层在首次提交成功时生成，不由 Hermes 提供，保证排序与持久化提交时间一致。

## 数据模型草案

服务器侧 inbox item 建议：

```json
{
  "notification_id": "msg_001",
  "source": "hermes",
  "kind": "reminder",
  "created_at": "2026-06-25T02:30:00Z",
  "title": "取快递提醒",
  "preview": "下午三点去驿站取快递",
  "body": "你让我提醒：下午三点去取快递。",
  "read": false
}
```

V1 字段长度契约按 UTF-8 编码后的字节数计算，并与固件固定缓冲区保持一致：

- `notification_id`：最多 63 字节。
- `kind`：最多 23 字节。
- `title`：最多 63 字节，纯中文约 21 个汉字。
- `preview`：最多 127 字节，纯中文约 42 个汉字。
- `body`：最多 383 字节，纯中文约 127 个汉字。
- 服务端不做静默截断；任一字段超限时返回 HTTP `422`，不写入数据库。由于失败请求没有创建记录，Hermes 可以缩短内容后复用同一个 `notification_id` 重试。
- 环形缓冲区用于网络流式接收或最近 20 条消息的有界队列，不替代单条消息长度上限；LVGL 最终显示仍需要连续、以 `\0` 结尾的 UTF-8 字符串。
- V1 `kind` 只允许 `reminder`、`info`、`warning`；未知值返回 HTTP `422`，不写入数据库。该字段只用于 Hermes 主动收件箱通知，普通对话完成提醒走 Conversation Reply Channel，不使用 inbox `kind`。
- `kind` 枚举校验归服务端；ESP32 V1 只从响应 JSON 中解析并保存该字段，不按 `kind` 切换图标、颜色、优先级、气泡策略或业务处理流程。
- `notification_id`、`kind`、`title`、`preview`、`body` 全部必填；字段缺失、空字符串或去除首尾空白后为空时返回 HTTP `422`，不写入数据库。短通知允许 `preview` 与 `body` 使用相同内容。

固件侧缓存建议增加：

```c
typedef struct {
    char notification_id[64];
    char source[24];
    char kind[24];
    char created_at[32];
    char title[64];
    char preview[128];
    char body[384];
    bool read;
} memory_watch_inbox_item_t;
```

`memory_watch_inbox_item_t` 是 service 内部的完整记录，20 条约占 15 KiB，不得放在 task 栈上。实现时将完整 store 和 HTTP 解析 staging buffer 放在 PSRAM，响应体设 24 KiB 硬上限；超限视为协议错误，保留上一份有效快照。

`surfaced` 不写入 server item，也不放入 service snapshot。`watch_notification_center` 用最多 20 个 `notification_id` 维护运行期 surfaced ledger，表示该消息是否已经被全局气泡展示过；它和 `read` 分离：

- `surfaced=false/read=false`：新消息，还没打扰过用户。
- `surfaced=true/read=false`：已经弹过气泡，但用户没打开详情。
- `read=true`：用户已打开详情或服务器已标记已读。

权责划分：

- `read` 由服务器持久化，用户打开消息详情后手表本地立即置已读，并异步上报服务器。
- 保持当前 UI 语义：仅打开收件箱列表不算已读，只有进入某条消息详情才标记该条已读。
- `surfaced` 只由 `watch_notification_center` 在手表本地运行期维护，不回写服务器。
- 如果上报已读失败，`memory_watch_service` 保留该消息的待同步已读状态并在后台重试；后续完整快照合并不得把它恢复为未读。
- V1 待同步已读状态只保存在 RAM，不写入 NVS；普通断网期间状态持续保留并在网络恢复后重试。若断网期间重启、关机或掉电，待同步状态会丢失，服务器仍为未读的消息可能重新显示未读点。
- 如果手表重启后又对未读消息弹一次气泡，视为合理的提醒补偿。
- 标记已读接口是幂等操作：消息从未读变为已读和已经处于已读状态时都返回成功；目标 `device_id + notification_id` 不存在时返回 HTTP `404`。

保留策略：

- 每个 device inbox 在服务器侧只保留最近 20 条。
- 超过 20 条后淘汰最旧消息，不区分该消息是否已读；旧未读消息也不能阻塞新通知进入。
- `unread_count` 统计这 20 条里的未读数。
- 手表 V1 每次最多拉最近 20 条，列表不做分页。
- `GET /v1/watch/inbox` 每次返回最近 20 条完整快照，按 `created_at` 从新到旧排列，并附带这 20 条中的 `unread_count`。
- V1 不使用增量游标、`since` 参数或分页；固件 service 按 `notification_id` 合并快照，notification center 独立保留本地 `surfaced` 状态。

服务器存储：

- V1 使用 SQLite 持久化 inbox，服务重启后消息和已读状态仍保留。
- SQLite 只承接 inbox 通知；现有对话请求的 inflight/completed/canceled 内存状态不在本轮迁移。
- 按 `device_id + notification_id` 建立唯一约束，轮询或重试写入时保持幂等。
- `notification_id` 由 Hermes 按业务事件生成；同一提醒的所有网络重试必须复用同一个 ID。
- 服务端重复收到同一 `device_id + notification_id` 时不得新增第二条，也不返回冲突错误：首次写入返回 `created=true`，重复写入返回 `created=false` 和已有消息。
- V1 HTTP 适配层在首次成功创建时返回 `201 Created` 与 `created=true`；幂等重复写入时返回 `200 OK` 与 `created=false`，并返回已有消息。未来改为同进程直调后，`inbox_create()` 返回等价的 `created` 布尔状态和消息对象，HTTP 状态码只由对外适配层负责映射。
- 同一个 `notification_id` 首次创建后内容不可变；重复请求即使携带不同的标题、正文或时间，也不得覆盖已有消息，只记录警告日志。需要修正内容时，Hermes 必须生成新的 `notification_id`。
- `created_at` 由 Inbox 存储层在首次成功写入时生成，表示通知进入收件箱的时间；Hermes 不提交该字段，幂等重试也不更新时间。
- 每次插入后按 `device_id` 清理超过最近 20 条的最旧记录。

鉴权：

- V1 不新增独立 inbox push key，复用现有 `watch device token`。
- Hermes 下发时必须携带目标 `device_id` 对应的 token；服务端继续通过 `_require_device()` 校验二者匹配。
- V1 写入接口固定为 `POST /v1/watch/inbox?device_id={device_id}`；请求体只提交 `notification_id`、`kind`、`title`、`preview` 和 `body` 等通知内容，不接收调用方提供的 `source` 或 `created_at`。
- 服务端对 V1 inbox item 固定写入 `source=hermes`；未来接入其他来源时再单独定义来源身份与写入权限。
- 手表拉取 inbox 和标记已读也使用同一 token。
- 该方案意味着 Hermes 侧需要安全保存目标设备 token；V1 接受这一取舍，后续设备规模扩大时再拆分服务端写入凭据。

调度边界：

- 提醒的定时、触发判断、失败补偿和业务条件继续由 Hermes 负责。
- Hermes 只在“现在应该通知手表”时调用 inbox 写入接口。
- watch endpoint V1 不接收 `scheduled_at`，不运行定时任务，也不成为第二套 reminder scheduler。

## V1 HTTP API 契约

所有设备端点继续使用 `Authorization: Bearer <watch device token>` 和 `device_id` 配对校验，对外只使用 HTTPS。鉴权失败沿用现有 `_require_device()` 的 `401/403` 语义，不新建第二套错误封装。

### Hermes 创建主动提示

```http
POST /v1/watch/inbox?device_id=watch-001
Content-Type: application/json
Authorization: Bearer <watch device token>
```

```json
{
  "notification_id": "msg_001",
  "kind": "reminder",
  "title": "取快递提醒",
  "preview": "下午三点去驿站取快递",
  "body": "你让我提醒：下午三点去取快递。"
}
```

首次创建返回 `201 Created`：

```json
{
  "created": true,
  "item": {
    "notification_id": "msg_001",
    "source": "hermes",
    "kind": "reminder",
    "created_at": "2026-06-25T02:30:00Z",
    "title": "取快递提醒",
    "preview": "下午三点去驿站取快递",
    "body": "你让我提醒：下午三点去取快递。",
    "read": false
  }
}
```

相同 `device_id + notification_id` 重试返回 `200 OK`，`created=false`，`item` 始终是数据库中已有的不可变记录。

### 手表拉取完整快照

```http
GET /v1/watch/inbox?device_id=watch-001
Authorization: Bearer <watch device token>
```

```json
{
  "items": [],
  "unread_count": 0
}
```

- 空收件箱返回 `200 OK + items=[] + unread_count=0`，不返回 `404`。
- 非空列表的 `items` 按 `created_at` 从新到旧，最多 20 条；`unread_count` 必须等于返回集合中 `read=false` 的数量。
- 返回体不包含 token、Hermes API key、内部 SQLite 主键或调试字段。

### 手表标记单条已读

```http
POST /v1/watch/inbox/{notification_id}/read?device_id=watch-001
Authorization: Bearer <watch device token>
```

首次标记和重复标记均返回 `200 OK`：

```json
{
  "read": true,
  "notification_id": "msg_001"
}
```

目标不存在返回 `404`。如果消息已因最近 20 条保留策略被淘汰，ESP32 将这个 `404` 视为已读同步的终态结果，从 RAM 待同步集合中移除，不无限重试。

### 输入与错误处理

- `POST inbox` 仅接受 JSON object；字段类型错误、缺失、空白、超限或非法 `kind` 均返回 `422`，不落库。
- `notification_id` 按 UTF-8 字节长度校验后，还必须拒绝控制字符和 CR/LF，避免污染 URL、日志或响应头。
- 服务端错误体沿用当前 FastAPI 风格；ESP32 只依赖 HTTP 状态码和成功体的固定字段，不解析人类错误文案。
- 服务端日志只记录 endpoint、device_id、notification_id、kind、created 和错误分类；不记录 Authorization、token、title、preview 或 body 正文。

### SQLite 表与事务

V1 使用一张 `watch_inbox` 表，内部可使用自增 `row_id` 作为稳定排序键，对外不暴露；`UNIQUE(device_id, notification_id)` 作为幂等约束。必要字段为：

```text
row_id, device_id, notification_id, source, kind,
created_at, title, preview, body, read
```

- `created_at` 在首次插入事务中由服务端以 UTC RFC3339 生成；列表用 `row_id DESC` 作为同一时间粒度下的稳定次序。
- “插入或读取旧记录 + 删除该设备超过 20 条的最旧记录”放在同一事务内，防止并发重试产生短暂超限或重复。
- 数据库文件必须位于容器持久化数据目录，不放在临时目录或镜像可写层；重建容器后消息和已读状态必须保留。
- 启动时使用非破坏性 `CREATE TABLE IF NOT EXISTS`/版本迁移，不在进程启动时删表重建。

## V1 HTTP 轮询节奏

- 首次联网：立刻拉 inbox。
- 正常亮屏/活跃：1 分钟拉一次。
- 普通运行：5 分钟拉一次。
- 失败重试：1 分钟。
- 低功耗/熄屏：不为 inbox 单独唤醒；随已有维护窗口最多 30 分钟检查一次。
- 打开收件箱：立即拉一次。

调度口径：

- 轮询调度归 `memory_watch_service`。
- 活跃/低功耗状态来自已有 power/UI activity owner 的只读快照。
- LVGL 页面不直接发 HTTP，不直接改变轮询周期。
- 调度优先级固定为：“打开收件箱/网络刚恢复”立即请求 > 临时失败 1 分钟重试 > 亮屏活跃 1 分钟 > 普通运行 5 分钟 > 熄屏/低功耗维护窗口 30 分钟。
- 熄屏时不专门唤醒系统只为拉 inbox；如果系统因已有维护窗口醒来，且距上次成功拉取已达 30 分钟，可顺带拉取一次。
- 同一时刻最多只允许一个 inbox HTTP 请求在途；在途期间的多个 poll-now 意图合并为一个 pending bit，不堆积重复 GET。
- timeout、DNS、TLS、断网和 `5xx` 属于临时错误，1 分钟后重试；`401/403` 属于配置/鉴权错误，`422` 属于协议错误，不做每分钟紧循环，等配置更新、网络重连或用户打开收件箱时再试。

## 固件执行模型与 FreeRTOS 边界

V1 沿用现有 `memory_watch_service` owner，不新增通用后台 manager。为避免 HTTP 阻塞 service owner task，增加一个窄 `inbox worker`：

```text
UI intent / network-ready / power snapshot
  -> memory_watch_service command queue or task notification
  -> owner 计算 poll due，投递 POLL / MARK_READ
  -> inbox worker 串行执行 HTTP
  -> result queue 返回状态与 staging result
  -> owner 合并、原子发布 generation/snapshot
  -> LVGL controller 只读取快照
```

- `command queue`用于携带 `POLL_NOW(reason)` 和 `MARK_READ(notification_id)` 参数；`task notification` 用于网络恢复、power/activity 变化等无大参数唤醒。
- inbox worker 串行化 GET 和 read POST，避免两个 HTTP client 同时改写缓存；worker 不持有 LVGL 对象。
- owner task 是 inbox store、poll deadline、sync state 和 pending-read set 的唯一写入者。snapshot 交换使用现有 mutex/critical-section 风格，锁内只复制已完成的结果，不做 JSON 解析或 I/O。
- worker 创建仍属于 Deferred Services 阶段，不进入 `hardware_init()`，不阻塞 UI 首帧。

建议固定同步状态：

```text
UNCONFIGURED -> IDLE -> POLLING -> READY
                    -> RETRY_WAIT
                    -> AUTH_ERROR / PROTOCOL_ERROR
```

- 失败时保留上一份有效 inbox，只更新 `sync_state/last_error/next_poll_due`，不将列表清空。
- 无网络或 endpoint 未配置时不创建 HTTP 请求，保留缓存，等 readiness 事件唤醒。

### 快照 API 与拷贝预算

不提供“每帧拷贝 20 条完整 body”的 getter。建议固定为三类无副作用读取：

```c
memory_watch_service_get_inbox_meta(out_meta);
memory_watch_service_copy_inbox_summaries(out_items, capacity, out_count);
memory_watch_service_get_inbox_item(notification_id, out_item);
```

- `meta` 只包含 `generation/item_count/unread_count/sync_state/last_success`，LVGL timer 可低成本读取。
- 只有 `generation` 变化或进入收件箱列表时，controller 才复制最多 20 条 summary；summary 不含 `body`。
- 进入详情时才按 ID 复制单条完整 item，页面离开后释放/复用详情缓存。
- 所有 getter 只返回副本，不返回 service 内部可写指针，不做网络、重试、状态推进或长时间等待。

### 完整快照合并算法

1. worker 在 staging 区完整接收并解析 JSON，校验 `items <= 20`、ID 唯一、必填字段、字节上限、`kind`、`read` 和 `unread_count` 一致性。
2. 任一校验失败则整份快照拒绝，不部分更新 store。
3. owner 按 `notification_id` 对比旧 store，识别真正新到达且 `read=false` 的消息；列表中不再存在的旧记录按服务器保留策略移除。
4. 本地 pending-read set 优先于服务器快照；同步尚未成功时，对应 item 的有效 `read` 仍为 `true`。
5. owner 重算有效 `unread_count`，再一次性替换 store并增加 `generation`。
6. 如果当前详情在下一次快照中被淘汰，已打开页面使用进入详情时拷贝的 item 继续显示到返回；不保留第二份长期数据库。

## 全局气泡行为

默认显示：

- 单条新消息：显示 `title + preview`。
- 多条新消息：合并为 `Hermes 有 N 条新提示`，点击进入收件箱列表。
- notification center 同一时刻最多显示一个全局气泡；已有气泡时收到后续 Hermes 主动提示，复用当前气泡并原位更新，不堆叠第二个气泡。
- 当前待处理集合只有一条时，点击气泡进入对应详情；累计为多条后，气泡改为 `Hermes 有 N 条新提示`，点击进入收件箱列表。
- 停留方式：深色顶部气泡常驻显示，不自动收起，直到用户手动清除或点击处理。
- 点击单条气泡：进入对应详情；如果是后台 Hermes 对话回复，进入 Hermes 对话页并滚到底部。
- 从任意页面通过气泡进入详情或 Hermes 对话时，记录进入前的页面；用户返回时恢复到该来源页面，不固定跳主界面或 Hermes 首页。
- 用户停留在收件箱列表页时收到新提示，不再叠加全局气泡；列表直接把新消息插到顶部并保留未读点。
- 用户正在查看某条收件箱详情时收到另一条新提示，仍显示全局气泡，避免当前详情遮蔽新消息到达。
- 右滑气泡：只清除当前气泡，不标记 `read`，不影响收件箱未读数。
- 右滑后，本批已出现过的旧消息保持 `surfaced=true`，即使仍未读也不重复弹气泡；后续真正新到达且 `surfaced=false` 的消息仍会正常弹出。
- 合并气泡中的 `N` 统计本次新增且尚未展示的消息数，不等于收件箱总未读数。
- 遮挡策略：气泡是 `lv_layer_top()` 上的浮层，覆盖当前页面但不推动、不重排页面布局；用户觉得挡住时右滑清除。

气泡内部状态固定为：

```text
HIDDEN -> DEFERRED -> VISIBLE -> HIDDEN
                    -> VISIBLE_UPDATED
```

- `active_ids[20]` 记录当前气泡承载的 inbox 消息；消息被加入气泡时同时写入 surfaced ledger。
- 已有 inbox 气泡时到达新 inbox item，把 ID 加入 `active_ids`并原位更新文案；不重建 top-layer 对象，不改变页面焦点。
- 点击或右滑后清空 `active_ids`，但 surfaced ledger 保留，因此这批未读不会再弹。
- 点击单条 inbox 气泡时进入详情并立即本地标记已读；点击多条合并气泡时只进入列表，不批量已读。
- surfaced ledger 以服务器最新 20 条为边界清理；当 ID 不再存在于快照且也不在 `active_ids` 时可移除，防止长期增长。

视觉与手势边界：

- 遵守 CO5300 安全区：`x=40`、`w=330`、`y>=24`，高度建议 `72..96px`，8px 圆角，不超过屏幕右边界 `370`。
- 深色背景使用近炭黑，标题白色，preview 用低对比浅灰；不使用重阴影、渐变或高饱和大色块。
- 标题最多 1 行，preview 最多 2 行，超出用 LVGL 省略/截断显示，详情页显示完整 body；不在绘制路径重新分配大字符串。
- 气泡本体可点击；右滑距离达 48px 且水平位移明显大于垂直位移时视为清除，其余手势不吞掉页面纵向滚动。

暂缓显示：

- 正在按住说话、录音或编码。
- 正在显示更高优先级安全告警。
- OTA、配网或其他关键流程。
- 熄屏或低功耗。

暂缓不是丢弃；消息保持未读并等待下一次可打扰窗口。

暂缓条件解除后，notification center 立即检查待展示消息并补弹一次；多条消息仍按 `Hermes 有 N 条新提示` 合并，不等待下一轮 HTTP 轮询。

暂缓条件不由 notification center 直接查询硬件，而是读取已有 owner 快照并合成 blocker bitmask。高优先级安全提醒始终高于 Hermes 气泡；Hermes 气泡不得覆盖或抢占安全 overlay。

## Hermes 后台回复与收件箱的关系

两条通道必须分开：

```text
Conversation Reply Channel
  -> 用户主动发起请求后的 Hermes 回复
  -> 展示在 Hermes 对话页
  -> 如果回复到达时 Hermes 页面不在前台，可弹“回复已到达”气泡
  -> 默认不进入收件箱

Inbox Notification Channel
  -> Hermes 主动下发提示、提醒、后台任务结果或需要稍后查看的信息
  -> 展示在收件箱
  -> 新消息默认触发全局气泡
```

这个边界避免收件箱变成聊天记录垃圾堆。

两条通道共用一个全局气泡视图，但不合并点击语义：

- 当前显示 inbox 气泡时到达 conversation reply，先保留当前气泡，将 reply 记为一个 pending channel；当前气泡被点击或右滑后，立即显示“Hermes 回复已到达”。
- 当前显示 conversation reply 时到达 inbox item，同样放入 pending inbox 集合，不把两类内容改成一个点击目标不明的通用气泡。
- 每个通道最多保留一个待展示槽位；inbox 槽位用 ID 集合合并，conversation reply 槽位指向最新完成的请求。
- 点击 conversation reply 只导航到 Hermes 对话页并滚动到底部，不改变 inbox 未读数。

## 收件箱 UI 状态

- 列表页从 service summary snapshot 渲染最多 20 条：未读点、title、preview、本地化时间；最新在上。
- 列表页不显示“全部已读”、删除、回复或确认操作；V1 保持只读。
- 列表为空时显示低噪声空状态，不弹错误框。拉取失败但有缓存时继续显示缓存，只显示轻量“尚未同步”状态。
- 点击条目时先拷贝详情、本地立即置已读和更新未读数，再异步投递 `MARK_READ`；不让用户等 HTTP 成功才进入详情。
- 详情页显示 title、时间和完整 body，正文可纵向滚动；返回列表时保留原滚动位置。
- 页面进入只投递 `POLL_NOW(OPEN_INBOX)`，不在 LVGL callback 中同步等待网络。

## V2 MQTT 迁移边界

V1 不为 MQTT 预先新增 transport 抽象层。只固定一个传输无关的业务入口：“有新 inbox 数据可能到达，请 service 同步”。

V2 迁移时：

- MQTT 只发送轻量 wake-up/event（例如 device_id + notification_id/version），或在后续契约明确后传输完整 item。V1 默认路线是 MQTT 到达后触发一次 HTTP 完整快照同步，保留单一真相源。
- `memory_watch_service` 的 store、pending-read、generation、snapshot API 和 notification center 全部保留；只替换“何时触发同步”。
- MQTT client 必须属于长期 service owner，不随 Hermes 页面进出创建/销毁。实现前必须复查现有 `official_chat` MQTT shutdown/lwIP mutex 事故记录，禁止在回调或链路未静默时直接 destroy client。
- HTTP 保留为首次上线、断线恢复、漏消息补偿和手动刷新通道，不因 MQTT 上线而删除。

## 实施拆分

### 阶段 1：服务器 inbox 契约与存储

- 扩展 `server/watch_voice_endpoint/watch_contract.v1.json`，锁定 create/list/read 路径、请求字段、字节上限、状态码和响应字段。
- 新增 SQLite repository 与非破坏性初始化，将 FastAPI route 保持为薄适配层。
- 先写 pytest 覆盖验证、鉴权、幂等、不可变、最近 20 条、已读和持久化重启。

阶段门禁：server pytest 全通过，容器重建后消息仍存在，日志不出现 token 或正文。

### 阶段 2：固件 client、worker 与 store

- 在 `memory_watch_voice_client` 增加三个窄 HTTP 函数，不把调度或缓存逻辑放进 client。
- 在 `memory_watch_service` 增加 inbox worker、调度、PSRAM store、pending-read 和只读 snapshot API。
- 先用 fake client/source tests 验证完整快照原子替换、失败保留旧快照、无重叠 GET、本地已读优先和 404 终止重试。

阶段门禁：UI 调用路径不发 HTTP，owner task 不做阻塞 I/O，常驻大对象不在 task 栈，`idf.py build` 通过。

### 阶段 3：收件箱真实 UI

- 保留现有 list/detail 页面骨架，将 mock 数据替换为 meta/summary/item snapshot。
- 补齐空状态、缓存离线状态、本地立即已读、详情滚动和返回位置保留。
- host preview 使用固定 fake service snapshot，不在模拟器 UI 层直连真实服务器。

阶段门禁：列表/详情/空状态截图通过，中文无方框，所有控件位于 CO5300 安全区。

### 阶段 4：全局 notification center

- 在 LVGL 线程初始化单例 controller 和一个 top-layer bubble，不改 generated 页面的业务状态。
- 实现 surfaced ledger、active IDs、同通道合并、跨通道待展示槽位、blocker bitmask、点击导航与右滑清除。
- 将 conversation reply 的 off-page 到达事件接入同一 notification center，但保留独立点击目标。

阶段门禁：普通页面、Hermes、收件箱列表/详情、录音、安全 overlay、OTA/配网和熄屏场景均验证；气泡不拦截无关滚动。

### 阶段 5：端到端与真机收尾

- Hermes 用同一 `notification_id` 执行首次提交、超时后重试和内容冲突重试，确认只生成一条不可变记录。
- 真机覆盖首次联网立即拉取、断网恢复、亮屏 1 分钟、普通 5 分钟、打开收件箱立即拉取和已读失败后恢复同步。
- 用串口日志验证无 watchdog、无栈溢出、无 `ESP_ERR_NO_MEM`、无每帧 HTTP/大快照拷贝。

阶段门禁：server release gate、source tests、host 截图、`idf.py build`、`app-flash` 和限时 monitor 全部有证据。

## 进度

- `[x]` 已确认系统级消息中心定位，V1 只接 Hermes。
- `[x]` 已确认 V1 收件箱只读，不做任务操作。
- `[x]` 已确认 Hermes 后台回复退出页面后默认继续等待。
- `[x]` 已确认后台回复气泡点击回到 Hermes 对话页。
- `[x]` 已确认收件箱只放 Hermes 主动提示，不放普通对话回复。
- `[x]` 已确认 V1 先 HTTP 轮询，后续 MQTT。
- `[x]` 已确认全局气泡默认弹出，但录音/安全告警/关键流程/低功耗暂缓。
- `[x]` 已确认多条新消息合并为 `Hermes 有 N 条新提示`，点击进入收件箱列表。
- `[x]` 已确认服务器每个 device 保留最近 20 条，手表每次最多拉 20 条，V1 不分页。
- `[x]` 已确认 `read` 由服务器持久化，`surfaced` 仅手表本地运行期维护。
- `[x]` 已确认气泡使用深色顶部样式，并常驻显示，直到用户手动清除或点击处理。
- `[x]` 已确认气泡右滑清除，点击处理。
- `[x]` 已确认常驻气泡覆盖页面但不推动、不重排页面布局。
- `[x]` 已确认右滑后旧未读不重复弹，新到消息照常弹；气泡 `N` 只统计新增未展示消息。
- `[x]` 已确认暂缓条件解除后立即补弹合并气泡，不等待下一轮 HTTP 轮询。
- `[x]` 已确认 server inbox V1 使用 SQLite 持久化，现有对话请求缓存不随本轮迁移。
- `[x]` 已确认 inbox 下发、读取和已读回写复用现有 watch device token，不新增第二套密钥。
- `[x]` 已确认轮询返回最近 20 条完整快照和未读数，不做增量游标或分页。
- `[x]` 已确认 `notification_id` 由 Hermes 稳定生成，同一事件重试不得产生重复 inbox item。
- `[x]` 已确认提醒调度归 Hermes，watch endpoint 只负责即时入箱和持久化。
- `[x]` 已确认保留现有已读语义：打开列表不算已读，进入单条详情才标记已读。
- `[x]` 已确认气泡打开目标内容后，返回时回到气泡出现前的来源页面。
- `[x]` 已确认收件箱列表页收到新提示时原位更新列表、不重复弹气泡；详情页收到其他新提示时仍弹气泡。
- `[x]` 已确认全局只显示一个气泡；已有气泡期间的新提示在原位置合并更新，不堆叠多个气泡。
- `[x]` 已确认 inbox 写入按 `device_id + notification_id` 幂等成功：首次 `created=true`，重复请求 `created=false` 并返回已有消息。
- `[x]` 已确认 V1 HTTP 首次创建返回 `201 + created=true`，重复 ID 返回 `200 + created=false` 和已有消息；同进程直调时保留等价的布尔语义。
- `[x]` 已确认同一 `notification_id` 的内容首次创建后不可变；不同内容复用旧 ID 时不覆盖，只记录警告。
- `[x]` 已确认 V1 写入接口为 `POST /v1/watch/inbox?device_id={device_id}`，`source` 由服务端固定为 `hermes`。
- `[x]` 已确认 `created_at` 由服务端首次写入时生成，幂等重试不更新时间。
- `[x]` 已确认 V1 单条消息采用有界 UTF-8 字节长度，超限返回 `422`、不截断、不落库；环形缓冲区不替代单条消息上限。
- `[x]` 已确认 V1 `kind` 固定为 `reminder`、`info`、`warning`，未知类型返回 `422`；对话完成提醒不属于 inbox `kind`。
- `[x]` 已确认 ESP32 V1 仅解析并保存 `kind`，不增加基于类型的 UI 或业务分支。
- `[x]` 已确认标记已读接口可安全重复调用；已读消息重复标记仍成功，不存在的消息返回 `404`。
- `[x]` 已确认已读上报失败时本地保持已读并后台重试，服务端快照不得让未读点反复出现。
- `[x]` 已确认 V1 待同步已读只保存在 RAM；普通断网可恢复同步，断网期间重启后可能重新显示未读。
- `[x]` 已确认第 21 条消息到达时淘汰最旧记录，不区分旧消息是否已读。
- `[x]` 已确认 inbox 写入的五个内容字段全部必填且不能为空，非法请求返回 `422`。
- `[x]` 已确认 Hermes 是通知内容与触发 owner，Inbox 模块只负责持久化、幂等去重和读取；未来合并容器只改变内部调用方式，不改变职责与外部协议。
- `[x]` 已细化 V1 create/list/read HTTP 契约、空列表语义、错误分类和 SQLite 事务边界。
- `[x]` 已细化 FreeRTOS owner/worker 通信、轮询优先级、PSRAM 缓存、原子快照合并和低成本 getter。
- `[x]` 已明确 surfaced ledger 归 notification center 独占，并细化单气泡合并、跨通道排队、手势、安全区与 blocker 策略。
- `[x]` 已拆分 server、firmware service、inbox UI、notification center 和真机验收五个实施阶段。
- `[ ]` 待实现 server inbox API。
- `[ ]` 待实现固件 inbox client/service/store。
- `[ ]` 待实现全局 notification controller。
- `[ ]` 待把 mock inbox UI 切到真实 service snapshot。

## 决策记录

- 2026-06-25：决策：消息中心允许多个来源，但 V1 只接 Hermes。原因：架构上避免把全局通知绑死在 Hermes 页面；实现上保持范围可控。
- 2026-06-25：决策：V1 只读通知，不做确认/取消/回复/删除。原因：先打通提示回到手表的闭环，避免引入任务状态机。
- 2026-06-25：决策：退出 Hermes 页面不取消已经发送的请求。原因：用户希望离开 Hermes 后仍能通过气泡知道 Hermes 回复，不必一直等在页面里。
- 2026-06-25：决策：后台 Hermes 回复气泡点击回 Hermes 对话页。原因：这是用户刚才主动发起的对话结果，回到上下文最自然。
- 2026-06-25：决策：普通 Hermes 对话回复默认不进收件箱，收件箱主要放 Hermes 主动提示。原因：防止收件箱变成聊天历史。
- 2026-06-25：决策：V1 先 HTTP 轮询，后续 MQTT。原因：先用简单可靠链路打通业务和 UI，再用 MQTT 优化功耗和实时性。
- 2026-06-25：决策：短时间内多条新 Hermes 主动提示合并为 `Hermes 有 N 条新提示`，点击进入收件箱列表。原因：手表屏幕小，逐条弹出会打断用户，合并能保留可见性同时降低干扰。
- 2026-06-25：决策：服务器每个 device inbox 只保留最近 20 条，手表每次最多拉 20 条，V1 不分页。原因：手表小屏只适合快速扫最近提示，20 条足够 V1 使用，也能减少固件缓存和服务端清理复杂度。
- 2026-06-25：决策：`read` 由服务器持久化，`surfaced` 只在手表本地运行期维护。原因：已读是跨同步的用户状态，已弹出只是 UI 呈现状态，不应污染服务端模型。
- 2026-06-25：决策：全局通知使用深色顶部气泡，并常驻显示，直到用户手动清除或点击处理。原因：用户希望主动提示不会像 toast 一样自动消失，避免错过 Hermes 下发的重要内容。
- 2026-06-25：决策：右滑气泡清除，点击气泡处理。右滑只清除当前横幅，不标记 `read`，不影响收件箱未读数。原因：清除表示“先不看”，不是“已读”。
- 2026-06-25：决策：常驻气泡覆盖当前页面但不推动、不重排页面布局。原因：气泡是系统级 top-layer 浮层，避免每个页面都为通知横幅做布局适配；遮挡时用户可右滑清除。
- 2026-06-25：决策：右滑清除后，本批已经展示过但仍未读的旧消息不再重复弹出；后续新到消息正常弹出，合并数量只统计新增且尚未展示的消息。原因：区分“未读”和“是否已经打扰过”，避免用户明确清除后同一批消息反复出现。
- 2026-06-25：决策：录音、安全告警、关键流程或低功耗等暂缓条件解除后，立即补弹一次待展示消息；多条消息合并，不等待下一轮轮询。原因：消息已经在本地缓存，解除限制后立即呈现既降低延迟，也避免为 UI 展示额外触发网络请求。
- 2026-06-25：决策：server inbox V1 使用 SQLite 持久化，并按 `device_id + notification_id` 保持幂等；现有对话请求仍保留内存态。原因：inbox 必须跨服务重启保留，而 SQLite 足以支撑当前单服务、每设备 20 条的小规模数据，同时避免扩大任务持久化范围。
- 2026-06-25：决策：Hermes 下发、手表拉取和标记已读统一复用目标设备的现有 `watch device token`，不新增独立 push key。原因：用户选择 V1 单密钥方案以降低配置复杂度；代价是 Hermes 侧需要安全保存设备 token，后续规模扩大再拆分权限。
- 2026-06-25：决策：手表每次轮询获取最近 20 条完整 inbox 快照和 `unread_count`，不做增量游标或分页。原因：数据规模固定且很小，完整快照能简化断网恢复、服务重启和固件端状态合并。
- 2026-06-25：决策：`notification_id` 由 Hermes 按业务事件稳定生成，同一提醒重试复用相同 ID；服务端以 `device_id + notification_id` 去重。原因：只有消息生产者能稳定识别业务事件，服务端临时生成 ID 会让网络重试产生重复提醒。
- 2026-06-25：决策：提醒定时和业务触发继续由 Hermes 负责，watch endpoint 只接收已经到达通知时机的消息并立即写入 inbox，不支持 `scheduled_at`。原因：避免在 watch endpoint 内复制一套调度器和失败补偿状态机。
- 2026-06-25：决策：保留当前收件箱 UI 的已读语义，进入列表不改变消息状态，只有打开单条详情才标记该条已读。原因：用户浏览列表不代表已经阅读正文，也避免未读计数被意外清空。
- 2026-06-25：决策：用户从任意页面点击气泡进入通知详情或 Hermes 对话后，返回时恢复到气泡出现前的页面。原因：系统级通知是临时打断，不应破坏用户原本的操作位置和导航上下文。
- 2026-06-25：决策：用户已经停留在收件箱列表页时，新提示只在列表顶部原位出现并保留未读点，不再重复覆盖全局气泡；若正在查看某条详情，其他新提示仍弹气泡。原因：列表本身已经是通知承载面，而详情页只展示单条内容，仍需要提示其他消息到达。
- 2026-06-25：决策：notification center 同一时刻最多显示一个全局气泡；已有气泡期间又收到 Hermes 主动提示时，在原位置合并更新。待处理消息只有一条时点击进入详情，多条时显示 `Hermes 有 N 条新提示` 并点击进入收件箱列表。原因：手表顶部空间有限，堆叠横幅既遮挡内容，也会让清除和点击目标变得含糊。
- 2026-06-25：决策：同一 `device_id + notification_id` 的重复写入按幂等成功处理，不返回冲突；首次写入返回 `created=true`，重复写入返回 `created=false` 和已有消息。原因：Hermes 在超时或断网恢复后可以安全重试，并能明确区分本次是否真正创建了新通知。
- 2026-06-25：决策：V1 HTTP 写入接口首次创建返回 `201 Created + created=true`，重复 ID 写入返回 `200 OK + created=false` 和已有消息；未来合并进程时，`inbox_create()` 保留等价的返回结果。原因：`201` 准确表达新资源已创建，`200` 表达重试已幂等处理，同时把业务语义与 HTTP 部署形式解耦。
- 2026-06-25：决策：同一 `notification_id` 首次创建后内容不可变；重复请求携带不同标题、正文或时间时不覆盖已有消息，只记录警告，需要修正内容时由 Hermes 使用新 ID。原因：避免网络重试意外修改用户已经看过或已经标记已读的通知。
- 2026-06-25：决策：V1 写入接口使用 `POST /v1/watch/inbox?device_id={device_id}`，请求体不开放 `source`，服务端固定写入 `source=hermes`。原因：V1 只有 Hermes 来源，先固定来源身份能防止调用方伪造其他消息来源；未来扩展时再增加对应权限模型。
- 2026-06-25：决策：`created_at` 由 Inbox 存储层在通知首次成功入箱时生成，Hermes 不提交该字段，重复写入也不更新。原因：收件箱排序统一使用服务端时钟，避免 Hermes 主机时区或时钟偏差造成列表顺序错误。
- 2026-06-25：决策：V1 按 UTF-8 字节数限制 `notification_id/kind/title/preview/body` 为 `63/23/63/127/383` 字节；超限返回 HTTP `422`，不截断、不落库。环形缓冲区可用于接收流和最近消息队列，但不能消除单条消息的最大长度，且 LVGL 显示仍需要连续字符串。原因：服务端输入契约必须与 ESP32 固件固定缓冲区一致，避免溢出、半个 UTF-8 字符和不可预测的内存占用。
- 2026-06-25：决策：V1 inbox `kind` 固定为 `reminder`、`info`、`warning`，未知值返回 HTTP `422`；Hermes 普通对话完成提醒走独立的 Conversation Reply Channel。原因：有限枚举便于服务端校验和保持协议语义稳定，同时避免把对话回复误归入主动通知收件箱。
- 2026-06-25：决策：`kind` 的枚举合法性由服务端保证；ESP32 V1 只解析并保存字段，不根据类型改变显示或行为。原因：当前三类通知在手表端采用相同的只读处理流程，提前增加类型分支只会扩大固件复杂度。
- 2026-06-25：决策：`POST /v1/watch/inbox/{notification_id}/read?device_id={device_id}` 按幂等语义处理；首次标记和重复标记都成功，目标消息不存在时返回 HTTP `404`。原因：ESP32 在超时或断网恢复时可以直接重试，不需要为避免重复请求维护额外远端状态。
- 2026-06-25：决策：用户打开详情后，本地立即保持已读；即使服务器上报暂时失败，后续完整快照也不能将该消息恢复为未读，service 应保留待同步状态并后台重试。原因：网络波动不应让用户已经看过的消息反复出现未读点。
- 2026-06-25：决策：V1 的待同步已读集合只保存在 RAM，不写 NVS。普通断网不丢失状态，联网后继续重试；只有断网期间重启或掉电时可能恢复为服务器的未读状态。原因：先覆盖常见断网场景，避免 V1 提前引入 Flash 持久化、版本迁移和写放大管理。
- 2026-06-25：决策：每个设备严格保留最近 20 条，第 21 条写入后淘汰最旧记录，不区分最旧记录是否已读。原因：容量上限必须可预测，旧未读消息不能阻塞新的主动提示进入收件箱。
- 2026-06-25：决策：`notification_id`、`kind`、`title`、`preview`、`body` 全部必填且必须包含非空白内容；缺失或空白字段返回 HTTP `422`，短通知可以让 `preview` 与 `body` 相同。原因：这些字段分别承担幂等、类型、气泡/列表和详情展示职责，服务端拒绝不完整记录可避免 ESP32 增加缺字段兜底分支。
- 2026-06-25：决策：Hermes 负责产生通知的业务语义、内容和稳定 ID，Inbox 模块只负责校验、持久化、幂等去重、保留策略和手表读取。V1 分容器时通过内部 HTTP 写入，未来合并进 Hermes 同一进程后可改为 `inbox_create()`/repository 直调，但手表 API 和消息契约保持稳定。原因：容器边界属于部署细节，不应让存储模块替代 Hermes 成为通知生产者，也不应因后续合并而重做协议。
- 2026-06-25：决策：空收件箱固定返回 `200 + items=[] + unread_count=0`；列表是最近 20 条完整快照，已读接口返回 `404` 时 ESP32 将其视为该待同步项已终止。原因：空列表是正常资源状态，而被保留策略淘汰的旧消息已无法再标记，继续重试只会浪费网络和功耗。
- 2026-06-25：决策：`memory_watch_service` 独占 inbox store、poll 调度和 pending-read，窄 inbox worker 串行执行阻塞 HTTP；`watch_notification_center` 独占 surfaced ledger 和气泡状态。原因：分开网络真相与 UI 呈现状态，避免 owner task 阻塞和两个模块竞态改写 `surfaced`。
- 2026-06-25：决策：V1 不预先建 MQTT transport 抽象层；V2 默认用 MQTT 作为同步唤醒，HTTP 完整快照继续承担首次上线、断网恢复和漏消息补偿。原因：先保持单一真相源和最小 V1 复杂度，后续只替换触发时机而不重写 store/UI。

## 意外与发现

- 当前代码中 `memory_watch_service_init()` 已在开机时创建常驻 service task、upload worker、health worker 和 cancel worker；Hermes 网络处理天然可以脱离页面生命周期。
- 当前 `memory_watch_controller_back()` 在 `uploading/thinking` 时会调用 `memory_watch_service_cancel_waiting()`，这与“退出页面后台继续”等待的新产品口径冲突；实现时需要改为只在录音/编码中退出时取消。
- 当前 view/controller 已有 mock inbox page/detail/unread count 雏形；后续应替换数据来源，而不是重写整个 UI。

## 验证与验收

计划运行的验证命令：

```powershell
uv run python -m pytest server/watch_voice_endpoint/tests -q
uv run python -m unittest tests.test_memory_watch_ui_source tests.test_memory_watch_service_source
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

UI 相关实现后还需要：

```powershell
main/ui/agent_preview/scripts/build_apple_watch_s5_preview.ps1
main/ui/agent_preview/scripts/capture_apple_watch_s5_preview.ps1
```

期望看到的结果：

- create/list/read 的请求与响应字段受 `watch_contract.v1.json` 和 pytest 共同锁定；首次创建 `201`，重复创建 `200`，空列表 `200`。
- SQLite 中每设备最多 20 条，容器重启/重建后消息和已读状态保留。
- 轮询在独立 worker 中串行执行；失败保留最后有效快照，不重叠 GET，不在 LVGL getter 中推进网络状态。
- Hermes 页面外收到主动提示时出现全局气泡。
- 用户正在录音、安全告警或关键流程时，气泡不抢占，消息保留未读。
- inbox 与 conversation reply 共用一个视图但不混合点击目标；跨通道同时到达时顺序展示，不覆盖、不丢失。
- 退出 Hermes 页面后，已发送请求继续等待；回复到达后弹气泡，点击回 Hermes 对话页。
- 收件箱只展示 Hermes 主动提示，不展示普通对话回复。
- UI poll/getter 只读 snapshot，不执行网络。

当前实际结果：

- 产品、API、owner、FreeRTOS 执行模型、快照合并、全局气泡和分阶段门禁已细化；尚未实现代码。

## 幂等与恢复

- 如果中途中断，下次从本文件的“进度”和“决策记录”继续，不要回到旧的“收件箱只在 Hermes 页面内、不全局打断”口径。
- 如果 V1 HTTP 轮询实现失败，保留现有 Hermes 对话页与 mock inbox UI；不要引入 MQTT 作为救火方案。
- 如果全局气泡导致 UI 干扰，先通过优先级/暂缓策略收窄，不要把 inbox owner 移回页面层。

## 下一步

- 下一步最小动作：执行“阶段 1”，先扩展 `watch_contract.v1.json` 和 server pytest，再实现 SQLite repository 与 create/list/read route。
- 阶段 1 门禁通过前不开始固件 worker/UI，避免两端边写边猜协议。
