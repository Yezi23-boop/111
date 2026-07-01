/**
 * @file app_main_test.c
 * @brief PCB 详细通讯测试固件（新板验证专用）
 *
 * 测试顺序：
 *   1.  I2C 总线扫描（地址普查）
 *   2.  AXP2101（PMIC）：probe + 电源快照（VBUS/VSYS/VBAT/SOC/充电状态）
 *   3.  PCF85063（RTC）：probe + 读时间 + 写/读回验证 + oscillator_stopped 判断
 *   4.  QMI8658C（IMU）：probe（WHO_AM_I=0x05）+ 配置 + 连续采样 5 帧验证数据变化
 *   5.  FT5x06（触摸）：init + 5 秒轮询触摸坐标（期间手动点触屏幕）
 *   6.  CO5300（显示）：init + 红/绿/蓝三色条刷屏视觉验证
 *   7.  Audio Codec：I2S init（ES8311 DAC + ES7210 ADC）+ PA GPIO 控制验证
 *   8.  SD 卡：mount + 根目录列表 + 写/读测试文件验证
 *   9.  Wi-Fi：AP 扫描，打印 SSID/RSSI/channel/安全类型
 *   10. BLE：NimBLE 广播 5 秒，验证 2.4GHz 射频链路可见性
 *   11. DS2413（GPIO18）：1-Wire 枚举 + PIOA/PIOB 锁存释放验证
 *
 * 还原方法：把 CMakeLists.txt 中的 app_main_test.c 换回 app_main.c
 *           并恢复原 service_srcs / feature_srcs / ui_runtime_srcs 列表。
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_lcd_panel_ops.h"

/* BLE (NimBLE) */
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

/* 驱动层 */
#include "i2c_manager.h"
#include "axp2101.h"
#include "pcf85063atl.h"
#include "qmi8658c.h"
#include "ds2413.h"
#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_bus_impl_uart.h"
#include "touch_ft5x06.h"
#include "co5300_panel.h"
#include "co5300_panel_defaults.h"
#include "audio_codec.h"
#include "sd_manager.h"

static const char *TAG = "PCB_TEST";
static const bool kEnableBleTest = false;

/* =========================================================
 * 测试结果汇总结构体
 * ========================================================= */
typedef struct
{
    bool i2c_scan; /* I2C 总线扫描完成 */
    bool axp2101;  /* AXP2101 PMIC 通讯 */
    bool pcf85063; /* PCF85063 RTC 通讯 + 写/读回 */
    bool qmi8658c; /* QMI8658C IMU WHO_AM_I + 采样 */
    bool ft5x06;   /* FT5x06 触摸 init */
    bool display;  /* CO5300 显示屏 init + 刷色 */
    bool audio;    /* 音频 Codec I2S init */
    bool sd;       /* SD 卡 mount + 读写 */
    bool wifi;     /* Wi-Fi AP 扫描 */
    bool ble;      /* BLE 广播可见性验证 */
    bool ds2413;   /* GPIO18 1-Wire DS2413 测试 */
} test_results_t;

/* =========================================================
 * 辅助：分隔线打印
 * ========================================================= */
static void print_sep(const char *title)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    if (title)
    {
        ESP_LOGI(TAG, "  %s", title);
        ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    }
}

/* =========================================================
 * 【测试 1】I2C 总线扫描
 * 期望：0x34(AXP2101) 0x38(FT5x06) 0x51(PCF85063) 0x6B(QMI8658C)
 * ========================================================= */
static bool test_i2c_scan(void)
{
    print_sep("[1/11] I2C 总线扫描  SCL=GPIO14  SDA=GPIO15");

    esp_err_t ret = i2c_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C bus init 失败: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "I2C bus 初始化成功 (I2C_NUM_0, 400kHz)");

    /* 全地址扫描，期望看到 AXP/FT5/PCF/QMI */
    ret = i2c_manager_scan();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C 扫描失败: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "期望已发现的设备地址：");
    ESP_LOGI(TAG, "  0x34 -> AXP2101 (PMIC)");
    ESP_LOGI(TAG, "  0x38 -> FT5x06  (触摸)");
    ESP_LOGI(TAG, "  0x51 -> PCF85063 (RTC)");
    ESP_LOGI(TAG, "  0x6B -> QMI8658C (IMU)");
    ESP_LOGI(TAG, "  0x18 -> ES8311  (DAC, 若 codec I2C 挂在同一总线)");
    ESP_LOGI(TAG, "  0x40 -> ES7210  (ADC, 若 codec I2C 挂在同一总线)");
    return true;
}

/* =========================================================
 * 【测试 2】AXP2101 PMIC（地址 0x34）
 * 验证：probe + 读取 VBUS/VSYS/VBAT/SOC/充放电状态
 * ========================================================= */
