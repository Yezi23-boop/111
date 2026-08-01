import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
OTA_HOST_PATH = REPO_ROOT / "tools" / "ota_host" / "ota_host.py"
SPEC = importlib.util.spec_from_file_location("ota_host", OTA_HOST_PATH)
assert SPEC is not None and SPEC.loader is not None
ota_host = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ota_host)


class OtaHostTests(unittest.TestCase):
    def test_prepare_release_copies_image_and_writes_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            source = root / "input.bin"
            source.write_bytes(b"firmware-data")
            release_dir = root / "release"

            manifest = ota_host.prepare_release(
                source, "1.2.3", "https://192.168.1.20:8443", release_dir, "111.bin"
            )

            self.assertEqual("1.2.3", manifest["version"])
            self.assertEqual("https://192.168.1.20:8443/111.bin", manifest["url"])
            self.assertEqual(len(b"firmware-data"), manifest["size"])
            self.assertEqual(hashlib.sha256(b"firmware-data").hexdigest(), manifest["sha256"])
            self.assertEqual(b"firmware-data", (release_dir / "111.bin").read_bytes())
            self.assertEqual(manifest, json.loads((release_dir / "manifest.json").read_text()))

    def test_prepare_rejects_non_https_base_url(self) -> None:
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            ota_host.build_manifest("1.2.3", "111.bin", 1, "a" * 64, "http://example.test")

    def test_bad_sha_fault_changes_only_manifest_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            release_dir = pathlib.Path(temp_dir)
            manifest = {"version": "1.0.0", "sha256": "a" * 64}
            (release_dir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

            faulted = json.loads(ota_host.faulted_manifest_bytes(release_dir, "bad-sha"))

            self.assertEqual("1.0.0", faulted["version"])
            self.assertEqual("0" * 64, faulted["sha256"])

    def test_image_faults_have_distinct_length_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            image = pathlib.Path(temp_dir) / "111.bin"
            image.write_bytes(b"0123456789")

            truncated, truncated_length, truncated_close = ota_host.image_response(image, "truncated")
            disconnected, disconnected_length, disconnected_close = ota_host.image_response(image, "disconnect")

            self.assertEqual(5, len(truncated))
            self.assertEqual(5, truncated_length)
            self.assertFalse(truncated_close)
            self.assertEqual(5, len(disconnected))
            self.assertEqual(10, disconnected_length)
            self.assertTrue(disconnected_close)

    def test_publish_remote_streams_bin_to_admin_api(self) -> None:
        class FakeResponse:
            status = 200

            @staticmethod
            def read() -> bytes:
                return b'{"version":"0.2.0","size":8}'

        class FakeConnection:
            instance = None

            def __init__(self, host, port, timeout) -> None:
                self.host = host
                self.port = port
                self.timeout = timeout
                self.headers = {}
                self.sent = bytearray()
                FakeConnection.instance = self

            def putrequest(self, method, path) -> None:
                self.method = method
                self.path = path

            def putheader(self, name, value) -> None:
                self.headers[name] = value

            def endheaders(self) -> None:
                pass

            def send(self, data) -> None:
                self.sent.extend(data)

            @staticmethod
            def getresponse() -> FakeResponse:
                return FakeResponse()

            def close(self) -> None:
                pass

        with tempfile.TemporaryDirectory() as temp_dir:
            image = pathlib.Path(temp_dir) / "111.bin"
            image.write_bytes(b"firmware")
            with mock.patch.object(ota_host.http.client, "HTTPSConnection", FakeConnection):
                manifest = ota_host.publish_remote(
                    image,
                    "0.2.0",
                    "stable",
                    "https://watch.example/v1/watch/ota/admin/releases",
                    "admin-token",
                )

        connection = FakeConnection.instance
        self.assertEqual({"version": "0.2.0", "size": 8}, manifest)
        self.assertEqual("watch.example", connection.host)
        self.assertEqual(443, connection.port)
        self.assertEqual("POST", connection.method)
        self.assertEqual("admin-token", connection.headers["X-OTA-Admin-Token"])
        self.assertIn(b"firmware", connection.sent)


if __name__ == "__main__":
    unittest.main()
