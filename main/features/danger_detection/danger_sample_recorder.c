/**
 * @file danger_sample_recorder.c
 * @brief 危险样本录制器实现。
 *
 * 通过 PCM tap 回调机制捕获音频数据，维护环形缓冲区，
 * 在 capture event 触发时将缓冲区内容通过队列发送到独立写入任务。
 *
 * 文件命名规则：/{date}/{time}_{label}_{confidence}.pcm
 * 元数据文件：/{date}/{time}_{label}_{confidence}.meta
 *
 * 使用 FreeRTOS 互斥锁保护环形缓冲区，使用队列分离 SD 卡 I/O。
 * 写入任务在独立线程中执行，避免阻塞推理循环。
 */

#include "danger_sample_recorder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_manager.h"

static const char *TAG = "danger_recorder";

/** 默认配置。 */
static const uint32_t kDefaultBufferDurationMs = 5000U;  /**< 默认缓冲区时长 5 秒。 */
static const uint32_t kDefaultSampleRateHz = 16000U;     /**< 默认采样率 16kHz。 */
static const char *kDefaultOutputDir = "/sdcard/danger_samples";
static const uint32_t kWriteTaskStackSize = 8192U;       /**< 写入任务栈大小。 */
static const UBaseType_t kWriteTaskPriority = 3U;         /**< 写入任务优先级。 */
static const UBaseType_t kWriteQueueLength = 4U;          /**< 写入队列长度。 */

/** 环形缓冲区结构。 */
typedef struct {
    int16_t *data;           /**< 缓冲区数据。 */
    size_t capacity;         /**< 缓冲区容量（样本数）。 */
    size_t write_pos;        /**< 写入位置。 */
    size_t available;        /**< 可用样本数。 */
} ring_buffer_t;

/** 写入请求结构。 pcm_data == NULL 表示退出哨兵。 */
typedef struct {
    uint32_t label_index;        /**< 识别标签索引。 */
    float confidence;            /**< 识别置信度。 */
    int16_t *pcm_data;           /**< PCM 数据缓冲区（由写入任务释放），NULL = 哨兵。 */
    size_t samples;              /**< 样本数。 */
    uint64_t start_sample;       /**< 起始样本索引。 */
    uint32_t generation;         /**< 创建时的 runtime_generation。 */
} write_request_t;

/** 录制器状态。 */
typedef struct {
    ring_buffer_t ring_buffer;           /**< 环形缓冲区。 */
    SemaphoreHandle_t mutex;             /**< 互斥锁保护环形缓冲区。 */
    QueueHandle_t write_queue;           /**< 写入队列。 */
    TaskHandle_t write_task_handle;      /**< 写入任务句柄。 */
    uint32_t sample_rate_hz;             /**< 采样率。 */
    char output_dir[256];                /**< 输出目录。 */
    bool is_initialized;                 /**< 是否已初始化。 */
    bool is_recording;                   /**< 是否正在录制。 */
    uint64_t last_window_start_sample;   /**< 上一个窗口起始样本索引。 */
    uint32_t runtime_generation;         /**< 运行时代次，stop 时递增使旧事件失效。 */
} recorder_state_t;

/** 全局录制器状态。 */
static recorder_state_t s_recorder = {
    .ring_buffer = {NULL, 0U, 0U, 0U},
    .mutex = NULL,
    .write_queue = NULL,
    .write_task_handle = NULL,
    .sample_rate_hz = kDefaultSampleRateHz,
    .output_dir = "",
    .is_initialized = false,
    .is_recording = false,
    .last_window_start_sample = 0U,
    .runtime_generation = 0U,
};

/**
 * @brief 初始化环形缓冲区。
 *
 * @param[in] rb 环形缓冲区指针。
 * @param[in] capacity 缓冲区容量（样本数）。
 * @return ESP_OK 表示初始化成功。
 */