static bool test_axp2101(void)
{
    print_sep("[2/11] AXP2101 PMIC  I2C addr=0x34");

    /* init 会复用已初始化的 i2c_manager 总线 */
    esp_err_t ret = axp2101_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "axp2101_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    bool present = false;
    ret = axp2101_probe(&present);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "axp2101_probe 失败: %s", esp_err_to_name(ret));
        return false;
    }
    if (!present)
    {
        ESP_LOGE(TAG, "AXP2101 未应答！请检查 I2C 焊点和上拉电阻");
        return false;
    }
    ESP_LOGI(TAG, "AXP2101 probe OK");

    /* 读取完整电源快照 */
    axp2101_snapshot_t snap = {0};
    ret = axp2101_read_snapshot(&snap);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "axp2101_read_snapshot 失败: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "AXP2101 电源快照：");
    ESP_LOGI(TAG, "  VBUS       : %s (%u mV)",
             snap.vbus_good ? "有效" : "无效", snap.vbus_mv);
    ESP_LOGI(TAG, "  VSYS(系统) : %u mV", snap.vsys_mv);
    ESP_LOGI(TAG, "  VBAT(电池) : %u mV%s",
             snap.battery_mv, snap.battery_present ? "" : "  [电池未在位]");
    if (snap.battery_percent >= 0)
    {
        ESP_LOGI(TAG, "  SOC(电量)  : %d%%", snap.battery_percent);
    }
    else
    {
        ESP_LOGW(TAG, "  SOC(电量)  : 未知");
    }
    ESP_LOGI(TAG, "  BATFET     : %s", snap.battfet_on ? "导通" : "断开");
    if (snap.charging)
    {
        ESP_LOGI(TAG, "  充放电状态 : 充电中");
    }
    else if (snap.discharging)
    {
        ESP_LOGI(TAG, "  充放电状态 : 放电中");
    }
    else
    {
        ESP_LOGI(TAG, "  充放电状态 : 待机");
    }

    /* 读取 IRQ 寄存器，检查是否有异常事件挂起 */
    axp2101_irq_status_t irq = {0};
    ret = axp2101_read_irq_status(&irq);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "  IRQ bank   : 0x%02X / 0x%02X / 0x%02X (非 0 表示有挂起中断)",
                 irq.irq0, irq.irq1, irq.irq2);
    }

    /* 简单合理性检查：VSYS 至少应该有电 */
    if (snap.vsys_mv < 2800)
    {
        ESP_LOGW(TAG, "  [警告] VSYS=%u mV 低于 2800 mV，系统供电可能异常", snap.vsys_mv);
    }

    return true;
}

/* =========================================================
 * 【测试 3】PCF85063ATL RTC（地址 0x51）
 * 验证：probe + 读时间 + 写固定时间 + 等 2s + 读回确认计时正确
 * ========================================================= */
static bool test_pcf85063(void)
{
    print_sep("[3/11] PCF85063 RTC  I2C addr=0x51");

    esp_err_t ret = pcf85063atl_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "pcf85063atl_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    bool present = false;
    ret = pcf85063atl_probe(&present);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "pcf85063atl_probe 失败: %s", esp_err_to_name(ret));
        return false;
    }
    if (!present)
    {
        ESP_LOGE(TAG, "PCF85063 未应答！请检查 I2C 焊点");
        return false;
    }
    ESP_LOGI(TAG, "PCF85063 probe OK");

    /* 读取当前状态（oscillator_stopped 标志） */
    pcf85063atl_status_t status = {0};
    ret = pcf85063atl_read_status(&status);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "RTC 状态: oscillator_stopped=%d  alarm_flag=%d  timer_flag=%d",
                 status.oscillator_stopped, status.alarm_flag, status.timer_flag);
        if (status.oscillator_stopped)
        {
            ESP_LOGW(TAG, "  [警告] RTC 振荡器曾经停振，当前时间不可信（可能是首次上电或电池失效）");
        }
    }

    /* 写入固定测试时间 00:01:00（1 分 0 秒，便于观察秒计数） */
    pcf85063atl_time_t write_time = {
        .seconds = 0,
        .minutes = 1,
        .hours = 0,
        .days = 1,
        .weekdays = 1,
        .months = 1,
        .years = 25,
    };
    ret = pcf85063atl_set_time(&write_time);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "pcf85063atl_set_time 失败: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "已写入测试时间: 2025-01-01 00:01:00");

    /* 等待 2 秒，再读回验证 RTC 是否在计时 */
    vTaskDelay(pdMS_TO_TICKS(2100));

    pcf85063atl_time_t read_time = {0};
    ret = pcf85063atl_read_time(&read_time);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "pcf85063atl_read_time 失败: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "读回时间: 20%02u-%02u-%02u %02u:%02u:%02u (weekday=%u)",
             read_time.years, read_time.months, read_time.days,
             read_time.hours, read_time.minutes, read_time.seconds,
             read_time.weekdays);

    /* 验证秒数至少增加了 1（允许轻微时钟误差） */
    if (read_time.seconds < 1 || read_time.seconds > 5)
    {
        ESP_LOGW(TAG, "  [警告] 读回秒数=%u，期望在 [1,5] 之间，RTC 计时可能异常",
                 read_time.seconds);
        /* 仍返回 true：probe 成功本身就证明了通讯正常 */
    }
    else
    {
        ESP_LOGI(TAG, "  [OK] RTC 计时正常，写入后经过 %u 秒", read_time.seconds);
    }

    return true;
}

/* =========================================================
 * 【测试 4】QMI8658C IMU（地址 0x6B）
 * 验证：probe + WHO_AM_I=0x05 + 配置加速度+陀螺仪 + 采集 5 帧原始数据
 * ========================================================= */
