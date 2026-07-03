import unittest

from tests.main_paths import REPO_ROOT


ESPDL_DIR = REPO_ROOT / "components" / "espdl_inference"
ESPDL_CMAKE = ESPDL_DIR / "CMakeLists.txt"
ESPDL_AUDIO_RUNTIME_HEADER = ESPDL_DIR / "include" / "espdl_audio_runtime.h"
ESPDL_AUDIO_RUNTIME_SOURCE = ESPDL_DIR / "espdl_audio_runtime.cpp"
ESPDL_MODEL_RUNNER_HEADER = ESPDL_DIR / "include" / "espdl_model_runner.h"


class EspdlSingleModelRuntimeSourceTests(unittest.TestCase):
    def test_component_embeds_only_active_dscnn_model(self) -> None:
        cmake = ESPDL_CMAKE.read_text(encoding="utf-8")

        self.assertIn("edge_mix_teacher_dscnn_medium_v59_v54_anchor_softdistill_t90_20260608.espdl", cmake)
        self.assertNotIn("edge_mix_teacher_dscnn_small_v34_core_t90_sharp_20260511.espdl", cmake)
        self.assertNotIn("edge_mix_teacher_dscnn_tiny_1s_int8input_v20260503.espdl", cmake)
        self.assertNotIn("edge_mix_teacher_dstcn_small_1s_int8input_v20260503.espdl", cmake)
        self.assertNotIn("espdl_dual_runner.cpp", cmake)

    def test_runtime_uses_single_model_runner_contract(self) -> None:
        header = ESPDL_AUDIO_RUNTIME_HEADER.read_text(encoding="utf-8")
        source = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "espdl_model_runner.h"', header)
        self.assertIn("const espdl_model_result_t *result", header)
        self.assertIn("espdl_model_runner_t *model_runner", source)
        self.assertIn("espdl_model_runner_create(&s_runtime.model_runner", source)
        self.assertIn("espdl_model_runner_run(s_runtime.model_runner", source)
        self.assertIn("espdl_model_runner_destroy(s_runtime.model_runner", source)
        self.assertNotIn("espdl_dual_runner", header)
        self.assertNotIn("espdl_dual_runner", source)
        self.assertNotIn("fusion_strategy", header)
        self.assertNotIn("fusion_strategy", source)
        self.assertNotIn("dstcn_small_espdl", source)

    def test_active_dscnn_threshold_is_tuned_for_speech_false_positive(self) -> None:
        runner_header = ESPDL_MODEL_RUNNER_HEADER.read_text(encoding="utf-8")

        self.assertIn("ESPDL_DSCNN_DANGER_THRESHOLD  0.90f", runner_header)

    def test_runtime_stop_timeout_can_cleanup_on_later_stop_or_start(self) -> None:
        source = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")

        self.assertIn("cleanup_stopped_runtime_resources", source)
        self.assertIn("ESP-DL runtime cleanup deferred", source)
        self.assertIn("return cleanup_stopped_runtime_resources();", source)
        self.assertLess(
            source.index("cleanup_stopped_runtime_resources();"),
            source.index("espdl_model_runner_create(&s_runtime.model_runner"),
        )

    def test_runtime_does_not_allocate_pcm_float_inside_inference_window(self) -> None:
        source = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")

        self.assertIn("std::vector<float> pcm_float(ESPDL_WINDOW_SAMPLES);", source)
        self.assertIn("避免每 300ms 窗口反复申请堆内存", source)
        self.assertEqual(
            source.count("std::vector<float> pcm_float(ESPDL_WINDOW_SAMPLES);"),
            1,
        )
        self.assertLess(
            source.index("std::vector<float> pcm_float(ESPDL_WINDOW_SAMPLES);"),
            source.index("while (!s_runtime.stop_requested.load())"),
        )

    def test_pcm_tap_publishes_continuous_resampled_chunks(self) -> None:
        header = ESPDL_AUDIO_RUNTIME_HEADER.read_text(encoding="utf-8")
        source = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")

        self.assertIn("espdl_audio_runtime_set_pcm_tap_callback", header)
        self.assertIn("resample_24k_to_16k(mono_samples, &resample_state, &resampled_samples);", source)
        self.assertIn("resampled_samples.data()", source)
        self.assertIn("chunk_first_sample_index", source)
        self.assertIn("s_runtime.absolute_sample_index += resampled_samples.size();", source)
        self.assertNotIn("s_runtime.pcm_tap_callback(pcm_buffer.data()", source)
        self.assertLess(
            source.index("s_runtime.pcm_tap_callback(resampled_samples.data()"),
            source.index("s_runtime.absolute_sample_index += resampled_samples.size();"),
        )

    def test_result_payload_carries_window_end_sample_index(self) -> None:
        runner_header = ESPDL_MODEL_RUNNER_HEADER.read_text(encoding="utf-8")
        source = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")

        self.assertIn("uint64_t window_end_sample_index", runner_header)
        self.assertIn("window_start_sample_index", source)
        self.assertIn("window_end_sample_index", source)
        self.assertIn("result.window_end_sample_index = window_end_sample_index;", source)

    def test_espdl_component_does_not_depend_on_recorder_or_sd_manager(self) -> None:
        header = ESPDL_AUDIO_RUNTIME_HEADER.read_text(encoding="utf-8")
        source = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")

        combined = header + "\n" + source
        self.assertNotIn("danger_sample_recorder", combined)
        self.assertNotIn("sd_manager", combined)


if __name__ == "__main__":
    unittest.main()
