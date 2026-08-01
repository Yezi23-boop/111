---
id: plan-2026-08-01-onenet-ota-provider-minimal
tags: plan, active, ota, onenet, fuse-ota, https, sota, dual-provider
summary: 在不复制现有 Flash、双槽和回滚逻辑的前提下，为 ota_service 增加 OneNET Fuse OTA 最小 provider，并完成一次手动 SOTA 完整包真机闭环。
last_reviewed: 2026-08-01
memory_type: task
scope: task
status: archived
owners: main/services/ota, main/ui/custom/ota_maintenance_view.c, server/watch_voice_endpoint, tools/ota_host, tests/test_ota_service_source.py
triggers: OneNET OTA, fuse-ota, SOTA, OneNET provider, 双 provider, MCU 软件升级
evidence_level: evidence
---

# OneNET OTA Provider 最小实现计划

## 目标与全局

- 保留已经通过 COM7 验证的自建云端 OTA，不替换、不回归其 manifest、SHA-256、弱网续传、双槽和 rollback 行为。
- 新增一个窄 `OneNET provider`，只负责鉴权、上报版本、检查 SOTA 任务、提供带鉴权的下载请求和上报 OneNET 状态。
- 用户在 OTA 页面明确选择“自建云端”或“OneNET”，继续使用既有三步：`CHECK -> DOWNLOAD -> ACTIVATE`。
- 第一版只完成 `watch-001` 的手动检查和完整包 SOTA 真机闭环。

## 当前基线与已确认事实

- OneNET 产品：`watch`；产品 ID：`w23kT21Z3x`；设备：`watch-001`。
- OneNET 已上传 `watch_1.0.6_full.bin`，升级模块为“MCU 软件”，目前未验证。
- OneNET Fuse OTA 当前接口为：
  - `POST /fuse-ota/{pid}/{device}/version`
  - `GET /fuse-ota/{pid}/{device}/check?type=2&version={app_version}`
  - `GET /fuse-ota/{pid}/{device}/{tid}/download`
  - `POST /fuse-ota/{pid}/{device}/{tid}/status`
- `type=2` 表示 SOTA；当前应用版本写入 `s_version`。设备没有独立通信模组时，`f_version` 使用固定占位版本，不参与 SOTA 选择。
- OneNET check 返回 `target / tid / size / md5`，没有当前自建 manifest 的 SHA-256。
- 现有 `ota_service / ota_transport / ota_boot_check / runtime_coordinator` 已完成双槽、维护前台、弱网续传、`STAGED`、`ACTIVATE`、`PENDING_VERIFY` 和 rollback 真机验证；OneNET 不重复实现这些能力。

## 范围与非目标

### 本轮明确要做

- 手动从 OTA 页面选择 OneNET 并执行检查。
- SOTA 完整包，`type=2`。
- OneNET HTTPS 版本上报、任务检查、下载鉴权、最小状态上报。
- 对 OneNET 下载同时校验 `size + MD5`；自建云端继续执行原有 `size + SHA-256`，不得被弱化。
- 升级任务 `tid` 和目标版本跨重启持久化；新固件通过既有 boot-check 后，联网再向 OneNET 上报成功和新版本。

### 本轮明确不做

- 不接入 OneNET MQTT，不订阅 `ota/inform`，不新增常驻连接或后台 task。
- 不做周期检查、平台推送、批量升级、差分包、OneNET OpenAPI 自动上传或网页发布集成。
- 不导入 OneNET 通用 SDK、RT-Thread/OneOS SDK或第三方项目的 OTA 状态机。
- 不修改分区表、`otadata` 格式、bootloader、assets、resources、model 或现有回滚策略。
- 用户明确允许产品级 AccessKey 写入 C 源码并编译进固件；仍不得写入日志或最终回复。
- 不新增通用 ProviderManager、网络管理器或新的资源仲裁层。

## Owner 与最小落点

| 层 | 最小职责 | 计划落点 |
| --- | --- | --- |
| UI | 选择 provider、提交现有三步 intent、读取 snapshot | `main/ui/custom/ota_maintenance_view.c/.h` |
| OTA service | 保存当前 provider、编排既有 command queue/前台 owner、持久化待上报任务 | `main/services/ota/ota_service.c/.h` |
| OneNET provider | token/HTTPS API/JSON/错误翻译；同步运行在既有 OTA task | 新增 `main/services/ota/onenet_ota_provider.c/.h` |
| OTA transport | 注入 OneNET Authorization，保持现有 `esp_https_ota` 生命周期和续传；按 provider 校验 SHA-256 或 MD5 | `main/services/ota/ota_transport.c/.h` |
| boot-check | 只完成本地 valid/rollback 判定并记录结果，不在早期启动访问网络 | `main/services/ota/ota_boot_check.c/.h` |
| host 预检 | 从仓库运维文件验证真实 OneNET 鉴权和 API，不打印密钥 | `tools/ota_host/onenet_probe.py` |