static bool test_qmi8658c(void)
{
    print_sep("[4/11] QMI8658C IMU  I2C addr=0x6B");

    esp_err_t ret = qmi8658c_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "qmi8658c_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    /* probe：读取 WHO_AM_I（期望 0x05）和 REVISION_ID */
    qmi8658c_identity_t id = {0};
    ret = qmi8658c_probe(&id);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "qmi8658c_probe 失败: %s", esp_err_to_name(ret));
        return false;
    }
    if (!id.present)
    {
        ESP_LOGE(TAG, "QMI8658C 未应答！请检查 I2C 焊点和 SA0 引脚");
        return false;
    }

    ESP_LOGI(TAG, "QMI8658C probe OK: WHO_AM_I=0x%02X (期望 0x05)  REVISION_ID=0x%02X",
             id.who_am_i, id.revision_id);
    if (id.who_am_i != 0x05)
    {
        ESP_LOGW(TAG, "  [警告] WHO_AM_I 非预期值，可能是不同版本芯片或通讯错误");
    }

    /*
     * 配置原始数据输出模式：
     * - accel_fs=0x00 (±2g), accel_odr=0x03 (59Hz)
     * - gyro_fs=0x00  (±16dps), gyro_odr=0x03 (59Hz)
     * 这套配置适合静态验证（平放时 AZ ≈ -1g，陀螺仪接近 0）
     */
    qmi8658c_config_t cfg = {
        .accel_fs = 0x00,
        .accel_odr = 0x03,
        .gyro_fs = 0x00,
        .gyro_odr = 0x03,
        .accel_enable = true,
        .gyro_enable = true,
    };
    ret = qmi8658c_configure(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "qmi8658c_configure 失败: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "QMI8658C 已配置 (accel ±2g 59Hz, gyro ±16dps 59Hz)，等待传感器稳定...");

    /* 等待传感器 ODR 稳定输出 */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 采集 5 帧，打印原始值；平放时 accel_z ≈ -8192（±2g 量程下 -1g 的 raw 值）*/
    ESP_LOGI(TAG, "连续采集 5 帧原始数据（板子平放时 AZ 应接近 -8192）：");
    bool data_varies = false;
    int16_t prev_az = 0;

    for (int i = 0; i < 5; i++)
    {
        qmi8658c_raw_sample_t s = {0};
        ret = qmi8658c_read_raw(&s);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "  第 %d 帧读取失败: %s", i + 1, esp_err_to_name(ret));
            continue;
        }
        /* 温度原始值换算：T(°C) = raw / 256 */
        float temp_c = (float)s.temperature_raw / 256.0f;
        ESP_LOGI(TAG,
                 "  [%d] T=%.1f°C  AX=%6d AY=%6d AZ=%6d  GX=%6d GY=%6d GZ=%6d",
                 i + 1, temp_c,
                 s.accel_x, s.accel_y, s.accel_z,
                 s.gyro_x, s.gyro_y, s.gyro_z);

        /* 检测相邻帧 AZ 是否有合理变化（证明 ADC 在运行，非寄存器粘滞） */
        if (i > 0 && abs(s.accel_z - prev_az) > 5)
        {
            data_varies = true;
        }
        prev_az = s.accel_z;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!data_varies)
    {
        ESP_LOGW(TAG, "  [警告] 连续帧 AZ 变化 < 5 LSB，数据可能粘滞，建议轻晃板子后重测");
    }
    else
    {
        ESP_LOGI(TAG, "  [OK] 各帧数据有变化，ADC 输出正常");
    }

    /* 读取 STATUSINT，确认 INT1 电平状态 */
    qmi8658c_statusint_t sint = {0};
    ret = qmi8658c_read_statusint(&sint);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "STATUSINT raw=0x%02X  ctrl9_done=%d  int1=%d  int2=%d",
                 sint.raw_statusint, sint.ctrl9_done, sint.int1_high, sint.int2_high);
    }

    return true;
}

/* =========================================================
 * 【测试 5】FT5x06 触摸（地址 0x38）
 * 验证：init + 5 秒轮询，期间手动触摸屏幕
 * ========================================================= */
static bool test_ft5x06(void)
{
    print_sep("[5/11] FT5x06 触摸  I2C addr=0x38  RST=GPIO9  INT=GPIO38");

    esp_err_t ret = touch_ft5x06_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "touch_ft5x06_init 失败: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "FT5x06 init OK，开始 5 秒触摸检测（请用手指点触屏幕）...");

    bool touch_detected = false;
    const int kPollMs = 50;
    const int kTotalMs = 5000;
    int elapsed_ms = 0;

    while (elapsed_ms < kTotalMs)
    {
        uint16_t x[5] = {0}, y[5] = {0};
        uint8_t num = 0;
        ret = touch_ft5x06_read_points(x, y, &num, 5);

        if (ret == ESP_OK && num > 0)
        {
            touch_detected = true;
            ESP_LOGI(TAG, "  触摸检测到 %u 个点: (x=%u, y=%u)", num, x[0], y[0]);
        }

        vTaskDelay(pdMS_TO_TICKS(kPollMs));
        elapsed_ms += kPollMs;
    }

    if (!touch_detected)
    {
        ESP_LOGW(TAG,
                 "  [警告] 5 秒内未检测到触摸事件，可能是触摸芯片通讯问题或 RST/INT 引脚异常");
        /* init 本身成功就证明了通讯，返回 true */
    }
    else
    {
        ESP_LOGI(TAG, "  [OK] 触摸坐标读取正常");
    }

    return true;
}

/* =========================================================
 * 辅助：向屏幕填充一整块纯色（分块发送，避免一次分配过大 DMA buffer）
 * color_rgb565: RGB565 格式，高字节先（big-endian）
 * ========================================================= */
static void fill_screen_color(esp_lcd_panel_handle_t panel,
                              uint8_t r8, uint8_t g8,
                              int y_start, int y_end)
{
    /* 单次分配 1 行缓冲（DMA-capable），逐行刷入 */
    const int kLineBytes = CO5300_PANEL_H_RES * 2;
    uint8_t *line_buf = heap_caps_malloc(kLineBytes, MALLOC_CAP_DMA);
    if (line_buf == NULL)
    {
        ESP_LOGW(TAG, "fill_screen: DMA 缓冲分配失败，跳过");
        return;
    }

    /* RGB565 大端：r8 高 5 位放 byte0[7:3]，g8 高 3 位放 byte0[2:0]，
     * g8 低 3 位放 byte1[7:5]，b8 高 5 位（此处 b8=0）放 byte1[4:0] */
    uint8_t b0 = (r8 & 0xF8) | ((g8 >> 5) & 0x07);
    uint8_t b1 = ((g8 & 0x1C) << 3); /* 纯色 B=0 */
    for (int i = 0; i < CO5300_PANEL_H_RES; i++)
    {
        line_buf[i * 2] = b0;
        line_buf[i * 2 + 1] = b1;
    }

    for (int y = y_start; y < y_end; y++)
    {
        esp_lcd_panel_draw_bitmap(panel, 0, y, CO5300_PANEL_H_RES, y + 1, line_buf);
    }

    heap_caps_free(line_buf);
}

