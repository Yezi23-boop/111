#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "get_time.h"
#include "clock_functions.h"
#include "audio_codec.h"
#include "esp_spiffs.h"
#include "i2c_manager.h"
#include "mp3_player.h"

static const char *TAG = "audio_example";
#define AUDIO_MOUNT "/spiffs"
#define WAV_FILE_PATH AUDIO_MOUNT "/1.wav"
#define AUDIO_BUFFER_SIZE (64 * 1024) // 64KB缓冲区,利用PSRAM
void audio_spiffs_init(void);
void play_audio_example(void);
void time_and_weather(void *pvParameters)
{
    esp_wait_sntp_sync(); // 初始SNTP同步,确保时间准确

    // 1. 初始化音频编解码器
    esp_err_t ret = audio_codec_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "音频编解码器初始化失败: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "音频系统初始化成功");
        audio_codec_set_volume(10);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 2. 初始化SPIFFS
    audio_spiffs_init();

    // 3. 扫描I2C总线
    i2c_manager_scan();

    // 4. 初始化MP3播放器
    ret = mp3_player_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "MP3播放器初始化失败: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "MP3播放器初始化成功");
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. 播放音频示例
    // mp3_player支持自动识别MP3和WAV格式

    // 方式1: 使用统一的播放接口 (推荐)
    // mp3_player_play_file(AUDIO_MOUNT "/music.mp3");  // 播放MP3
    mp3_player_play_file(AUDIO_MOUNT "/1.wav"); // 播放WAV

    // 方式2: 使用原始WAV播放函数
    // play_audio_example();  // 直接操作I2S播放WAV
    uint32_t time_update_counter = 0; // 时间更新计数器
    while (1)
    {
        // 每5秒更新一次时间显示
        if (time_update_counter % 5 == 0) // 每5次循环（每5秒）更新时间
        {
            my_time_t now_time;
            get_local_time(&now_time);
            ESP_LOGI("TIME", "time: %d-%d-%d %d:%d:%d", now_time.year, now_time.month, now_time.day, now_time.hour, now_time.min, now_time.sec);

            // update_digital_clock(now_time.hour, now_time.min, now_time.sec);
        }
        time_update_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延时
    }
}

void audio_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = AUDIO_MOUNT,
        .partition_label = "audio",
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("audio", &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "SPIFFS: %.2fMB/%.2fMB", used / 1024.0 / 1024.0, total / 1024.0 / 1024.0);
    }

    // 测试文件是否存在
    FILE *test = fopen(WAV_FILE_PATH, "rb");
    if (test)
    {
        fclose(test);
        ESP_LOGI(TAG, "WAV文件就绪");
    }
    else
    {
        ESP_LOGE(TAG, "WAV文件不存在");
    }
}

// WAV文件头结构体(44字节)
typedef struct
{
    char riff[4];             // "RIFF"
    uint32_t file_size;       // 文件大小-8
    char wave[4];             // "WAVE"
    char fmt[4];              // "fmt "
    uint32_t fmt_size;        // fmt块大小(通常16)
    uint16_t audio_format;    // 音频格式(1=PCM)
    uint16_t num_channels;    // 声道数
    uint32_t sample_rate;     // 采样率
    uint32_t byte_rate;       // 字节率
    uint16_t block_align;     // 块对齐
    uint16_t bits_per_sample; // 位深度
    char data[4];             // "data"
    uint32_t data_size;       // PCM数据大小
} wav_header_t;

