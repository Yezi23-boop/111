import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class TrafficInferenceComponentSourceTests(unittest.TestCase):
    def test_experimental_component_is_not_linked_by_main_firmware(self) -> None:
        root_cmake = (REPO_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("EXCLUDE_COMPONENTS traffic_inference", root_cmake)

        cmake_path = (
            REPO_ROOT / "components" / "traffic_inference" / "CMakeLists.txt"
        )
        self.assertTrue(
            cmake_path.exists(), "traffic_inference CMakeLists.txt should exist"
        )

        cmake_source = cmake_path.read_text(encoding="utf-8")
        self.assertIn("edge_impulse/manual_v7_1s", cmake_source)
        self.assertIn("edge_impulse/manual_v5/edge-impulse-sdk", cmake_source)
        self.assertIn("edge_impulse/manual_v5", cmake_source)
        self.assertIn("traffic_audio_runtime.cc", cmake_source)
        self.assertIn("traffic_inference_postprocess.cc", cmake_source)

        model_metadata_path = (
            REPO_ROOT
            / "components"
            / "traffic_inference"
            / "edge_impulse"
            / "manual_v7_1s"
            / "model-parameters"
            / "model_metadata.h"
        )
        model_metadata = model_metadata_path.read_text(encoding="utf-8")
        self.assertIn("EI_CLASSIFIER_PROJECT_DEPLOY_VERSION     7", model_metadata)
        self.assertIn("EI_CLASSIFIER_RAW_SAMPLE_COUNT           16000", model_metadata)

        header_path = (
            REPO_ROOT
            / "components"
            / "traffic_inference"
            / "include"
            / "traffic_audio_runtime.h"
        )
        self.assertTrue(header_path.exists(), "traffic_audio_runtime.h should exist")


if __name__ == "__main__":
    unittest.main()
