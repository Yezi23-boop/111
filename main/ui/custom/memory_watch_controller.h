#ifndef MEMORY_WATCH_CONTROLLER_H
#define MEMORY_WATCH_CONTROLLER_H

#include "gui_guider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AI Memory Watch 页面控制器并绑定主菜单入口。
 * @param[in] ui GUI Guider 生成的 UI 树。
 */
void memory_watch_controller_init(lv_ui *ui);

/**
 * @brief 打开独立 Hermes 手表页面。
 */
void memory_watch_controller_open(void);

/**
 * @brief 在 LVGL 线程轮询 service 快照并刷新页面。
 *
 * 该函数只读 service 快照，不执行 HTTP、录音或阻塞等待。
 */
void memory_watch_controller_poll_ui(void);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_WATCH_CONTROLLER_H
