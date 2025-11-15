/**
 * @file sd_manager.h
 * @brief ESP32 SD卡管理器
 * @details 提供SD卡文件系统操作接口，支持SDMMC高速模式和FAT文件系统
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 初始化SD卡并挂载文件系统
     * @return esp_err_t ESP_OK成功，其他值失败
     */
    esp_err_t sd_manager_init(void);

    /**
     * @brief 卸载SD卡文件系统并释放资源
     */
    void sd_manager_deinit(void);

    /**
     * @brief 列出指定目录的所有内容
     * @param path 目录路径
     */
    void sd_manager_list_dir(const char *path);

    /**
     * @brief 检查指定文件是否存在
     * @param file_path 文件路径
     * @return true 文件存在，false 文件不存在
     */
    bool sd_manager_file_exists(const char *file_path);

    /**
     * @brief 从SD卡读取文件内容
     * @param file_path 文件路径
     * @param buffer 接收数据的缓冲区
     * @param buffer_size 缓冲区大小
     * @param bytes_read 实际读取的字节数（可选）
     * @return esp_err_t ESP_OK成功，其他值失败
     */
    esp_err_t sd_manager_read_file(const char *file_path, void *buffer, size_t buffer_size, size_t *bytes_read);

    /**
     * @brief 将数据写入SD卡文件
     * @param file_path 文件路径
     * @param data 要写入的数据
     * @param data_size 数据大小
     * @return esp_err_t ESP_OK成功，其他值失败
     */
    esp_err_t sd_manager_write_file(const char *file_path, const void *data, size_t data_size);

    /**
     * @brief 创建新目录
     * @param dir_path 目录路径
     * @return esp_err_t ESP_OK成功，其他值失败
     */
    esp_err_t sd_manager_create_dir(const char *dir_path);

    /**
     * @brief 删除文件
     * @param file_path 文件路径
     * @return esp_err_t ESP_OK成功，其他值失败
     */
    esp_err_t sd_manager_delete_file(const char *file_path);

    /**
     * @brief 获取文件大小
     * @param file_path 文件路径
     * @param file_size 用于接收文件大小的指针
     * @return esp_err_t ESP_OK成功，其他值失败
     */
    esp_err_t sd_manager_get_file_size(const char *file_path, size_t *file_size);

#ifdef __cplusplus
}
#endif
