// 标准库头文件
#include <string.h> // 字符串操作函数

// 自定义头文件
#include "audio_codec.h" // 音频编解码器接口定义
#include "i2c_manager.h" // I2C总线管理器
#include "esp_log.h"     // ESP-IDF日志输出
#include "driver/gpio.h" // GPIO驱动
#include "driver/i2c.h"
#include "driver/i2s_std.h"         // I2S标准驱动
#include "esp_codec_dev.h"          // ESP编解码设备高层API
#include "esp_codec_dev_defaults.h" // 编解码设备默认配置
#include "es8311_codec.h"           // ES8311编解码器驱动
#include "es7210_adc.h"             // ES7210 ADC驱动

static const char *TAG = "audio_codec"; // 日志标签

// I2S 发送和接收通道句柄
static i2s_chan_handle_t s_i2s_tx_handle = NULL; // TX:播放通道
static i2s_chan_handle_t s_i2s_rx_handle = NULL; // RX:录音通道

// Codec 底层接口句柄
static const audio_codec_if_t *s_playback_codec_if = NULL; // 播放接口(ES8311)
static const audio_codec_if_t *s_record_codec_if = NULL;   // 录音接口(ES7210)

// 数据传输接口句柄(I2S)
static const audio_codec_data_if_t *s_data_if = NULL;

// Codec 设备句柄(高层封装,提供统一API)
static esp_codec_dev_handle_t s_playback_dev = NULL; // 播放设备
static esp_codec_dev_handle_t s_record_dev = NULL;   // 录音设备

// 当前音量值(0-100)
static int s_current_volume = 60;

// I2C 设备地址定义(8位格式,包含读写位)
#define ES8311_CODEC_ADDR 0x30 // ES8311编解码器地址(7位0x18左移1位)
#define ES7210_ADC_ADDR 0x80   // ES7210 ADC地址(7位0x40左移1位)

/**
 * @brief 初始化 I2C 总线（使用共享的 i2c_manager）
 */
static esp_err_t audio_i2c_init(void)
{
    // 初始化I2C总线管理器(多次调用安全)
    return i2c_manager_init();
}

/**
 * @brief 初始化 I2S 接口（Duplex 模式，同时支持播放和录音）
 */
static esp_err_t audio_i2s_init(void)
{
    esp_err_t ret; // 错误码变量

    // 配置 I2S 双工模式(同时TX和RX,使用I2S_NUM_0作为主机)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // 启用自动清空缓冲区

    // 创建双工I2S通道(TX用于播放,RX用于录音)
    ret = i2s_new_channel(&chan_cfg, &s_i2s_tx_handle, &s_i2s_rx_handle);
    if (ret != ESP_OK)
    { // 检查通道创建是否成功
        ESP_LOGE(TAG, "Failed to create I2S duplex channel: %s", esp_err_to_name(ret));
        return ret; // 失败则返回错误码
    }

    // 统一配置 I2S 标准模式 (TX/RX 共用配置)
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_DEFAULT_SAMPLE_RATE, // 48kHz采样率
            .clk_src = I2S_CLK_SRC_DEFAULT,              // 使用默认时钟源
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,      // MCLK = 256 * 48kHz = 12.288MHz
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG( // Philips标准插槽配置
            I2S_DATA_BIT_WIDTH_16BIT,                    // 16位数据宽度
            I2S_SLOT_MODE_STEREO                         // 立体声模式
            ),
        .gpio_cfg = {
            // GPIO引脚配置
            .mclk = AUDIO_I2S_MCLK_GPIO,   // 主时钟引脚(GPIO16)
            .bclk = AUDIO_I2S_SCLK_GPIO,   // 位时钟引脚(GPIO41)
            .ws = AUDIO_I2S_LRCK_GPIO,     // 字选择/左右声道时钟(GPIO45)
            .dout = AUDIO_I2S_ASDOUT_GPIO, // TX数据输出引脚(GPIO42)
            .din = AUDIO_I2S_DSDIN_GPIO,   // RX数据输入引脚(GPIO40)
            .invert_flags = {
                // 信号反转标志
                .mclk_inv = false, // MCLK不反转
                .bclk_inv = false, // BCLK不反转
                .ws_inv = false,   // WS不反转
            },
        },
    };

    // 初始化TX通道为标准模式
    ret = i2s_channel_init_std_mode(s_i2s_tx_handle, &std_cfg);
    if (ret != ESP_OK)
    { // 检查初始化是否成功
        ESP_LOGE(TAG, "Failed to init I2S TX standard mode: %s", esp_err_to_name(ret));
        return ret; // 失败则返回错误码
    }

    // 初始化RX通道为标准模式 (复用相同配置)
    ret = i2s_channel_init_std_mode(s_i2s_rx_handle, &std_cfg);
    if (ret != ESP_OK)
    { // 检查初始化是否成功
        ESP_LOGE(TAG, "Failed to init I2S RX standard mode: %s", esp_err_to_name(ret));
        return ret; // 失败则返回错误码
    }

    ESP_LOGI(TAG, "I2S duplex interface initialized"); // 记录初始化成功日志
    return ESP_OK;                                     // 成功返回
}

