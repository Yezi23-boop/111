#!/usr/bin/env python3
"""Shared stdio setup for Windows-friendly context CLI output."""

from __future__ import annotations

import sys


def configure_utf8_stdio() -> None:
    """Prefer UTF-8 when the host console supports stream reconfiguration."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is None:
            continue
        try:
            reconfigure(encoding="utf-8", errors="replace")
        except (OSError, ValueError):
            # Some redirected streams reject reconfiguration; keep the CLI usable.
            continue
