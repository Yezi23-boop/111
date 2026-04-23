# 知识地图

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

- `knowledge/project/repo-overview.md`
- `knowledge/project/display-touch-audio-bus-map.md`
- `knowledge/project/startup-init-and-blocking-chain.md`
- `knowledge/project/power-wakeup-control-map.md`
- `knowledge/project/low-power-management-baseline.md`
- `knowledge/project/hardware-capability-gap-map.md`
- `knowledge/project/storage-and-provisioning-paths.md`
- `knowledge/project/plan-mode-rules.md`
- `knowledge/project/embedded-c-cpp-engineering-rules.md`
- `knowledge/project/embedded-framework-mentor-skill.md`
- `knowledge/project/teaching-milestones.md`
- `decisions/ADR-20260311-default-embedded-codegen-rules.md`
- `decisions/README.md`

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

## 扩展方式

- 在 `knowledge/<domain>/` 下新增主题文件。
- 在 `decisions/` 下新增 ADR 文件。
- 修改后重建索引：`python scripts/context/build_index.py`