不新增 OneNET task。并发语义继续由现有 OTA owner task、command queue 和 snapshot 负责；provider 是该 task 内的同步 adapter，因此不需要额外 queue、event group 或裸 flag。

## 鉴权闸门

官方 Fuse OTA 示例使用 `version=2022-05-01` 的统一 Authorization。设备密钥只适用于 MQTT 设备连接，不能未经验证地当作 Fuse OTA API 密钥。

实现固件前必须先在 PC 侧完成：

1. 用 `res=products/{pid}` 和产品 AccessKey 生成短期 HMAC token。
2. 调用 `POST .../version`，只上报真实的 `s_version`，确认 OneNET 返回 `code=0`。
3. 调用 `GET .../check?type=2&version=...`，确认 `code=12012`（无任务）或得到合法任务结构。
4. 预检从本机 `--access-key-file` 读取密钥，stdout/stderr 不打印 token、AccessKey 或完整 Authorization。

安全决策：

- 用户已确认使用产品级 AccessKey；它用于本机预检、C 源码默认值和设备侧 NVS，仍不写入日志或最终回复。
- 预检默认读取 `tools/ota_host/onenet.txt`；设备侧首次缺少 NVS 凭据时使用 C 源码默认值并落盘，后续优先使用 NVS。
- 产品级鉴权若被 Fuse OTA 拒绝，立即停止；禁止静默退回主用户 AccessKey。

## 最小阶段与验收

### 阶段 0：PC 鉴权与真实 API 预检

- [x] 验证产品级 Authorization 可调用版本上报和 SOTA check。
- [x] 保存不含密钥的响应证据：`version_code=0`、`check_code=12012`（当前无任务）。
- [x] 预检工具支持 `--access-key-file`，不要求设置环境变量且不打印密钥。
- [x] 产品级 AccessKey 已放入 `tools/ota_host/onenet.txt`，预检默认读取该文件。
- [x] 产品级 AccessKey 已写入 `onenet_ota_provider.c` 默认常量并随固件编译；轮换时同步更新运维文件和 C 常量。

验收：OneNET 页面显示 `watch-001` 的真实当前 `s_version`；产品级鉴权失败时不继续固件实现；AccessKey 按用户授权编译进固件。

### 阶段 1：只实现 report/check 的 OneNET provider

- [x] 新增窄 provider，使用证书 bundle 和主机名校验访问 `https://iot-api.heclouds.com/fuse-ota`。
- [x] `report_version()` 上报 `s_version=esp_app_desc.version`；`check()` 固定使用 `type=2`。
- [x] 严格校验响应 `code / target / tid / size / md5 / type`，长度有上限；`code=12012` 映射为“无更新”。
- [x] UI 增加 OneNET 明确检查入口，不做自动双查；`CHECK` 不进入 Flash 写入路径。

验收：选择 OneNET 后能看到 `1.0.7` 更新信息；选择自建云端的行为和测试保持不变；错误响应不会进入 DOWNLOAD。

### 阶段 2：复用现有 transport 完成下载和 STAGED

- [x] 给 transport 增加最小的请求头注入能力，断线续传时每次请求都携带有效 Authorization。
- [x] OneNET 路径校验实际接收长度、任务 size 和 MD5；自建路径继续校验原 SHA-256。
- [x] DOWNLOAD 继续复用既有维护前台、4 KiB OTA 缓冲、256 KiB Range、续传重试和 60 秒无进度超时。
- [x] 下载完成只进入 `STAGED`，不改启动槽；取消/失败走现有 abort/cleanup。

验收：从当前槽只写备用槽；MD5/长度错误均不切换 `otadata`；自建 OTA 聚焦测试不回归。

### 阶段 3：ACTIVATE、重启确认和 OneNET 状态闭环

