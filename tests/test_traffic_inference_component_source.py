import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class TrafficInferenceComponentSourceTests(unittest.TestCase):
    def test_component_cmake_and_sources_are_present(self) -> None:
        cmake_path = (
            REPO_ROOT / "components" / "traffic_inference" / "CMakeLists.txt"
        )
        self.assertTrue(
            cmake_path.exists(), "traffic_inference CMakeLists.txt should exist"
        )

        cmake_source = cmake_path.read_text(encoding="utf-8")
        self.assertIn("traffic_audio_runtime.cc", cmake_source)
        self.assertIn("traffic_inference_postprocess.cc", cmake_source)
        self.assertIn("audio_codec", cmake_source)
        self.assertIn("espressif__esp-dsp", cmake_source)

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
