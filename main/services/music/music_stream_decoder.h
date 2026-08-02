#ifndef MUSIC_STREAM_DECODER_H
#define MUSIC_STREAM_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct music_stream_decoder music_stream_decoder_t;

    /** 流式解码输出格式信息。 */
    typedef struct
    {
        uint32_t sample_rate;
        uint8_t bits_per_sample;
        uint8_t channels;
        uint32_t bitrate;
    } music_stream_decoder_info_t;

    /**
     * @brief 创建 MP3 增量解码器。
     *
     * 解码器只拥有 esp_audio_codec 的 parser/decoder handle，不复制输入流；
     * 调用方必须保证未消费的 MP3 字节在下一次调用前仍然有效。
     *
     * @param[out] out_decoder 输出解码器句柄。
     * @return ESP_OK 表示成功；ESP_ERR_INVALID_ARG 表示参数为空；
     *         ESP_ERR_NO_MEM 表示解码器注册或创建失败。
     */
    esp_err_t music_stream_decoder_open(
        music_stream_decoder_t **out_decoder);

    /**
     * @brief 将一段连续 MP3 输入解码为 PCM。
     *
     * 单次调用最多处理当前输入的一部分。`consumed` 是 decoder 实际消费的
     * 字节数，调用方只能丢弃这些字节；当返回 ESP_OK 且 decoded_bytes 为 0
     * 时，通常表示 parser 正在等待更多输入。
     *
     * @param[in] decoder 解码器句柄。
     * @param[in] input MP3 输入；函数不会修改内容。
     * @param[in] input_bytes 输入字节数。
     * @param[in] eos 是否为流尾。
     * @param[out] output PCM 输出缓冲区。
     * @param[in] output_capacity 输出缓冲区容量，单位字节。
     * @param[out] consumed 实际消费的输入字节数，可为 NULL。
     * @param[out] decoded_bytes 实际输出的 PCM 字节数，可为 NULL。
     * @return ESP_OK 表示处理完成；ESP_ERR_INVALID_SIZE 表示输出缓冲区不足；
     *         其他错误表示 decoder 处理失败。
     */
    esp_err_t music_stream_decoder_process(
        music_stream_decoder_t *decoder, const uint8_t *input,
        size_t input_bytes, bool eos, uint8_t *output,
        size_t output_capacity, size_t *consumed, size_t *decoded_bytes);

    /**
     * @brief 获取已解析出的音频格式。
     * @param[in] decoder 解码器句柄。
     * @param[out] info 输出格式信息。
     * @return ESP_OK 表示信息可用；ESP_ERR_NOT_FOUND 表示尚未解出首帧。
     */
    esp_err_t music_stream_decoder_get_info(
        const music_stream_decoder_t *decoder,
        music_stream_decoder_info_t *info);

    /**
     * @brief 关闭流式解码器并释放 parser/decoder。
     * @param[in] decoder 解码器句柄，可为 NULL。
     */
    void music_stream_decoder_close(music_stream_decoder_t *decoder);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_STREAM_DECODER_H */
