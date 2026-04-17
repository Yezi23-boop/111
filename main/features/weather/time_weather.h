#ifndef TIME_WEATHER_H
#define TIME_WEATHER_H

/*
 * 时间天气后台任务入口：
 * - 当前主要负责周期更新时间并刷新 UI 数字时钟；
 * - 天气刷新链路可在后续扩展时继续挂到该任务或独立任务中。
 */

/* 后台时间任务入口：定期刷新本地时间并更新 UI 数字时钟。 */
void time_and_weather(void *pvParameters);

#endif // TIME_WEATHER_H