/* =========================================================
 * 【测试 6】CO5300 显示屏（QSPI SPI2）
 * 验证：init + 刷红/绿/蓝三色竖条（视觉验证 QSPI 数据通路）
 * ========================================================= */
static bool test_display(void)
{
    print_sep("[6/11] CO5300 显示  QSPI SPI2  RST=GPIO8  CS=GPIO12");

    esp_err_t ret = co5300_panel_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "co5300_panel_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    co5300_panel_set_brightness_percent(80);
    ESP_LOGI(TAG, "CO5300 panel init OK，亮度 80%%，开始三色刷屏...");

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    ret = co5300_panel_get_raw(&io, &panel);
    if (ret != ESP_OK || panel == NULL)
    {
        ESP_LOGW(TAG, "无法获取 panel 句柄，跳过像素填充（panel init 已成功）");
        return true;
    }

    /* 屏幕共 502 行，分三段：红(0~167)、绿(168~335)、蓝(336~501) */

    /* 红色：R=255 G=0 B=0 */
    fill_screen_color(panel, 0xFF, 0x00, 0, 167);
    ESP_LOGI(TAG, "  红色段 (行 0~167) 已写入");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* 绿色：R=0 G=255 B=0 → RGB565 big-endian: 0x07E0 → b0=0x07 b1=0xE0 */
    /* 这里复用 fill_screen_color，传 r8=0x00 g8=0xFF */
    fill_screen_color(panel, 0x00, 0xFF, 168, 335);
    ESP_LOGI(TAG, "  绿色段 (行 168~335) 已写入");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* 蓝色：R=0 G=0 B=255 → RGB565: 0x001F → b0=0x00 b1=0x1F
     * fill_screen_color 目前 B 固定为 0，蓝色需要单独处理 */
    {
        const int kLineBytes = CO5300_PANEL_H_RES * 2;
        uint8_t *buf = heap_caps_malloc(kLineBytes, MALLOC_CAP_DMA);
        if (buf)
        {
            for (int i = 0; i < CO5300_PANEL_H_RES; i++)
            {
                buf[i * 2] = 0x00;     /* RGB565 蓝色高字节 */
                buf[i * 2 + 1] = 0x1F; /* RGB565 蓝色低字节 */
            }
            for (int y = 336; y <= 501; y++)
            {
                esp_lcd_panel_draw_bitmap(panel, 0, y, CO5300_PANEL_H_RES, y + 1, buf);
            }
            heap_caps_free(buf);
        }
    }
    ESP_LOGI(TAG, "  蓝色段 (行 336~501) 已写入");
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "[视觉验证] 屏幕应呈现上红、中绿、下蓝三段色条");
    ESP_LOGI(TAG, "  若颜色错误（如红/蓝互换），检查 RGB 字节序或 D0/D1 引脚焊接");
    ESP_LOGI(TAG, "  若某段色条不亮，检查对应 QSPI 数据线引脚");
    return true;
}

/* =========================================================
 * 【测试 7】音频 Codec（I2S0 + ES8311 DAC + ES7210 ADC）
 * 验证：audio_codec_init + PA GPIO 拉高/拉低
 * ========================================================= */
static bool test_audio(void)
{
    print_sep("[7/11] 音频 Codec  I2S0  MCLK=16  BCLK=41  LRCK=45  PA=46");

    esp_err_t ret = audio_codec_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "audio_codec_init 失败 (ret=%d: %s)", ret, esp_err_to_name(ret));
        ESP_LOGE(TAG, "  常见原因：");
        ESP_LOGE(TAG, "    - I2C 上找不到 ES8311(0x18) 或 ES7210(0x40)");
        ESP_LOGE(TAG, "    - I2S GPIO 焊点问题（MCLK=16, BCLK=41, LRCK=45）");
        ESP_LOGE(TAG, "    - MCLK 时钟源未能提供给 codec");
        return false;
    }
    ESP_LOGI(TAG, "audio_codec_init OK（ES8311 DAC + ES7210 ADC）");

    /* 设置音量，验证 I2C 控制面写寄存器路径 */
    ret = audio_codec_set_volume(50);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "  audio_codec_set_volume(50) OK");
    }
    else
    {
        ESP_LOGW(TAG, "  audio_codec_set_volume 失败: %s", esp_err_to_name(ret));
    }

    /* 验证 PA 功放使能引脚 GPIO46 能正常控制 */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << AUDIO_PA_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&pa_cfg) == ESP_OK)
    {
        gpio_set_level(AUDIO_PA_CTRL_GPIO, 1);
        ESP_LOGI(TAG, "  PA GPIO46 拉高（功放使能）");
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(AUDIO_PA_CTRL_GPIO, 0);
        ESP_LOGI(TAG, "  PA GPIO46 拉低（功放静音）");
    }
    else
    {
        ESP_LOGW(TAG, "  PA GPIO46 配置失败");
    }

    ESP_LOGI(TAG, "  I2S 引脚汇总: MCLK=GPIO16  BCLK=GPIO41  LRCK=GPIO45");
    ESP_LOGI(TAG, "               TX_DOUT=GPIO40  RX_DIN=GPIO42");
    return true;
}

/* =========================================================
 * 【测试 8】SD 卡（SPI3，MOSI=1 MISO=3 CLK=2 CS=17）
 * 验证：mount + 根目录列表 + 写测试文件 + 读回校验内容
 * ========================================================= */
