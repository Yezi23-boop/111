import base64
import hashlib
import hmac
import importlib.util
import pathlib
import sys
import tempfile
import unittest
from urllib.parse import quote


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE_PATH = REPO_ROOT / "tools" / "ota_host" / "onenet_probe.py"
SPEC = importlib.util.spec_from_file_location("onenet_probe", PROBE_PATH)
assert SPEC is not None and SPEC.loader is not None
onenet_probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = onenet_probe
SPEC.loader.exec_module(onenet_probe)


class OneNetProbeTests(unittest.TestCase):
    def test_build_authorization_matches_official_formula(self) -> None:
        access_key = base64.b64encode(b"test-product-key").decode("ascii")
        resource = "products/w23kT21Z3x"
        token = onenet_probe.build_authorization(
            access_key, resource, now=1_700_000_000, ttl_seconds=3600, method="sha256"
        )
        expire_at = "1700003600"
        signature_source = f"{expire_at}\nsha256\n{resource}\n2022-05-01"
        signature = base64.b64encode(
            hmac.new(b"test-product-key", signature_source.encode(), hashlib.sha256).digest()
        ).decode("ascii")
        expected = (
            "version=2022-05-01&res=products%2Fw23kT21Z3x&et=1700003600"
            f"&method=sha256&sign={quote(signature, safe='')}"
        )
        self.assertEqual(expected, token)

    def test_version_payload_uses_s_version_for_mcu_software(self) -> None:
        self.assertEqual(
            {"s_version": "1.0.6", "f_version": "1.0.0"},
            onenet_probe.build_version_payload("1.0.6"),
        )

    def test_check_response_parses_sota_task(self) -> None:
        result = onenet_probe.parse_check_response({
            "code": 0,
            "msg": "succ",
            "data": {
                "target": "1.0.7",
                "tid": 123,
                "size": 11229248,
                "md5": "CD580293829DA0E5A94265F5CD7BF286",
                "status": 1,
                "type": 1,
            },
        })
        self.assertEqual("1.0.7", result.target)
        self.assertEqual(123, result.task_id)
        self.assertEqual("cd580293829da0e5a94265f5cd7bf286", result.md5)
        self.assertEqual(1, result.package_type)

    def test_no_task_response_does_not_create_download_metadata(self) -> None:
        result = onenet_probe.parse_check_response({"code": 12012, "msg": "not exist"})
        self.assertEqual(12012, result.code)
        self.assertIsNone(result.task_id)
        self.assertIsNone(result.md5)

    def test_check_response_rejects_non_md5_payload(self) -> None:
        with self.assertRaisesRegex(ValueError, "invalid md5"):
            onenet_probe.parse_check_response({
                "code": 0,
                "msg": "succ",
                "data": {
                    "target": "1.0.7",
                    "tid": 123,
                    "size": 1,
                    "md5": "not-md5",
                    "status": 1,
                    "type": 1,
                },
            })

    def test_load_access_key_file_reads_label_without_echoing_value(self) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as secret_file:
            secret_file.write("AccessKey:c2VjcmV0\n")
            path = secret_file.name
        try:
            self.assertEqual("c2VjcmV0", onenet_probe.load_access_key_file(path))
        finally:
            pathlib.Path(path).unlink()


if __name__ == "__main__":
    unittest.main()
