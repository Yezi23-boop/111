/**
 * @file espdl_dual_runner.cpp
 * @brief 双模型并行推理运行器实现。
 *
 * 同时管理 V3.2 DS-TCN-small 和 V3.3 DS-CNN-tiny，共享 Fbank 特征，
 * 串行推理后按可配置策略融合结果。
 *
 * 设计选择：
 * - 串行执行而非 FreeRTOS 并行任务：ESP-DL 单核模式非线程安全，
 *   且两个模型共用 PSRAM，并行执行反而增加内存峰值和调度开销。
 * - 共享特征缓冲：两个模型使用相同的 [98, 40] Fbank，避免重复计算。
 * - C API 包装：内部使用 C++ 句柄，对外暴露 C 接口，兼容现有 C 代码。
 */

#include "espdl_dual_runner.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"

static const char *TAG = "espdl_dual";

/** 双模型运行器内部结构。 */
struct espdl_dual_runner_t {
    espdl_model_runner_t *dstcn;          /**< V3.2 DS-TCN-small 运行器。 */
    espdl_model_runner_t *dscnn;          /**< V3.3 DS-CNN-tiny 运行器。 */
    espdl_fusion_strategy_t strategy;     /**< 融合策略。 */
};

extern "C" {

esp_err_t espdl_dual_runner_create(espdl_dual_runner_t **out_runner,
                                   const uint8_t *dstcn_data,
                                   const uint8_t *dscnn_data,
                                   espdl_fusion_strategy_t strategy)
{
    if (out_runner == nullptr || dstcn_data == nullptr || dscnn_data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto *runner = new (std::nothrow) espdl_dual_runner_t();
    if (runner == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    runner->dstcn = nullptr;
    runner->dscnn = nullptr;
    runner->strategy = strategy;

    /* 加载 V3.2 DS-TCN-small */
    esp_err_t ret = espdl_model_runner_create(&runner->dstcn, dstcn_data,
                                              "dstcn_v3.2");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS-TCN-small (V3.2) 加载失败: %s", esp_err_to_name(ret));
        delete runner;
        return ret;
    }

    /* 加载 V3.3 DS-CNN-tiny */
    ret = espdl_model_runner_create(&runner->dscnn, dscnn_data,
                                    "dscnn_v3.3");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS-CNN-tiny (V3.3) 加载失败: %s", esp_err_to_name(ret));
        espdl_model_runner_destroy(runner->dstcn);
        delete runner;
        return ret;
    }

    ESP_LOGI(TAG, "双模型加载成功: V3.2 DS-TCN + V3.3 DS-CNN-tiny, fusion=%d",
             strategy);
    *out_runner = runner;
    return ESP_OK;
}

void espdl_dual_runner_destroy(espdl_dual_runner_t *runner)
{
    if (runner == nullptr) {
        return;
    }
    espdl_model_runner_destroy(runner->dstcn);
    espdl_model_runner_destroy(runner->dscnn);
    delete runner;
}

esp_err_t espdl_dual_runner_run(espdl_dual_runner_t *runner,
                                const espdl_feature_frame_t *feature,
                                espdl_dual_result_t *result)
{
    if (runner == nullptr || feature == nullptr || result == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    result->dstcn_ok = false;
    result->dscnn_ok = false;
    result->fused_label_index = -1;
    result->fused_confidence = 0.0f;
    result->fused_danger_prob = 0.0f;

    /* 串行执行两个模型推理，共享同一份 Fbank 特征。 */
    esp_err_t dstcn_ret = espdl_model_runner_run(runner->dstcn, feature,
                                                  &result->dstcn_result);
    result->dstcn_ok = (dstcn_ret == ESP_OK);

    esp_err_t dscnn_ret = espdl_model_runner_run(runner->dscnn, feature,
                                                  &result->dscnn_result);
    result->dscnn_ok = (dscnn_ret == ESP_OK);

    if (!result->dstcn_ok && !result->dscnn_ok) {
        ESP_LOGE(TAG, "双模型推理均失败: dstcn=%s, dscnn=%s",
                 esp_err_to_name(dstcn_ret), esp_err_to_name(dscnn_ret));
        return ESP_FAIL;
    }

    /* 融合两个模型的结果 */
    const float dstcn_danger = result->dstcn_result.probabilities[1];
    const float dscnn_danger = result->dscnn_result.probabilities[1];

    switch (runner->strategy) {
    case ESPDL_FUSION_ANY_DANGER:
        if (result->dstcn_result.label_index == 1 ||
            result->dscnn_result.label_index == 1) {
            result->fused_label_index = 1;
        } else {
            result->fused_label_index = 0;
        }
        result->fused_danger_prob = std::max(dstcn_danger, dscnn_danger);
        break;

    case ESPDL_FUSION_BOTH_DANGER:
        if (result->dstcn_result.label_index == 1 &&
            result->dscnn_result.label_index == 1) {
            result->fused_label_index = 1;
            result->fused_danger_prob = (dstcn_danger + dscnn_danger) / 2.0f;
        } else {
            result->fused_label_index = 0;
            result->fused_danger_prob = std::max(dstcn_danger, dscnn_danger);
        }
        break;

    case ESPDL_FUSION_MAX_PROB:
    default:
        result->fused_danger_prob = std::max(dstcn_danger, dscnn_danger);
        result->fused_label_index =
            result->fused_danger_prob >= ESPDL_DSTCN_DANGER_THRESHOLD ? 1 : 0;
        break;
    }

    result->fused_confidence = result->fused_label_index == 1
                                   ? result->fused_danger_prob
                                   : (1.0f - result->fused_danger_prob);

    ESP_LOGI(TAG,
             "DUAL fused=%s(%d), danger=%.4f, "
             "dstcn=%s(%.4f) %s, dscnn=%s(%.4f) %s",
             espdl_model_runner_label_name(result->fused_label_index),
             result->fused_label_index,
             result->fused_danger_prob,
             espdl_model_runner_label_name(result->dstcn_result.label_index),
             dstcn_danger,
             result->dstcn_ok ? "ok" : "FAIL",
             espdl_model_runner_label_name(result->dscnn_result.label_index),
             dscnn_danger,
             result->dscnn_ok ? "ok" : "FAIL");
    return ESP_OK;
}

esp_err_t espdl_dual_runner_self_test(espdl_dual_runner_t *runner)
{
    if (runner == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = espdl_model_runner_self_test(runner->dstcn);
    esp_err_t ret2 = espdl_model_runner_self_test(runner->dscnn);
    return ret != ESP_OK ? ret : ret2;
}

espdl_model_runner_t *espdl_dual_runner_get_dstcn(espdl_dual_runner_t *runner)
{
    return runner != nullptr ? runner->dstcn : nullptr;
}

espdl_model_runner_t *espdl_dual_runner_get_dscnn(espdl_dual_runner_t *runner)
{
    return runner != nullptr ? runner->dscnn : nullptr;
}

}  // extern "C"
