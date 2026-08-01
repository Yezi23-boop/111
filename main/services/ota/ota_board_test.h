#ifndef OTA_BOARD_TEST_H
#define OTA_BOARD_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 启动一次性独立 OTA 板端测试任务。
     *
     * 仅在 `CONFIG_OTA_SERVICE_BOARD_TEST=y` 时创建任务。任务等待网络 owner
     * 已发布 SERVICE_READY 后提交 manifest 和下载请求；默认配置为空操作，
     * 因而不会让普通开机自动检查或下载 OTA。
     */
    esp_err_t ota_board_test_start(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_BOARD_TEST_H */
