---
id: 2026-06-17-ai-memory-watch-softap-nvs-config-plan
tags: plan, archived, ai-memory-watch, hermes, softap, nvs, ap-portal, endpoint-config
summary: AI Memory Watch 通过 SoftAP 门户写入 watch endpoint NVS 配置的可执行计划，固定字段、UI 行为、安全边界和验证步骤。
created: 2026-06-17
last_reviewed: 2026-06-17
status: archived
memory_type: project_plan
scope: repo
owners: components/ap_portal_adapter, main/services/memory_watch_service.c, main/app/app_main.c
triggers: ai-memory-watch, hermes, softap, nvs, device_token, watch_endpoint_config
evidence_level: design
---

# AI Memory Watch SoftAP / NVS 配置执行计划

## 目标

把 AI Memory Watch 的 watch endpoint 配置从开发期源码兜底收敛到运行期配置：

```text
SoftAP 门户
  -> POST /api/memory-watch/config
  -> app_main.c 桥接回调
  -> memory_watch_service_save_endpoint_to_nvs()
  -> NVS namespace: memory_watch
```

最终用户可以通过 SoftAP 门户写入：

```text
base_url
device_id
device_token
timeout_ms
allow_http
```

V1 页面不让用户填写 `timeout_ms`，但前端提交固定 `120000`。

## 当前已知状态

- `memory_watch_service` 已有 NVS 读写路径，NVS namespace 为 `memory_watch`。
- 已有 NVS key：
  - `base_url`
  - `device_id`
  - `device_token`
  - `timeout_ms`
  - `allow_http`
- `memory_watch_service_save_endpoint_to_nvs()` 已是配置保存 owner。
- `app_main.c` 已注册 `app_memory_watch_portal_config_cb()`，把 AP portal 配置桥接到 `memory_watch_service_save_endpoint_to_nvs()`。
- `components/ap_portal_adapter/src/ap_portal_routes.c` 已有 `POST /api/memory-watch/config`，能解析 `base_url/device_id/device_token/timeout_ms/allow_http`。
- `ap_portal_adapter` 不应 include 或反向依赖 `memory_watch_service`；继续通过 callback 解耦。
- 当前还缺主要是 SoftAP 前端 UI 与 `/api/status` 的“是否已配置”只读状态。

## 已确认产品决策

- SoftAP 页面分成两个按钮：
  - `保存 Hermes 配置`
  - `连接 Wi-Fi`
- AI Memory Watch 配置区默认折叠。
- `base_url` 可编辑，默认 `https://watch.934000.xyz`。
- `device_id` 可编辑，默认 `watch-001`。
- `device_token` 默认隐藏，提供显示/隐藏按钮。
- 保存成功后清空 `device_token` 输入框。
- 保存失败时保留 `device_token` 输入框内容。
- `timeout_ms` 不在页面展示，前端提交固定 `120000`。
- `allow_http` 默认关闭，只用于本地开发明文 HTTP endpoint。
- 保存成功后立即应用到当前运行态，同时写入 NVS。
- 如果当前有录音、上传、等待或 cancel 等活跃请求，返回 `409 request_active`，不要半路换 endpoint。
- V1 不做测试连接按钮。
- V1 不做清除 Hermes 配置按钮。
- 允许直接覆盖已有 Hermes 配置，不做二次确认。
- 页面显示“已配置 / 未配置”状态，但不回显 `base_url/device_id/device_token`。

## 安全边界

- 不修改 `official_chat`。
- 不把真实 `device_token`、Hermes API key、MiMo key、Cloudflare token 写入仓库。
- 不在日志、响应、文档或测试输出中打印真实 token。
- `device_token` 只代表 ESP32-S3 到 watch endpoint 的设备 token；不是 Hermes API Server key。
- SoftAP 保存成功响应不得回显 token。
- `/api/status` 只能返回布尔配置状态，不能返回 `device_token`。
- 默认不允许 `http://` endpoint；只有 `allow_http=true` 时才允许。
- `base_url` 是 endpoint 根地址，例如 `https://watch.934000.xyz`，不能是 `/v1/watch/voice-command` 这类具体接口。

## 目标接口

已有并继续使用：

```http
POST /api/memory-watch/config
```

请求体：

```json
{
  "base_url": "https://watch.934000.xyz",
  "device_id": "watch-001",
  "device_token": "<watch endpoint device token>",
  "timeout_ms": 120000,
  "allow_http": false
}
```

成功响应：

```json
{
  "ok": true,
  "memory_watch_configured": true
}
```

扩展现有状态接口：

```http
GET /api/status
```

增加字段：

```json
{
  "memory_watch_config_supported": true,
  "memory_watch_endpoint_configured": true
}
```

