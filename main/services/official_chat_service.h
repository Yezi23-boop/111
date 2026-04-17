#ifndef OFFICIAL_CHAT_SERVICE_H
#define OFFICIAL_CHAT_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * 官方聊天服务层：
 * - 将 `official_chat` 的原始句柄封装成“前台可进入 / 可退出 / 可安全关闭”的长期后台任务；
 * - 负责等待网络就绪、转发状态变化、缓存最近对话文本；
 * - 提供给 UI 层一个稳定、低耦合的查询接口，避免页面直接操作底层会话对象。
 */

#ifdef __cplusplus
extern "C"
{
#endif

    /* 服务层对外暴露的聊天会话状态，统一屏蔽底层 `official_chat` 细节。 */
    typedef enum
    {
        OFFICIAL_CHAT_SERVICE_STATE_STOPPED = 0,     /* 未持有底层聊天实例。 */
        OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK, /* 等待 `network_service` 真正可用。 */
        OFFICIAL_CHAT_SERVICE_STATE_STARTING,        /* 正在创建实例或启动底层服务。 */
        OFFICIAL_CHAT_SERVICE_STATE_ACTIVATING,      /* 设备激活中。 */
        OFFICIAL_CHAT_SERVICE_STATE_CONNECTING,      /* WebSocket 或云端链路建立中。 */
        OFFICIAL_CHAT_SERVICE_STATE_IDLE,            /* 已就绪，等待唤醒或交互。 */
        OFFICIAL_CHAT_SERVICE_STATE_LISTENING,       /* 正在采集用户语音。 */
        OFFICIAL_CHAT_SERVICE_STATE_SPEAKING,        /* 正在播报助手回复。 */
        OFFICIAL_CHAT_SERVICE_STATE_ERROR,           /* 底层会话出错。 */
    } official_chat_service_state_t;

    typedef enum
    {
        OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER = 0,
        OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT,
    } official_chat_service_message_role_t;

    /* 单条消息历史快照，供 UI 直接读取展示。 */
    typedef struct
    {
        official_chat_service_message_role_t role; // 消息角色：用户或助手
        char text[256];                            // UTF-8 文本内容（截断上限 255 字节）
    } official_chat_service_message_t;

    /* 初始化后台服务任务；重复调用安全。 */
    esp_err_t official_chat_service_init(void);

    /* 标记聊天页进入前台，后台任务会在网络满足条件后自动拉起会话。 */
    void official_chat_service_enter_foreground(void);

    /* 标记聊天页离开前台，但不立即销毁，是否真正关闭由上层控制。 */
    void official_chat_service_leave_foreground(void);

    /* 请求异步关闭会话，适合不希望阻塞 UI 的场景。 */
    void official_chat_service_request_shutdown(void);

    /* 查询是否已有关闭流程在进行中。 */
    bool official_chat_service_is_shutdown_pending(void);

    /* 同步等待关闭完成，超时返回错误。 */
    esp_err_t official_chat_service_shutdown(void);

    /* 获取服务层状态。 */
    official_chat_service_state_t official_chat_service_get_state(void);

    /* 获取最近一次底层错误。 */
    esp_err_t official_chat_service_get_last_error(void);

    /* 获取当前缓存的消息条数。 */
    size_t official_chat_service_get_message_count(void);

    /* 读取历史消息快照，索引越大表示越新的消息。 */
    esp_err_t official_chat_service_get_message(size_t index,
                                                official_chat_service_message_t *out_message);

    /* 获取最近一条用户文本。 */
    esp_err_t official_chat_service_get_last_user_text(char *buffer, size_t size);

    /* 获取最近一条助手文本。 */
    esp_err_t official_chat_service_get_last_assistant_text(char *buffer,
                                                            size_t size);

    /* 供日志或 UI 文本展示使用的状态字符串。 */
    const char *official_chat_service_state_to_string(
        official_chat_service_state_t state);

#ifdef __cplusplus
}
#endif

#endif // OFFICIAL_CHAT_SERVICE_H