- [x] ACTIVATE 前持久化 `provider=onenet / tid / target`，然后复用现有 finish、设置启动分区和重启流程。
- [x] early boot-check 只做本地 valid/rollback，并由 OTA owner 读取 pending；不在早期启动等待网络或调用 OneNET。
- [x] 网络和 TLS 时间就绪后，由 OTA owner 上报成功/失败状态及当前版本，成功后清除 pending 记录。
- [x] 上报失败保留 pending，下一次启动或用户进入 OneNET 检查页面时只重试上报，不重复下载或切槽。

验收：OneNET 控制台显示成功且版本更新；断电/回滚后旧槽仍可启动，pending 状态不会造成重复升级。

### 阶段 4：COM7 最小真机闭环

- [x] 已通过 `app-flash` 将含 OneNET provider 的 `1.0.6` 写入 `ota_0`，再用一次明确的 `otatool.py switch_ota_partition --slot 0` 让它成为当前启动槽；启动日志确认 `App version: 1.0.6`、加载地址 `0x20000`。
- [x] 已上传含 provider 的 `1.0.7` 完整包，并为 `watch-001` 创建验证任务；OneNET 预检返回 `tid=1486220`、`status=1`、`target=1.0.7`。
- [x] 自动板测镜像已依次执行 CHECK、DOWNLOAD、ACTIVATE，并记录 OneNET、串口和 `otadata` 证据。

验收：`1.0.6 -> 1.0.7`、备用槽启动、`PENDING_VERIFY -> VALID`、OneNET 任务成功和版本上报形成同一条证据链；无 panic/WDT。

## 进度

- [x] OneNET 产品 `watch` 和设备 `watch-001` 已创建。
- [x] `watch_1.0.6_full.bin` 已上传为 MCU 软件完整包，但尚未验证。
- [x] 已核对当前 Fuse OTA 官方接口和一份 ESP-IDF 端到端参考实现；确认第三方代码只能作为协议参考，不能直接合并。
- [x] 已确认复用现有 ota_service/transport/boot-check，不新增 OneNET OTA 状态机或 MQTT task。
- [x] 阶段 0：产品级鉴权与真实 API 预检；仓库内 AccessKey 默认值和设备侧 NVS 回填已完成。
- [x] 阶段 1：最小 report/check provider 已接入 OTA 页面 CHECK；真实任务已返回 `target=1.0.7`、`tid=1486220`、`status=1`。
- [x] 阶段 2：自动板测已完成 OneNET CHECK、11.2 MiB 下载、MD5、`STAGED`；OneNET 的 HEAD/406 差异已在 transport 中隔离。
- [x] 阶段 3：ACTIVATE、备用槽重启和 `PENDING_VERIFY -> VALID` 已完成；状态终态 `code=20/task finish` 已兼容。
- [x] 阶段 4：COM7 `1.0.6 -> 1.0.7` 真机闭环已完成；最终 `otadata` 选择 `ota_0`（序号 `0x11`），当前应用为 `1.0.7`。

## 决策记录

- 2026-08-01：OneNET 作为第二 provider，自建云端 provider 保留。
- 2026-08-01：第一版只做用户进入 OTA 页面后的手动 HTTPS 检查，不做 MQTT、推送或周期轮询。
- 2026-08-01：只支持 SOTA 完整包，固定 `type=2`；不做差分。
- 2026-08-01：OneNET 返回 MD5，因此只为 OneNET 增加 MD5 准入；自建云端 SHA-256 合同不变。
- 2026-08-01：禁止复制第三方 OTA 状态机；只参考其真实接口响应，下载和回滚继续使用已验证的 ESP-IDF/本项目实现。
- 2026-08-01：用户明确允许产品级 AccessKey 进入仓库、写入 C 源码并随固件发布；不写入日志或对话回复。

## 意外与发现