`memory_watch_endpoint_configured` 只表示当前运行态或 NVS 已有完整 watch endpoint 配置，不回显任何字段值。

## 前端行为

修改范围优先限定在：

```text
components/ap_portal_adapter/web/index.html
components/ap_portal_adapter/web/app.js
components/ap_portal_adapter/web/app.css
```

页面结构：

```text
Wi-Fi 配网主区
  - 扫描网络
  - 选择 SSID
  - Wi-Fi 密码
  - 按钮：连接 Wi-Fi

AI Memory Watch 折叠区，默认折叠
  - base_url
  - device_id
  - device_token
  - 显示/隐藏 token
  - allow_http
  - 按钮：保存 Hermes 配置
  - 状态：已配置 / 未配置
```

默认值：

```text
base_url = https://watch.934000.xyz
device_id = watch-001
device_token = 空
allow_http = false
timeout_ms = 120000
```

前端校验：

```text
base_url 非空
device_id 非空
device_token 非空
base_url 不含空格或换行
base_url 必须以 https:// 或 http:// 开头
http:// 只有 allow_http=true 时允许
base_url 不能包含 /v1/watch/health
base_url 不能包含 /v1/watch/voice-command
base_url 不能包含 /v1/watch/request
```

保存成功：

```text
清空 device_token 输入框
显示：Hermes 配置已保存。联网后手表会自动检测在线状态。
刷新配置状态为已配置
```

保存失败：

```text
保留 device_token 输入框内容
显示稳定错误码或短错误提示
```

## 后端行为

优先复用已有后端：

```text
components/ap_portal_adapter/src/ap_portal_routes.c
main/app/app_main.c
main/services/memory_watch_service.c
main/services/memory_watch_service.h
```

后端要保证：

- `POST /api/memory-watch/config` 不打印请求体。
- 保存成功不回显 token。
- 字段校验失败返回 `400 invalid_config` 或已有稳定错误码。
- 当前有活跃请求时返回 `409 request_active`。
- 保存成功后立即更新运行态并写入 NVS。
- 写 NVS 失败返回 `500 save_failed`。
- `/api/status` 增加 `memory_watch_endpoint_configured`。

如现有 `memory_watch_service` 没有公开“是否已配置”的只读 API，可新增窄接口，例如：

```c
bool memory_watch_service_is_endpoint_configured(void);
```

该接口只能返回布尔值，不返回配置内容。

## 验证计划

先跑 source tests：

```powershell
uv run python -m pytest tests/test_ap_portal_http_api_source.py tests/test_ap_portal_adapter_source.py tests/test_memory_watch_service_source.py -q
```

再跑格式检查：

```powershell
git diff --check -- components/ap_portal_adapter main tests docs/context/plans/active/2026-06-17-ai-memory-watch-softap-nvs-config-plan.md
```

如果改动包含 C 代码或前端嵌入资源，跑构建：

```powershell
& 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'
idf.py build
```

如果改动过 `sdkconfig`，必须先：

```powershell
idf.py fullclean
idf.py build
```

真机验证建议：

```text
1. app-flash
2. 进入 SoftAP 门户
3. 展开 AI Memory Watch 配置区
4. 保存 base_url/device_id/device_token/allow_http
5. 确认页面提示保存成功且 token 输入框清空
6. 重启设备
7. 串口日志看到 watch endpoint configured from NVS
8. 联网后 boot health check 显示 Hermes online
```

## 完成条件

- SoftAP 页面有默认折叠的 AI Memory Watch 配置区。
- Wi-Fi 配网按钮与 Hermes 配置保存按钮分离。
- `POST /api/memory-watch/config` 可由页面提交完整配置。
- 保存成功后 token 输入框清空。
- 保存失败后 token 输入框保留。
- `/api/status` 能显示 `memory_watch_endpoint_configured` 布尔状态。
- 不回显、不记录、不提交任何真实 token。
- 不修改 `official_chat`。
- source tests 通过。
- `idf.py build` 通过。

## 非目标

- 不做 Hermes 连接测试按钮。
- 不做清除 Hermes 配置按钮。
- 不做 token 读取或回显。
- 不做公网 watch endpoint、Hermes API Server 或 Cloudflare 配置变更。
- 不做 ESP32 真机按住说话新验证；那是配置写入完成后的下一步。

## 中断恢复

如果中途中断，下一位 agent 从以下顺序继续：

1. 检查当前 diff，确认没有真实 token。
2. 确认 `POST /api/memory-watch/config` 后端仍通过 callback 到 `memory_watch_service_save_endpoint_to_nvs()`。
3. 补或检查 `/api/status` 的 `memory_watch_endpoint_configured`。
4. 补 SoftAP 前端折叠区和保存按钮。
5. 跑 source tests。
6. 跑 `idf.py build`。
7. 只提交本任务相关文件。