/**
 * @brief 初始化功放控制引脚
 */
static esp_err_t audio_pa_init(void)
{
    // GPIO配置结构体
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << AUDIO_PA_CTRL_GPIO), // 设置功放控制引脚(GPIO46)位掩码
        .mode = GPIO_MODE_OUTPUT,                     // 配置为输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,            // 禁用内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,        // 禁用内部下拉
        .intr_type = GPIO_INTR_DISABLE,               // 禁用中断
    };

    // 应用GPIO配置
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    { // 检查配置是否成功
        ESP_LOGE(TAG, "Failed to configure PA GPIO: %s", esp_err_to_name(ret));
        return ret; // 失败则返回错误码
    }

    // 初始状态:开启功放(低电平)
    gpio_set_level(AUDIO_PA_CTRL_GPIO, 0);
    ESP_LOGI(TAG, "PA control pin initialized"); // 记录初始化成功日志
    return ESP_OK;                               // 成功返回
}

/**
 * @brief 初始化 ES8311 编解码器
 */
static esp_err_t audio_es8311_init(void)
{
    // 配置硬件增益参数
    esp_codec_dev_hw_gain_t hw_gain = {
        .pa_voltage = 5.0,        // PA供电电压(NS4150B使用5V)
        .codec_dac_voltage = 3.3, // ES8311 DAC输出电压(3.3V)
    };

    // 创建 ES8311 codec 底层接口(使用复合字面量简化代码)
    s_playback_codec_if = es8311_codec_new(&(es8311_codec_cfg_t){
        .ctrl_if = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
            .port = I2C_MANAGER_PORT,
            .addr = ES8311_CODEC_ADDR,
        }),
        .gpio_if = audio_codec_new_gpio(),         // 创建GPIO控制接口
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, // 工作模式:DAC(数模转换,播放)
        .pa_pin = AUDIO_PA_CTRL_GPIO,              // 功放控制引脚(GPIO46)
        .pa_reverted = false,                      // 功放控制不反转(高电平有效)
        .master_mode = false,                      // 从机模式(时钟由ESP32提供)
        .use_mclk = true,                          // 使用主时钟MCLK
        .digital_mic = false,                      // 不使用数字麦克风
        .invert_mclk = false,                      // MCLK不反转
        .invert_sclk = false,                      // SCLK不反转
        .hw_gain = hw_gain,                        // 硬件增益配置(匹配PA/DAC电压)

    });

    if (s_playback_codec_if == NULL)
    { // 检查codec接口创建是否成功
        ESP_LOGE(TAG, "Failed to create ES8311 codec");
        return ESP_FAIL; // 失败则返回错误
    }

    // 创建高层播放设备对象
    s_playback_dev = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .codec_if = s_playback_codec_if,    // 绑定codec接口
        .data_if = s_data_if,               // 绑定数据接口(I2S)
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, // 设备类型:输出(播放)
    });

    // 打开播放设备并配置采样参数
    if (s_playback_dev && esp_codec_dev_open(s_playback_dev, &(esp_codec_dev_sample_info_t){
                                                                 .sample_rate = AUDIO_DEFAULT_SAMPLE_RATE,         // 采样率(48kHz)
                                                                 .channel = AUDIO_DEFAULT_CHANNELS,                // 声道数(2,立体声)
                                                                 .bits_per_sample = AUDIO_DEFAULT_BITS_PER_SAMPLE, // 采样位宽(16位)
                                                             }) == ESP_CODEC_DEV_OK)
    {                                                                // 检查打开是否成功
        esp_codec_dev_set_out_vol(s_playback_dev, s_current_volume); // 设置默认音量
        ESP_LOGI(TAG, "ES8311 initialized");                         // 记录初始化成功日志
        return ESP_OK;                                               // 成功返回
    }

    ESP_LOGE(TAG, "Failed to init ES8311"); // 记录失败日志
    return ESP_FAIL;                        // 失败返回
}