- OneNET Fuse OTA 的 API 鉴权与 MQTT 设备密钥不是同一个权限模型；此前“把 device key 存 NVS 即可完成 OTA”的假设不成立。
- 当前已上传的 `1.0.6` 不包含尚未实现的 OneNET provider，不能作为最终“重启后成功上报”闭环目标；正式板测应使用 provider 基线 `1.0.6` 升级到 provider 目标 `1.0.7`。
- OneNET 任务只给 MD5，不给 SHA-256；不能伪造 SHA-256 字段或静默跳过完整性校验。
- OneNET Fuse OTA 下载接口拒绝 `HEAD`（HTTP 406），而 ESP-IDF `partial_http_download` 默认先发 HEAD；OneNET 路径改走普通 GET，仍保留 Range 断线续传，自建 manifest 保留分段下载。
- `ota_service` 的 8 KiB task stack 在 OneNET HTTPS/TLS 与维护窗口交接时溢出；已提升到 16 KiB，并通过自动板测确认无再次溢出。
- 弱网重连时 TLS 建连失败原先会直接退出；已增加建连阶段有限重试（5 秒间隔、最多 3 次），实测完整 11.2 MiB 下载完成。
- 产品级 AccessKey 预检已通过：`version_code=0`、`check_code=12012`；`12012` 只表示当前设备尚未加入升级任务，不是鉴权失败。
- 官方 OneNET Studio 文档把升级包上传、测试验证和设备任务添加放在控制台“运维监控 -> 远程升级”；设备侧 Fuse OTA API 不提供创建任务入口，因此当前 `12012` 只能通过控制台建任务消除。
- 2026-08-01 COM7 基线证据：`board_logs/2026-08-01-03-42-59-onenet-provider-slot0-reset.log` 显示从 `ota_0` 启动、应用版本 `1.0.6`、Wi-Fi 连接 `li`；对应摘要 `panic_log_seen=false`、无残留 monitor 进程。随后只读 `otatool.py read_otadata` 为 `0x0000000f / 0xa79cefa9` 与 `0x0000000e / 0x1f2088cc`，当前选择 `ota_0`。
- 2026-08-01 目标包已生成到 `.ota-release/onenet/watch_1.0.7_full.bin`，大小 `11235840` 字节，SHA-256 为 `A44B6F6C13FC6C063ECF680DB8B366B634F910DE6899092E4D371ED7C0C2A013`。
- 2026-08-01 OneNET 验证任务已创建：预检 `version_code=0`、`check_code=0`、`target=1.0.7`、`tid=1486220`、`status=1`、`type=1`；平台返回 MD5 `35c90db1f7325a720e511a4a5447e290`，与本地完整包一致。
- 2026-08-01 无人值守真机证据：`board_logs/2026-08-01-11-21-54-onenet-auto-closed-loop-retry.log` 显示 OneNET CHECK 返回目标 `1.0.7`，下载进度到 `100% (11235840/11235840)`，随后 `state=STAGED`、`state=RESTARTING`；重启后从 `ota_1` 启动，`App version: 1.0.7`，`ota_boot_check: PENDING_VERIFY confirmed valid`，无 panic/WDT。
- 2026-08-01 状态接口核对：OneNET `POST .../1486220/status` 在 `step=100` 返回 `code=20 / task finish`；provider 已将该终态视为成功。随后 host probe 以 `1.0.7` 上报版本并得到 `check_code=12012`，表示任务已完成且无待升级任务。
- 2026-08-01 最终只读 `otadata`：`0x00000011 / 0x175acf05` 与 `0x00000010 / 0xafe6a860`，当前选择 `ota_0`；修复后的同版本镜像已写入备用槽并启动验证 provider 终态兼容，未再次创建 OneNET 任务。

## 验证与验收

- context：`uv run python scripts/context/validate_context.py --level standard --q "OneNET OTA provider SOTA 最小实现" --brief`
- host：产品级 token 向量测试、真实 version/check API，不打印凭据。
- 聚焦测试：OneNET JSON、`type=2`、无任务、非法 size/MD5、Header 注入、两 provider 不互相污染。
- firmware：代码收敛后统一执行 `idf.py fullclean build`；不因每个小改动重复全量构建。
- board：只使用 `idf.py -p COM7 app-flash` 和 `scripts/board/agent_serial_monitor.ps1`；读 `otadata` 只读验证槽与序号。

## 幂等与恢复

- 中断后先从本计划“进度”恢复，已通过的阶段不重复跑全量验证。
- 阶段 0 未通过时不写固件 provider。
- CHECK/状态上报失败不写 Flash；DOWNLOAD 失败复用现有 abort；STAGED 未 ACTIVATE 时旧槽保持选择。
- pending 状态上报只有收到 OneNET `code=0` 才清除，重复上报不得触发重复下载或再次切槽。
- 若 OneNET 路线需要主用户 AccessKey 才能工作，暂停实现并重新讨论 server proxy，不把主用户密钥降级写入设备。

## 下一步

- 产品包继续沿用 `.ota-release/onenet/watch_1.0.7_full.bin`；后续轮换版本只需重新上传完整包、创建新任务，设备侧复用同一 CHECK -> DOWNLOAD -> ACTIVATE 闭环。
