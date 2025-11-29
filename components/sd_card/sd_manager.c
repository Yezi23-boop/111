/**
 * @file sd_manager.c
 * @brief ESP32 SD卡管理器实现 (SPI模式)
 * @details 实现SD卡的初始化、文件系统挂载、文件读写等核心功能
 */

#include "sd_manager.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"

#define TAG "sd_manager"
#define MOUNT_POINT "/sdcard"

// SPI引脚定义 (根据原理图)
#define PIN_NUM_MISO 3
#define PIN_NUM_MOSI 1
#define PIN_NUM_CLK 2
#define PIN_NUM_CS 17

// SD卡设备句柄
static sdmmc_card_t *card = NULL;
// 标记SPI总线是否由sd_manager初始化，用于deinit时决定是否释放总线
static bool spi_initialized_by_sd = false;

esp_err_t sd_manager_init(void)
{
    esp_err_t ret;

    // FAT文件系统挂载配置
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024};

    ESP_LOGI(TAG, "初始化SD卡 (SPI模式)...");
    ESP_LOGI(TAG, "引脚配置: MOSI=%d, MISO=%d, CLK=%d, CS=%d",
             PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK, PIN_NUM_CS);

    // 1. 配置SPI总线
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // 强制使用 SPI3_HOST (HSPI)，避免与屏幕的 SPI2_HOST (FSPI) 冲突
    host.slot = SPI3_HOST;

    // 大幅降低频率以解决CRC错误问题 (1MHz)
    // 挂载成功后，如果需要高速，可以尝试逐步提高到 10MHz 或 20MHz
    host.max_freq_khz = 10000;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // 初始化SPI3总线
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "SPI3总线已被初始化，跳过初始化步骤，尝试复用总线");
            spi_initialized_by_sd = false; // 总线不是我们初始化的，所以不要释放
        }
        else
        {
            ESP_LOGE(TAG, "SPI3总线初始化失败: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    else
    {
        spi_initialized_by_sd = true; // 标记总线由我们初始化
    }

    // 2. 配置SD卡插槽
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // 3. 挂载文件系统
    ESP_LOGI(TAG, "正在挂载文件系统(SPI3)...");
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "挂载失败: 无法挂载文件系统。");
            ESP_LOGE(TAG, "如果这是新卡，可能需要先在电脑上格式化为FAT32。");
        }
        else
        {
            ESP_LOGE(TAG, "SD卡挂载失败 (%s). 请检查硬件连接。", esp_err_to_name(ret));
        }

        // 只有在我们初始化了总线的情况下才释放它
        if (spi_initialized_by_sd)
        {
            spi_bus_free(host.slot);
        }
        return ret;
    }

    ESP_LOGI(TAG, "SD卡挂载成功！");
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}

void sd_manager_deinit(void)
{
    if (card != NULL)
    {
        // 卸载文件系统
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        card = NULL;

        // 只有在我们初始化了总线的情况下才释放它
        if (spi_initialized_by_sd)
        {
            spi_bus_free(SPI3_HOST);
            ESP_LOGI(TAG, "SPI3总线已释放");
        }

        ESP_LOGI(TAG, "SD卡已安全卸载");
    }
    else
    {
        ESP_LOGW(TAG, "SD卡未初始化，无需卸载");
    }
}

void sd_manager_list_dir(const char *path)
{
    if (path == NULL)
    {
        ESP_LOGE(TAG, "目录路径参数为空");
        return;
    }

    DIR *dir = opendir(path);

    if (dir != NULL)
    {
        struct dirent *ent;
        ESP_LOGI(TAG, "正在列出目录内容: %s", path);

        while ((ent = readdir(dir)) != NULL)
        {
            if (ent->d_type == DT_DIR)
            {
                ESP_LOGI(TAG, "  [DIR]  %s", ent->d_name);
            }
            else
            {
                ESP_LOGI(TAG, "  [FILE] %s", ent->d_name);
            }
        }

        closedir(dir);
    }
    else
    {
        ESP_LOGE(TAG, "无法打开目录: %s (可能不存在或未挂载)", path);
    }
}

bool sd_manager_file_exists(const char *file_path)
{
    if (file_path == NULL)
    {
        ESP_LOGW(TAG, "文件路径参数为空");
        return false;
    }

    struct stat st;
    if (stat(file_path, &st) == 0)
    {
        return true;
    }
    return false;
}