/**
 * @brief 初始化 ES7210 ADC
 */
static esp_err_t audio_es7210_init(void)
{
    // 创建 ES7210 codec 底层接口
    s_record_codec_if = es7210_codec_new(&(es7210_codec_cfg_t){
        .ctrl_if = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
            .port = I2C_MANAGER_PORT,
            .addr = ES7210_ADC_ADDR,
        }),
        .master_mode = false,                              // 从机模式(时钟由ESP32提供)
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2, // 选择双麦克风输入
        .mclk_src = ES7210_MCLK_FROM_PAD,                  // MCLK来源:外部引脚(GPIO16）
    });

    if (s_record_codec_if == NULL)
    { // 检查codec接口创建是否成功
        ESP_LOGE(TAG, "Failed to create ES7210 codec");
        return ESP_FAIL; // 失败则返回错误
    }

    // 创建高层录音设备对象
    s_record_dev = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .codec_if = s_record_codec_if,     // 绑定codec接口
        .data_if = s_data_if,              // 绑定数据接口(I2S)
        .dev_type = ESP_CODEC_DEV_TYPE_IN, // 设备类型:输入(录音)
    });

    // 打开录音设备并配置采样参数
    if (s_record_dev && esp_codec_dev_open(s_record_dev, &(esp_codec_dev_sample_info_t){
                                                             .sample_rate = AUDIO_DEFAULT_SAMPLE_RATE,         // 采样率(48kHz)
                                                             .channel = AUDIO_DEFAULT_CHANNELS,                // 声道数(2,双麦克风)
                                                             .bits_per_sample = AUDIO_DEFAULT_BITS_PER_SAMPLE, // 采样位宽(16位)
                                                         }) == ESP_CODEC_DEV_OK)
    {                                                  // 检查打开是否成功
        esp_codec_dev_set_in_gain(s_record_dev, 36.0); // 设置默认增益(36dB)
        ESP_LOGI(TAG, "ES7210 initialized");           // 记录初始化成功日志
        return ESP_OK;                                 // 成功返回
    }

    ESP_LOGE(TAG, "Failed to init ES7210"); // 记录失败日志
    return ESP_FAIL;                        // 失败返回
}

