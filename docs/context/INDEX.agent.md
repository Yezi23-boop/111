---
id: context-agent-index
tags: context, index, agent, low-token, entrypoint
summary: 面向 agent 首读的低 token 上下文入口，固定先用 validate_context light 查 runs 与稳定知识，再按 brief 只读必要原文。
last_reviewed: 2026-06-07
memory_type: procedural
scope: repo
status: active
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

## Ten Second Route

| Situation | First action | Validation level |
| --- | --- | --- |
| Normal code/doc task | Run light retrieval with task keywords | `light` |
| Planning, framework, route, architecture | Run light retrieval, then open matched plan/framework cards before code | `light` |
| Context document only change | Edit the matched context card, then validate docs | `standard` |
| Index, entrypoint, routing, golden query change | Validate routing behavior | `routing` |
| `scripts/context`, memory, promotion, archive mechanism change | Run full context validation | `full` |

Default command:

```powershell
uv run python scripts/context/validate_context.py --level light --q "<task keywords / file / error / symptom>" --brief
```

## Hard Rules

- Do not read `docs/context/README.md`, `knowledge-map.md`, or `repo-overview.md` by default.
- If `project-profile.md` mentions `repo-overview.md`, treat it as on-demand, not default.
- Do not bulk-open `docs/context/knowledge/**`.
- Do not treat `CHANGELOG.md` as the primary memory; it is too long.
- Use `validate_context.py --level light --brief` as the default entry.
- Treat `query.py` and `pack_context.py` as implementation details or advanced debugging tools, not the normal first step.
- Open raw Markdown only when the brief output points to a file you truly need.
- For planning/framework/route/architecture tasks, open matched planning or architecture Markdown before reading implementation code.
- Do not infer product framework directly from current source when current context cards already define owners and boundaries.

## Default Workflow

1. Run light retrieval:
   `uv run python scripts/context/validate_context.py --level light --q "<module file error symptom>" --brief`
2. Read the brief pack first.
3. If `runs/` hits, decide whether to reuse, avoid, or extend the previous attempt.
4. Open only the top matched source Markdown needed for evidence.
5. Decide owner, layer, and verification before editing code.
6. Use `--level standard|routing|full` only when the change affects context docs, routing, or context mechanisms.

## Planning And Framework Tasks

Trigger words include: `规划`, `框架`, `整体方案`, `路线`, `架构`, `先搭起来`, `按之前方案`, `上下文库里有`.

Workflow:

1. Run:
   `uv run python scripts/context/validate_context.py --level light --q "<任务关键词>" --brief`
2. If retrieval hits `docs/context/knowledge/project/*.md`, `docs/context/plans/**/*.md`, or `docs/context/runs/**/*.md`, open those docs first.
3. Summarize goal, owner, boundary, forbidden paths, and verification evidence from the matched docs.
4. Read implementation code only after those docs cannot answer the owner or layer question.

## When Runs Hit

If `runs/` shows a related attempt, report the handling explicitly:

- `reuse`: what evidence or conclusion will be reused.
- `avoid`: what failed route or repeated action will not be retried.
- `extend`: what new evidence, file, or test will be added if needed.
- `evidence`: which run file, log, build result, or board observation supports the decision.

If a previous route failed, do not repeat it unless new evidence changes the premise.

If a new meaningful attempt should be preserved, record it:

```powershell
uv run python scripts/context/log_attempt.py --title "<short-title>" --record-because repeat-risk --status partial --changed <path> --tried "<action>" --avoid "<do-not-repeat>" --evidence "<proof>" --next "<next>"
```

## Error And Route Capture

Capture only big errors and route choices that have reuse value. If a problem blocked progress, crossed owners, required multiple attempts, or changed which route should be used next, consider writing a short `runs/attempt`.

Use `error-signature` for major build/link failures, panic/Guru, watchdog, assertions, `ESP_ERR_*`, `NO_MEM`, board/serial anomalies, hardware communication failures, or context validation failures when the result would help a future agent avoid repeated diagnosis.

Use `route-choice` when the useful memory is the path selection itself: tried route, rejected route, why it failed or was dropped, chosen next route, and the evidence behind that choice.

Minimum content: raw error or route question, trigger command/action, related owner/file, tried path, rejected path, current conclusion, next evidence.

Recommended reasons when recording:

```powershell
--record-because error-signature --record-because evidence
--record-because route-choice --record-because evidence
```

## Authority And Conflicts

- `project-profile.md` is the low-token repo snapshot.
- `project-framework.md` is the current overall framework map.
- More specific active cards beat broad cards for their own domain.
- If two active cards conflict, prefer the one with the newer `last_reviewed` and narrower scope, then update the broader card or `CHANGELOG.md` to remove drift.
- `stale`, `superseded`, `retired`, `deprecated`, and archived cards are history unless the user asks for history or migration context.

## Core Routing

| Topic | Start with | Notes |
| --- | --- | --- |
| Repo state and owner map | `docs/context/knowledge/project/project-profile.md` | First repo snapshot after this index |
| Overall project framework | `docs/context/knowledge/project/project-framework.md` | Startup, owner, resource budget, update rules |
| Current `main/` layout | `docs/context/knowledge/project/main-directory-map.md` | Directory and owner map |
| Layering boundary | `docs/context/knowledge/project/layering-boundary-map.md` | App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK |
| Runtime owner contract | `docs/context/knowledge/project/runtime-owner-contract.md` | Startup phases, owner lifecycle, forbidden paths |
| Owner snapshot / FreeRTOS communication | `docs/context/knowledge/project/owner-snapshot-lifecycle-freertos-contract.md` | Snapshot getter, queue, notification, event group, release contract |
| Detailed agent operations | `docs/context/knowledge/project/agent-operational-rules.md` | Build, flash, monitor, hardware safety |
| Embedded C/C++ rules | `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md` | Code style, Doxygen, modularity, verification |