static esp_err_t ring_buffer_init(ring_buffer_t *rb, size_t capacity)
{
    if (rb == NULL || capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    rb->data = (int16_t *)heap_caps_malloc(capacity * sizeof(int16_t),
                                           MALLOC_CAP_SPIRAM);
    if (rb->data == NULL) {
        ESP_LOGE(TAG, "环形缓冲区内存分配失败: %u 样本",
                 (unsigned)capacity);
        return ESP_ERR_NO_MEM;
    }

    rb->capacity = capacity;
    rb->write_pos = 0U;
    rb->available = 0U;
    return ESP_OK;
}

/**
 * @brief 释放环形缓冲区。
 *
 * @param[in] rb 环形缓冲区指针。
 */
static void ring_buffer_deinit(ring_buffer_t *rb)
{
    if (rb != NULL && rb->data != NULL) {
        heap_caps_free(rb->data);
        rb->data = NULL;
        rb->capacity = 0U;
        rb->write_pos = 0U;
        rb->available = 0U;
    }
}

/**
 * @brief 向环形缓冲区写入数据。
 *
 * @param[in] rb 环形缓冲区指针。
 * @param[in] data 要写入的数据。
 * @param[in] samples 数据样本数。
 */
static void ring_buffer_write(ring_buffer_t *rb, const int16_t *data,
                              size_t samples)
{
    if (rb == NULL || rb->data == NULL || data == NULL || samples == 0U) {
        return;
    }

    for (size_t i = 0U; i < samples; ++i) {
        rb->data[rb->write_pos] = data[i];
        rb->write_pos = (rb->write_pos + 1U) % rb->capacity;
        if (rb->available < rb->capacity) {
            rb->available++;
        }
    }
}

/**
 * @brief 从环形缓冲区读取数据（按时间顺序）。
 *
 * @param[in] rb 环形缓冲区指针。
 * @param[out] output 输出缓冲区。
 * @param[in] max_samples 最大读取样本数。
 * @return 实际读取的样本数。
 */
static size_t ring_buffer_read_ordered(const ring_buffer_t *rb,
                                       int16_t *output,
                                       size_t max_samples)
{
    if (rb == NULL || rb->data == NULL || output == NULL || max_samples == 0U) {
        return 0U;
    }

    const size_t to_read = (rb->available < max_samples) ? rb->available
                                                          : max_samples;
    if (to_read == 0U) {
        return 0U;
    }

    /* 计算起始读取位置（最旧的数据）。 */
    size_t read_pos;
    if (rb->available < rb->capacity) {
        /* 缓冲区未满，从 0 开始。 */
        read_pos = 0U;
    } else {
        /* 缓冲区已满，从 write_pos 开始（最旧的数据）。 */
        read_pos = rb->write_pos;
    }

    /* 读取数据。 */
    for (size_t i = 0U; i < to_read; ++i) {
        output[i] = rb->data[(read_pos + i) % rb->capacity];
    }

    return to_read;
}

/**
 * @brief PCM tap 回调函数。
 *
 * 在每次推理窗口准备完成时调用，将 PCM 数据写入环形缓冲区。
 * 此函数在推理任务上下文中执行，需尽快返回。
 *
 * @param[in] pcm_data 窗口 PCM 数据（int16_t 格式，16kHz 单声道）。
 * @param[in] samples PCM 数据样本数。
 * @param[in] meta 窗口元数据（绝对样本索引等）。
 * @param[in] user_data 用户数据指针（未使用）。
 */
void pcm_tap_callback(const int16_t *pcm_data, size_t samples,
                      const danger_sample_pcm_window_meta_t *meta,
                      void *user_data)
{
    (void)user_data;

    if (!s_recorder.is_initialized || pcm_data == NULL || samples == 0U ||
        meta == NULL) {
        return;
    }

    /* 使用互斥锁保护环形缓冲区写入，超时 10ms 避免阻塞推理循环。 */
    if (xSemaphoreTake(s_recorder.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        ring_buffer_write(&s_recorder.ring_buffer, pcm_data, samples);
        s_recorder.last_window_start_sample = meta->absolute_sample_index;
        xSemaphoreGive(s_recorder.mutex);
    }
}

/**
 * @brief 生成文件名。
 *
 * 格式：{output_dir}/{date}/{time}_{label}_{confidence}.wav
 *
 * @param[in] label_index 识别标签索引。
 * @param[in] confidence 识别置信度。
 * @param[out] filename 输出文件名缓冲区。
 * @param[in] filename_size 缓冲区大小。
 * @return ESP_OK 表示生成成功。
 */
static esp_err_t generate_filename(uint32_t label_index, float confidence,
                                   char *filename, size_t filename_size)
{
    if (filename == NULL || filename_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 获取当前时间。 */
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    /* 生成日期目录。 */
    char date_dir[16];
    strftime(date_dir, sizeof(date_dir), "%Y%m%d", &timeinfo);

    /* 生成时间戳。 */
    char timestamp[16];
    strftime(timestamp, sizeof(timestamp), "%H%M%S", &timeinfo);

    /* 格式化置信度（保留 2 位小数）。 */
    int confidence_int = (int)(confidence * 100.0f);
    if (confidence_int > 99) {
        confidence_int = 99;
    } else if (confidence_int < 0) {
        confidence_int = 0;
    }

    /* 生成完整文件名（WAV 格式）。 */
    int ret = snprintf(filename, filename_size, "%s/%s/%s_%u_%02d.wav",
                       s_recorder.output_dir, date_dir, timestamp,
                       (unsigned)label_index, confidence_int);
    if (ret < 0 || (size_t)ret >= filename_size) {
        ESP_LOGE(TAG, "文件名生成失败");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/**
 * @brief 确保目录存在。
 *
 * @param[in] dir_path 目录路径。
 * @return ESP_OK 表示目录存在或创建成功。
 */
static esp_err_t ensure_directory_exists(const char *dir_path)
{
    if (dir_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 检查目录是否存在。 */
    struct stat st;
    if (stat(dir_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return ESP_OK;
    }

    /* 创建目录。 */
    esp_err_t ret = sd_manager_create_dir(dir_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建目录失败: %s", dir_path);
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief 创建日期子目录。
 *
 * @param[in] date_dir 日期目录名（如 "20260704"）。
 * @return ESP_OK 表示创建成功。
 */
static esp_err_t create_date_directory(const char *date_dir)
{
    if (date_dir == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[280];
    int ret = snprintf(full_path, sizeof(full_path), "%s/%s",
                       s_recorder.output_dir, date_dir);
    if (ret < 0 || (size_t)ret >= sizeof(full_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ensure_directory_exists(full_path);
}

/** WAV 文件头结构（44 字节）。 */
typedef struct __attribute__((packed)) {
    char riff_tag[4];           /* "RIFF" */
    uint32_t file_size;         /* 文件总大小 - 8 */
    char wave_tag[4];           /* "WAVE" */
    char fmt_tag[4];            /* "fmt " */
    uint32_t fmt_size;          /* fmt 块大小 = 16 */
    uint16_t audio_format;      /* PCM = 1 */
    uint16_t num_channels;      /* mono = 1 */
    uint32_t sample_rate;       /* 16000 */
    uint32_t byte_rate;         /* sample_rate * channels * bits/8 */
    uint16_t block_align;       /* channels * bits/8 */
    uint16_t bits_per_sample;   /* 16 */
    char data_tag[4];           /* "data" */
    uint32_t data_size;         /* PCM 数据字节数 */
} wav_header_t;

/**
 * @brief 写入 WAV 文件到 SD 卡。
 *
 * @param[in] filepath 文件路径（最终路径，非 .tmp）。
 * @param[in] data PCM 数据。
 * @param[in] samples 样本数。
 * @return ESP_OK 表示写入成功。
 */
static esp_err_t write_wav_file(const char *filepath, const int16_t *data,
                                size_t samples)
{
    if (filepath == NULL || data == NULL || samples == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t data_bytes = (uint32_t)(samples * sizeof(int16_t));

    /* 构造 WAV 头。 */
    wav_header_t header = {
        .riff_tag = {'R', 'I', 'F', 'F'},
        .file_size = data_bytes + sizeof(wav_header_t) - 8U,
        .wave_tag = {'W', 'A', 'V', 'E'},
        .fmt_tag = {'f', 'm', 't', ' '},
        .fmt_size = 16U,
        .audio_format = 1U,  /* PCM */
        .num_channels = 1U,
        .sample_rate = s_recorder.sample_rate_hz,
        .byte_rate = s_recorder.sample_rate_hz * 1U * sizeof(int16_t),
        .block_align = 1U * sizeof(int16_t),
        .bits_per_sample = 16U,
        .data_tag = {'d', 'a', 't', 'a'},
        .data_size = data_bytes,
    };

    /* 写入 .tmp 文件。 */
    size_t tmp_len = strlen(filepath) + 5U;
    char *tmp_path = (char *)heap_caps_malloc(tmp_len, MALLOC_CAP_SPIRAM);
    if (tmp_path == NULL) {
        ESP_LOGE(TAG, "分配 .tmp 路径失败");
        return ESP_ERR_NO_MEM;
    }
    snprintf(tmp_path, tmp_len, "%s.tmp", filepath);

    /* 使用 VFS 直接写入 header + data。 */
    FILE *f = fopen(tmp_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "打开 .tmp 文件失败: %s", tmp_path);
        heap_caps_free(tmp_path);
        return ESP_FAIL;
    }
    size_t written = fwrite(&header, 1, sizeof(header), f);
    if (written != sizeof(header)) {
        ESP_LOGE(TAG, "写入 WAV header 失败");
        fclose(f);
        unlink(tmp_path);
        heap_caps_free(tmp_path);
        return ESP_FAIL;
    }
    written = fwrite(data, 1, data_bytes, f);
    fclose(f);
    if (written != data_bytes) {
        ESP_LOGE(TAG, "写入 PCM 数据失败");
        unlink(tmp_path);
        heap_caps_free(tmp_path);
        return ESP_FAIL;
    }

    /* rename .tmp → final。 */
    esp_err_t ret = sd_manager_rename_file(tmp_path, filepath);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rename 失败: %s -> %s", tmp_path, filepath);
        unlink(tmp_path);
        heap_caps_free(tmp_path);
        return ret;
    }

    heap_caps_free(tmp_path);
    ESP_LOGI(TAG, "已写入 WAV 文件: %s (%u 样本)", filepath, (unsigned)samples);
    return ESP_OK;
}

/**
 * @brief 写入 JSON 元数据文件到 SD 卡。
 *
 * @param[in] filepath 最终文件路径（.json）。
 * @param[in] label_index 识别标签索引。
 * @param[in] confidence 识别置信度。
 * @param[in] start_sample 窗口起始样本索引。
 * @param[in] samples 样本数。
 * @param[in] generation runtime_generation。
 * @return ESP_OK 表示写入成功。
 */
static esp_err_t write_json_file(const char *filepath, uint32_t label_index,
                                 float confidence, uint64_t start_sample,
                                 size_t samples, uint32_t generation)
{
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* JSON 内容。 */
    char json_content[512];
    int ret = snprintf(json_content, sizeof(json_content),
        "{\n"
        "  \"label_index\": %u,\n"
        "  \"confidence\": %.4f,\n"
        "  \"start_sample\": %llu,\n"
        "  \"samples\": %u,\n"
        "  \"sample_rate\": %u,\n"
        "  \"channels\": 1,\n"
        "  \"bits_per_sample\": 16,\n"
        "  \"runtime_generation\": %u,\n"
        "  \"window_overlap\": true,\n"
        "  \"uploaded\": false\n"
        "}\n",
        (unsigned)label_index,
        (double)confidence,
        (unsigned long long)start_sample,
        (unsigned)samples,
        (unsigned)s_recorder.sample_rate_hz,
        (unsigned)generation);
    if (ret < 0 || (size_t)ret >= sizeof(json_content)) {
        ESP_LOGE(TAG, "JSON 内容生成失败");
        return ESP_ERR_INVALID_SIZE;
    }

    /* 写入 .tmp 文件。 */
    size_t tmp_len = strlen(filepath) + 5U;
    char *tmp_path = (char *)heap_caps_malloc(tmp_len, MALLOC_CAP_SPIRAM);
    if (tmp_path == NULL) {
        ESP_LOGE(TAG, "分配 .tmp 路径失败");
        return ESP_ERR_NO_MEM;
    }
    snprintf(tmp_path, tmp_len, "%s.tmp", filepath);

    esp_err_t err = sd_manager_write_file(tmp_path, json_content,
                                          strlen(json_content));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入 JSON .tmp 文件失败: %s", tmp_path);
        heap_caps_free(tmp_path);
        return err;
    }

    /* rename .tmp → final。 */
    err = sd_manager_rename_file(tmp_path, filepath);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rename 失败: %s -> %s", tmp_path, filepath);
        unlink(tmp_path);
        heap_caps_free(tmp_path);
        return err;
    }

    heap_caps_free(tmp_path);
    ESP_LOGI(TAG, "已写入 JSON 文件: %s", filepath);
    return ESP_OK;
}

/**
 * @brief 写入任务函数。
 *
 * 从写入队列中取出请求，执行 SD 卡文件写入操作。
 * pcm_data == NULL 表示退出哨兵。
 * 运行在独立的 FreeRTOS 任务中，避免阻塞推理循环。
 *
 * @param[in] arg 任务参数（未使用）。
 */
static void write_task(void *arg)
{
    (void)arg;

    write_request_t request;
    while (true) {
        if (xQueueReceive(s_recorder.write_queue, &request,
                          portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* 哨兵消息：退出任务。 */
        if (request.pcm_data == NULL) {
            ESP_LOGI(TAG, "写入任务收到退出哨兵，退出");
            vTaskDelete(NULL);
            return;
        }

        /* 检查 generation 是否匹配。 */
        if (request.generation != s_recorder.runtime_generation) {
            ESP_LOGW(TAG, "generation 不匹配 (%u vs %u)，丢弃",
                     (unsigned)request.generation,
                     (unsigned)s_recorder.runtime_generation);
            heap_caps_free(request.pcm_data);
            continue;
        }

        s_recorder.is_recording = true;

        /* 生成 WAV 文件名。 */
        char wav_filename[280];
        esp_err_t ret = generate_filename(request.label_index,
                                          request.confidence,
                                          wav_filename,
                                          sizeof(wav_filename));
        if (ret != ESP_OK) {
            heap_caps_free(request.pcm_data);
            s_recorder.is_recording = false;
            continue;
        }

        /* 创建日期目录。 */
        char date_dir[16];
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(date_dir, sizeof(date_dir), "%Y%m%d", &timeinfo);

        ret = create_date_directory(date_dir);
        if (ret != ESP_OK) {
            heap_caps_free(request.pcm_data);
            s_recorder.is_recording = false;
            continue;
        }

        /* 写入 WAV 文件（.tmp → rename）。 */
        ret = write_wav_file(wav_filename, request.pcm_data,
                             request.samples);
        if (ret != ESP_OK) {
            heap_caps_free(request.pcm_data);
            s_recorder.is_recording = false;
            continue;
        }

        /* 生成 JSON 文件名。 */
        char json_filename[280];
        strncpy(json_filename, wav_filename, sizeof(json_filename) - 1U);
        json_filename[sizeof(json_filename) - 1U] = '\0';
        char *dot_pos = strrchr(json_filename, '.');
        if (dot_pos != NULL) {
            strncpy(dot_pos, ".json", 6U);
        }

        /* 写入 JSON 元数据文件（.tmp → rename）。 */
        ret = write_json_file(json_filename, request.label_index,
                              request.confidence, request.start_sample,
                              request.samples, request.generation);

        /* 释放 PCM 数据缓冲区。 */
        heap_caps_free(request.pcm_data);

        s_recorder.is_recording = false;

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "样本录制完成: %s (%u 样本)",
                     wav_filename, (unsigned)request.samples);
        }
    }
}

esp_err_t danger_sample_recorder_init(
    const danger_sample_recorder_config_t *config)
{
    if (s_recorder.is_initialized) {
        ESP_LOGW(TAG, "录制器已初始化");
        return ESP_ERR_INVALID_STATE;
    }

    /* 应用配置。 */
    uint32_t buffer_duration_ms = kDefaultBufferDurationMs;
    uint32_t sample_rate_hz = kDefaultSampleRateHz;
    const char *output_dir = kDefaultOutputDir;

    if (config != NULL) {
        if (config->buffer_duration_ms > 0U) {
            buffer_duration_ms = config->buffer_duration_ms;
        }
        if (config->sample_rate_hz > 0U) {
            sample_rate_hz = config->sample_rate_hz;
        }
        if (config->output_dir != NULL) {
            output_dir = config->output_dir;
        }
    }

    /* 计算缓冲区容量。 */
    const size_t buffer_capacity =
        (buffer_duration_ms * sample_rate_hz) / 1000U;

    /* 初始化环形缓冲区。 */
    esp_err_t ret = ring_buffer_init(&s_recorder.ring_buffer, buffer_capacity);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "环形缓冲区初始化失败");
        return ret;
    }

    /* 创建互斥锁。 */
    s_recorder.mutex = xSemaphoreCreateMutex();
    if (s_recorder.mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        ring_buffer_deinit(&s_recorder.ring_buffer);
        return ESP_ERR_NO_MEM;
    }

    /* 创建写入队列。 */
    s_recorder.write_queue = xQueueCreate(kWriteQueueLength,
                                          sizeof(write_request_t));
    if (s_recorder.write_queue == NULL) {
        ESP_LOGE(TAG, "写入队列创建失败");
        vSemaphoreDelete(s_recorder.mutex);
        ring_buffer_deinit(&s_recorder.ring_buffer);
        return ESP_ERR_NO_MEM;
    }

    /* 创建写入任务。 */
    BaseType_t created = xTaskCreate(write_task,
                                     "danger_rec_wr",
                                     kWriteTaskStackSize,
                                     NULL,
                                     kWriteTaskPriority,
                                     &s_recorder.write_task_handle);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "写入任务创建失败");
        vQueueDelete(s_recorder.write_queue);
        vSemaphoreDelete(s_recorder.mutex);
        ring_buffer_deinit(&s_recorder.ring_buffer);
        return ESP_ERR_NO_MEM;
    }

    /* 保存配置。 */
    s_recorder.sample_rate_hz = sample_rate_hz;
    strncpy(s_recorder.output_dir, output_dir,
            sizeof(s_recorder.output_dir) - 1U);
    s_recorder.output_dir[sizeof(s_recorder.output_dir) - 1U] = '\0';

    /* 确保输出目录存在。 */
    ret = ensure_directory_exists(s_recorder.output_dir);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "输出目录创建失败: %s", s_recorder.output_dir);
        vTaskDelete(s_recorder.write_task_handle);
        vQueueDelete(s_recorder.write_queue);
        vSemaphoreDelete(s_recorder.mutex);
        ring_buffer_deinit(&s_recorder.ring_buffer);
        return ret;
    }

    s_recorder.is_initialized = true;
    s_recorder.is_recording = false;
    s_recorder.last_window_start_sample = 0U;

    ESP_LOGI(TAG, "危险样本录制器已初始化: buffer=%ums, rate=%uHz, dir=%s",
             (unsigned)buffer_duration_ms,
             (unsigned)sample_rate_hz,
             output_dir);

    return ESP_OK;
}

void danger_sample_recorder_deinit(void)
{
    if (!s_recorder.is_initialized) {
        return;
    }

    /* 递增 generation 使未完成事件失效。 */
    s_recorder.runtime_generation++;

    /* 等待写入队列排空。 */
    if (s_recorder.write_queue != NULL) {
        /* 等待一小段时间让写入任务完成当前工作。 */
        vTaskDelay(pdMS_TO_TICKS(100));

        /* 通过哨兵消息通知写入任务退出。 */
        write_request_t sentinel = {
            .label_index = 0U,
            .confidence = 0.0f,
            .pcm_data = NULL,
            .samples = 0U,
            .start_sample = 0U,
        };
        if (xQueueSend(s_recorder.write_queue, &sentinel, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "发送退出哨兵失败，强制删除写入任务");
        } else {
            /* 等待写入任务自行退出。 */
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        vQueueDelete(s_recorder.write_queue);
        s_recorder.write_queue = NULL;
        s_recorder.write_task_handle = NULL;
    }

    /* 释放资源。 */
    if (s_recorder.mutex != NULL) {
        vSemaphoreDelete(s_recorder.mutex);
        s_recorder.mutex = NULL;
    }

    ring_buffer_deinit(&s_recorder.ring_buffer);

    s_recorder.is_initialized = false;
    s_recorder.is_recording = false;

    ESP_LOGI(TAG, "危险样本录制器已反初始化");
}

esp_err_t danger_sample_recorder_capture(uint32_t label_index,
                                         float confidence)
{
    if (!s_recorder.is_initialized) {
        ESP_LOGE(TAG, "录制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    /* 使用互斥锁保护环形缓冲区读取。 */
    if (xSemaphoreTake(s_recorder.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "获取互斥锁超时");
        return ESP_ERR_TIMEOUT;
    }

    /* 读取环形缓冲区数据。 */
    const size_t available = s_recorder.ring_buffer.available;
    if (available == 0U) {
        xSemaphoreGive(s_recorder.mutex);
        ESP_LOGW(TAG, "缓冲区为空，跳过录制");
        return ESP_OK;
    }

    /* 记录当前 generation。 */
    const uint32_t generation = s_recorder.runtime_generation;

    /* 分配临时缓冲区（使用 PSRAM）。 */
    int16_t *temp_buffer = (int16_t *)heap_caps_malloc(
        available * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (temp_buffer == NULL) {
        xSemaphoreGive(s_recorder.mutex);
        ESP_LOGE(TAG, "临时缓冲区内存分配失败");
        return ESP_ERR_NO_MEM;
    }

    /* 读取数据。 */
    const size_t samples_read = ring_buffer_read_ordered(
        &s_recorder.ring_buffer, temp_buffer, available);

    /* 计算起始样本索引：
     * last_window_start_sample 是最新窗口的起始绝对样本索引，
     * ring buffer 中的数据从最旧到最新排列，
     * 所以最旧数据的绝对索引 = last_window_start_sample - (available - stride)。
     * 这里简化为：start_sample = last_window_start_sample - (available - samples_read)。
     */
    const uint64_t start_sample =
        s_recorder.last_window_start_sample -
        (available - samples_read);

    xSemaphoreGive(s_recorder.mutex);

    /* 构造写入请求。 */
    write_request_t request = {
        .label_index = label_index,
        .confidence = confidence,
        .pcm_data = temp_buffer,
        .samples = samples_read,
        .start_sample = start_sample,
        .generation = generation,
    };

    /* 发送到写入队列（非阻塞，队列满则丢弃）。 */
    if (xQueueSend(s_recorder.write_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "写入队列已满，丢弃本次录制");
        heap_caps_free(temp_buffer);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "录制请求已提交: %u 样本", (unsigned)samples_read);
    return ESP_OK;
}

bool danger_sample_recorder_is_recording(void)
{
    return s_recorder.is_recording;
}

danger_sample_pcm_tap_callback_t danger_sample_recorder_get_pcm_callback(void)
{
    return pcm_tap_callback;
}