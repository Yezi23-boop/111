import argparse
import csv
import pathlib
import re
from typing import Dict, Iterable, List


WOM_RE = re.compile(
    r"wom_event: event_id=(?P<event_id>\d+) source=(?P<source>\w+) "
    r"gpio=(?P<gpio>-?\d+) level=(?P<level>-?\d+) "
    r"statusint=0x(?P<statusint>[0-9a-fA-F]+) int1_mirror=(?P<int1_mirror>\d+) "
    r"status1=0x(?P<status1>[0-9a-fA-F]+) "
    r"raw_accel=\((?P<raw_x>-?\d+),(?P<raw_y>-?\d+),(?P<raw_z>-?\d+)\)"
    r"(?: raw_gyro=\((?P<raw_gx>-?\d+),(?P<raw_gy>-?\d+),(?P<raw_gz>-?\d+)\))?"
)

AE_FRAME_RE = re.compile(
    r"imu_csv: source=ae_dq,label=(?P<log_label>[^,]+),event_id=(?P<event_id>\d+),"
    r"trigger=(?P<trigger>[^,]+),index=(?P<index>\d+),ready=(?P<ready>\d+),"
    r"clipped=(?P<clipped>\d+),dqw=(?P<dqw>-?\d+),dqx=(?P<dqx>-?\d+),"
    r"dqy=(?P<dqy>-?\d+),dqz=(?P<dqz>-?\d+),frame_mdeg=(?P<frame_mdeg>-?\d+),"
    r"total_mdeg=(?P<total_mdeg>-?\d+),(?:status0=0x(?P<status0>[0-9a-fA-F]+),)?"
    r"ae1=0x(?P<ae1>[0-9a-fA-F]+),"
    r"ae2=0x(?P<ae2>[0-9a-fA-F]+)"
)

FINAL_POSE_RE = re.compile(
    r"final_pose: event_id=(?P<event_id>\d+) source=(?P<source>\w+) "
    r"pass=(?P<final_pose_pass>\d+) norm_pass=(?P<norm_pass>\d+) "
    r"stable_pass=(?P<stable_pass>\d+) face_pass=(?P<face_pass>\d+) "
    r"accel=\((?P<final_x>-?\d+),(?P<final_y>-?\d+),(?P<final_z>-?\d+)\) "
    r"norm_mg=(?P<final_norm_mg>-?\d+) stability_mg=(?P<final_stability_mg>-?\d+) "
    r"face_axis=(?P<face_axis>\S+) face_threshold_raw=(?P<face_threshold_raw>-?\d+)"
)

RAISE_RESULT_RE = re.compile(
    r"raise_result: event_id=(?P<event_id>\d+) source=(?P<source>\w+) "
    r"raise_detected=(?P<raise_detected>\d+) motion_pass=(?P<motion_pass>\d+) "
    r"final_pose_pass=(?P<final_pose_pass>\d+) reject_reason=(?P<reject_reason>\w+) "
    r"valid_dq=(?P<valid_dq>\d+) clipped=(?P<clipped>\d+) "
    r"total_mdeg=(?P<total_mdeg>-?\d+) max_frame_mdeg=(?P<max_frame_mdeg>-?\d+) "
    r"final_norm_mg=(?P<final_norm_mg>-?\d+) "
    r"final_stability_mg=(?P<final_stability_mg>-?\d+) "
    r"final_accel=\((?P<final_x>-?\d+),(?P<final_y>-?\d+),(?P<final_z>-?\d+)\)"
)


EVENT_FIELDS = [
    "label",
    "event_id",
    "source",
    "raise_detected",
    "reject_reason",
    "motion_pass",
    "final_pose_pass",
    "valid_dq",
    "clipped",
    "total_mdeg",
    "max_frame_mdeg",
    "final_norm_mg",
    "final_stability_mg",
    "raw_x",
    "raw_y",
    "raw_z",
    "raw_gx",
    "raw_gy",
    "raw_gz",
    "final_x",
    "final_y",
    "final_z",
    "norm_pass",
    "stable_pass",
    "face_pass",
    "face_axis",
    "face_threshold_raw",
    "statusint",
    "status1",
]

FRAME_FIELDS = [
    "label",
    "event_id",
    "trigger",
    "index",
    "ready",
    "clipped",
    "dqw",
    "dqx",
    "dqy",
    "dqz",
    "frame_mdeg",
    "total_mdeg",
    "status0",
    "ae1",
    "ae2",
]


def _label_for(log_label: str, cli_label: str) -> str:
    if cli_label != "unknown":
        return cli_label
    return log_label or cli_label


def parse_log(log_path: pathlib.Path, label: str) -> tuple[List[Dict[str, str]], List[Dict[str, str]]]:
    events: Dict[str, Dict[str, str]] = {}
    frames: List[Dict[str, str]] = []

    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if match := WOM_RE.search(line):
            row = match.groupdict()
            event = events.setdefault(row["event_id"], {"event_id": row["event_id"]})
            event.update(
                {
                    "label": label,
                    "source": row["source"],
                    "raw_x": row["raw_x"],
                    "raw_y": row["raw_y"],
                    "raw_z": row["raw_z"],
                    "raw_gx": row.get("raw_gx") or "",
                    "raw_gy": row.get("raw_gy") or "",
                    "raw_gz": row.get("raw_gz") or "",
                    "statusint": row["statusint"],
                    "status1": row["status1"],
                }
            )
            continue

        if match := AE_FRAME_RE.search(line):
            row = match.groupdict()
            row["label"] = _label_for(row.pop("log_label"), label)
            row["status0"] = row.get("status0") or ""
            frames.append(row)
            event = events.setdefault(row["event_id"], {"event_id": row["event_id"]})
            event.setdefault("label", row["label"])
            event.setdefault("source", row["trigger"])
            continue

        if match := FINAL_POSE_RE.search(line):
            row = match.groupdict()
            event = events.setdefault(row["event_id"], {"event_id": row["event_id"]})
            event.update(row)
            event.setdefault("label", label)
            continue

        if match := RAISE_RESULT_RE.search(line):
            row = match.groupdict()
            event = events.setdefault(row["event_id"], {"event_id": row["event_id"]})
            event.update(row)
            event.setdefault("label", label)

    event_rows = [events[key] for key in sorted(events, key=lambda value: int(value))]
    return event_rows, frames


def write_csv(path: pathlib.Path, fieldnames: Iterable[str], rows: List[Dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=list(fieldnames), extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract IMU raise-wrist event and dQ frame samples from board logs."
    )
    parser.add_argument("log_path", type=pathlib.Path, help="Input board log path.")
    parser.add_argument("--label", default="unknown", help="Label for this capture, e.g. raise or negative.")
    parser.add_argument("--out", required=True, type=pathlib.Path, help="Output event summary CSV path.")
    parser.add_argument("--frames-out", type=pathlib.Path, help="Optional output dQ frame CSV path.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    events, frames = parse_log(args.log_path, args.label)
    write_csv(args.out, EVENT_FIELDS, events)
    if args.frames_out is not None:
        write_csv(args.frames_out, FRAME_FIELDS, frames)
    print(f"IMU_RAISE_EVENTS={len(events)}")
    print(f"IMU_RAISE_FRAMES={len(frames)}")
    print(f"IMU_RAISE_EVENTS_CSV={args.out}")
    if args.frames_out is not None:
        print(f"IMU_RAISE_FRAMES_CSV={args.frames_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