void play_audio_example(void)
{
    /* ========== 步骤1: 打开WAV文件 ========== */
    // fopen() - 文件打开函数
    // 参数1: WAV_FILE_PATH = "/spiffs/1.wav" (文件路径)
    // 参数2: "rb" = Read Binary (只读二进制模式)
    //        - 'r' 表示只读,不能写入
    //        - 'b' 表示二进制模式,逐字节精确读取
    // 返回值: 成功返回FILE指针,失败返回NULL
    FILE *wav_file = fopen(WAV_FILE_PATH, "rb");

    // 检查文件是否成功打开
    // !wav_file 等价于 wav_file == NULL
    if (!wav_file)
    {
        // 打开失败的可能原因:
        // 1. 文件不存在
        // 2. SPIFFS未挂载
        // 3. 文件路径错误
        // 4. 存储空间损坏
        ESP_LOGE(TAG, "打开WAV文件失败: %s", WAV_FILE_PATH);
        ESP_LOGE(TAG, "跳过音频播放测试");
        return; // 提前退出函数
    }

    /* ========== 步骤2: 读取并解析WAV文件头 ========== */
    // WAV文件结构: [文件头44字节] + [PCM音频数据]
    // 文件头包含: 采样率、位深度、声道数等元数据

    // 在栈上分配wav_header结构体(44字节)
    wav_header_t wav_header;

    // fread() - 从文件读取数据到内存
    // 参数1: &wav_header - 目标内存地址(将数据读到这里)
    // 参数2: 1 - 每个元素的大小(字节)
    // 参数3: sizeof(wav_header_t) - 要读取的元素个数(44字节)
    // 参数4: wav_file - 源文件指针
    // 返回值: 实际读取的字节数
    size_t read_bytes = fread(&wav_header, 1, sizeof(wav_header_t), wav_file);

    // 检查是否完整读取了44字节文件头
    if (read_bytes != sizeof(wav_header_t))
    {
        // 读取失败的可能原因:
        // 1. 文件损坏或不完整
        // 2. 不是标准WAV格式
        // 3. 读取过程中发生错误
        ESP_LOGE(TAG, "读取WAV文件头失败");
        fclose(wav_file); // 关闭文件,释放资源
        return;
    }

    /* ========== 步骤3: 验证WAV文件格式 ========== */
    // 标准WAV文件必须以"RIFF"开头,包含"WAVE"标识
    // WAV文件头格式:
    // 字节0-3:  "RIFF" (资源交换文件格式标识)
    // 字节4-7:  文件大小-8
    // 字节8-11: "WAVE" (波形音频格式标识)

    // memcmp() - 内存比较函数
    // 参数1: wav_header.riff - 文件头的前4字节
    // 参数2: "RIFF" - 期望的字符串
    // 参数3: 4 - 比较的字节数
    // 返回值: 0表示相同,非0表示不同
    if (memcmp(wav_header.riff, "RIFF", 4) != 0 || // 检查RIFF标识
        memcmp(wav_header.wave, "WAVE", 4) != 0)   // 检查WAVE标识
    {
        // 验证失败说明不是有效的WAV文件
        ESP_LOGE(TAG, "无效的WAV文件格式");
        fclose(wav_file); // 关闭文件
        return;
    }

    /* ========== 步骤4: 打印WAV文件信息 ========== */
    // 从文件头提取音频参数并显示
    // %lu - unsigned long (无符号长整型)
    // %u  - unsigned int (无符号整型)
    // %.2f - 浮点数,保留2位小数
    ESP_LOGI(TAG, "WAV: %luHz, %uch, %ubit, %.2fMB",
             wav_header.sample_rate,                  // 采样率 (例如: 48000 Hz)
             wav_header.num_channels,                 // 声道数 (1=单声道, 2=立体声)
             wav_header.bits_per_sample,              // 位深度 (8/16/24/32 bit)
             wav_header.data_size / 1024.0 / 1024.0); // 数据大小(转换为MB)
    // 例如输出: "WAV: 48000Hz, 2ch, 16bit, 4.10MB"

    /* ========== 步骤5: 关闭文件 ========== */
    // fclose() - 关闭文件,释放文件句柄
    // 原因: 前面只是读取文件头,现在要重新从头读取音频数据
    fclose(wav_file);

    /* ========== 步骤6: 重新打开文件用于播放 ========== */
    // 为什么要重新打开?
    // 1. 前面读取了44字节文件头,文件指针已移动
    // 2. 重新打开后文件指针回到开头(位置0)
    // 3. 后续可以用fseek跳过文件头,直接读取PCM数据
    wav_file = fopen(WAV_FILE_PATH, "rb");
    if (!wav_file)
    {
        ESP_LOGE(TAG, "重新打开WAV文件失败");
        return;
    }

    /* ========== 步骤7: 移动文件指针到音频数据起始位置 ========== */
    // fseek() - 移动文件读取位置
    // 参数1: wav_file - 文件指针
    // 参数2: sizeof(wav_header_t) - 偏移量(44字节)
    // 参数3: SEEK_SET - 从文件开头计算偏移
    // 作用: 跳过44字节文件头,指向PCM音频数据
    //
    // 文件结构示意:
    // [0-43字节: 文件头] [44字节开始: PCM数据...]
    //                     ↑ 文件指针移动到这里
    fseek(wav_file, sizeof(wav_header_t), SEEK_SET);

    /* ========== 步骤8: 获取音频播放设备 ========== */
    // audio_codec_get_playback_dev() - 获取ES8311 DAC播放设备句柄
    // 这个句柄是在audio_codec_init()中创建的
    // 返回值: esp_codec_dev_handle_t 类型指针
    //        - 成功: 返回有效的设备句柄
    //        - 失败: 返回NULL (说明codec未初始化)
    esp_codec_dev_handle_t playback = audio_codec_get_playback_dev();
    if (!playback)
    {
        // playback为NULL的原因:
        // 1. audio_codec_init()未调用
        // 2. audio_codec_init()调用失败
        // 3. I2C/I2S初始化异常
        ESP_LOGE(TAG, "获取播放设备失败(可能codec未初始化)");
        fclose(wav_file);
        return;
    }

    /* ========== 步骤9: 启用音频硬件 ========== */
    // 9.1 启用功率放大器(PA: Power Amplifier)
    // audio_codec_set_pa_enable(true) - 使能GPIO46控制NS4150B功放芯片
    // 作用: 打开功放,允许音频信号输出到扬声器
    // 参数: true=启用, false=禁用(静音但省电)
    audio_codec_set_pa_enable(true);

    // 9.2 解除DAC静音
    // esp_codec_dev_set_out_mute() - 控制ES8311的MUTE寄存器
    // 参数1: playback - 设备句柄
    // 参数2: false - 取消静音(true=静音, false=正常输出)
    esp_codec_dev_set_out_mute(playback, false);

    // 9.3 等待硬件稳定
    // vTaskDelay() - FreeRTOS延时函数
    // pdMS_TO_TICKS(50) - 将50毫秒转换为系统时钟节拍数
    // 作用: 等待功放和DAC启动稳定,避免爆音
    vTaskDelay(pdMS_TO_TICKS(50));

    /* ========== 步骤10: 分配音频缓冲区 ========== */
    // 缓冲区用于暂存从文件读取的PCM数据,然后发送到I2S
    // 为什么需要缓冲区?
    // 1. 文件读取和I2S播放速度不同步
    // 2. 批量传输提高效率
    // 3. 减少文件系统访问次数

    // heap_caps_malloc() - ESP32特殊内存分配函数
    // 参数1: AUDIO_BUFFER_SIZE = 64KB (64*1024字节)
    // 参数2: MALLOC_CAP_SPIRAM - 指定从外部PSRAM分配
    // 为什么用PSRAM?
    // - ESP32S3内部RAM有限(~400KB)
    // - PSRAM容量大(8MB),适合大缓冲区
    // - 音频播放对速度要求不高,PSRAM够用
    uint8_t *audio_buffer = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

    if (!audio_buffer)
    {
        // PSRAM分配失败的原因:
        // 1. PSRAM未启用(sdkconfig配置)
        // 2. PSRAM已用完
        // 3. 硬件故障
        ESP_LOGE(TAG, "分配PSRAM缓冲区失败,尝试使用内部RAM");

        // 降级方案: 使用内部RAM
        // malloc() - 标准C内存分配,从内部RAM分配
        audio_buffer = malloc(AUDIO_BUFFER_SIZE);

        if (!audio_buffer)
        {
            // 内部RAM也分配失败,说明内存严重不足
            ESP_LOGE(TAG, "分配缓冲区失败");
            fclose(wav_file);                 // 关闭文件
            audio_codec_set_pa_enable(false); // 关闭功放
            return;
        }
    }

    ESP_LOGI(TAG, "开始播放音频...");

    /* ========== 步骤11: 读取并播放PCM数据 ========== */
    // 播放流程:
    // 1. 从文件读取64KB PCM数据到缓冲区
    // 2. 将缓冲区数据写入I2S (发送到ES8311 DAC)
    // 3. DAC转换为模拟信号 → 功放放大 → 扬声器播放
    // 4. 循环直到文件播放完成

    // 11.1 初始化播放状态变量
    size_t total_played = 0;                     // 已播放的字节数 (用于计算进度)
    size_t bytes_to_read = wav_header.data_size; // 剩余待读取的字节数
    int iteration = 0;                           // 循环计数器 (用于控制任务调度)

    // 11.2 主播放循环
    while (bytes_to_read > 0) // 当还有数据未播放时继续
    {
        /* --- 11.2.1 从文件读取PCM数据 --- */
        // 计算本次读取大小
        // 三元运算符: (条件) ? 真值 : 假值
        // 如果剩余数据>64KB,读取64KB; 否则读取剩余全部数据
        size_t chunk_size = (bytes_to_read > AUDIO_BUFFER_SIZE) ? AUDIO_BUFFER_SIZE : bytes_to_read;

        // fread() - 读取音频数据到缓冲区
        // 参数1: audio_buffer - 目标缓冲区地址
        // 参数2: 1 - 每个元素大小(字节)
        // 参数3: chunk_size - 读取的字节数
        // 参数4: wav_file - 源文件
        // 返回值: 实际读取的字节数
        size_t bytes_read = fread(audio_buffer, 1, chunk_size, wav_file);

        // 检查是否读到数据
        if (bytes_read == 0)
        {
            // 读取失败的原因:
            // 1. 已到文件末尾 (正常情况)
            // 2. 读取错误 (文件损坏/存储故障)
            ESP_LOGI(TAG, "文件读取完成");
            break; // 跳出循环,结束播放
        }

        /* --- 11.2.2 将PCM数据写入I2S播放 --- */
        // esp_codec_dev_write() - ESP Codec设备写入函数
        // 参数1: playback - 播放设备句柄(ES8311)
        // 参数2: audio_buffer - PCM数据缓冲区
        // 参数3: bytes_read - 要写入的字节数
        // 返回值: 0=成功(ESP_CODEC_DEV_OK), 负数=错误码
        //
        // 内部流程:
        // audio_buffer → I2S DMA → ES8311 DAC → 模拟信号 → PA → 扬声器
        int written = esp_codec_dev_write(playback, audio_buffer, bytes_read);

        /* --- 11.2.3 处理写入结果 --- */
        if (written == 0) // ESP_CODEC_DEV_OK = 0 表示成功
        {
            // 更新统计信息
            total_played += bytes_read;  // 累加已播放字节数
            bytes_to_read -= bytes_read; // 减少剩余字节数

            /* --- 11.2.4 打印播放进度 --- */
            // 每播放5秒打印一次进度
            // 计算: 48000Hz × 2声道 × 2字节(16bit) × 5秒 = 960000字节
            // 使用取模运算: total_played % 960000 == 0
            // 含义: 当total_played是960000的整数倍时,说明又播放了5秒
            if (total_played % (48000 * 2 * 2 * 5) == 0)
            {
                // %.0f - 浮点数格式化,0位小数 (显示整数百分比)
                ESP_LOGI(TAG, "播放进度: %.0f%%", (total_played * 100.0) / wav_header.data_size);
                // 例如: "播放进度: 25%"
            }
        }
        else
        {
            // 写入失败,记录错误信息
            // written是负数错误码,例如:
            // -1: 通用错误
            // -2: 超时
            // -3: 设备未就绪
            ESP_LOGE(TAG, "第%d次写入失败,错误码=%d", iteration, written);
            break; // 停止播放
        }

        /* --- 11.2.5 任务调度 --- */
        iteration++; // 循环计数+1

        // 每1000次循环主动让出CPU
        // 为什么需要?
        // 1. 这个while循环会持续数分钟(播放完整首歌)
        // 2. FreeRTOS是协作式调度,需要主动让出CPU
        // 3. 不让出会导致其他任务(如WiFi、UI)饿死
        if (iteration % 1000 == 0)
        {
            // vTaskDelay(1 tick) - 让出CPU至少1个系统时钟节拍
            // 1 tick ≈ 10ms (取决于FreeRTOS配置)
            // 效果: 让调度器切换到其他任务
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    ESP_LOGI(TAG, "播放完成");

    /* ========== 步骤12: 清理资源 ========== */
    // 播放结束后必须释放资源,否则会内存泄漏

    // 12.1 释放音频缓冲区
    // free() - 释放之前malloc()或heap_caps_malloc()分配的内存
    // 作用: 将64KB内存归还给系统
    free(audio_buffer);

    // 12.2 关闭文件
    // fclose() - 关闭文件,释放文件句柄
    // 作用:
    // 1. 刷新文件缓冲区(写入未保存数据,但这里是只读)
    // 2. 释放系统文件描述符
    // 3. 释放VFS层资源
    fclose(wav_file);

    // 12.3 关闭功放
    // audio_codec_set_pa_enable(false) - 禁用GPIO46功放控制
    // 作用:
    // 1. 省电(功放是主要耗电元件)
    // 2. 避免底噪(播放停止后DAC可能有微弱信号)
    // 3. 延长硬件寿命
    audio_codec_set_pa_enable(false);

    // 注意: 这里没有禁用DAC静音,因为:
    // 1. 功放已关闭,即使DAC输出也没有声音
    // 2. 保持DAC工作状态,下次播放可以更快启动
}