#include "music_stream_player.h"

#include <atomic>
#include <cstdint>
#include <new>
#include <string>

#include "audio_codec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "micro_decoder/decoder_source.h"
#include "services/runtime/safety_monitor_policy.h"

namespace {

constexpr const char *kTag = "music_stream";
// Opus 解码实测会超过原 MP3 路径的 8 KiB；该任务栈留在 PSRAM，不挤占 internal RAM。
constexpr size_t kDecoderStackBytes = 16384U;
constexpr uint32_t kOpusDecoderSampleRate = 48000U;
constexpr uint32_t kAudioCodecSampleRate = 24000U;
// 120 ms Opus 包在 48 kHz 单声道 16-bit 下最多 11520 B；降采样后不超过 5760 B。
constexpr size_t kOpusDownsampleBufferBytes = 8192U;

} // namespace

struct music_stream_player final : public micro_decoder::DecoderListener
{
    music_stream_player(QueueHandle_t queue,
                        const micro_decoder::DecoderConfig &config)
        : event_queue(queue), decoder(config)
    {
        decoder.set_listener(this);
    }

    ~music_stream_player() override
    {
        decoder.set_listener(nullptr);
        decoder.stop();
        release_audio();
        heap_caps_free(opus_downsample_buffer);
    }

    void on_stream_info(const micro_decoder::AudioStreamInfo &info) override
    {
        pcm_format_ready = info.get_sample_rate() == kOpusDecoderSampleRate &&
                           info.get_bits_per_sample() == 16U &&
                           info.get_channels() == 1U;
        if (!pcm_format_ready)
        {
            audio_error.store(ESP_ERR_NOT_SUPPORTED);
            ESP_LOGE(kTag, "unsupported Opus PCM: %lu Hz, %u-bit, %u channel(s)",
                     (unsigned long)info.get_sample_rate(),
                     (unsigned)info.get_bits_per_sample(),
                     (unsigned)info.get_channels());
            return;
        }
        ESP_LOGI(kTag, "decoded Opus PCM: %lu Hz -> %lu Hz, %u-bit, mono",
                 (unsigned long)info.get_sample_rate(),
                 (unsigned long)kAudioCodecSampleRate,
                 (unsigned)info.get_bits_per_sample());
    }

    size_t on_audio_write(const uint8_t *data, size_t length,
                          uint32_t /*timeout_ms*/) override
    {
        if (!pcm_format_ready || opus_downsample_buffer == nullptr ||
            (length % sizeof(int16_t)) != 0U)
        {
            audio_error.store(ESP_ERR_NOT_SUPPORTED);
            return length;
        }

        const size_t input_bytes =
            length > kOpusDownsampleBufferBytes * 2U
                ? kOpusDownsampleBufferBytes * 2U
                : length;
        const auto *input = reinterpret_cast<const int16_t *>(data);
        auto *output = reinterpret_cast<int16_t *>(opus_downsample_buffer);
        const size_t input_samples = input_bytes / sizeof(*input);
        size_t output_samples = 0U;
        for (size_t index = 0U; index < input_samples; ++index)
        {
            if (discard_next_opus_sample)
            {
                discard_next_opus_sample = false;
                continue;
            }
            // 服务端先限带到 24 kHz；跨回调保留每两个 48 kHz sample 的首个。
            output[output_samples++] = input[index];
            discard_next_opus_sample = true;
        }

        const esp_err_t write_ret =
            audio_codec_write(output, output_samples * sizeof(*output));
        if (!decoder_stack_reported)
        {
            const UBaseType_t free_words = uxTaskGetStackHighWaterMark(nullptr);
            decoder_stack_reported = true;
            ESP_LOGI(kTag, "md_decoder stack free: %u B / %u B",
                     (unsigned)(free_words * sizeof(StackType_t)),
                     (unsigned)kDecoderStackBytes);
        }
        if (write_ret != ESP_OK)
        {
            esp_err_t expected = ESP_OK;
            (void)audio_error.compare_exchange_strong(expected, write_ret);
        }
        // audio_codec_write() 是完整 PCM block 写入接口；发生错误时消费本块，
        // 由 owner task 停止 decoder，避免 decoder task 在同一块上忙等重试。
        return length;
    }

    void on_state_change(micro_decoder::DecoderState state) override
    {
        if (stopping || terminal_emitted)
        {
            return;
        }
        if (state == micro_decoder::DecoderState::PLAYING)
        {
            emit(MUSIC_STREAM_PLAYER_EVENT_PLAYING, ESP_OK);
            return;
        }
        if (state == micro_decoder::DecoderState::FAILED)
        {
            finish_error(ESP_FAIL);
            return;
        }
        if (state == micro_decoder::DecoderState::IDLE)
        {
            release_audio();
            terminal_emitted = true;
            stopped = true;
            emit(MUSIC_STREAM_PLAYER_EVENT_ENDED, ESP_OK);
        }
    }

    void poll()
    {
        const esp_err_t write_error = audio_error.exchange(ESP_OK);
        if (write_error != ESP_OK && !stopping && !terminal_emitted)
        {
            finish_error(write_error);
            return;
        }
        decoder.loop();
    }