esp_err_t audio_codec_init(void)
{
    esp_err_t ret; // 错误码变量

    ESP_LOGI(TAG, "Initializing audio codec..."); // 记录初始化开始日志

    // 步骤1: 初始化 I2C 总线
    ret = audio_i2c_init();
    if (ret != ESP_OK)
    {               // 检查是否成功
        return ret; // 失败则返回错误码
    }

    // 步骤2: 初始化 I2S 接口(配置双工模式:TX播放+RX录音)
    ret = audio_i2s_init();
    if (ret != ESP_OK)
    {               // 检查是否成功
        return ret; // 失败则返回错误码
    }

    // 步骤3: 启用 I2S TX通道(必须在创建数据接口前启用)
    ret = i2s_channel_enable(s_i2s_tx_handle);
    if (ret != ESP_OK)
    { // 检查是否成功
        ESP_LOGE(TAG, "Failed to enable I2S TX channel: %s", esp_err_to_name(ret));
        return ret; // 失败则返回错误码
    }

    // 步骤3.5: 启用 I2S RX通道(双工模式需要同时启用TX和RX)
    ret = i2s_channel_enable(s_i2s_rx_handle);
    if (ret != ESP_OK)
    { // 检查是否成功
        ESP_LOGE(TAG, "Failed to enable I2S RX channel: %s", esp_err_to_name(ret));
        return ret; // 失败则返回错误码
    }

    // 步骤4: 创建数据接口(将I2S通道绑定到codec设备)
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = s_i2s_rx_handle, // 绑定RX通道
        .tx_handle = s_i2s_tx_handle, // 绑定TX通道
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg); // 创建I2S数据接口
    if (s_data_if == NULL)
    { // 检查创建是否成功
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        return ESP_FAIL; // 失败则返回错误
    }

    // 步骤5: 初始化功放控制引脚(GPIO46)
    ret = audio_pa_init();
    if (ret != ESP_OK)
    {               // 检查是否成功
        return ret; // 失败则返回错误码
    }

    // 步骤6: 初始化 ES8311 编解码器(播放设备)
    ret = audio_es8311_init();
    if (ret != ESP_OK)
    {               // 检查是否成功
        return ret; // 失败则返回错误码
    }

    // 步骤7: 初始化 ES7210 ADC(录音设备)
    ret = audio_es7210_init();
    if (ret != ESP_OK)
    {               // 检查是否成功
        return ret; // 失败则返回错误码
    }

    ESP_LOGI(TAG, "Audio codec initialization complete"); // 记录初始化完成日志
    return ESP_OK;                                        // 所有步骤成功,返回OK
}

esp_err_t audio_codec_deinit(void)
{
    // 关闭并删除播放设备(如果已初始化)
    if (s_playback_dev)
    {
        esp_codec_dev_close(s_playback_dev);  // 关闭设备
        esp_codec_dev_delete(s_playback_dev); // 删除设备对象
        s_playback_dev = NULL;                // 清空句柄
    }

    // 关闭并删除录音设备(如果已初始化)
    if (s_record_dev)
    {
        esp_codec_dev_close(s_record_dev);  // 关闭设备
        esp_codec_dev_delete(s_record_dev); // 删除设备对象
        s_record_dev = NULL;                // 清空句柄
    }

    // 删除播放codec接口(如果已初始化)
    if (s_playback_codec_if)
    {
        audio_codec_delete_codec_if(s_playback_codec_if); // 删除接口对象
        s_playback_codec_if = NULL;                       // 清空句柄
    }

    // 删除录音codec接口(如果已初始化)
    if (s_record_codec_if)
    {
        audio_codec_delete_codec_if(s_record_codec_if); // 删除接口对象
        s_record_codec_if = NULL;                       // 清空句柄
    }

    // 删除数据接口(如果已初始化)
    if (s_data_if)
    {
        audio_codec_delete_data_if(s_data_if); // 删除数据接口对象
        s_data_if = NULL;                      // 清空句柄
    }

    // 禁用并删除I2S TX通道(如果已初始化)
    if (s_i2s_tx_handle)
    {
        i2s_channel_disable(s_i2s_tx_handle); // 禁用通道
        i2s_del_channel(s_i2s_tx_handle);     // 删除通道
        s_i2s_tx_handle = NULL;               // 清空句柄
    }

    // 禁用并删除I2S RX通道(如果已初始化)
    if (s_i2s_rx_handle)
    {
        i2s_channel_disable(s_i2s_rx_handle); // 禁用通道
        i2s_del_channel(s_i2s_rx_handle);     // 删除通道
        s_i2s_rx_handle = NULL;               // 清空句柄
    }

    // I2C总线由 i2c_manager 统一管理,不在此处删除

    ESP_LOGI(TAG, "Audio codec deinitialized"); // 记录反初始化完成日志
    return ESP_OK;                              // 返回成功
}