## Domain Routing

| Area | Owners | Start with |
| --- | --- | --- |
| Display and touch | `components/lvgl_port`, `components/co5300_panel`, `components/touch_ft5x06`, `main/ui` | `docs/context/knowledge/project/display-render-touch-transfer-pipeline.md` |
| GUI Guider / LVGL preview | `main/ui/generated`, `main/ui/custom`, host preview scripts | `docs/context/knowledge/project/gui-guider-lvgl-host-preview-workflow.md` |
| Network and provisioning | `components/network_manager`, `network_provisioning_adapter`, `ap_portal_adapter`, `wifi_control` | `docs/context/knowledge/project/network-provisioning-custom-upper-architecture.md` |
| SoftAP portal evidence | `components/ap_portal_adapter` | `docs/context/runs/2026-05-04-attempt-softap-captive-portal-official-provisioning.md` |
| BLE miniapp provisioning evidence | `network_provisioning_adapter`, BLE client/protobuf | `docs/context/runs/2026-05-04-attempt-ble-official-provisioning-mtu-fragmentation.md` |
| Official Chat | `components/official_chat`, `main/services/official_chat_service.c`, `network_service.c` | `docs/context/knowledge/project/official-chat-ota-tls-time-bootstrap.md` |
| Power / PMIC | `components/axp2101`, `main/app/board_power.c`, `main/services/power_service.c` | `docs/context/knowledge/project/axp2101-power-component-implementation.md` |
| Low power framework | `power_policy`, `sleep_coordinator`, wake evidence owners | `docs/context/knowledge/project/low-power-framework-architecture.md` |
| Audio sessions | `components/audio_codec`, `components/mp3_player`, feature/service users | `docs/context/runs/2026-05-04-attempt-audio-codec-owner-session.md` |
| ESP-DL danger model | `components/espdl_inference`, `danger_detection_service` | `docs/context/knowledge/project/espdl-danger-model-plan-anchor.md` |
| Hearing assist / danger alerts | `danger_detection_service`, `app_alert_manager`, controller UI | `docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md` |
| IMU / motion framework | `components/qmi8658c`, `main/app/board_imu`, `main/services/imu_service`, `components/imu_motion` | `docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md` |
| QMI8658C board evidence | `qmi8658c`, `board_imu`, GPIO21/INT evidence | `docs/context/runs/2026-06-04-attempt-qmi8658c-int1-gpio21-schematic-board-evidence.md` |
| AI Memory Watch / Hermes | future `memory_watch_service`, `voice_client_service`, audio session owner | `docs/context/plans/active/2026-06-05-ai-memory-watch-hermes-page-plan.md` |
| AI Memory Watch positioning | Hermes server + ESP32-S3 watch product boundary | `docs/context/knowledge/project/ai-memory-watch-product-positioning.md` |

## Memory Writes

Memory policy:
`docs/context/knowledge/project/context-memory-policy.md`

Cleanup and promotion policy:
`docs/context/procedures/context-garden-policy.md`

| Content | Write to | Gate |
| --- | --- | --- |
| Stable facts, owner, module boundary, hardware/protocol fact | `docs/context/knowledge/` | Confirmed and reusable |
| Long-lived architecture boundary or project decision | `docs/context/knowledge/project/` | Affects owner, layer, startup, resource, product state, or route |
| User plan, roadmap, staged execution | `docs/context/plans/` | Needed across sessions or agents |
| Attempt, big error, route choice, board log, verification, anti-repeat evidence | `docs/context/runs/` | Has `repeat-risk`, `high-cost`, `error-signature`, `route-choice`, `owner-architecture`, `evidence`, `handoff`, `plan-decision`, or `project-knowledge` reason |
| Repeatable diagnosis or operating procedure | `docs/context/procedures/` | Useful as a future workflow |
| Current task compression | `docs/context/handoffs/current-task.md` | Needed for handoff, not a stable fact source |

Do not write long-term memory for one-off typo fixes, routine command output, or unconfirmed ideas.

## Framework And Changelog Updates

If a framework change alters startup phases, owner boundaries, call direction, long-lived services, product states, power budget, sleep route, or context routing, update:

- `docs/context/knowledge/project/project-framework.md`
- `docs/context/CHANGELOG.md`

Always update `docs/context/CHANGELOG.md` for context-system changes.

## Validation Levels

| Level | Use when | Command shape |
| --- | --- | --- |
| `light` | Normal lookup and anti-repeat check | `uv run python scripts/context/validate_context.py --level light --q "<task>" --brief` |
| `standard` | Context-document-only changes | `uv run python scripts/context/validate_context.py --level standard --q "<topic>" --brief` |
| `routing` | Index, entrypoint, retrieval baseline, golden routing changes | `uv run python scripts/context/validate_context.py --level routing --q "<topic>" --brief` |
| `full` | `scripts/context`, memory, promotion, archive, validation mechanisms | `uv run python scripts/context/validate_context.py --level full --q "<topic>" --brief` |

Context-only changes do not require firmware build, flash, or monitor.
