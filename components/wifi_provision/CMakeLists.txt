# wifi_provision 组件构建脚本
# - 统一配网协调（BLE 配网、AP 门户、STA 凭据连接）
# - 依赖 http_server / esp_wifi / bt / lwip / json
idf_component_register(
    SRCS 
        "src/ble_server/ble_provision_protocol.c"
        "src/ble_server/ble_provision_transport.c"
        "src/wifi_provision.c"
        "src/wifi_driver/wifi_manager.c"
        "src/web_server/ws_server.c"
    INCLUDE_DIRS 
        "include"
    PRIV_INCLUDE_DIRS 
        "src/wifi_driver"
        "src/web_server"
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        nvs_flash
        bt
        json
        lwip
    EMBED_TXTFILES 
        "html/apcfg.html"
)
