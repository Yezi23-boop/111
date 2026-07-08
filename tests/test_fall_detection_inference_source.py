import hashlib
import unittest

from tests.main_paths import FALL_DETECTION_INFERENCE_CMAKE
from tests.main_paths import FALL_DETECTION_MODEL_ASSET
from tests.main_paths import FALL_MODEL_RUNNER_HEADER
from tests.main_paths import FALL_MODEL_RUNNER_SOURCE


EXPECTED_MODEL_SHA256 = (
    "cbe18c7e089bac506ff5229f2ed8c4b728148df5902dce9527aad3a315504684"
)


class FallDetectionInferenceSourceTests(unittest.TestCase):
    def test_component_embeds_expected_espdl_asset(self) -> None:
        self.assertTrue(FALL_DETECTION_MODEL_ASSET.exists())
        digest = hashlib.sha256(FALL_DETECTION_MODEL_ASSET.read_bytes()).hexdigest()
        self.assertEqual(digest, EXPECTED_MODEL_SHA256)
        model_dir = FALL_DETECTION_MODEL_ASSET.parent
        espdl_assets = sorted(path.name for path in model_dir.glob("*.espdl"))
        self.assertEqual(espdl_assets, ["cnn_v1_recall90_6ch_2s_with_test.espdl"])

        cmake = FALL_DETECTION_INFERENCE_CMAKE.read_text(encoding="utf-8")
        self.assertIn("cnn_v1_recall90_6ch_2s_with_test.espdl", cmake)
        self.assertIn("target_add_aligned_binary_data", cmake)
        self.assertIn("esp-dl", cmake)
        self.assertNotIn("managed_components", cmake)

    def test_runner_declares_fall_model_contract(self) -> None:
        header = FALL_MODEL_RUNNER_HEADER.read_text(encoding="utf-8")

        self.assertIn("#define FALL_MODEL_INPUT_ELEMENTS 600U", header)
        self.assertIn("#define FALL_MODEL_CLASS_COUNT 2U", header)
        self.assertIn("#define FALL_MODEL_LABEL_ADL 0", header)
        self.assertIn("#define FALL_MODEL_LABEL_FALL 1", header)
        self.assertIn("#define FALL_MODEL_THRESHOLD_DEFAULT 0.30f", header)
        self.assertIn("float adl_prob", header)
        self.assertIn("float fall_prob", header)
        self.assertIn("int64_t infer_us", header)
        self.assertIn("fall_model_runner_self_test", header)
        self.assertIn("fall_model_runner_run", header)

    def test_runner_validates_tensor_contract_and_uses_probabilities_directly(self) -> None:
        source = FALL_MODEL_RUNNER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kExpectedInputShape = {1, FALL_MODEL_INPUT_ELEMENTS}", source)
        self.assertIn("model_input->get_dtype() != dl::DATA_TYPE_FLOAT", source)
        self.assertIn("tensor contract mismatch", source)
        self.assertIn("get_size() == FALL_MODEL_CLASS_COUNT", source)
        self.assertIn("fbs::MODEL_LOCATION_IN_FLASH_RODATA", source)
        self.assertIn("runner->model->test()", source)
        self.assertIn("dl::dequantize<int8_t, float>", source)
        self.assertIn("dl::dequantize<int16_t, float>", source)
        self.assertIn("result->adl_prob = probabilities[FALL_MODEL_LABEL_ADL];", source)
        self.assertIn("result->fall_prob = probabilities[FALL_MODEL_LABEL_FALL];", source)
        self.assertIn("result->fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT", source)
        self.assertNotIn("std::exp", source)
        self.assertNotIn("softmax", source.lower())


if __name__ == "__main__":
    unittest.main()
