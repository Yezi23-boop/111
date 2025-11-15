/**
 * @file sd_manager.c
 * @brief ESP32 SD卡管理器实现
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
#include "driver/sdmmc_host.h"

#define TAG "sd_manager"
#define MOUNT_POINT "/sdcard"

// SD卡设备句柄
static sdmmc_card_t *card = NULL;

esp_err_t sd_manager_init(void)
{
    // FAT文件系统挂载配置
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, .max_files = 8, .allocation_unit_size = 32 * 1024};

    // SDMMC主机控制器配置
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // SD卡插槽配置
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "正在挂载SD卡文件系统...");

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD卡挂载失败 (%s)", esp_err_to_name(ret));
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
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        card = NULL;
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
            ESP_LOGI(TAG, "发现项目: %s", ent->d_name);
        }

        closedir(dir);
    }
    else
    {
        ESP_LOGE(TAG, "无法打开目录: %s", path);
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
    bool exists = (stat(file_path, &st) == 0);

    if (exists)
    {
        ESP_LOGD(TAG, "文件存在: %s", file_path);
    }
    else
    {
        ESP_LOGD(TAG, "文件不存在: %s", file_path);
    }

    return exists;
}

esp_err_t sd_manager_read_file(const char *file_path, void *buffer, size_t buffer_size, size_t *bytes_read)
{
    if (file_path == NULL || buffer == NULL || buffer_size == 0)
    {
        ESP_LOGE(TAG, "读取文件参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(file_path, "rb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "无法打开文件进行读取: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t read_bytes = fread(buffer, 1, buffer_size, f);

    if (bytes_read != NULL)
    {
        *bytes_read = read_bytes;
    }

    fclose(f);

    ESP_LOGI(TAG, "从文件读取了 %zu 字节: %s", read_bytes, file_path);

    return ESP_OK;
}

esp_err_t sd_manager_write_file(const char *file_path, const void *data, size_t data_size)
{
    if (file_path == NULL || data == NULL || data_size == 0)
    {
        ESP_LOGE(TAG, "写入文件参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(file_path, "wb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "无法打开文件进行写入: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t written_bytes = fwrite(data, 1, data_size, f);
    fclose(f);

    if (written_bytes != data_size)
    {
        ESP_LOGE(TAG, "文件写入不完整: %s (期望 %zu 字节，实际 %zu 字节)", file_path, data_size, written_bytes);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "成功写入 %zu 字节到文件: %s", written_bytes, file_path);
    return ESP_OK;
}

esp_err_t sd_manager_create_dir(const char *dir_path)
{
    if (dir_path == NULL)
    {
        ESP_LOGE(TAG, "目录路径参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    if (mkdir(dir_path, 0755) == 0)
    {
        ESP_LOGI(TAG, "目录创建成功: %s", dir_path);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "目录创建失败: %s", dir_path);
        return ESP_FAIL;
    }
}

esp_err_t sd_manager_delete_file(const char *file_path)
{
    if (file_path == NULL)
    {
        ESP_LOGE(TAG, "文件路径参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    if (unlink(file_path) == 0)
    {
        ESP_LOGI(TAG, "文件删除成功: %s", file_path);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "文件删除失败: %s", file_path);
        return ESP_FAIL;
    }
}

esp_err_t sd_manager_get_file_size(const char *file_path, size_t *file_size)
{
    if (file_path == NULL || file_size == NULL)
    {
        ESP_LOGE(TAG, "获取文件大小参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(file_path, &st) == 0)
    {
        *file_size = st.st_size;
        ESP_LOGD(TAG, "文件大小: %zu 字节 (%s)", *file_size, file_path);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "无法获取文件信息: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }
}