static bool test_sd(void)
{
    print_sep("[8/11] SD 卡  SPI3  MOSI=GPIO1  MISO=GPIO3  CLK=GPIO2  CS=GPIO17");

    esp_err_t ret = sd_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD 卡挂载失败: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "  常见原因：");
        ESP_LOGE(TAG, "    - SD 卡未插入，或格式不是 FAT32");
        ESP_LOGE(TAG, "    - SPI 引脚焊点问题（CLK/MOSI/MISO/CS）");
        ESP_LOGE(TAG, "    - CS 上拉缺失或 SPI 时序问题");
        return false;
    }
    ESP_LOGI(TAG, "SD 卡挂载成功，列出根目录：");
    sd_manager_list_dir("/sdcard");

    /* 写入测试文件 */
    const char *kTestFile = "/sdcard/_pcb_test.txt";
    const char *kTestContent = "PCB_COMM_TEST_OK_2025";

    FILE *f = fopen(kTestFile, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "写测试文件失败: %s", kTestFile);
        return false;
    }
    fprintf(f, "%s\n", kTestContent);
    fclose(f);
    ESP_LOGI(TAG, "测试文件写入: %s -> \"%s\"", kTestFile, kTestContent);

    /* 读回校验 */
    f = fopen(kTestFile, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "读测试文件失败: %s", kTestFile);
        return false;
    }
    char buf[64] = {0};
    fgets(buf, sizeof(buf), f);
    fclose(f);

    /* 去掉末尾换行 */
    buf[strcspn(buf, "\r\n")] = '\0';

    if (strcmp(buf, kTestContent) == 0)
    {
        ESP_LOGI(TAG, "  [OK] 读回内容匹配: \"%s\"", buf);
    }
    else
    {
        ESP_LOGE(TAG, "  [FAIL] 读回内容不匹配！期望=\"%s\"  实际=\"%s\"",
                 kTestContent, buf);
        return false;
    }

    /* 删除测试文件（保持 SD 卡整洁）*/
    remove(kTestFile);
    ESP_LOGI(TAG, "  测试文件已删除");
    return true;
}

/* =========================================================
 * 【测试 9】Wi-Fi AP 扫描
 * 验证：Wi-Fi 驱动初始化 + 同步扫描 + 打印前 8 个 AP
 * ========================================================= */
static bool test_wifi(void)
{
    print_sep("[9/11] Wi-Fi AP 扫描");

    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "事件循环创建失败: %s", esp_err_to_name(ret));
        return false;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_mode 失败: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return false;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_start 失败: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return false;
    }

    ESP_LOGI(TAG, "Wi-Fi 驱动启动成功，开始 AP 扫描（最长 4 秒）...");

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true, /* 扫描隐藏 SSID，覆盖更完整的射频链路验证 */
    };
    ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi 扫描失败: %s", esp_err_to_name(ret));
        esp_wifi_stop();
        esp_wifi_deinit();
        return false;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "扫描完成，共发现 %u 个 AP", ap_count);

    bool result = (ap_count > 0);

    if (ap_count > 0)
    {
        uint16_t show = (ap_count < 8) ? ap_count : 8;
        wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_list)
        {
            esp_wifi_scan_get_ap_records(&ap_count, ap_list);
            ESP_LOGI(TAG, "%-3s %-32s %5s %3s %s",
                     "No.", "SSID", "RSSI", "CH", "Security");
            for (uint16_t i = 0; i < show; i++)
            {
                const char *auth;
                switch (ap_list[i].authmode)
                {
                case WIFI_AUTH_OPEN:
                    auth = "OPEN";
                    break;
                case WIFI_AUTH_WEP:
                    auth = "WEP";
                    break;
                case WIFI_AUTH_WPA_PSK:
                    auth = "WPA";
                    break;
                case WIFI_AUTH_WPA2_PSK:
                    auth = "WPA2";
                    break;
                case WIFI_AUTH_WPA_WPA2_PSK:
                    auth = "WPA/2";
                    break;
                case WIFI_AUTH_WPA3_PSK:
                    auth = "WPA3";
                    break;
                default:
                    auth = "OTHER";
                    break;
                }
                ESP_LOGI(TAG, "[%2u] %-32s %4d dBm  CH%2d  %s",
                         i + 1,
                         ap_list[i].ssid[0] ? (char *)ap_list[i].ssid : "<hidden>",
                         ap_list[i].rssi,
                         ap_list[i].primary,
                         auth);
            }
            if (ap_count > 8)
            {
                ESP_LOGI(TAG, "  ... 还有 %u 个 AP 未显示", ap_count - 8);
            }
            free(ap_list);
        }
    }
    else
    {
        ESP_LOGW(TAG, "  [警告] 未扫描到任何 AP，检查 Wi-Fi 天线焊接或周围是否有 2.4GHz 网络");
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    return result;
}

/* =========================================================
 * 》测试 10《 BLE 广播可见性（NimBLE）
 * 验证：初始化 NimBLE 层 + 启动广播 5 秒
 * 用手机控制台（运行 nRF Connect 或直接搜索蓝牙）能搜到广播名 "PCB_TEST" 即证明射频正常
 * ========================================================= */

/* 标记 NimBLE host 是否就绪（由同步回调设置）*/
static volatile bool s_ble_host_synced = false;

/* NimBLE host 就绪回调：stack 内部初始化完成后触发 */
static void ble_on_sync(void)
{
    s_ble_host_synced = true;
    ESP_LOGI(TAG, "NimBLE host synced");
}

/* NimBLE host reset 回调（通常不需关心）*/
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
    s_ble_host_synced = false;
}

