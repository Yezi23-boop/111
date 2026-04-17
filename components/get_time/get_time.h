#ifndef GET_TIME_H
#define GET_TIME_H

typedef struct
{
    int year;          /**< 年。 */
    int month;         /**< 月。 */
    int day;           /**< 日。 */
    int hour;          /**< 时。 */
    int min;           /**< 分。 */
    int sec;           /**< 秒。 */
    char time_str[64]; /**< 格式化后的时间字符串缓存。 */
} my_time_t;

/**
 * @brief 等待 SNTP 时间同步完成。
 *
 * 当前实现会阻塞等待系统时间有效，并在成功后设置东八区时区。
 *
 * @return 无返回值。
 */
void esp_wait_sntp_sync(void);

/**
 * @brief 获取当前本地时间。
 * @param[out] out_time 输出时间结构体。
 * @return 无返回值。
 */
void get_local_time(my_time_t *out_time);

/** 全局当前时间缓存，由更新时间接口刷新。 */
extern my_time_t now_time;

/**
 * @brief 刷新全局当前时间缓存。
 * @return 无返回值。
 */
void update_now_time(void);

#endif // GET_TIME_H
