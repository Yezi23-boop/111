import csv
import importlib.util
import tempfile
import unittest

from tests.main_paths import IMU_RAISE_SAMPLE_SCRIPT


class ExtractImuRaiseSamplesScriptTests(unittest.TestCase):
    def test_script_extracts_event_summary_and_physical_frames(self) -> None:
        spec = importlib.util.spec_from_file_location(
            "extract_imu_raise_samples", IMU_RAISE_SAMPLE_SCRIPT
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        log_text = "\n".join(
            [
                "I (1000) imu_service: wom_event: event_id=1 source=poll gpio=21 level=1 statusint=0x02 int1_mirror=1 status1=0x04 accel_mg=(1,-1,-977) action=log_only",
                "I (1040) imu_service: imu_csv: source=physical_6axis,label=unknown,event_id=1,trigger=poll,index=0,ax_mg=1,ay_mg=-1,az_mg=-977,gx_mdps=781,gy_mdps=-1563,gz_mdps=2344,nx=1,ny=-1,nz=-1000,motion_detected=1,motion_reason=RAISE_DETECTED,roll_delta_deg=-45",
                "I (1080) imu_service: final_pose: event_id=1 source=poll pass=1 norm_pass=1 stable_pass=1 face_pass=1 accel_mg=(1,-1,-982) gyro_mdps=(781,-1563,2344) norm_mg=1002 stability_mg=25 face_axis=-Z face_threshold_mg=-397 action=log_only",
                "I (1120) imu_service: raise_result: event_id=1 source=poll raise_detected=1 motion_pass=1 final_pose_pass=1 reject_reason=PASS motion_samples=16 motion_reason=RAISE_DETECTED roll_delta_deg=-45 final_norm_mg=1002 final_stability_mg=25 final_accel_mg=(1,-1,-982) final_gyro_mdps=(781,-1563,2344) action=log_only",
            ]
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = module.pathlib.Path(temp_dir) / "imu.log"
            events_csv = module.pathlib.Path(temp_dir) / "events.csv"
            frames_csv = module.pathlib.Path(temp_dir) / "frames.csv"
            log_path.write_text(log_text, encoding="utf-8")

            events, frames = module.parse_log(log_path, "raise")
            module.write_csv(events_csv, module.EVENT_FIELDS, events)
            module.write_csv(frames_csv, module.FRAME_FIELDS, frames)

            with events_csv.open("r", encoding="utf-8", newline="") as csv_file:
                event_rows = list(csv.DictReader(csv_file))
            with frames_csv.open("r", encoding="utf-8", newline="") as csv_file:
                frame_rows = list(csv.DictReader(csv_file))

        self.assertEqual(len(event_rows), 1)
        self.assertEqual(event_rows[0]["label"], "raise")
        self.assertEqual(event_rows[0]["event_id"], "1")
        self.assertEqual(event_rows[0]["raise_detected"], "1")
        self.assertEqual(event_rows[0]["reject_reason"], "PASS")
        self.assertEqual(event_rows[0]["motion_samples"], "16")
        self.assertEqual(event_rows[0]["wom_az_mg"], "-977")
        self.assertEqual(event_rows[0]["final_az_mg"], "-982")
        self.assertEqual(event_rows[0]["final_gz_mdps"], "2344")
        self.assertEqual(len(frame_rows), 1)
        self.assertEqual(frame_rows[0]["trigger"], "poll")
        self.assertEqual(frame_rows[0]["az_mg"], "-977")
        self.assertEqual(frame_rows[0]["gz_mdps"], "2344")
        self.assertEqual(frame_rows[0]["motion_reason"], "RAISE_DETECTED")


if __name__ == "__main__":
    unittest.main()
