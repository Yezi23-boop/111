import pathlib


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_DIR = REPO_ROOT / "main"

APP_DIR = MAIN_DIR / "app"
SERVICES_DIR = MAIN_DIR / "services"
FEATURES_DIR = MAIN_DIR / "features"
UI_DIR = MAIN_DIR / "ui"

ALERTS_DIR = FEATURES_DIR / "alerts"
DANGER_DETECTION_DIR = FEATURES_DIR / "danger_detection"
AUDIO_DIR = FEATURES_DIR / "audio"
WEATHER_DIR = FEATURES_DIR / "weather"

APP_MAIN_SOURCE = APP_DIR / "app_main.c"
HARDWARE_INIT_SOURCE = APP_DIR / "hardware_init.c"
HARDWARE_INIT_HEADER = APP_DIR / "hardware_init.h"

NETWORK_SERVICE_SOURCE = SERVICES_DIR / "network_service.c"
NETWORK_SERVICE_HEADER = SERVICES_DIR / "network_service.h"
OFFICIAL_CHAT_SERVICE_SOURCE = SERVICES_DIR / "official_chat_service.c"
OFFICIAL_CHAT_SERVICE_HEADER = SERVICES_DIR / "official_chat_service.h"

APP_ALERT_MANAGER_SOURCE = ALERTS_DIR / "app_alert_manager.c"
APP_ALERT_MANAGER_HEADER = ALERTS_DIR / "app_alert_manager.h"
AUDIO_ALERT_PLAYER_SOURCE = ALERTS_DIR / "audio_alert_player.c"
AUDIO_ALERT_PLAYER_HEADER = ALERTS_DIR / "audio_alert_player.h"
DISPLAY_ALERT_ADAPTER_SOURCE = ALERTS_DIR / "display_alert_adapter.c"
DISPLAY_ALERT_ADAPTER_HEADER = ALERTS_DIR / "display_alert_adapter.h"

DANGER_DETECTION_SERVICE_SOURCE = (
    DANGER_DETECTION_DIR / "danger_detection_service.c"
)
DANGER_DETECTION_SERVICE_HEADER = (
    DANGER_DETECTION_DIR / "danger_detection_service.h"
)

AUDIO_APP_SOURCE = AUDIO_DIR / "audio_app.c"
AUDIO_APP_HEADER = AUDIO_DIR / "audio_app.h"

TIME_WEATHER_SOURCE = WEATHER_DIR / "time_weather.c"
TIME_WEATHER_HEADER = WEATHER_DIR / "time_weather.h"
HPTTS_SOURCE = WEATHER_DIR / "hptts.c"
HPTTS_HEADER = WEATHER_DIR / "hptts.h"

LVGL_TASK_SOURCE = UI_DIR / "lvgl_task.c"
LVGL_TASK_HEADER = UI_DIR / "lvgl_task.h"
UI_REFRESH_POLICY_SOURCE = UI_DIR / "ui_refresh_policy.c"
UI_REFRESH_POLICY_HEADER = UI_DIR / "ui_refresh_policy.h"

BOARD_POWER_SOURCE = APP_DIR / "board_power.c"
BOARD_POWER_HEADER = APP_DIR / "board_power.h"

POWER_SERVICE_SOURCE = SERVICES_DIR / "power_service.c"
POWER_SERVICE_HEADER = SERVICES_DIR / "power_service.h"
