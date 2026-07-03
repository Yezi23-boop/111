/**
 * @file danger_sample_recorder.h
 * @brief 危险样本录制器接口。
 *
 * 负责在危险识别触发时录制原始 PCM 音频样本到 SD 卡。
 * 通过 PCM tap 回调机制捕获音频数据，维护环形缓冲区，
 * 在 capture event 触发时将缓冲区内容写入文件。
 *
 * 文件格式：WAV（16kHz/mono/16-bit）+ JSON 元数据。
 * 文件命名规则：{output_dir}/{date}/{time}_{label}_{confidence}.wav
 * 元数据文件：{output_dir}/{date}/{time}_{label}_{confidence}.json
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** PCM 窗口元数据（由 ESP-DL runtime 提供）。 */
typedef struct {
    uint64_t absolute_sample_index;  /**< 窗口起始处的绝对样本索引（16kHz 单声道）。 */
    uint32_t window_samples;         /**< 窗口大小（样本数）。 */
    uint32_t stride_samples;         /**< 滑窗步长（样本数）。 */
} danger_sample_pcm_window_meta_t;

/** PCM tap 回调类型。 */
typedef void (*danger_sample_pcm_tap_callback_t)(
    const int16_t *pcm_data,
    size_t samples,
    const danger_sample_pcm_window_meta_t *meta,
    void *user_data);

/** 危险样本录制器配置。 */
typedef struct {
    uint32_t buffer_duration_ms;   /**< 环形缓冲区时长（毫秒），默认 5000ms。 */
    uint32_t sample_rate_hz;       /**< 采样率（Hz），默认 16000。 */
    const char *output_dir;        /**< 输出目录路径，默认 "/sdcard/danger_samples"。 */
} danger_sample_recorder_config_t;

/**
 * @brief 初始化危险样本录制器。
 *
 * @param[in] config 配置参数，NULL 使用默认值。
 * @return ESP_OK 表示初始化成功。
 */
esp_err_t danger_sample_recorder_init(
    const danger_sample_recorder_config_t *config);

/**
 * @brief 反初始化危险样本录制器，释放资源。
 */
void danger_sample_recorder_deinit(void);

/**
 * @brief 触发样本录制。
 *
 * 在危险识别触发时调用，将当前缓冲区内容写入文件。
 *
 * @param[in] label_index 识别标签索引（0=non-danger, 1=danger）。
 * @param[in] confidence 识别置信度。
 * @return ESP_OK 表示录制成功。
 */
esp_err_t danger_sample_recorder_capture(uint32_t label_index, float confidence);

/**
 * @brief 检查录制器是否正在录制。
 *
 * @return true 表示正在录制。
 */
bool danger_sample_recorder_is_recording(void);

/**
 * @brief 获取 recorder 的 PCM tap 回调函数指针。
 *
 * 由 danger_detection_service.c 用于注册到 espdl_audio_runtime。
 * callback 签名兼容 espdl_audio_pcm_tap_callback_t。
 *
 * @return PCM tap 回调函数指针。
 */
danger_sample_pcm_tap_callback_t danger_sample_recorder_get_pcm_callback(void);

#ifdef __cplusplus
}
#endif