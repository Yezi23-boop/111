#include "audio/local_audio_codec_adapter.h"

#include <cstring>

#include "audio_codec.h"
#include "audio_platform_config.h"

namespace official_chat {

bool LocalAudioCodecAdapter::Initialize() {
  int volume = 0;
  if (audio_codec_get_volume(&volume) != ESP_OK) {
    return false;
  }
  output_volume_ = volume;
  return true;
}

void LocalAudioCodecAdapter::Shutdown() {}

void LocalAudioCodecAdapter::Start() {}

void LocalAudioCodecAdapter::SetOutputVolume(int volume) {
  if (audio_codec_set_volume(volume) == ESP_OK) {
    output_volume_ = volume;
  }
}

void LocalAudioCodecAdapter::SetInputGain(float gain_db) {
  if (audio_codec_set_record_gain(gain_db) == ESP_OK) {
    input_gain_db_ = gain_db;
  }
}

void LocalAudioCodecAdapter::EnableInput(bool enable) {
  input_enabled_ = enable;
}

void LocalAudioCodecAdapter::EnableOutput(bool enable) {
  output_enabled_ = enable;
  (void)audio_codec_set_pa_enable(enable);
}

void LocalAudioCodecAdapter::OutputData(std::vector<int16_t> &data) {
  if (data.empty()) {
    return;
  }
  (void)audio_codec_write(data.data(), data.size() * sizeof(int16_t));
}

bool LocalAudioCodecAdapter::InputData(std::vector<int16_t> &data) {
  size_t bytes_read = 0;
  const size_t bytes = data.size() * sizeof(int16_t);
  return audio_codec_read(data.data(), bytes, &bytes_read, portMAX_DELAY) ==
             ESP_OK &&
         bytes_read == bytes;
}

bool LocalAudioCodecAdapter::duplex() const {
  return true;
}

const char *LocalAudioCodecAdapter::input_format() const {
  return AUDIO_PLATFORM_ADC_CHANNEL_FORMAT;
}

bool LocalAudioCodecAdapter::input_reference() const {
  for (const char *cursor = AUDIO_PLATFORM_ADC_CHANNEL_FORMAT;
       cursor != nullptr && *cursor != '\0'; ++cursor) {
    if (*cursor == 'R') {
      return true;
    }
  }
  return false;
}

int LocalAudioCodecAdapter::input_sample_rate() const {
  return AUDIO_PLATFORM_HW_SAMPLE_RATE;
}

int LocalAudioCodecAdapter::input_channels() const {
  return AUDIO_PLATFORM_HW_INPUT_CHANNELS;
}

int LocalAudioCodecAdapter::output_sample_rate() const {
  return AUDIO_PLATFORM_HW_SAMPLE_RATE;
}

int LocalAudioCodecAdapter::output_channels() const {
  return AUDIO_PLATFORM_HW_OUTPUT_CHANNELS;
}

int LocalAudioCodecAdapter::output_volume() const {
  return output_volume_;
}

float LocalAudioCodecAdapter::input_gain() const {
  return input_gain_db_;
}

bool LocalAudioCodecAdapter::input_enabled() const {
  return input_enabled_;
}

bool LocalAudioCodecAdapter::output_enabled() const {
  return output_enabled_;
}

}  // namespace official_chat