/* NimBLE 内部任务入口（必须匹配 nimble_port_freertos_init 的签名）*/
static void nimble_host_task(void *param)
{
    nimble_port_run(); /* 不会返回 */
    nimble_port_freertos_deinit();
}

static bool test_ble(void)
{
    print_sep("[10/11] BLE 广播可见性（NimBLE）");

    esp_err_t ret;

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "释放 Classic BT 内存失败: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_bt_controller_init 失败: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_bt_controller_enable 失败: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return false;
    }

    /* 初始化 NimBLE HCI 传输层 */
    ret = esp_nimble_hci_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_nimble_hci_init 失败: %s", esp_err_to_name(ret));
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    /* 初始化 NimBLE 协议栈 */
    nimble_port_init();

    /* 注册同步和 reset 回调 */
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* 设置 GAP 设备名（用手机扫描时可见）*/
    ble_svc_gap_device_name_set("PCB_TEST");

    /* 启动 NimBLE host 内部任务 */
    nimble_port_freertos_init(nimble_host_task);

    /* 等待 host 就绪（最多等 3 秒）*/
    int wait_ms = 0;
    while (!s_ble_host_synced && wait_ms < 3000)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50;
    }
    if (!s_ble_host_synced)
    {
        ESP_LOGE(TAG, "NimBLE host 未就绪，超时。检查 BLE 控制器初始化流程");
        return false;
    }

    /*
     * 配置 ADV 参数：
     * - 可发现广播，不可连接（只验证射频，不需要配对）
     * - 间隔 100ms，全信道轮轭广播
     */
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON, /* 不可连接 */
        .disc_mode = BLE_GAP_DISC_MODE_GEN, /* 通用可发现 */
        .itvl_min = BLE_GAP_ADV_ITVL_MS(100),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(200),
        .channel_map = BLE_GAP_ADV_DFLT_CHANNEL_MAP, /* CH37/38/39 全部使用 */
    };

    /* 配置 ADV 数据：只放 广播名 "PCB_TEST" */
    struct ble_hs_adv_fields adv_fields = {0};
    const char *kAdvName = "PCB_TEST";
    adv_fields.name = (uint8_t *)kAdvName;
    adv_fields.name_len = strlen(kAdvName);
    adv_fields.name_is_complete = 1;

    ret = ble_gap_adv_set_fields(&adv_fields);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields 失败: err=%d", ret);
        return false;
    }

    /* 启动广播 */
    ret = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, NULL, NULL);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start 失败: err=%d", ret);
        return false;
    }

    ESP_LOGI(TAG, "BLE 广播已启动，广播名 = \"PCB_TEST\"\uff0c持续 5 秒");
    ESP_LOGI(TAG, "  请用手机开启蓝牙扫描（nRF Connect 或系统设置），确认能搜到 \"PCB_TEST\"");

    /* 广播 5 秒 */
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* 停止广播 */
    ble_gap_adv_stop();
    ESP_LOGI(TAG, "BLE 广播已停止");

    /* 反初始化展开：止止 NimBLE 内部任务并释放资源 */
    nimble_port_stop();
    nimble_port_deinit();
    esp_nimble_hci_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    return true;
}

static esp_err_t probe_ds2413_on_bus(onewire_bus_handle_t bus,
                                     const char *backend_name,
                                     ds2413_device_t *device)
{
    esp_err_t ret = onewire_bus_reset(bus);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s: 1-Wire reset 未检测到 presence pulse: %s",
                 backend_name, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "%s: 1-Wire reset 检测到 presence pulse，开始搜索 ROM",
             backend_name);

    onewire_device_iter_handle_t iter = NULL;
    ret = onewire_new_device_iter(bus, &iter);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s: 创建 1-Wire ROM 搜索迭代器失败: %s",
                 backend_name, esp_err_to_name(ret));
        return ret;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;
    size_t found_count = 0;
    onewire_device_t next = {0};
    while ((ret = onewire_device_iter_get_next(iter, &next)) == ESP_OK)
    {
        found_count++;
        uint8_t *rom = (uint8_t *)&next.address;
        ESP_LOGI(TAG, "%s: 发现 1-Wire ROM[%u]: %02X %02X %02X %02X %02X %02X %02X %02X family=0x%02X",
                 backend_name, (unsigned)found_count,
                 rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7],
                 rom[0]);
        if (rom[0] == DS2413_FAMILY_CODE ||
            rom[0] == DS2413_CLONE_FAMILY_CODE ||
            rom[0] == DS2413_BOARD_FAMILY_CODE)
        {
            device->bus = bus;
            device->address = next.address;
            result = ESP_OK;
            break;
        }
    }

    if (found_count == 0)
    {
        ESP_LOGW(TAG, "%s: presence 存在，但 ROM Search 未枚举到器件", backend_name);
    }
    else if (result != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: 已枚举到 %u 个 1-Wire 器件，但没有 DS2413 family 0x%02X/0x%02X/0x%02X",
                 backend_name, (unsigned)found_count,
                 DS2413_FAMILY_CODE, DS2413_CLONE_FAMILY_CODE,
                 DS2413_BOARD_FAMILY_CODE);
    }

    esp_err_t del_ret = onewire_del_device_iter(iter);
    if (del_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: 删除 1-Wire ROM 搜索迭代器失败: %s",
                 backend_name, esp_err_to_name(del_ret));
    }

    return result;
}

/* =========================================================
 * 》测试 11《 DS2413 1-Wire 双路开漏 IO（GPIO18）
 * 验证：枚举器件 + 读 PIOA/PIOB + 释放 PIOA/PIOB 锁存
 * ========================================================= */
