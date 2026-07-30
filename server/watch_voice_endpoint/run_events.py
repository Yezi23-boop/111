"""Hermes run-event normalization for the watch protocol.

Hermes emits structured lifecycle events over SSE.  This module deliberately
keeps the watch contract smaller than the upstream event payload: the watch
only receives a stable phase and never receives tool names or token deltas.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class WatchRunEvent:
    """One normalized Hermes event relevant to the watch client."""

    name: str
    phase: str | None = None
    terminal: bool = False


class SseLineParser:
    """Parse the data-only SSE stream used by Hermes API Server."""

    def __init__(self) -> None:
        self._data: list[str] = []

    def feed_line(self, line: str) -> WatchRunEvent | None:
        if line == "":
            if not self._data:
                return None
            payload = "\n".join(self._data)
            self._data.clear()
            try:
                decoded = json.loads(payload)
            except (TypeError, json.JSONDecodeError):
                return None
            return normalize_run_event(decoded)

        if line.startswith(":"):
            return None
        if line.startswith("data:"):
            self._data.append(line[5:].lstrip())
        return None


def normalize_run_event(payload: Any) -> WatchRunEvent | None:
    """Map Hermes lifecycle event names to the watch's four phases."""

    if not isinstance(payload, dict):
        return None
    name = str(
        payload.get("event")
        or payload.get("type")
        or payload.get("name")
        or ""
    ).strip().lower()
    if not name:
        return None

    if name in {"message.complete", "message.completed", "run.completed", "run.complete"}:
        return WatchRunEvent(name=name, terminal=True)
    if name in {"run.failed", "run.error", "run.cancelled", "run.canceled"}:
        return WatchRunEvent(name=name, terminal=True)
    if name in {"message.delta", "message.output.delta", "response.output_text.delta"}:
        return WatchRunEvent(name=name, phase="composing")
    if name in {"tool.start", "tool.progress"}:
        tool_text = _tool_text(payload)
        if any(word in tool_text for word in ("search", "browser", "web", "retrieve")):
            return WatchRunEvent(name=name, phase="searching")
        return WatchRunEvent(name=name, phase="executing")
    return None


def _tool_text(payload: dict[str, Any]) -> str:
    values = [payload.get("tool"), payload.get("tool_name"), payload.get("name")]
    tool = payload.get("tool")
    if isinstance(tool, dict):
        values.extend([tool.get("name"), tool.get("id")])
    return " ".join(str(value or "").lower() for value in values)
