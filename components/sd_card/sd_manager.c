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

// SPI 引脚定义来自当前板级原理图；这里固定走 SPI3，避免与显示链路占用同一主机。
#define PIN_NUM_MISO 3
#define PIN_NUM_MOSI 1
#define PIN_NUM_CLK 2
#define PIN_NUM_CS 17

static sdmmc_card_t *card = NULL;            // SD 卡设备句柄；非 NULL 表示已成功挂载。
static bool spi_initialized_by_sd = false;   // 标记 SPI 总线是否由本模块初始化，用于 deinit 决定是否释放总线。

/**
 * @brief 初始化 SD 卡并挂载文件系统。
 * @return `ESP_OK` 表示挂载成功；其他错误表示总线初始化或挂载失败。
 */
esp_err_t sd_manager_init(void)
{
    esp_err_t ret;

    // FAT 挂载参数倾向“稳定优先”而不是最大吞吐，避免在资源紧张时额外放大失败概率。
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024};

    ESP_LOGI(TAG, "初始化SD卡 (SPI模式)...");
    ESP_LOGI(TAG, "引脚配置: MOSI=%d, MISO=%d, CLK=%d, CS=%d",
             PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK, PIN_NUM_CS);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // 强制使用 SPI3_HOST，避免与屏幕的 SPI2_HOST/QSPI 链路冲突。
    host.slot = SPI3_HOST;

    /* 当前频率保持在 10MHz，优先换稳定性。
     * 若后续要提速，应逐步验证 CRC 错误和长时间读写稳定性。 */
    host.max_freq_khz = 10000;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "SPI3总线已被初始化，跳过初始化步骤，尝试复用总线");
            spi_initialized_by_sd = false;
        }
        else
        {
            ESP_LOGE(TAG, "SPI3总线初始化失败: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    else
    {
        spi_initialized_by_sd = true;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

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

        // 只有在本模块初始化了总线时才释放，避免误伤外部共享总线使用者。
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

/**
 * @brief 卸载 SD 卡文件系统并释放资源。
 * @return 无返回值。
 */
void sd_manager_deinit(void)
{
    if (card != NULL)
    {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        card = NULL;

        // 只有在本模块初始化了总线时才释放，避免误释放共享 SPI 主机。
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

/**
 * @brief 列出指定目录内容。
 * @param[in] path 目录路径。
 * @return 无返回值。
 */
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

/**
 * @brief 检查指定文件是否存在。
 * @param[in] file_path 文件路径。
 * @return true 表示文件存在。
 */
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
