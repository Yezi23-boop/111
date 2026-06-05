import csv
import importlib.util
import tempfile
import unittest

from tests.main_paths import IMU_RAISE_SAMPLE_SCRIPT


class ExtractImuRaiseSamplesScriptTests(unittest.TestCase):
    def test_script_extracts_event_summary_and_dq_frames(self) -> None:
        spec = importlib.util.spec_from_file_location(
            "extract_imu_raise_samples", IMU_RAISE_SAMPLE_SCRIPT
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        log_text = "\n".join(
            [
                "I (1000) imu_service: wom_event: event_id=1 source=poll gpio=21 level=1 statusint=0x02 int1_mirror=1 status1=0x04 raw_accel=(10,-20,-16000) raw_gyro=(100,-200,300) action=log_only",
                "I (1040) imu_service: imu_csv: source=ae_dq,label=unknown,event_id=1,trigger=poll,index=0,ready=1,clipped=0,dqw=16380,dqx=10,dqy=20,dqz=30,frame_mdeg=120,total_mdeg=120,status0=0x0b,ae1=0x08,ae2=0x00",
                "I (1080) imu_service: final_pose: event_id=1 source=poll pass=1 norm_pass=1 stable_pass=1 face_pass=1 accel=(11,-21,-16100) norm_mg=1002 stability_mg=25 face_axis=-Z face_threshold_raw=-6500 action=log_only",
                "I (1120) imu_service: raise_result: event_id=1 source=poll raise_detected=1 motion_pass=1 final_pose_pass=1 reject_reason=PASS valid_dq=12 clipped=0 total_mdeg=45000 max_frame_mdeg=9000 final_norm_mg=1002 final_stability_mg=25 final_accel=(11,-21,-16100) action=log_only",
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
        self.assertEqual(event_rows[0]["total_mdeg"], "45000")
        self.assertEqual(event_rows[0]["raw_gz"], "300")
        self.assertEqual(event_rows[0]["final_z"], "-16100")
        self.assertEqual(len(frame_rows), 1)
        self.assertEqual(frame_rows[0]["trigger"], "poll")
        self.assertEqual(frame_rows[0]["status0"], "0b")
        self.assertEqual(frame_rows[0]["frame_mdeg"], "120")


if __name__ == "__main__":
    unittest.main()