/**
 * @brief 获取播放设备句柄
 * @return 播放设备句柄(可用于直接操作codec设备)
 */
esp_codec_dev_handle_t audio_codec_get_playback_dev(void)
{
    return s_playback_dev; // 返回播放设备句柄
}

/**
 * @brief 获取录音设备句柄
 * @return 录音设备句柄(可用于直接操作codec设备)
 */
esp_codec_dev_handle_t audio_codec_get_record_dev(void)
{
    return s_record_dev; // 返回录音设备句柄
}

/**
 * @brief 设置播放音量
 * @param volume 音量值(0-100)
 * @return ESP_OK:成功, ESP_ERR_INVALID_ARG:参数无效, ESP_FAIL:设置失败
 */
esp_err_t audio_codec_set_volume(int volume)
{
    // 检查设备是否初始化且音量值是否在有效范围内
    if (!s_playback_dev || volume < 0 || volume > 100)
    {
        return ESP_ERR_INVALID_ARG; // 参数无效
    }

    // 调用底层API设置音量
    if (esp_codec_dev_set_out_vol(s_playback_dev, volume) == ESP_CODEC_DEV_OK)
    {
        s_current_volume = volume; // 更新当前音量值
        return ESP_OK;             // 设置成功
    }
    return ESP_FAIL; // 设置失败
}

/**
 * @brief 获取当前播放音量
 * @param volume 输出参数,存储当前音量值
 * @return ESP_OK:成功, ESP_ERR_INVALID_ARG:参数无效
 */
esp_err_t audio_codec_get_volume(int *volume)
{
    // 检查设备是否初始化且输出指针是否有效
    if (!s_playback_dev || !volume)
    {
        return ESP_ERR_INVALID_ARG; // 参数无效
    }
    *volume = s_current_volume; // 返回当前音量值
    return ESP_OK;              // 获取成功
}

/**
 * @brief 设置静音状态
 * @param enable true:静音, false:取消静音
 * @return ESP_OK:成功, ESP_ERR_INVALID_STATE:设备未初始化, ESP_FAIL:设置失败
 */
esp_err_t audio_codec_set_mute(bool enable)
{
    // 检查播放设备是否已初始化
    if (!s_playback_dev)
    {
        return ESP_ERR_INVALID_STATE; // 设备未初始化
    }
    // 调用底层API设置静音状态并返回结果
    return (esp_codec_dev_set_out_mute(s_playback_dev, enable) == ESP_CODEC_DEV_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 控制功放使能
 * @param enable true:开启功放, false:关闭功放
 * @return ESP_OK:成功
 */
esp_err_t audio_codec_set_pa_enable(bool enable)
{
    gpio_set_level(AUDIO_PA_CTRL_GPIO, enable ? 1 : 0); // 设置PA控制引脚电平
    return ESP_OK;                                      // 总是返回成功
}

esp_err_t audio_codec_set_record_gain(float db)
{
    if (!s_record_dev)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return (esp_codec_dev_set_in_gain(s_record_dev, db) == ESP_CODEC_DEV_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_codec_set_record_channel_gain(uint16_t channel_mask, float db)
{
    if (!s_record_dev)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return (esp_codec_dev_set_in_channel_gain(s_record_dev, channel_mask, db) == ESP_CODEC_DEV_OK) ? ESP_OK : ESP_FAIL;
}
