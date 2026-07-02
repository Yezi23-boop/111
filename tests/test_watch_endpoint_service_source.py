import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import WATCH_ENDPOINT_SERVICE_HEADER
from tests.main_paths import WATCH_ENDPOINT_SERVICE_SOURCE


class WatchEndpointServiceSourceTests(unittest.TestCase):
    def test_watch_endpoint_facade_exists_and_is_built(self) -> None:
        source = WATCH_ENDPOINT_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = WATCH_ENDPOINT_SERVICE_HEADER.read_text(encoding="utf-8")
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn("watch_endpoint_danger_alert_t", header)
        self.assertIn("watch_endpoint_service_init", header)
        self.assertIn("watch_endpoint_service_post_danger_alert", header)
        self.assertIn("watch_endpoint_service_init", source)
        self.assertIn("watch_endpoint_service_post_danger_alert", source)
        self.assertIn("services/watch_endpoint_service.c", cmake)
        self.assertNotIn("memory_watch_service_post_danger_alert", source)
        self.assertNotIn("memory_watch_service_danger_alert_t", source)

    def test_facade_keeps_danger_detection_away_from_memory_watch_name(self) -> None:
        header = WATCH_ENDPOINT_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("WATCH_ENDPOINT_DANGER_TYPE_MAX_BYTES", header)
        self.assertIn("WATCH_ENDPOINT_DANGER_MESSAGE_MAX_BYTES", header)
        self.assertIn("中性 facade", header)

    def test_service_owns_danger_alert_worker(self) -> None:
        source = WATCH_ENDPOINT_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = WATCH_ENDPOINT_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "freertos/queue.h"', source)
        self.assertIn('#include "freertos/task.h"', source)
        self.assertIn('#include "freertos/idf_additions.h"', source)
        self.assertIn('#include "network_service.h"', source)
        self.assertIn('#include "services/memory_watch_voice_client.h"', source)
        self.assertIn("watch_endpoint_alert_job_t", source)
        self.assertIn("s_alert_worker_queue", source)
        self.assertIn("xQueueCreateStatic(", source)
        self.assertIn("xTaskCreateWithCaps(", source)
        self.assertIn('"watch_alert"', source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertIn("kAlertWorkerStackBytes = 8192U", source)
        self.assertIn("kDangerAlertTimeoutMs = 8000U", source)
        self.assertIn("xQueueReceive(s_alert_worker_queue", source)
        self.assertIn("xQueueSend(s_alert_worker_queue", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertIn("memory_watch_service_copy_endpoint_config", source)
        self.assertIn("memory_watch_voice_client_post_danger_alert", source)
        self.assertIn("watch_endpoint_service_is_safe_alert_text", source)
        self.assertIn("PSRAM worker task", header)

    def test_app_main_starts_watch_endpoint_after_memory_watch_config_owner(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/watch_endpoint_service.h"', source)
        self.assertIn("watch_endpoint_service_init()", source)
        self.assertIn("boot_stage: watch_endpoint_ready", source)
        self.assertLess(
            source.index("memory_watch_service_init()"),
            source.index("watch_endpoint_service_init()"),
        )


if __name__ == "__main__":
    unittest.main()
