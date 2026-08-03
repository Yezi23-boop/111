import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import POWER_POLICY_HEADER
from tests.main_paths import POWER_POLICY_SOURCE
from tests.main_paths import REPO_ROOT
from tests.main_paths import SYSTEM_TIME_SERVICE_SOURCE


AUDIO_CODEC_HEADER = (
    REPO_ROOT / "components" / "audio_codec" / "include" / "audio_codec.h"
)
AUDIO_CODEC_SOURCE = REPO_ROOT / "components" / "audio_codec" / "audio_codec.c"
POWER_POLICY_AUDIO_BRIDGE = (
    REPO_ROOT / "main" / "services" / "power" / "power_policy_audio_bridge.c"
)


class PowerPolicyProviderSourceTests(unittest.TestCase):
    def test_participant_registry_config_contract(self) -> None:
        header = POWER_POLICY_HEADER.read_text(encoding="utf-8")

        self.assertIn("power_policy_provider_id_t", header)
        self.assertIn("POWER_POLICY_PROVIDER_SAFETY_MONITOR", header)
        self.assertIn("POWER_POLICY_PROVIDER_AUDIO", header)
        self.assertIn("POWER_POLICY_PROVIDER_RUNTIME_COORDINATOR", header)
        self.assertIn("POWER_POLICY_PARTICIPANT_FACTS_ONLY", header)
        self.assertIn("POWER_POLICY_PARTICIPANT_CONSUMER_ONLY", header)
        self.assertIn("POWER_POLICY_PARTICIPANT_FACTS_AND_CONSUMER", header)
        self.assertIn("power_policy_provider_facts_t", header)
        self.assertIn("bool running", header)
        self.assertIn("bool must_keep_alive", header)
        self.assertIn("bool can_defer_work", header)
        self.assertIn("sleep_blockers", header)
        self.assertIn("last_error", header)
        self.assertIn("power_policy_get_facts_cb_t", header)
        self.assertIn("power_policy_budget_changed_cb_t", header)
        self.assertIn("power_policy_register_participant", header)
        self.assertIn("power_policy_budget_version", header)
        self.assertIn("power_policy_budget_changed_notify", header)
        self.assertIn("power_policy_audio_bridge_register", header)

    def test_registry_fixed_capacity_and_idempotent_register(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("POWER_POLICY_MAX_PARTICIPANTS (8U)", source)
        self.assertIn("power_policy_add_participant", source)
        self.assertIn("ESP_ERR_INVALID_STATE", source)
        self.assertIn("register participant table full", source)
        self.assertIn("register participant conflict", source)
        self.assertIn("register participant idempotent", source)
        # 注册只发生在 policy task 启动前，运行期不允许卸载或追加。
        self.assertIn("register participant after policy task started", source)
        self.assertIn("config->get_facts == NULL && config->on_budget_changed "
                      "== NULL", source)

    def test_provider_callbacks_run_outside_policy_lock(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        # 聚合遍历与预算广播都不在 s_lock 临界区内；回调在锁外执行。
        self.assertIn("power_policy_apply_provider_facts", source)
        self.assertIn("power_policy_coordinator_facts", source)
        self.assertNotIn("taskENTER_CRITICAL(&s_lock);\n"
                         "        participant", source)
        self.assertIn("provider facts failed", source)

    def test_fail_closed_on_provider_error(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        # provider 读取失败时不允许发布 sleep 许可，blocker 不被静默清除。
        self.assertIn("provider_ok = power_policy_apply_provider_facts", source)
        self.assertIn("if (!provider_ok)", source)
        self.assertIn("budget.sleep_permission = POWER_POLICY_SLEEP_NONE",
                      source)
        self.assertIn("all_ok = false", source)

    def test_blocker_aggregation_never_publishes_light_allowed(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        # M1 修复：sleep_permission 的推导在 provider blocker 聚合之后统一收紧，
        # 任何 blocker（音频/后台关键/OTA/provisioning/UI force active）存在时
        # 都不允许发布 LIGHT_ALLOWED。
        self.assertIn("power_policy_apply_provider_facts(&budget)", source)
        self.assertIn("sleep_blockers != POWER_POLICY_SLEEP_BLOCKER_NONE",
                      source)
        self.assertIn("budget.sleep_permission == POWER_POLICY_SLEEP_LIGHT_ALLOWED",
                      source)
        # 收紧发生在 apply_provider_facts 之后、store_budget 之前。
        apply_pos = source.index("power_policy_apply_provider_facts(&budget)")
        tighten_pos = source.index(
            "budget.sleep_permission == POWER_POLICY_SLEEP_LIGHT_ALLOWED")
        store_pos = source.index("power_policy_store_budget(&budget)")
        self.assertLess(apply_pos, tighten_pos)
        self.assertLess(tighten_pos, store_pos)

    def test_coordinator_facts_provider_maps_ota_and_provisioning(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/runtime/runtime_coordinator.h"',
                      source)
        self.assertIn("POWER_POLICY_PROVIDER_RUNTIME_COORDINATOR", source)
        self.assertIn("POWER_POLICY_SLEEP_BLOCKER_OTA_ACTIVE", source)
        self.assertIn("POWER_POLICY_SLEEP_BLOCKER_PROVISIONING_ACTIVE", source)
        self.assertIn("RUNTIME_COORDINATOR_PARTICIPANT_OTA", source)
        self.assertIn("RUNTIME_COORDINATOR_PARTICIPANT_NETWORK_PROVISIONING",
                      source)
        self.assertIn("runtime_coordinator_get_snapshot()", source)

    def test_power_policy_no_longer_directly_notifies_safety(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("safety_monitor_policy_notify_power_changed", source)
        self.assertIn("power_policy_budget_changed_notify()", source)

    def test_audio_bridge_only_reads_cache_and_maps_blocker(self) -> None:
        bridge = POWER_POLICY_AUDIO_BRIDGE.read_text(encoding="utf-8")
        audio_header = AUDIO_CODEC_HEADER.read_text(encoding="utf-8")
        audio_source = AUDIO_CODEC_SOURCE.read_text(encoding="utf-8")

        # 非阻塞缓存快照读取合同必须先存在。
        self.assertIn("audio_codec_get_cached_session_snapshot", audio_header)
        self.assertIn("audio_codec_set_session_change_callback", audio_header)
        self.assertIn("audio_codec_publish_session_change", audio_source)
        self.assertIn("s_cached_session_snapshot", audio_source)
        # 阻塞式 getter 仍在资源 mutex 内。
        self.assertIn("audio_codec_lock_resources(UINT32_MAX)",
                      audio_source)
        # bridge 只注册 AUDIO_ACTIVE 事实，不包含播放/录音/告警逻辑。
        self.assertIn("POWER_POLICY_PROVIDER_AUDIO", bridge)
        self.assertIn("POWER_POLICY_PARTICIPANT_FACTS_ONLY", bridge)
        self.assertIn("POWER_POLICY_SLEEP_BLOCKER_AUDIO_ACTIVE", bridge)
        self.assertIn("audio_codec_set_session_change_callback", bridge)
        self.assertIn("power_policy_notify(POWER_POLICY_NOTIFY_AUDIO)", bridge)
        # bridge 只做映射：事实回调直接读 audio_codec 非阻塞缓存，无本地缓存副本。
        self.assertIn("audio_codec_get_cached_session_snapshot(&snapshot)",
                      bridge)
        self.assertNotIn("s_audio_snapshot", bridge)
        self.assertNotIn("s_snapshot_lock", bridge)
        self.assertNotIn("audio_codec_acquire_input", bridge)
        self.assertNotIn("audio_codec_acquire_output", bridge)
        self.assertNotIn("audio_codec_read", bridge)
        self.assertNotIn("audio_codec_write", bridge)

    def test_app_main_registers_audio_bridge_before_policy_start(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        bridge_pos = source.index("power_policy_audio_bridge_register()")
        policy_start_pos = source.index("power_policy_start()")
        self.assertLess(bridge_pos, policy_start_pos)
        self.assertIn("Power policy audio bridge register failed", source)

    def test_system_time_sync_defers_by_power_budget(self) -> None:
        source = SYSTEM_TIME_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/power/power_policy.h"', source)
        self.assertIn("power_policy_get_budget()", source)
        self.assertIn("budget.network_sync_allowed", source)
        self.assertIn("network time sync deferred by power budget", source)
        self.assertIn("kDeferredSyncMaxWaitLoops", source)


if __name__ == "__main__":
    unittest.main()
