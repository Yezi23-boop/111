// 触摸屏驱动头文件
#include "touch_ft5x06.h"          // FT5x06/FT3168触摸控制器接口定义
#include "i2c_manager.h"           // I2C总线管理器
#include "esp_log.h"               // ESP-IDF日志系统
#include "esp_check.h"             // ESP错误检查宏
#include "driver/gpio.h"           // GPIO驱动
#include "driver/i2c_master.h"     // I2C主机驱动(新API)
#include "co5300_panel_defaults.h" // 显示屏分辨率定义
#include "freertos/FreeRTOS.h"     // FreeRTOS实时操作系统
#include "freertos/task.h"         // FreeRTOS任务管理

static const char *TAG = "touch_ft5x06"; // 日志标签

// FT5x06/FT3168寄存器和常量定义(两者寄存器兼容)
#define FT5X06_ADDR 0x38            // FT5x06/FT3168的I2C地址(7位)
#define FT5X06_REG_NUM_TOUCHES 0x02 // 触摸点数量寄存器地址
#define FT5X06_REG_TOUCH1_XH 0x03   // 第一个触摸点X坐标高字节寄存器
#define FT5X06_MAX_TOUCHES 5        // 最大支持触摸点数(FT3168支持10点,此处读取1点)

// 注意: FT3168事件类型检测有限,主要通过INT引脚和触摸点数量判断
// FT5x06的详细事件标志(bit7-6)在FT3168上可能不可靠,故不使用

// 触摸点数据结构
typedef struct
{
    uint16_t x;    // X坐标
    uint16_t y;    // Y坐标
    uint8_t event; // 触摸事件类型
    uint8_t id;    // 触摸点ID
} touch_point_t;

// FT5x06设备控制结构体
typedef struct
{
    i2c_master_dev_handle_t i2c_dev;          // I2C设备句柄
    int rst_gpio;                             // 复位引脚编号
    int int_gpio;                             // 中断引脚编号
    uint16_t max_x;                           // X轴最大坐标
    uint16_t max_y;                           // Y轴最大坐标
    uint8_t point_num;                        // 当前触摸点数量
    touch_point_t points[FT5X06_MAX_TOUCHES]; // 触摸点数组
} touch_ft5x06_t;

// 全局静态变量
static touch_ft5x06_t *s_touch = NULL; // 触摸控制器实例指针

/**
 * @brief 从FT5x06读取寄存器数据
 * @param touch 触摸控制器结构体指针
 * @param reg 寄存器地址
 * @param data 读取数据缓冲区
 * @param len 读取数据长度
 * @return ESP_OK:成功, 其他:失败
 */
static esp_err_t touch_ft5x06_i2c_read(touch_ft5x06_t *touch, uint8_t reg, uint8_t *data, size_t len)
{
    // 使用I2C master API先发送寄存器地址,再接收数据
    // 超时从100ms增加到500ms,适应100kHz低速和总线繁忙情况
    return i2c_master_transmit_receive(touch->i2c_dev, &reg, 1, data, len, 500);
}

/**
 * @brief 复位FT5x06触摸控制器
 * @param touch 触摸控制器结构体指针
 * @return ESP_OK:成功
 */
static esp_err_t touch_ft5x06_reset(touch_ft5x06_t *touch)
{
    if (touch->rst_gpio >= 0)
    {                                       // 检查复位引脚是否有效
        gpio_set_level(touch->rst_gpio, 0); // 拉低复位引脚
        vTaskDelay(pdMS_TO_TICKS(10));      // 延时10ms
        gpio_set_level(touch->rst_gpio, 1); // 拉高复位引脚
        vTaskDelay(pdMS_TO_TICKS(200));     // 延时200ms等待芯片启动
    }
    return ESP_OK; // 返回成功
}

/**
 * @brief 初始化FT5x06触摸控制器
 * @return ESP_OK:成功, 其他:失败
 */
