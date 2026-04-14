#ifndef AUDIO_ALERT_PLAYER_H
#define AUDIO_ALERT_PLAYER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t audio_alert_player_init(void);
    // 单次播放危险提示音；重复调用会在已有播放进行中直接返回。
    esp_err_t audio_alert_player_play_warning_once(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_ALERT_PLAYER_H
