# MP3播放器使用指南

## 功能说明

本项目集成了 `esp-audio-player` 组件，支持播放 **MP3** 和 **WAV** 格式音频文件。

## 组件架构

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (main)                          │
│                                                          │
│  ┌──────────────┐        ┌──────────────┐               │
│  │  mp3_player  │        │ time_weather │               │
│  │   (封装层)    │        │   (应用逻辑)  │               │
│  └──────┬───────┘        └──────────────┘               │
└─────────┼──────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────┐
│              audio_player 组件 (C++)                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │  audio_mp3   │  │  audio_wav   │  │ 播放控制逻辑  │   │
│  └──────┬───────┘  └──────┬───────┘  └──────────────┘   │
└─────────┼─────────────────┼────────────────────────────┘
          │                 │
          ▼                 ▼
┌─────────────────────────────────────────────────────────┐
│           libhelix-mp3 解码器                             │
│         (Real Networks MP3解码库)                         │
└─────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────┐
│              audio_codec 组件                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │   ES8311     │  │   ES7210     │  │   I2S驱动    │   │
│  │  (DAC编码器)  │  │  (ADC录音)   │  │  (48kHz)     │   │
│  └──────────────┘  └──────────────┘  └──────────────┘   │
└─────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────┐
│                    硬件层                                 │
│         NS4150B PA + 扬声器                               │
└─────────────────────────────────────────────────────────┘
```

## API 使用方法

### 1. 初始化音频系统

```c
#include "audio_codec.h"
#include "mp3_player.h"

void audio_init(void) {
    // 步骤1: 初始化音频编解码器 (I2S + ES8311)
    esp_err_t ret = audio_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "音频编解码器初始化失败");
        return;
    }
    
    // 步骤2: 设置音量 (0-100)
    audio_codec_set_volume(80);
    
    // 步骤3: 初始化MP3播放器
    ret = mp3_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MP3播放器初始化失败");
        return;
    }
}
```

### 2. 播放音频文件

#### 播放 MP3 文件

```c
// 播放单个MP3文件
esp_err_t ret = mp3_player_play_file("/spiffs/music.mp3");
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "开始播放MP3");
}
```

#### 播放 WAV 文件

```c
// 使用现有的play_audio_example()函数播放WAV
play_audio_example();
```

### 3. 播放控制

```c
// 暂停播放
mp3_player_pause();

// 恢复播放
mp3_player_resume();

// 停止播放
mp3_player_stop();

// 获取播放器状态
audio_player_state_t state = mp3_player_get_state();
switch (state) {
    case AUDIO_PLAYER_STATE_IDLE:
        // 空闲状态
        break;
    case AUDIO_PLAYER_STATE_PLAYING:
        // 正在播放
        break;
    case AUDIO_PLAYER_STATE_PAUSE:
        // 暂停状态
        break;
    default:
        break;
}
```

### 4. 音量控制

```c
// 设置音量 (0-100)
audio_codec_set_volume(60);

// 获取当前音量
int volume;
audio_codec_get_volume(&volume);
ESP_LOGI(TAG, "当前音量: %d", volume);

// 静音
audio_codec_set_mute(true);

