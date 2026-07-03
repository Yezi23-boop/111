import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

from tests.main_paths import REPO_ROOT


SCRIPT_PATH = REPO_ROOT / "scripts" / "danger_detection" / "simulate_danger_trigger.py"


class DangerTriggerSimulationScriptTests(unittest.TestCase):
    def test_script_runs_unattended_and_writes_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            report_path = pathlib.Path(temp_dir) / "danger-trigger-report.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--report-path",
                    str(report_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            if result.returncode != 0:
                self.fail(
                    f"script failed with {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
                )

            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual("passed", report["summary"]["status"])
        self.assertEqual(4, report["summary"]["total"])
        self.assertTrue(report["safe_mode"]["host_only"])
        self.assertFalse(report["safe_mode"]["hardware_access"])
        self.assertFalse(report["safe_mode"]["network_access"])
        self.assertFalse(report["safe_mode"]["sd_card_access"])

        scenario_names = {scenario["name"] for scenario in report["scenarios"]}
        self.assertEqual(
            {
                "confirmed_alerting_capture",
                "post_backfill_partial_chunk",
                "early_window_safe_skip",
                "service_stop_resets_pending_capture",
            },
            scenario_names,
        )

    def test_report_contains_trigger_parameters_and_response_details(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            report_path = pathlib.Path(temp_dir) / "danger-trigger-one.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--scenario",
                    "confirmed_alerting_capture",
                    "--report-path",
                    str(report_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(0, result.returncode, result.stderr)
            report = json.loads(report_path.read_text(encoding="utf-8"))

        scenario = report["scenarios"][0]
        self.assertEqual("confirmed_alerting_capture", scenario["name"])
        self.assertEqual("passed", scenario["status"])
        self.assertIn("trigger_parameters", scenario)
        self.assertIn("window_end_sample_index", scenario["trigger_parameters"])
        self.assertIn("response_details", scenario)
        self.assertIn("app_alerts", scenario["response_details"])
        self.assertIn("cloud_alerts", scenario["response_details"])
        self.assertIn("completed_requests", scenario["response_details"])
        self.assertTrue(all(item["passed"] for item in scenario["assertions"]))


if __name__ == "__main__":
    unittest.main()
