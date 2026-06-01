# Context

This repository uses `docs/context/` as its canonical project context system.
This file is a bridge glossary for engineering skills that expect a root `CONTEXT.md`.

Start with:

- `docs/context/INDEX.agent.md`
- `docs/context/knowledge/project/project-profile.md`

Stable architecture boundaries and project decisions live in:

- `docs/context/knowledge/project/`

## Language

**STANDBY**:
The watch's long-idle runtime standby mode. It keeps the ESP32 application running, but treats the screen, refresh cadence, networking, and pausable background work as power-budget consumers that should degrade until user activity or a high-priority alert wakes the UI.
_Avoid_: IDLE_DIM as a separate user-facing power state, Light Sleep, Deep Sleep.

## Flagged Ambiguities

**IDLE_DIM vs STANDBY**:
Earlier notes used `IDLE_DIM` for short-idle screen dimming and `STANDBY` for deeper long-idle runtime saving. The resolved product language keeps only `STANDBY` as the idle power state; any short-idle dimming code is an implementation detail to be migrated or folded into `STANDBY`, not a separate product state.

## Example Dialogue

Developer: "The user stopped touching the watch. Should we enter IDLE_DIM first?"

Domain expert: "No. Treat idle power saving as STANDBY. STANDBY can choose how aggressive the UI and background services become, but we do not expose a separate IDLE_DIM state."
