#ifndef LVGL_TASK_H
#define LVGL_TASK_H

#include "gui_guider.h" // guider_ui的定义在这里

/*
 * LVGL UI 主任务入口：
 * - 负责初始化显示驱动、界面对象和 UI 控制器；
 * - 持续驱动 `lv_timer_handler()`；
 * - 协调刷新策略、告警展示和录音按钮等前台交互逻辑。
 */

extern lv_ui guider_ui;

/* FreeRTOS 任务入口，由系统启动阶段创建。 */
void lvgl_task(void *pvParameter);

#endif // LVGL_TASK_H
