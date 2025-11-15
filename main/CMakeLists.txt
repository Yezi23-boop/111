# 自动查找所有 C 源文件（递归 main 与 ui 目录）
file(GLOB_RECURSE srcs
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/ui/*.c
)

# 设置包含目录
set(include_dirs
    .
    ui/custom
    ui/generated
)

# 注册组件
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES  
    lvgl_port 
    z_print_esp32 
    esp_http_client         
    get_time
    sd_card
    nvs_flash              # NVS存储管理
    esp_wifi               # WiFi功能
    esp_event              # 事件系统
    esp_http_client        # HTTP客户端
    esp_netif              # 网络接口
    esp_system             # 系统功能
    esp-tls                # TLS安全连接
    json                   # JSON数据解析
    driver                 # 硬件驱动 (GPIO, SPI等)
    freertos               # FreeRTOS系统
)