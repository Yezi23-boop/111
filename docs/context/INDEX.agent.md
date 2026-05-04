---
id: context-agent-index
tags: context, index, agent, low-token, entrypoint
summary: 面向 agent 首读的低 token 上下文入口，固定先查 runs 防重复，再 query/brief pack，最后只读必要原文。
last_reviewed: 2026-05-04
memory_type: procedural
scope: repo
owners: docs/context/INDEX.agent.md, docs/context/knowledge/project/project-profile.md, scripts/context/query.py, scripts/context/pack_context.py
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
- Use `query.py` first, then `pack_context.py --mode brief`.
- Open raw Markdown only when the brief pack points to a file you truly need.

## Low Token Workflow

1. Rebuild index:
   `uv run python scripts/context/build_index.py`
2. Check context health:
   `uv run python scripts/context/check.py`
3. Search past attempts first:
   `uv run python scripts/context/query.py --scope runs --q "<module file error symptom>" --top 8`
4. Search stable knowledge:
   `uv run python scripts/context/query.py --q "<task keywords>" --top 5`
5. Pack only concise context:
   `uv run python scripts/context/pack_context.py --q "<task keywords>" --top 5 --mode brief --max-chars 1800 --print`
6. Open raw files only for top hits or exact evidence.

## When Runs Hit

- If `runs/` shows the same attempt, say whether you will reuse, avoid, or extend it.
- If a previous route failed, do not repeat it unless new evidence changes the premise.
- If you create a new meaningful attempt, record it:
  `uv run python scripts/context/log_attempt.py --title "<short-title>" --status partial --changed <path> --tried "<action>" --avoid "<do-not-repeat>" --evidence "<proof>" --next "<next>"`

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
- Start with:
  `docs/context/knowledge/project/axp2101-power-component-implementation.md`
- Attempt record:
  `docs/context/runs/2026-05-04-attempt-axp2101-readonly-power-service.md`

## Audio And ESP-DL

- Audio owner session:
  `docs/context/runs/2026-05-04-attempt-audio-codec-owner-session.md`
- Danger model plan:
  `docs/context/knowledge/project/espdl-danger-model-plan-anchor.md`
- Single active threshold attempt:
  `docs/context/runs/2026-05-04-attempt-espdl-danger-single-active-threshold.md`

## Memory Writes

- Stable facts go to `docs/context/knowledge/`.
- Decisions go to `docs/context/decisions/`.
- Repeatable procedures go to `docs/context/procedures/`.
- Attempts, failures, board logs, and verification go to `docs/context/runs/`.
- Current task compression goes to `docs/context/handoffs/current-task.md`.
- Cleanup and promotion policy:
  `docs/context/procedures/context-garden-policy.md`
- Always update `docs/context/CHANGELOG.md` for context-system changes.
