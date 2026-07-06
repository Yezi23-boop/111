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

**表情表盘**:
The animated emoji watchface shown first when the screen wakes/turns on or the UI boots. It is the first visual entry surface, not the universal back-stack root; subpage back navigation returns to the existing main screen.
_Avoid_: Treating it as a lock screen, a Hermes-only page, or the default target for every back action.

**模型输入坐标系**:
The fixed coordinate frame consumed by an IMU ML model after board-specific sensor mounting has been mapped away. For fall detection this means the model receives `accX/accY/accZ`, not QMI8658C register `accel_x/accel_y/accel_z`.
For six-axis IMU data, acceleration channels use `accX/accY/accZ` and gyroscope channels use `gyroX/gyroY/gyroZ`.
_Avoid_: Feeding raw sensor XYZ directly to the fall model when the board mounting direction has not been accounted for.

## Flagged Ambiguities

**IDLE_DIM vs STANDBY**:
Earlier notes used `IDLE_DIM` for short-idle screen dimming and `STANDBY` for deeper long-idle runtime saving. The resolved product language keeps only `STANDBY` as the idle power state; any short-idle dimming code is an implementation detail to be migrated or folded into `STANDBY`, not a separate product state.

## Example Dialogue

Developer: "The user stopped touching the watch. Should we enter IDLE_DIM first?"

Domain expert: "No. Treat idle power saving as STANDBY. STANDBY can choose how aggressive the UI and background services become, but we do not expose a separate IDLE_DIM state."