// 取消静音
audio_codec_set_mute(false);
```

## 实际使用示例

### 示例1: 播放MP3音乐

```c
void play_mp3_music(void) {
    // 初始化音频系统
    audio_codec_init();
    audio_codec_set_volume(80);
    mp3_player_init();
    
    // 播放MP3文件
    mp3_player_play_file("/spiffs/song.mp3");
    
    // 等待播放完成或手动控制
    vTaskDelay(pdMS_TO_TICKS(5000));  // 播放5秒后暂停
    mp3_player_pause();
    
    vTaskDelay(pdMS_TO_TICKS(2000));  // 暂停2秒
    mp3_player_resume();  // 继续播放
}
```

### 示例2: 循环播放多个文件

```c
void play_playlist(void) {
    const char *playlist[] = {
        "/spiffs/music1.mp3",
        "/spiffs/music2.mp3",
        "/spiffs/music3.mp3"
    };
    
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "播放: %s", playlist[i]);
        mp3_player_play_file(playlist[i]);
        
        // 等待播放完成
        while (mp3_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
```

### 示例3: 根据文件格式自动选择播放方式

```c
void play_audio_auto(const char *file_path) {
    // 检查文件扩展名
    if (strstr(file_path, ".mp3") != NULL) {
        // MP3文件使用audio_player播放
        mp3_player_play_file(file_path);
    } else if (strstr(file_path, ".wav") != NULL) {
        // WAV文件使用直接播放
        play_wav_file(file_path);  // 你需要实现这个函数
    } else {
        ESP_LOGE(TAG, "不支持的文件格式: %s", file_path);
    }
}
```

## 上传音频文件到 SPIFFS

### 方法1: 使用 idf.py

1. 将MP3文件放到 `audio_data/` 目录
2. 编译并烧录：
```bash
idf.py build
idf.py -p COM3 flash
```

### 方法2: 使用 parttool.py

```bash
# 写入单个文件到SPIFFS分区
python %IDF_PATH%/components/partition_table/parttool.py -p COM3 write_partition --partition-name=audio --input music.mp3
```

## 支持的音频格式

### MP3
- **编码**: MPEG-1/2/2.5 Layer III
- **采样率**: 8kHz - 48kHz (推荐48kHz)
- **比特率**: 32kbps - 320kbps
- **声道**: 单声道/立体声
- **解码器**: libhelix-mp3 (Real Networks)

### WAV
- **采样率**: 8kHz - 48kHz (推荐48kHz)
- **位深度**: 16-bit
- **声道**: 单声道/立体声
- **格式**: PCM 未压缩

## 性能参数

- **MP3解码速度**: 实时解码，无需预先解压
- **内存占用**: 
  - libhelix解码器: ~30KB
  - 播放缓冲区: 可配置 (默认使用PSRAM)
- **CPU占用**: 约5-10% @ 240MHz
- **支持的最大比特率**: 320kbps

## 注意事项

1. **初始化顺序**: 必须先调用 `audio_codec_init()` 再调用 `mp3_player_init()`
2. **文件句柄管理**: `audio_player_play()` 会接管 FILE* 的生命周期，播放完成后自动关闭
3. **任务优先级**: MP3解码任务运行在优先级5，确保不与其他关键任务冲突
4. **PSRAM使用**: 建议使用PSRAM存储音频缓冲区，提高性能
5. **采样率匹配**: 当前I2S配置为48kHz，建议使用相同采样率的MP3文件以获得最佳效果

## 配置选项

在 `sdkconfig` 中可以配置:

```
CONFIG_AUDIO_PLAYER_ENABLE_MP3=y    # 启用MP3支持
CONFIG_AUDIO_PLAYER_ENABLE_WAV=y    # 启用WAV支持
```

## 调试日志

启用详细日志输出:

```c
esp_log_level_set("audio", ESP_LOG_DEBUG);
esp_log_level_set("mp3_player", ESP_LOG_DEBUG);
esp_log_level_set("audio_codec", ESP_LOG_DEBUG);
```

## 常见问题

### Q1: 播放MP3时没有声音
- 检查音量设置 `audio_codec_set_volume(80)`
- 检查PA使能 GPIO46 是否正常
- 确认MP3文件格式正确
- 查看日志是否有解码错误

### Q2: 播放卡顿或断续
- 检查SPIFFS读取速度
- 增加音频缓冲区大小
- 确保没有其他高优先级任务占用CPU

### Q3: 不支持某些MP3文件
- libhelix支持标准MP3格式
- 不支持某些特殊编码（如VBR v0）
- 建议重新编码为标准格式

## 参考资料

- [esp-audio-player GitHub](https://github.com/chmorgan/esp-audio-player)
- [libhelix-mp3 解码器](https://github.com/chmorgan/esp-libhelix-mp3)
- [ESP-IDF I2S 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html)
