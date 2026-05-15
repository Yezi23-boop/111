---
id: context-agent-index
tags: context, index, agent, low-token, entrypoint
summary: 面向 agent 首读的低 token 上下文入口，固定使用 validate_context light 先查 runs 与稳定知识，最后只读必要原文。
last_reviewed: 2026-05-05
memory_type: procedural
scope: repo
owners: docs/context/INDEX.agent.md, docs/context/knowledge/project/project-profile.md, scripts/context/validate_context.py
triggers: agent-index, low-token, context-entry, routing, anti-repeat
evidence_level: design
---

# Agent Context Index

## Purpose

- This is the first-read index for agents in `D:\esp32S3\111`.
- Goal: avoid loading the whole context garden.
- Goal: avoid repeating previous agent attempts.
- Goal: route to exact owner docs before touching code.
- Read this file and `docs/context/knowledge/project/project-profile.md` first.

## Hard Rule

- Do not read `docs/context/README.md`, `knowledge-map.md`, or `repo-overview.md` by default.
- If `project-profile.md` mentions `repo-overview.md`, treat it as on-demand, not default.
- Do not bulk-open `docs/context/knowledge/**`.
- Do not treat `CHANGELOG.md` as the primary memory; it is too long.
- Use `validate_context.py --level light --brief` as the default entry.
- Treat `query.py` and `pack_context.py` as implementation details or advanced debugging tools, not the normal first step.
- Open raw Markdown only when the brief output points to a file you truly need.
- For planning/framework/route/architecture tasks, open matched planning or architecture Markdown before reading implementation code; do not infer product framework directly from current code.

## Low Token Workflow

1. For normal tasks, use light retrieval:
   `uv run python scripts/context/validate_context.py --level light --q "<module file error symptom>" --brief`
2. This searches `runs/`, searches stable knowledge, and emits a brief pack.
3. Open raw Markdown only for top hits or exact evidence.
4. Use `--level standard|routing|full` only when editing context docs, routing, or context mechanisms.

## Planning And Framework Tasks

- Trigger words include: `规划`, `框架`, `整体方案`, `路线`, `架构`, `先搭起来`, `按之前方案`, `上下文库里有`.
- First run:
  `uv run python scripts/context/validate_context.py --level light --q "<任务关键词>" --brief`
- If retrieval hits `docs/context/knowledge/project/*.md`, `docs/context/plans/**/*.md`, or `docs/context/runs/**/*.md`, open those planning/architecture docs first.
- Summarize the document goal, owner, boundary, and forbidden paths before deciding where code should change.
- Read implementation code only after the matched documents cannot answer the owner or layer question.

## When Runs Hit

- If `runs/` shows the same attempt, say whether you will reuse, avoid, or extend it.
- If a previous route failed, do not repeat it unless new evidence changes the premise.
- If you create a new meaningful attempt, record it:
  `uv run python scripts/context/log_attempt.py --title "<short-title>" --record-because repeat-risk --status partial --changed <path> --tried "<action>" --avoid "<do-not-repeat>" --evidence "<proof>" --next "<next>"`

## Routing

- Repo state and owner map:
  `docs/context/knowledge/project/project-profile.md`
- Current `main/` layout:
  `docs/context/knowledge/project/main-directory-map.md`
- Layering boundary:
  `docs/context/knowledge/project/layering-boundary-map.md`
- Detailed agent operations:
  `docs/context/knowledge/project/agent-operational-rules.md`
- Embedded C/C++ rules:
  `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md`

## Display And Touch

- Owners: `components/lvgl_port`, `components/co5300_panel`, `components/touch_ft5x06`, `main/ui`.
- Start with:
  `docs/context/knowledge/project/display-render-touch-transfer-pipeline.md`
- For Agent-generated LVGL host previews:
  `docs/context/knowledge/project/gui-guider-lvgl-host-preview-workflow.md`
- For Wi-Fi UI lifecycle crash:
  `docs/context/runs/2026-05-04-attempt-wifi-ui-lifecycle-crash.md`

## Network And Provisioning

- Owners: `components/network_manager`, `components/network_provisioning_adapter`, `components/ap_portal_adapter`, `components/wifi_control`.
- Start with:
  `docs/context/knowledge/project/network-provisioning-custom-upper-architecture.md`
- For SoftAP portal:
  `docs/context/runs/2026-05-04-attempt-softap-captive-portal-official-provisioning.md`
- For BLE miniapp MTU/protobuf:
  `docs/context/runs/2026-05-04-attempt-ble-official-provisioning-mtu-fragmentation.md`

## Official Chat

- Owners: `components/official_chat`, `main/services/official_chat_service.c`, `main/services/network_service.c`.
- TLS/OTA time bootstrap:
  `docs/context/runs/2026-05-04-attempt-official-chat-ota-tls-time-bootstrap.md`
- Stable card:
  `docs/context/knowledge/project/official-chat-ota-tls-time-bootstrap.md`

## Power

- Owners: `components/axp2101`, `main/app/board_power.c`, `main/services/power_service.c`.
- For whole-watch resource framework / state budget / background feature arbitration:
  `docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md`
- Start with:
  `docs/context/knowledge/project/axp2101-power-component-implementation.md`
- Attempt record:
  `docs/context/runs/2026-05-04-attempt-axp2101-readonly-power-service.md`

## Audio And ESP-DL

- Audio owner session:
  `docs/context/runs/2026-05-04-attempt-audio-codec-owner-session.md`
- Danger model plan:
  `docs/context/knowledge/project/espdl-danger-model-plan-anchor.md`
- Firmware mapping:
  `docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`
- Single active threshold attempt:
  `docs/context/runs/2026-05-04-attempt-espdl-danger-single-active-threshold.md`

## Memory Writes

- Memory policy: `docs/context/knowledge/project/context-memory-policy.md`
- Stable facts go to `docs/context/knowledge/`.
- Decisions go to `docs/context/decisions/`.
- Repeatable procedures go to `docs/context/procedures/`.
- Attempts, failures, board logs, and verification go to `docs/context/runs/`.
- Current task compression goes to `docs/context/handoffs/current-task.md`.
- Cleanup and promotion policy: `docs/context/procedures/context-garden-policy.md`
- Always update `docs/context/CHANGELOG.md` for context-system changes.
