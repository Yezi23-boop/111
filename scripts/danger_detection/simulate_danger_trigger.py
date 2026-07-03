#!/usr/bin/env python3
"""Host-side unattended simulation for the danger trigger pipeline.

This script does not talk to hardware, cloud endpoints, Android apps, or an SD
card. It models the safety-critical contracts that can be checked on a PC:

- two consecutive danger inference windows enter Alerting;
- Alerting raises an app alert, dispatches a cloud-alert intent, and starts a
  recorder capture without blocking the alert path;
- recorder capture cuts pre-1s + post-1s PCM by window_end_sample_index;
- invalid early windows and stop/reset are safe and do not write samples.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SAMPLE_RATE_HZ = 16_000
WINDOW_SAMPLES = 16_000
STRIDE_SAMPLES = 4_800
CHUNK_SAMPLES = 1_600
RING_SAMPLES = 48_000
PRE_SAMPLES = 16_000
POST_SAMPLES = 16_000
TOTAL_CAPTURE_SAMPLES = PRE_SAMPLES + POST_SAMPLES
CONFIRM_WINDOWS = 2
DANGER_LABEL = 1
NON_DANGER_LABEL = 0


class AssertionCollector:
    def __init__(self) -> None:
        self.items: list[dict[str, Any]] = []

    def check(self, name: str, expected: Any, actual: Any) -> None:
        self.items.append(
            {
                "name": name,
                "expected": expected,
                "actual": actual,
                "passed": expected == actual,
            }
        )

    @property
    def passed(self) -> bool:
        return all(item["passed"] for item in self.items)


@dataclass
class PendingCapture:
    label_index: int
    confidence: float
    window_end_sample_index: int
    start_sample: int
    generation: int
    pcm_data: list[int]
    post_collected: int


@dataclass
class CaptureRequest:
    label_index: int
    confidence: float
    start_sample: int
    window_end_sample_index: int
    samples: int
    generation: int
    pcm_data: list[int]


@dataclass
class RecorderResponse:
    status: str
    reason: str
    details: dict[str, Any] = field(default_factory=dict)


class SimulatedRecorder:
    """Behavior model of danger_sample_recorder.c without SD or FreeRTOS."""

    def __init__(self) -> None:
        self.ring: list[tuple[int, int]] = []
        self.next_sample_index = 0
        self.generation = 0
        self.pending: PendingCapture | None = None
        self.completed_requests: list[CaptureRequest] = []
        self.events: list[dict[str, Any]] = []

    def _sample_value(self, sample_index: int) -> int:
        return ((sample_index % 65_536) - 32_768)

    def _copy_range(self, start: int, samples: int) -> list[int] | None:
        values = {sample_index: value for sample_index, value in self.ring}
        output: list[int] = []
        for sample_index in range(start, start + samples):
            if sample_index not in values:
                return None
            output.append(values[sample_index])
        return output

    def feed_pcm(self, first_sample_index: int, samples: int = CHUNK_SAMPLES) -> None:
        if samples <= 0:
            return
        if self.ring and first_sample_index != self.next_sample_index:
            self.reset_session(reason="non_continuous_pcm")
        chunk = [
            (sample_index, self._sample_value(sample_index))
            for sample_index in range(first_sample_index, first_sample_index + samples)
        ]
        self.ring.extend(chunk)
        if len(self.ring) > RING_SAMPLES:
            self.ring = self.ring[-RING_SAMPLES:]
        self.next_sample_index = first_sample_index + samples
        self._collect_pending_from_chunk(first_sample_index, samples)

    def feed_until(self, next_sample_index: int, chunk_samples: int = CHUNK_SAMPLES) -> None:
        while self.next_sample_index < next_sample_index:
            remaining = next_sample_index - self.next_sample_index
            self.feed_pcm(self.next_sample_index, min(chunk_samples, remaining))

    def _collect_pending_from_chunk(self, chunk_start: int, samples: int) -> None:
        if self.pending is None:
            return
        chunk_end = chunk_start + samples
        post_start = self.pending.window_end_sample_index
        post_end = post_start + POST_SAMPLES
        copy_start = max(chunk_start, post_start)
        copy_end = min(chunk_end, post_end)
        if copy_start >= copy_end:
            return
        source_values = self._copy_range(copy_start, copy_end - copy_start)
        if source_values is None:
            return
        post_offset = copy_start - post_start
        target_start = PRE_SAMPLES + post_offset
        self.pending.pcm_data[target_start : target_start + len(source_values)] = source_values
        self.pending.post_collected = max(
            self.pending.post_collected,
            post_offset + len(source_values),
        )
        if self.pending.post_collected >= POST_SAMPLES:
            self._complete_pending()

    def _complete_pending(self) -> None:
        if self.pending is None:
            return
        self.completed_requests.append(
            CaptureRequest(
                label_index=self.pending.label_index,
                confidence=self.pending.confidence,
                start_sample=self.pending.start_sample,
                window_end_sample_index=self.pending.window_end_sample_index,
                samples=TOTAL_CAPTURE_SAMPLES,
                generation=self.pending.generation,
                pcm_data=list(self.pending.pcm_data),
            )
        )
        self.events.append(
            {
                "event": "capture_completed",
                "generation": self.pending.generation,
                "start_sample": self.pending.start_sample,
                "window_end_sample_index": self.pending.window_end_sample_index,
                "samples": TOTAL_CAPTURE_SAMPLES,
            }
        )
        self.pending = None

    def capture(self, label_index: int, confidence: float, window_end_sample_index: int) -> RecorderResponse:
        if window_end_sample_index < PRE_SAMPLES:
            return RecorderResponse(
                status="skipped",
                reason="pre_buffer_not_ready",
                details={"window_end_sample_index": window_end_sample_index},
            )
        if self.pending is not None:
            return RecorderResponse(status="rejected", reason="pending_capture_active")

        start_sample = window_end_sample_index - PRE_SAMPLES
        oldest_sample = self.next_sample_index - len(self.ring)
        if start_sample < oldest_sample or window_end_sample_index > self.next_sample_index:
            return RecorderResponse(
                status="skipped",
                reason="ring_range_missing",
                details={
                    "start_sample": start_sample,
                    "window_end_sample_index": window_end_sample_index,
                    "oldest_sample": oldest_sample,
                    "next_sample_index": self.next_sample_index,
                },
            )

        pre_data = self._copy_range(start_sample, PRE_SAMPLES)
        if pre_data is None:
            return RecorderResponse(status="skipped", reason="pre_copy_failed")

        pcm_data = pre_data + [0] * POST_SAMPLES
        initial_post = min(
            max(0, self.next_sample_index - window_end_sample_index),
            POST_SAMPLES,
        )
        if initial_post:
            post_data = self._copy_range(window_end_sample_index, initial_post)
            if post_data is None:
                return RecorderResponse(status="skipped", reason="post_backfill_failed")
            pcm_data[PRE_SAMPLES : PRE_SAMPLES + initial_post] = post_data

        if initial_post >= POST_SAMPLES:
            self.completed_requests.append(
                CaptureRequest(
                    label_index=label_index,
                    confidence=confidence,
                    start_sample=start_sample,
                    window_end_sample_index=window_end_sample_index,
                    samples=TOTAL_CAPTURE_SAMPLES,
                    generation=self.generation,
                    pcm_data=pcm_data,
                )
            )
            status = "queued"
        else:
            self.pending = PendingCapture(
                label_index=label_index,
                confidence=confidence,
                window_end_sample_index=window_end_sample_index,
                start_sample=start_sample,
                generation=self.generation,
                pcm_data=pcm_data,
                post_collected=initial_post,
            )
            status = "pending"

        response = RecorderResponse(
            status=status,
            reason="capture_started",
            details={
                "start_sample": start_sample,
                "window_end_sample_index": window_end_sample_index,
                "initial_post_samples": initial_post,
                "target_samples": TOTAL_CAPTURE_SAMPLES,
                "generation": self.generation,
            },
        )
        self.events.append({"event": "capture_started", **response.details})
        return response

    def reset_session(self, reason: str = "manual_reset") -> None:
        self.generation += 1
        self.ring.clear()
        self.pending = None
        self.next_sample_index = 0
        self.events.append({"event": "session_reset", "reason": reason, "generation": self.generation})


@dataclass
class InferenceResult:
    label_index: int
    danger_probability: float
    window_end_sample_index: int


class SimulatedDangerService:
    def __init__(self, recorder: SimulatedRecorder) -> None:
        self.recorder = recorder
        self.runtime_started = True
        self.overlay_active = False
        self.danger_windows = 0
        self.alert_sequence = 0
        self.app_alerts: list[dict[str, Any]] = []
        self.cloud_alerts: list[dict[str, Any]] = []
        self.capture_responses: list[RecorderResponse] = []
        self.risk_transitions: list[str] = []

    def handle_result(self, result: InferenceResult) -> None:
        old_state = self.risk_transitions[-1] if self.risk_transitions else "MONITORING"
        if not self.runtime_started:
            return
        if result.label_index == DANGER_LABEL:
            self.danger_windows += 1
        else:
            self.danger_windows = 0

        if result.label_index == DANGER_LABEL and self.danger_windows >= CONFIRM_WINDOWS and not self.overlay_active:
            self.overlay_active = True
            self.alert_sequence += 1
            new_state = "ALERTING"
            self.app_alerts.append({"severity": "DANGER", "label": "DANGER"})
            self.cloud_alerts.append(
                {
                    "danger_type": "danger",
                    "danger_prob": result.danger_probability,
                    "alert_sequence": self.alert_sequence,
                }
            )
            self.capture_responses.append(
                self.recorder.capture(
                    result.label_index,
                    result.danger_probability,
                    result.window_end_sample_index,
                )
            )
        elif result.label_index == DANGER_LABEL:
            new_state = "SUSPICIOUS"
        else:
            new_state = "MONITORING"
        if new_state != old_state:
            self.risk_transitions.append(new_state)


def make_scenario_report(
    name: str,
    trigger_parameters: dict[str, Any],
    assertions: AssertionCollector,
    response_details: dict[str, Any],
) -> dict[str, Any]:
    return {
        "name": name,
        "status": "passed" if assertions.passed else "failed",
        "trigger_parameters": trigger_parameters,
        "response_details": response_details,
        "assertions": assertions.items,
    }


def scenario_confirmed_alerting_capture() -> dict[str, Any]:
    recorder = SimulatedRecorder()
    service = SimulatedDangerService(recorder)
    recorder.feed_until(WINDOW_SAMPLES)
    service.handle_result(InferenceResult(NON_DANGER_LABEL, 0.05, WINDOW_SAMPLES))
    recorder.feed_until(WINDOW_SAMPLES + STRIDE_SAMPLES)
    service.handle_result(InferenceResult(DANGER_LABEL, 0.93, WINDOW_SAMPLES + STRIDE_SAMPLES))
    recorder.feed_until(WINDOW_SAMPLES + 2 * STRIDE_SAMPLES)
    service.handle_result(InferenceResult(DANGER_LABEL, 0.96, WINDOW_SAMPLES + 2 * STRIDE_SAMPLES))
    while recorder.pending is not None:
        recorder.feed_pcm(recorder.next_sample_index)

    assertions = AssertionCollector()
    assertions.check("risk_reaches_alerting", True, "ALERTING" in service.risk_transitions)
    assertions.check("single_app_alert", 1, len(service.app_alerts))
    assertions.check("single_cloud_alert", 1, len(service.cloud_alerts))
    assertions.check("single_capture_completed", 1, len(recorder.completed_requests))
    request = recorder.completed_requests[0]
    expected_window_end = WINDOW_SAMPLES + 2 * STRIDE_SAMPLES
    assertions.check("capture_start_sample", expected_window_end - PRE_SAMPLES, request.start_sample)
    assertions.check("capture_total_samples", TOTAL_CAPTURE_SAMPLES, request.samples)
    assertions.check("pre_first_sample_value", recorder._sample_value(request.start_sample), request.pcm_data[0])
    assertions.check(
        "post_first_sample_value",
        recorder._sample_value(expected_window_end),
        request.pcm_data[PRE_SAMPLES],
    )

    return make_scenario_report(
        "confirmed_alerting_capture",
        {
            "danger_windows_required": CONFIRM_WINDOWS,
            "danger_probabilities": [0.93, 0.96],
            "window_end_sample_index": expected_window_end,
        },
        assertions,
        {
            "risk_transitions": service.risk_transitions,
            "app_alerts": service.app_alerts,
            "cloud_alerts": service.cloud_alerts,
            "capture_responses": [response.__dict__ for response in service.capture_responses],
            "completed_requests": [
                {
                    "start_sample": request.start_sample,
                    "window_end_sample_index": request.window_end_sample_index,
                    "samples": request.samples,
                    "generation": request.generation,
                }
            ],
        },
    )


def scenario_post_backfill() -> dict[str, Any]:
    recorder = SimulatedRecorder()
    window_end = 32_000
    existing_post = 3_200
    recorder.feed_until(window_end + existing_post)
    response = recorder.capture(DANGER_LABEL, 0.91, window_end)
    while recorder.pending is not None:
        recorder.feed_pcm(recorder.next_sample_index)

    request = recorder.completed_requests[0]
    assertions = AssertionCollector()
    assertions.check("capture_initial_status", "pending", response.status)
    assertions.check("initial_post_samples", existing_post, response.details["initial_post_samples"])
    assertions.check("capture_completed", 1, len(recorder.completed_requests))
    assertions.check(
        "backfilled_post_first_value",
        recorder._sample_value(window_end),
        request.pcm_data[PRE_SAMPLES],
    )
    assertions.check(
        "backfilled_post_last_value",
        recorder._sample_value(window_end + existing_post - 1),
        request.pcm_data[PRE_SAMPLES + existing_post - 1],
    )

    return make_scenario_report(
        "post_backfill_partial_chunk",
        {
            "window_end_sample_index": window_end,
            "existing_post_samples_before_capture": existing_post,
        },
        assertions,
        {
            "capture_response": response.__dict__,
            "completed_request": {
                "start_sample": request.start_sample,
                "window_end_sample_index": request.window_end_sample_index,
                "samples": request.samples,
            },
        },
    )


def scenario_early_window_is_safe() -> dict[str, Any]:
    recorder = SimulatedRecorder()
    recorder.feed_until(8_000)
    response = recorder.capture(DANGER_LABEL, 0.99, 8_000)

    assertions = AssertionCollector()
    assertions.check("capture_status", "skipped", response.status)
    assertions.check("skip_reason", "pre_buffer_not_ready", response.reason)
    assertions.check("no_completed_request", 0, len(recorder.completed_requests))
    assertions.check("no_pending_capture", None, recorder.pending)

    return make_scenario_report(
        "early_window_safe_skip",
        {"window_end_sample_index": 8_000, "pre_samples_required": PRE_SAMPLES},
        assertions,
        {"capture_response": response.__dict__},
    )


def scenario_reset_cancels_pending() -> dict[str, Any]:
    recorder = SimulatedRecorder()
    window_end = 32_000
    recorder.feed_until(window_end)
    response = recorder.capture(DANGER_LABEL, 0.94, window_end)
    recorder.reset_session(reason="danger_service_stop")
    recorder.feed_until(POST_SAMPLES)

    assertions = AssertionCollector()
    assertions.check("capture_initial_status", "pending", response.status)
    assertions.check("generation_after_reset", 1, recorder.generation)
    assertions.check("pending_cancelled", None, recorder.pending)
    assertions.check("no_completed_request_after_reset", 0, len(recorder.completed_requests))
    assertions.check("ring_restarted_from_zero", POST_SAMPLES, recorder.next_sample_index)

    return make_scenario_report(
        "service_stop_resets_pending_capture",
        {"window_end_sample_index": window_end, "reset_reason": "danger_service_stop"},
        assertions,
        {
            "capture_response": response.__dict__,
            "recorder_events": recorder.events,
            "generation": recorder.generation,
        },
    )


SCENARIOS = {
    "confirmed_alerting_capture": scenario_confirmed_alerting_capture,
    "post_backfill_partial_chunk": scenario_post_backfill,
    "early_window_safe_skip": scenario_early_window_is_safe,
    "service_stop_resets_pending_capture": scenario_reset_cancels_pending,
}


def build_report(selected_scenarios: list[str]) -> dict[str, Any]:
    scenarios = [SCENARIOS[name]() for name in selected_scenarios]
    passed = sum(1 for scenario in scenarios if scenario["status"] == "passed")
    failed = len(scenarios) - passed
    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "safe_mode": {
            "host_only": True,
            "hardware_access": False,
            "network_access": False,
            "sd_card_access": False,
            "firmware_mutation": False,
        },
        "constants": {
            "sample_rate_hz": SAMPLE_RATE_HZ,
            "window_samples": WINDOW_SAMPLES,
            "stride_samples": STRIDE_SAMPLES,
            "chunk_samples": CHUNK_SAMPLES,
            "ring_samples": RING_SAMPLES,
            "pre_samples": PRE_SAMPLES,
            "post_samples": POST_SAMPLES,
            "confirm_windows": CONFIRM_WINDOWS,
        },
        "summary": {
            "status": "passed" if failed == 0 else "failed",
            "total": len(scenarios),
            "passed": passed,
            "failed": failed,
        },
        "scenarios": scenarios,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a safe host-side simulation of the danger trigger pipeline.",
    )
    parser.add_argument(
        "--scenario",
        action="append",
        choices=sorted(SCENARIOS),
        help="Scenario to run. Repeat to run multiple. Defaults to all scenarios.",
    )
    parser.add_argument(
        "--report-path",
        type=Path,
        default=Path("artifacts/danger_trigger_sim/report.json"),
        help="JSON report output path.",
    )
    parser.add_argument(
        "--print-report",
        action="store_true",
        help="Print the full JSON report to stdout.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    selected = args.scenario if args.scenario else sorted(SCENARIOS)
    report = build_report(selected)

    args.report_path.parent.mkdir(parents=True, exist_ok=True)
    args.report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    if args.print_report:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        summary = report["summary"]
        print(
            "danger trigger simulation: "
            f"{summary['status']} ({summary['passed']}/{summary['total']} passed), "
            f"report={args.report_path}"
        )
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