esp_err_t touch_ft5x06_init(void)
{
    esp_err_t ret; // 错误码变量

    if (s_touch)
    {                  // 如果已初始化
        return ESP_OK; // 直接返回成功
    }

    // 初始化I2C总线管理器(多次调用安全)
    ESP_RETURN_ON_ERROR(i2c_manager_init(), TAG, "i2c manager init failed");

    // 获取共享的I2C总线句柄
    i2c_master_bus_handle_t i2c_bus = i2c_manager_get_bus();
    if (!i2c_bus)
    {
        ESP_LOGE(TAG, "Failed to get I2C bus from manager");
        return ESP_FAIL;
    }

    // 分配触摸控制器结构体内存(并清零)
    s_touch = calloc(1, sizeof(touch_ft5x06_t));
    ESP_RETURN_ON_FALSE(s_touch, ESP_ERR_NO_MEM, TAG, "alloc touch failed");

    // 配置复位引脚
    s_touch->rst_gpio = TOUCH_FT5X06_RST_GPIO; // 设置复位引脚号(GPIO9)
    if (s_touch->rst_gpio >= 0)
    { // 如果引脚有效
        gpio_config_t rst_cfg = {
            .mode = GPIO_MODE_OUTPUT,                 // 输出模式
            .pin_bit_mask = BIT64(s_touch->rst_gpio), // 引脚位掩码
            .pull_up_en = GPIO_PULLUP_DISABLE,        // 禁用上拉
            .pull_down_en = GPIO_PULLDOWN_DISABLE,    // 禁用下拉
        };
        ESP_GOTO_ON_ERROR(gpio_config(&rst_cfg), err, TAG, "RST GPIO config failed");
    }

    // 注意: INT引脚(GPIO38)未配置,当前使用轮询模式读取触摸数据
    // 如需中断驱动模式,可配置INT为下降沿中断并添加ISR处理

    // 添加I2C设备到总线
    i2c_device_config_t dev_cfg =
        {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, // 7位地址
            .device_address = FT5X06_ADDR,         // FT5x06设备地址(0x38)
            .scl_speed_hz = TOUCH_FT5X06_I2C_HZ,   // I2C时钟频率400kHz
        };
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_touch->i2c_dev), err, TAG, "add i2c device failed");

    // 设置触摸屏分辨率(从显示屏配置获取)
    s_touch->max_x = CO5300_PANEL_H_RES; // 水平分辨率
    s_touch->max_y = CO5300_PANEL_V_RES; // 垂直分辨率

    // 复位芯片
    ESP_GOTO_ON_ERROR(touch_ft5x06_reset(s_touch), err, TAG, "reset failed");

    ESP_LOGI(TAG, "FT5x06/FT3168 initialized successfully"); // 记录初始化成功日志
    return ESP_OK;                                           // 返回成功

err: // 错误处理标签
    if (s_touch)
    { // 如果已分配内存
        if (s_touch->i2c_dev)
        {                                               // 如果I2C设备已添加
            i2c_master_bus_rm_device(s_touch->i2c_dev); // 从总线移除设备
        }
        free(s_touch);  // 释放内存
        s_touch = NULL; // 清空指针
    }
    return ret; // 返回错误码
}

/**
 * @brief 读取触摸点坐标
 * @param x 输出X坐标数组
 * @param y 输出Y坐标数组
 * @param num_points 输出触摸点数量
 * @param max_points 最大读取点数
 * @return ESP_OK:成功, 其他:失败
 */
esp_err_t touch_ft5x06_read_points(uint16_t *x, uint16_t *y, uint8_t *num_points, uint8_t max_points)
{
    // 检查是否已初始化
    ESP_RETURN_ON_FALSE(s_touch, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    // 检查参数是否有效
    ESP_RETURN_ON_FALSE(x && y && num_points, ESP_ERR_INVALID_ARG, TAG, "invalid args");

    uint8_t data[4]; // 数据缓冲区(存储触摸点数据)

    // 读取触摸点数量寄存器,如果失败则静默返回无触摸(避免频繁报错)
    esp_err_t ret = touch_ft5x06_i2c_read(s_touch, FT5X06_REG_NUM_TOUCHES, data, 1);
    if (ret != ESP_OK)
    {
        *num_points = 0; // I2C忙碌或超时,返回无触摸
        return ESP_OK;   // 不报错,避免日志刷屏
    }

    uint8_t point_count = data[0] & 0x0F; // 取低4位作为触摸点数量
    if (point_count == 0 || point_count > FT5X06_MAX_TOUCHES)
    {                    // 如果无触摸或超过最大值
        *num_points = 0; // 触摸点数量设为0
        return ESP_OK;   // 返回成功
    }

    // 读取第一个触摸点的坐标数据(4字节:X高字节,X低字节,Y高字节,Y低字节)
    ret = touch_ft5x06_i2c_read(s_touch, FT5X06_REG_TOUCH1_XH, data, 4);
    if (ret != ESP_OK)
    {
        *num_points = 0; // 读取失败,返回无触摸
        return ESP_OK;   // 静默处理
    }

    // 提取X坐标(bit3-0为高4位,data[1]为低8位,共12位)
    x[0] = ((data[0] & 0x0F) << 8) | data[1];
    // 提取Y坐标(bit3-0为高4位,data[3]为低8位,共12位)
    y[0] = ((data[2] & 0x0F) << 8) | data[3];
    // 返回实际触摸点数(不超过调用者要求的最大值)
    *num_points = (point_count > max_points) ? max_points : point_count;

    return ESP_OK; // 返回成功
}

/**
 * @brief 获取触摸控制器句柄
 * @param out_handle 输出句柄指针
 * @return ESP_OK:成功, 其他:失败
 */
esp_err_t touch_ft5x06_get_handle(void **out_handle)
{
    // 检查输出指针是否有效
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "invalid arg");
    // 检查是否已初始化
    ESP_RETURN_ON_FALSE(s_touch, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    *out_handle = s_touch; // 返回触摸控制器句柄
    return ESP_OK;         // 返回成功
}
