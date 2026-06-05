/**
 * @file espdl_audio_runtime.h
 * @brief ESP-DL 单模型实时音频推理运行时（C API）。
 *
 * 镜像 traffic_audio_runtime 的生命周期管理，但使用 ESP-DL active 单模型推理：
 *   ES7210 ADC (24kHz, 2ch TDM) → 提取主麦克风 → 重采样到 16kHz
 *   → Fbank 特征提取 → V3.4 T90 sharp 单模型 → 回调通知上层
 *
 * 使用方法：
 *   1. espdl_audio_runtime_start() 启动后台 FreeRTOS 任务
 *   2. 任务持续读取麦克风、推理、通过回调上报结果
 *   3. espdl_audio_runtime_stop() 请求停止并等待任务退出
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "espdl_model_runner.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 运行时生命周期状态。 */
typedef enum {
    ESPDL_AUDIO_RUNTIME_STATE_IDLE = 0,
    ESPDL_AUDIO_RUNTIME_STATE_STARTING,
    ESPDL_AUDIO_RUNTIME_STATE_RUNNING,
    ESPDL_AUDIO_RUNTIME_STATE_STOPPING,
    ESPDL_AUDIO_RUNTIME_STATE_FAILED,
} espdl_audio_runtime_state_t;

/** 运行时配置。 */
typedef struct {
    size_t input_chunk_frames;   /**< 每次读取的硬件帧数，0 使用默认值。 */
    uint32_t read_timeout_ms;    /**< 音频读取超时，单位毫秒。 */
    uint32_t task_stack_size;    /**< FreeRTOS 任务栈大小，单位字节。 */
    UBaseType_t task_priority;   /**< FreeRTOS 任务优先级。 */
} espdl_audio_runtime_config_t;

/**
 * @brief 实时推理结果回调。
 *
 * 每次单模型推理完成后调用，由运行时任务上下文触发。
 * 回调内应尽快完成处理，避免阻塞推理循环。
 *
 * @param[in] result 单模型推理结果。
 * @param[in] user_data 用户数据指针。
 */
typedef void (*espdl_audio_runtime_result_callback_t)(
    const espdl_model_result_t *result,
    void *user_data);

/**
 * @brief 启动 ESP-DL 实时音频推理运行时。
 *
 * @param[in] config 运行时配置，NULL 使用默认值。
 * @return ESP_OK 表示已启动。
 */
esp_err_t espdl_audio_runtime_start(
    const espdl_audio_runtime_config_t *config);

/**
 * @brief 停止运行时。
 *
 * @param[in] timeout_ms 等待超时，0 使用默认 2000ms。
 * @return ESP_OK 表示已停止。
 */
esp_err_t espdl_audio_runtime_stop(uint32_t timeout_ms);

/**
 * @brief 查询运行时是否正在运行。
 */
bool espdl_audio_runtime_is_running(void);

/**
 * @brief 获取当前运行时状态。
 */
espdl_audio_runtime_state_t espdl_audio_runtime_get_state(void);

/**
 * @brief 注册推理结果回调。
 *
 * @param[in] callback 回调函数，NULL 注销。
 * @param[in] user_data 用户数据。
 * @return ESP_OK 表示注册成功。
 */
esp_err_t espdl_audio_runtime_set_result_callback(
    espdl_audio_runtime_result_callback_t callback,
    void *user_data);

#ifdef __cplusplus
}
#endif
