#ifndef UI_REFRESH_POLICY_H

#define UI_REFRESH_POLICY_H

#include <stdbool.h>

#include <stdint.h>

/*
 * 文件作用：
 * - 封装 UI 刷新频率与背光亮度之间的联动策略；
 * - 对外隐藏活跃态 / 空闲态 / 强制活跃态切换细节；
 * - 提供给 LVGL 主循环、输入驱动和亮度设置控件一个稳定接口。
 *
 * UI 刷新策略模块：
 * 1. 由 `lvgl_task()` 周期性调用，决定当前界面应保持活跃刷新还是进入空闲降频。
 * 2. 降频时同时配合 `co5300_panel` 调低背光，降低整机空闲功耗。
 * 3. 对上层暴露的接口尽量简单，只提供“触摸通知 / 强制常亮 / 用户亮度 / 延时调整”四类能力，
 *    这样 UI 控制器、输入设备和屏幕驱动可以解耦。
 */

#ifdef __cplusplus

extern "C"
{

#endif

    /* 初始化内部状态并立刻把面板亮度同步到策略默认值。 */
    void ui_refresh_policy_init(void);

    /* 在触摸、编码器、按键等任意“用户活跃”事件后调用，用来刷新活跃超时计时器。 */
    void ui_refresh_policy_notify_touch(void);

    /* 强制保持活跃模式，通常用于播放动画、重要提示页或不希望被自动调暗的场景。 */
    void ui_refresh_policy_set_force_active(bool enabled);

    /* 查询当前是否处于强制活跃模式。 */
    bool ui_refresh_policy_is_force_active(void);

    /* 设置用户期望亮度百分比，策略层会在活跃/空闲状态下换算出最终背光值。 */
    void ui_refresh_policy_set_user_brightness_percent(uint8_t percent);

    /* 获取用户配置的原始亮度百分比，而不是当前实际输出到屏幕的亮度。 */
    uint8_t ui_refresh_policy_get_user_brightness_percent(void);

    /*
     * 根据当前策略修正 LVGL 主循环的下一次唤醒时间：
     * - 活跃态：限制最大延时，保证交互和动画流畅。
     * - 空闲态：拉长最小延时，减少无意义刷新。
     */
    uint32_t ui_refresh_policy_adjust_delay(uint32_t next_call_ms);

    /* 周期轮询状态机，通常在 UI 主循环里调用一次即可。 */
    void ui_refresh_policy_poll(void);

#ifdef __cplusplus
}

#endif

#endif // UI_REFRESH_POLICY_H
