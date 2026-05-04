# 知识地图

## 低 token 入口

- `INDEX.agent.md`
- `knowledge/project/project-profile.md`

## 硬件与点亮

- `knowledge/esp32-s3/lvgl-porting-baseline.md`
- `knowledge/esp32-s3/amoled-206-board-hardware-map.md`
- `knowledge/esp32-s3/axp2101-minimal-probe.md`
- `knowledge/esp32-s3/audio-codec-address-clock-baseline.md`
- `knowledge/esp32-s3/ft3168-shared-i2c-baseline.md`
- `knowledge/esp32-s3/pcf85063atl-minimal-probe.md`
- `knowledge/esp32-s3/power-optimization-checklist.md`
- `knowledge/esp32-s3/qmi8658c-minimal-probe.md`

## 项目规则与结构

- `knowledge/project/project-profile.md`
- `knowledge/project/context-memory-policy.md`
- `knowledge/project/repo-overview.md`
- `knowledge/project/layering-boundary-map.md`
- `knowledge/project/display-touch-audio-bus-map.md`
- `knowledge/project/startup-init-and-blocking-chain.md`
- `knowledge/project/power-wakeup-control-map.md`
- `knowledge/project/low-power-management-baseline.md`
- `knowledge/project/hardware-capability-gap-map.md`
- `knowledge/project/storage-and-provisioning-paths.md`
- `knowledge/project/embedded-c-cpp-engineering-rules.md`
- `knowledge/project/embedded-framework-mentor-skill.md`
- `knowledge/project/teaching-milestones.md`
- `decisions/ADR-20260311-default-embedded-codegen-rules.md`
- `decisions/README.md`

## 程序化流程

- `procedures/README.md`
- `procedures/context-garden-policy.md`
- `knowledge/project/agent-operational-rules.md`
- `knowledge/project/embedded-c-cpp-engineering-rules.md`

## 联网与配网

- `knowledge/project/network-provisioning-custom-upper-architecture.md`
- `knowledge/project/wifi-management-ui-behavior.md`
- `knowledge/project/storage-and-provisioning-paths.md`
- `knowledge/project/softap-provisioning-placeholder-api-limit.md`
- `knowledge/project/wifi-provision-removal-migration-checklist.md`

## AI 与 official_chat

- `knowledge/project/official-chat-feasibility-and-gap-assessment.md`
- `knowledge/project/official-chat-config-completeness-audit.md`
- `knowledge/project/official-chat-ota-tls-time-bootstrap.md`
- `knowledge/project/ai-ui-entry-network-guidance.md`

## AI 音频与 ESP-DL

- `knowledge/project/espdl-danger-model-plan-anchor.md`
- `knowledge/project/hearing-assist-danger-alert-system-architecture.md`
- `knowledge/project/hearing-assist-danger-alert-state-machine-and-notification-policy.md`
- `knowledge/project/hearing-assist-danger-alert-parameter-defaults-table.md`
- `knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`
- `knowledge/project/espdl-audio-tdnn-port.md`
- `knowledge/project/danger-signal-detection-port.md`
- `knowledge/project/audio-horn-like-segment-extraction.md`
- `knowledge/project/audio-low-activity-filter-script.md`

## 运行记录与交接

- `runs/README.md`
- `runs/run-template.md`
- `runs/attempt-template.md`
- `plans/active/README.md`
- `plans/active/plan-template.md`
- `plans/completed/README.md`
- `handoffs/handoff-template.md`
- `handoffs/current-repo-state.md`
- `handoffs/current-task.md`

## 评测与维护

- `evals/query-golden.yaml`
- `../scripts/context/eval_query.py`
- `procedures/context-garden-policy.md`
- `README.md`
- `CHANGELOG.md`

## 扩展方式

- 在 `knowledge/<domain>/` 下新增主题文件。
- 在 `decisions/` 下新增 ADR 文件。
- 在 `procedures/`、`runs/`、`plans/`、`handoffs/` 中按记忆类型补充内容。
- 修改后重建索引：`uv run python scripts/context/build_index.py`
