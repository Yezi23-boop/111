<INSTRUCTIONS>
## Scope

- 本目录负责 `official_chat` 运行时，包括状态机、协议选择、音频服务、设备状态、OTA 和 board metadata 接入。
- 这是一个高耦合组件，改动往往会同时影响网络、音频和主流程。

## Read First

- 根目录 `AGENTS.md`
- `boards/esp32-s3-touch-amoled-2.06/AGENTS.md`
- `boards/esp32-s3-touch-amoled-2.06/README.md`
- `docs/context/knowledge/project/current-project-goal-traffic-audio.md`
- `docs/context/knowledge/project/traffic-audio-feature-manager.md`
- `components/official_chat/board_metadata/esp32-s3-touch-amoled-2.06.json`

## Do

- 先确认这次改动影响的是协议配置、状态机、音频服务、OTA，还是 board metadata。
- 保持 `protocol_config` 的 NVS 优先、fallback 次之的选择逻辑稳定。
- 修改 `Application` 启停、按钮行为、Wi-Fi 依赖或音频服务初始化时，说明对 feature-manager 和共享音频所有权的影响。
- 涉及 board metadata、设备状态上报或默认 websocket/token 时，明确区分“示例默认值”和“真实运行时来源”。

## Do Not

- 不要在未说明原因时扩大 `official_chat` 对共享音频资源的所有权边界。
- 不要把 Wi-Fi、feature-manager 或板级初始化细节偷偷重新内嵌回本组件。
- 不要在未验证前随意改协议字段、OTA 路径、NVS key 或默认 endpoint/token 行为。

## Validation

- 至少运行 `idf.py build`。
- 最小验证应说明：
  - `official_chat` 入口可编译
  - 状态机或协议选择逻辑无明显编译回归
  - 若改启动路径，需说明是否验证了 manager 路由或串口启动日志

## Escalation

- 若改运行时所有权、协议来源、OTA、board metadata 契约或与 `main/app_feature_manager` 的边界，需同步更新项目知识文档。
</INSTRUCTIONS>