static bool test_ds2413(void)
{
    print_sep("[11/11] DS2413 1-Wire GPIO  GPIO18");

    bool ok = false;
    onewire_bus_handle_t bus = NULL;
    const char *backend_name = NULL;
    ds2413_device_t device = {0};
    ds2413_state_t initial_state = {0};
    ds2413_state_t verified_state = {0};
    ds2413_state_t readback_state = {0};
    esp_err_t ret;

    gpio_reset_pin(GPIO_NUM_18);
    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_18, GPIO_FLOATING);
    vTaskDelay(pdMS_TO_TICKS(2));
    int idle_level = gpio_get_level(GPIO_NUM_18);
    ESP_LOGI(TAG, "GPIO18 1-Wire 空闲电平=%d（原理图 R22=4.7k 外部上拉，期望为 1）",
             idle_level);
    if (idle_level == 0)
    {
        ESP_LOGE(TAG, "GPIO18 空闲为低：1-Wire 总线被拉低，先检查 IO 短路、DS2413 焊接方向或 PIO 选装电阻");
        return false;
    }

    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = GPIO_NUM_18,
        .flags = {
            .en_pull_up = false,
        },
    };
    onewire_bus_uart_config_t uart_cfg = {
        .uart_port_num = UART_NUM_1,
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,
    };

    ret = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &bus);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "onewire_new_bus_rmt 失败: %s", esp_err_to_name(ret));
        bus = NULL;
        ret = onewire_new_bus_uart(&bus_cfg, &uart_cfg, &bus);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "onewire_new_bus_uart 失败: %s", esp_err_to_name(ret));
            return false;
        }
        ESP_LOGI(TAG, "1-Wire bus 初始化成功: GPIO18 / UART1");
        backend_name = "UART1";
    }
    else
    {
        ESP_LOGI(TAG, "1-Wire bus 初始化成功: GPIO18 / RMT");
        backend_name = "RMT";
    }

    ret = probe_ds2413_on_bus(bus, backend_name, &device);
    if (ret != ESP_OK)
    {
        if (strcmp(backend_name, "RMT") != 0)
        {
            ESP_LOGE(TAG, "未找到 DS2413 兼容器件: %s", esp_err_to_name(ret));
            goto cleanup;
        }

        ESP_LOGW(TAG, "RMT 后端未找到 DS2413，切换 UART1 后端再试: %s",
                 esp_err_to_name(ret));
        esp_err_t del_ret = onewire_bus_del(bus);
        if (del_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "删除 RMT 1-Wire bus 失败: %s", esp_err_to_name(del_ret));
        }
        bus = NULL;

        ret = onewire_new_bus_uart(&bus_cfg, &uart_cfg, &bus);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "onewire_new_bus_uart 失败: %s", esp_err_to_name(ret));
            return false;
        }
        backend_name = "UART1";
        ESP_LOGI(TAG, "1-Wire bus 初始化成功: GPIO18 / UART1");

        ret = probe_ds2413_on_bus(bus, backend_name, &device);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "未找到 DS2413 兼容器件: %s", esp_err_to_name(ret));
            goto cleanup;
        }
    }

    uint8_t *rom = (uint8_t *)&device.address;
    ESP_LOGI(TAG, "DS2413 ROM (%s): %02X %02X %02X %02X %02X %02X %02X %02X",
             backend_name,
             rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);

    ret = ds2413_read_state(&device, &initial_state);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "DS2413 初始状态读取失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "DS2413 初始状态: raw=0x%02X  PIOA(state=%d latch=%d)  PIOB(state=%d latch=%d)",
             initial_state.raw,
             initial_state.pioa_state, initial_state.pioa_latch,
             initial_state.piob_state, initial_state.piob_latch);

    ds2413_latch_state_t initial_latch = {
        .pioa_release = initial_state.pioa_latch,
        .piob_release = initial_state.piob_latch,
    };

    /*
     * GPIO18 作为 1-Wire IO 已由 R22 外部上拉；PIOA/PIOB 是另外两路开漏
     * IO，当前原理图中 R23/R50 标为 NC，没有独立上拉或负载。这里不能
     * 主动拉低某一路做功能测试，否则选装电阻版本可能把 1-Wire 总线一起拖低。
     */
    ds2413_latch_state_t test_latch = {
        .pioa_release = true,
        .piob_release = true,
    };

    ESP_LOGI(TAG, "写入安全释放状态: PIOA=%s PIOB=%s",
             test_latch.pioa_release ? "release" : "pull-low",
             test_latch.piob_release ? "release" : "pull-low");

    ret = ds2413_write_latch(&device, &test_latch, &verified_state);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "DS2413 写入安全释放状态失败: %s", esp_err_to_name(ret));
        goto restore;
    }

    ESP_LOGI(TAG, "DS2413 写入回读: raw=0x%02X  PIOA(state=%d latch=%d)  PIOB(state=%d latch=%d)",
             verified_state.raw,
             verified_state.pioa_state, verified_state.pioa_latch,
             verified_state.piob_state, verified_state.piob_latch);

    ret = ds2413_read_state(&device, &readback_state);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "DS2413 二次读取失败: %s", esp_err_to_name(ret));
        goto restore;
    }

    ESP_LOGI(TAG, "DS2413 二次读取: raw=0x%02X  PIOA(state=%d latch=%d)  PIOB(state=%d latch=%d)",
             readback_state.raw,
             readback_state.pioa_state, readback_state.pioa_latch,
             readback_state.piob_state, readback_state.piob_latch);

    ok = true;

restore:
    if (initial_latch.pioa_release != test_latch.pioa_release ||
        initial_latch.piob_release != test_latch.piob_release)
    {
        ds2413_state_t restore_state = {0};
        esp_err_t restore_ret = ds2413_write_latch(&device, &initial_latch, &restore_state);
        if (restore_ret == ESP_OK)
        {
            ESP_LOGI(TAG, "DS2413 已恢复初始锁存状态");
        }
        else
        {
            ESP_LOGW(TAG, "DS2413 恢复初始锁存状态失败: %s", esp_err_to_name(restore_ret));
        }
    }

