#ifndef GET_TIME_H
#define GET_TIME_H
typedef struct
{
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    char time_str[64];
} my_time_t;

/**
 * @brief 等待SNTP时间同步
 * @details 阻塞直到SNTP同步时间完成，确保系统时间有效（大于2019年）
 * @details 同步完成后设置时区为中国标准时间（东八区），并打印当前时间
 */
void esp_wait_sntp_sync(void);

/**
 * @brief 获取当前本地时间
 * @details 获取系统当前时间并填充到自定义结构体 my_time_t 中，自动处理年份和月份
 * @param out_time 指向 my_time_t 结构体的指针，用于存储当前时间
 */
void get_local_time(my_time_t *out_time);

extern my_time_t now_time;
void update_now_time(void);

#endif // GET_TIME_H
