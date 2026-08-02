#ifndef MUSIC_BOARD_TEST_H
#define MUSIC_BOARD_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 启动一次性固定流音乐板端测试。
     *
     * 仅在 `CONFIG_MUSIC_SERVICE_BOARD_TEST=y` 时创建测试任务；普通固件
     * 默认关闭，不会因为开机而自动联网或播放音乐。
     */
    esp_err_t music_board_test_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_BOARD_TEST_H */