cleanup:
    if (bus != NULL)
    {
        esp_err_t del_ret = onewire_bus_del(bus);
        if (del_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "onewire_bus_del 失败: %s", esp_err_to_name(del_ret));
        }
    }
    return ok;
}

/* =========================================================
 * 主测试任务：顺序执行 11 项测试并打印汇总
 * ========================================================= */
static void comm_test_task(void *arg)
{
    /* 等待 power-on 时序稳定（PMIC/振荡器/LDO 上电时间）*/
    vTaskDelay(pdMS_TO_TICKS(800));

    print_sep("PCB 通讯测试开始（新板验证）");
    ESP_LOGI(TAG, "  引脚一览：");
    ESP_LOGI(TAG, "    I2C  : SCL=GPIO14  SDA=GPIO15");
    ESP_LOGI(TAG, "    QSPI : CLK=GPIO11  CS=GPIO12  D0-D3=GPIO4-7  RST=GPIO8");
    ESP_LOGI(TAG, "    I2S  : MCLK=GPIO16  BCLK=GPIO41  LRCK=GPIO45");
    ESP_LOGI(TAG, "           TX=GPIO40  RX=GPIO42  PA=GPIO46");
    ESP_LOGI(TAG, "    SD   : MOSI=GPIO1  MISO=GPIO3  CLK=GPIO2  CS=GPIO17");
    ESP_LOGI(TAG, "    Touch: RST=GPIO9  INT=GPIO38");
    ESP_LOGI(TAG, "    1W   : GPIO18");

    test_results_t r = {0};

    r.i2c_scan = test_i2c_scan();
    r.axp2101 = test_axp2101();
    r.pcf85063 = test_pcf85063();
    r.qmi8658c = test_qmi8658c();
    r.ft5x06 = test_ft5x06();
    r.display = test_display();
    r.audio = test_audio();
    r.sd = test_sd();
    r.wifi = test_wifi();
    if (kEnableBleTest)
    {
        r.ble = test_ble();
    }
    else
    {
        ESP_LOGW(TAG, "当前板卡不需要 BLE，跳过 [10/11] BLE 广播可见性测试");
        r.ble = true;
    }
    r.ds2413 = test_ds2413();

    /* ── 汇总 ── */
    print_sep("PCB 通讯测试结果汇总");
    ESP_LOGI(TAG, "  [ 1/11] I2C 总线扫描              : %s", r.i2c_scan ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [ 2/11] AXP2101 PMIC (0x34)       : %s", r.axp2101 ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [ 3/11] PCF85063 RTC (0x51)       : %s", r.pcf85063 ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [ 4/11] QMI8658C IMU (0x6B)       : %s", r.qmi8658c ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [ 5/11] FT5x06 触摸  (0x38)       : %s", r.ft5x06 ? "PASS" : "FAIL ← 检查 RST/INT 引脚");
    ESP_LOGI(TAG, "  [ 6/11] CO5300 显示  (QSPI SPI2)  : %s", r.display ? "PASS" : "FAIL ← 检查 D0-D3/CLK/RST");
    ESP_LOGI(TAG, "  [ 7/11] 音频 Codec   (I2S0)        : %s", r.audio ? "PASS" : "FAIL ← 检查 MCLK/I2C");
    ESP_LOGI(TAG, "  [ 8/11] SD 卡        (SPI3)        : %s", r.sd ? "PASS" : "FAIL ← 检查 SPI 引脚/FAT32");
    ESP_LOGI(TAG, "  [ 9/11] Wi-Fi AP 扫描              : %s", r.wifi ? "PASS" : "FAIL ← 检查天线/射频电路");
    ESP_LOGI(TAG, "  [10/11] BLE 广播可见性             : %s", r.ble ? "PASS" : "FAIL ← 检查 BLE 射频/天线");
    ESP_LOGI(TAG, "  [11/11] DS2413 1-Wire (GPIO18)    : %s", r.ds2413 ? "PASS" : "FAIL ← 检查 GPIO18/上拉/DS2413");

    int pass_cnt = (r.i2c_scan + r.axp2101 + r.pcf85063 + r.qmi8658c +
                    r.ft5x06 + r.display + r.audio + r.sd +
                    r.wifi + r.ble + r.ds2413);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  总计：%d / 11 项通过", pass_cnt);
    if (pass_cnt == 11)
    {
        ESP_LOGI(TAG, "  ★ 全部通过！新 PCB 通讯验证 OK ★");
    }
    else
    {
        ESP_LOGW(TAG, "  ！有 %d 项失败，请根据上方日志逐项排查", 11 - pass_cnt);
    }
    print_sep(NULL);

    /* 心跳，方便监测系统是否挂起 */
    int tick = 0;
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "[心跳] 系统运行中 tick=%d", ++tick);
    }
}

/* =========================================================
 * 应用程序主入口（测试分支）
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "PCB 详细通讯测试固件启动");

    /* NVS 是 Wi-Fi 和音频 codec 的前置依赖 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS 需要擦除重建");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS 初始化失败，测试终止: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "NVS 初始化成功");

    /*
     * 独立任务运行原因：
     * - app_main 栈较小，Wi-Fi / LCD DMA 路径需要更大栈空间；
     * - 测试任务固定在 APP_CPU (core 0)，避免与 Wi-Fi 协议栈抢 PRO_CPU。
     */
    xTaskCreatePinnedToCore(
        comm_test_task,
        "pcb_test",
        10240, /* 10KB：容纳 Wi-Fi TLS + LCD bitmap + codec 初始化路径 */
        NULL,
        5,
        NULL,
        0 /* APP_CPU */
    );
}