    void stop()
    {
        if (stopped)
        {
            return;
        }
        stopping = true;
        decoder.stop();
        release_audio();
        stopped = true;
    }

    void emit(music_stream_player_event_type_t type, esp_err_t error)
    {
        music_stream_player_event_t event = {};
        event.type = type;
        event.error = error;
        event.buffered_bytes = 0U;
        (void)xQueueSend(event_queue, &event, 0U);
    }

    void finish_error(esp_err_t error)
    {
        terminal_emitted = true;
        decoder.stop();
        release_audio();
        stopped = true;
        emit(MUSIC_STREAM_PLAYER_EVENT_ERROR, error);
    }

    void release_audio()
    {
        if (!output_acquired)
        {
            return;
        }
        output_acquired = false;
        (void)audio_codec_flush_output();
        (void)audio_codec_release_output(AUDIO_CODEC_OWNER_MUSIC_PLAYER);
        const esp_err_t policy_ret =
            safety_monitor_policy_set_music_active(false, "music_stopped");
        if (policy_ret != ESP_OK)
        {
            ESP_LOGW(kTag, "music safety policy resume failed: %s",
                     esp_err_to_name(policy_ret));
        }
    }

    QueueHandle_t event_queue;
    micro_decoder::DecoderSource decoder;
    std::atomic<esp_err_t> audio_error{ESP_OK};
    uint8_t *opus_downsample_buffer{nullptr};
    bool pcm_format_ready{false};
    bool discard_next_opus_sample{false};
    bool decoder_stack_reported{false};
    bool output_acquired{false};
    bool stopping{false};
    bool terminal_emitted{false};
    bool stopped{false};
};

extern "C" esp_err_t music_stream_player_start(
    const music_http_client_config_t *config, const char *stream_id,
    QueueHandle_t event_queue, music_stream_player_t **out_player)
{
    if (config == nullptr || stream_id == nullptr || stream_id[0] == '\0' ||
        event_queue == nullptr || out_player == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_player = nullptr;

    char stream_url[MUSIC_SERVICE_URL_MAX_BYTES + MUSIC_SERVICE_STREAM_ID_MAX_BYTES +
                    MUSIC_SERVICE_DEVICE_ID_MAX_BYTES + 32U];
    esp_err_t ret = music_http_client_build_stream_url(config, stream_id,
                                                        stream_url,
                                                        sizeof(stream_url));
    if (ret != ESP_OK)
    {
        return ret;
    }

    micro_decoder::DecoderConfig decoder_config;
    decoder_config.ring_buffer_size = MUSIC_SERVICE_RING_BYTES;
    decoder_config.http_timeout_ms = config->timeout_ms > 0U ? config->timeout_ms : 5000U;
    // 保持原版 reader 的 internal RAM 栈；decoder 的 16 KiB 栈放 PSRAM，避免挤占
    // 当前紧张的 internal RAM。压缩 ring 由 micro-decoder 优先申请 PSRAM。
    decoder_config.decoder_stack_size = kDecoderStackBytes;
    decoder_config.decoder_stack_in_psram = true;
    // 对齐已实测稳定的纯 C 播放器：I2S 写入是实时路径，decoder 必须高于 reader，
    // 避免歌单 HTTPS 等普通后台工作抢占 PCM 输出。
    decoder_config.decoder_priority = 5;
    decoder_config.reader_priority = 4;

    auto *player = new (std::nothrow) music_stream_player(event_queue,
                                                            decoder_config);
    if (player == nullptr)
    {
        return ESP_ERR_NO_MEM;
    }
    player->opus_downsample_buffer = static_cast<uint8_t *>(
        heap_caps_malloc(kOpusDownsampleBufferBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (player->opus_downsample_buffer == nullptr)
    {
        delete player;
        return ESP_ERR_NO_MEM;
    }
    ret = audio_codec_acquire_output(AUDIO_CODEC_OWNER_MUSIC_PLAYER, 0U);
    if (ret != ESP_OK)
    {
        delete player;
        return ret;
    }
    player->output_acquired = true;
    const esp_err_t policy_ret =
        safety_monitor_policy_set_music_active(true, "music_buffering");
    if (policy_ret != ESP_OK)
    {
        ESP_LOGW(kTag, "music safety policy pause failed: %s",
                 esp_err_to_name(policy_ret));
    }
    if (!player->decoder.play_url(std::string(stream_url)))
    {
        player->release_audio();
        delete player;
        return ESP_FAIL;
    }

    player->emit(MUSIC_STREAM_PLAYER_EVENT_BUFFERING, ESP_OK);
    *out_player = player;
    return ESP_OK;
}

extern "C" void music_stream_player_poll(music_stream_player_t *player)
{
    if (player != nullptr)
    {
        player->poll();
    }
}

extern "C" esp_err_t music_stream_player_stop(music_stream_player_t *player)
{
    if (player != nullptr)
    {
        player->stop();
    }
    return ESP_OK;
}

extern "C" bool music_stream_player_is_stopped(
    const music_stream_player_t *player)
{
    return player == nullptr || player->stopped;
}

extern "C" void music_stream_player_release(music_stream_player_t *player)
{
    delete player;
}
