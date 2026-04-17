# Wi-Fi Management UI Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `screen_main_Wifi` 接成真实 Wi-Fi 状态入口，并新增 hand-written 全屏 Wi-Fi 管理页，统一承接“再次使用已保存凭据联网 / 断开联网 / 重新配网 / 默认配网方式设置”。

**Architecture:** 本轮不重命名 `network_service` 文件，而是在现有服务层上补齐 Wi-Fi 语义 façade，避免冲击 `official_chat`、AI 页面和现有状态查询方。UI 继续沿用当前仓库 hand-written controller 模式：主界面 `screen_main_Wifi` 只负责状态灯与入口，全屏 Wi-Fi 管理页负责动作与设置；BLE/AP transport 继续下沉到 `wifi_provision` / `wifi_manager`。

**Tech Stack:** ESP-IDF v5.5.3、FreeRTOS、LVGL 9.3、NVS、现有 `wifi_provision` / `wifi_manager`、Python `unittest` 源码级测试

---

## 关联规格

- Spec: `docs/superpowers/specs/2026-04-17-wifi-management-ui-design.md`

## 本轮范围锁定

- 本轮不把 `network_service` 文件名整体改成 `wifi_service`；先把“对外语义”收敛成 Wi-Fi 控制面。
- 主界面 Wi-Fi 图标只保留两态：
  - 灰色：未连接
  - 蓝色：已连接
- `进入配网` 的正式语义固定为：再次使用已保存凭据联网。
- `重连` 的正式语义固定为：重新开始新的配网流程，当前会话不再自动复用旧凭据，直到用户再次明确触发。
- `断开联网` 必须断开当前连接，并暂停自动重连，直到用户点击 `进入配网` 或 `重连`。
- `screen_main_Bluetooth` 在本轮完成后不再承担主界面配网入口语义；BLE/AP 选择移入新的 Wi-Fi 管理页设置区。

## 文件结构与职责

- 修改 `components/wifi_provision/src/wifi_driver/wifi_manager.h`
  - 增加“用户主动断开/暂停自动重连”所需的底层接口。
- 修改 `components/wifi_provision/src/wifi_driver/wifi_manager.c`
  - 收口自动重连暂停逻辑，避免 UI 主动断开后底层立即连回。
- 修改 `components/wifi_provision/include/wifi_provision.h`
  - 对上暴露 Wi-Fi 语义辅助接口，避免 `main/` 直接引用 `wifi_manager` 私有头。
- 修改 `components/wifi_provision/src/wifi_provision.c`
  - 实现 saved connect / disconnect / stop current provisioning transport 等桥接能力。
- 修改 `main/services/network_service.h`
  - 增加 Wi-Fi 语义查询/控制接口、默认配网方式枚举与 UI 状态结构体。
- 修改 `main/services/network_service.c`
  - 实现“再次尝试已存凭据 / 用户主动断开 / 重新配网 / 默认 transport 偏好 / Wi-Fi 页面状态快照”。
- 修改 `main/ui/custom/main_dropdown_controller.h`
  - 暴露 Wi-Fi 按钮点击入口。
- 修改 `main/ui/custom/main_dropdown_controller.c`
  - 同步 `screen_main_Wifi` 的真实连接状态，点击后进入 Wi-Fi 管理页；同时移除蓝牙主入口语义。
- 创建 `main/ui/custom/wifi_management_controller.h`
  - 定义全屏 Wi-Fi 管理页的 open/init 接口。
- 创建 `main/ui/custom/wifi_management_controller.c`
  - 创建 hand-written LVGL 页面、状态刷新、动作按钮、默认 transport 设置与返回主页逻辑。
- 修改 `main/ui/custom/custom.h`
  - 暴露新的 Wi-Fi 管理页控制器头文件。
- 修改 `main/ui/generated/events_init.c`
  - 给 `screen_main_Wifi` 绑定真实点击事件，删掉或停用主界面蓝牙入口绑定。
- 修改 `main/CMakeLists.txt`
  - 注册新的 `wifi_management_controller.c`。
- 修改 `tests/main_paths.py`
  - 增加 Wi-Fi 管理页相关源文件路径常量。
- 修改 `tests/test_main_screen_ble_toggle_source.py`
  - 由“主界面蓝牙按钮为真实入口”转成“蓝牙入口已收回，不再承担主路径语义”的校验。
- 创建 `tests/test_network_service_wifi_management_source.py`
  - 校验新的 Wi-Fi façade API、transport 偏好、断开/重连入口。
- 创建 `tests/test_main_screen_wifi_entry_source.py`
  - 校验 `screen_main_Wifi` 事件绑定、实时状态同步与页面打开接缝。
- 创建 `tests/test_wifi_management_controller_source.py`
  - 校验全屏 Wi-Fi 管理页的按钮、状态刷新和设置项存在。
- 修改 `docs/context/knowledge/project/ble-provisioning-ui-toggle-behavior.md`
  - 标注该文档只描述历史阶段行为，不再指导当前主界面入口。
- 创建 `docs/context/knowledge/project/wifi-management-ui-behavior.md`
  - 沉淀本轮完成后的真实 Wi-Fi UI 语义和入口分层。
- 修改 `docs/context/CHANGELOG.md`
  - 记录本轮计划与后续实现切换点。

## Chunk 1: 收口 Wi-Fi 控制面

### Task 1: 冻结新的 Wi-Fi façade 头文件契约

**Files:**
- Modify: `main/services/network_service.h`
- Modify: `tests/main_paths.py`
- Create: `tests/test_network_service_wifi_management_source.py`

- [ ] **Step 1: 先写失败的源码级测试**

```python
class NetworkServiceWifiManagementSourceTests(unittest.TestCase):
    def test_header_exposes_wifi_management_contract(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("typedef enum", header)
        self.assertIn("NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO", header)
        self.assertIn("NETWORK_SERVICE_PROVISION_TRANSPORT_BLE", header)
        self.assertIn("NETWORK_SERVICE_PROVISION_TRANSPORT_AP", header)
        self.assertIn("typedef struct", header)
        self.assertIn("network_service_wifi_status_t", header)
        self.assertIn(
            "esp_err_t network_service_request_connect_with_saved_credentials(void);",
            header,
        )
        self.assertIn(
            "esp_err_t network_service_request_disconnect(void);",
            header,
        )
        self.assertIn(
            "esp_err_t network_service_request_reprovision(void);",
            header,
        )
        self.assertIn(
            "bool network_service_is_wifi_connected(void);",
            header,
        )
```

- [ ] **Step 2: 运行测试，确认当前失败**

Run: `uv run python -m unittest tests.test_network_service_wifi_management_source -v`

Expected: FAIL，提示 `network_service` 头文件中尚未声明新的 Wi-Fi 管理接口。

- [ ] **Step 3: 在头文件加入最小但完整的对外契约**

```c
typedef enum
{
    NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO = 0,
    NETWORK_SERVICE_PROVISION_TRANSPORT_BLE,
    NETWORK_SERVICE_PROVISION_TRANSPORT_AP,
} network_service_provision_transport_t;

typedef struct
{
    bool wifi_connected;
    bool has_credentials;
    bool user_disconnect_latched;
    bool provisioning_active;
    bool ble_active;
    bool ap_active;
    network_service_provision_transport_t default_transport;
    char ip[16];
} network_service_wifi_status_t;
```

- [ ] **Step 4: 补齐新 API 声明**

```c
bool network_service_is_wifi_connected(void);
esp_err_t network_service_get_wifi_status(network_service_wifi_status_t *status);
esp_err_t network_service_request_connect_with_saved_credentials(void);
esp_err_t network_service_request_disconnect(void);
esp_err_t network_service_request_reprovision(void);
esp_err_t network_service_set_default_provision_transport(
    network_service_provision_transport_t transport);
network_service_provision_transport_t
network_service_get_default_provision_transport(void);
```

- [ ] **Step 5: 重新运行测试，确认转绿**

Run: `uv run python -m unittest tests.test_network_service_wifi_management_source -v`

Expected: PASS

- [ ] **Step 6: 提交**

```bash
git add tests/main_paths.py tests/test_network_service_wifi_management_source.py main/services/network_service.h
git commit -m "feat: 定义 WiFi 管理服务门面接口"
```

### Task 2: 给底层补上“主动断开 + 暂停自动重连”能力

**Files:**
- Modify: `components/wifi_provision/src/wifi_driver/wifi_manager.h`
- Modify: `components/wifi_provision/src/wifi_driver/wifi_manager.c`
- Modify: `components/wifi_provision/include/wifi_provision.h`
- Modify: `components/wifi_provision/src/wifi_provision.c`
- Test: `tests/test_network_service_wifi_management_source.py`

- [ ] **Step 1: 扩写失败测试，锁定底层桥接点**

```python
def test_source_uses_disconnect_and_retry_bridge(self) -> None:
    provision_header = WIFI_PROVISION_HEADER.read_text(encoding="utf-8")
    provision_source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")
    manager_header = WIFI_MANAGER_HEADER.read_text(encoding="utf-8")
    manager_source = WIFI_MANAGER_SOURCE.read_text(encoding="utf-8")

    self.assertIn("esp_err_t wifi_manager_disconnect(void);", manager_header)
    self.assertIn(
        "void wifi_manager_set_auto_reconnect_enabled(bool enabled);",
        manager_header,
    )
    self.assertIn("esp_err_t wifi_provision_disconnect_sta(void);", provision_header)
    self.assertIn("esp_err_t wifi_provision_connect_saved(void);", provision_header)
    self.assertIn("wifi_manager_set_auto_reconnect_enabled", manager_source)
    self.assertIn("wifi_manager_disconnect", provision_source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_network_service_wifi_management_source -v`

Expected: FAIL，提示缺少新桥接接口。

- [ ] **Step 3: 在 `wifi_manager` 增加用户断开相关底层接口**

```c
esp_err_t wifi_manager_disconnect(void);
void wifi_manager_set_auto_reconnect_enabled(bool enabled);
bool wifi_manager_is_auto_reconnect_enabled(void);
```

- [ ] **Step 4: 在 `wifi_manager.c` 实现运行时闸门**

```c
static bool s_auto_reconnect_enabled = true;

if (sta_connect_count < g_config.max_retry && s_auto_reconnect_enabled)
{
    esp_wifi_connect();
}
```

- [ ] **Step 5: 在 `wifi_provision` 暴露上层可用桥接**

```c
esp_err_t wifi_provision_connect_saved(void)
{
    return wifi_manager_connect_saved();
}

esp_err_t wifi_provision_disconnect_sta(void)
{
    return wifi_manager_disconnect();
}
```

- [ ] **Step 6: 增加“停止当前 provisioning transport”的统一桥接**

```c
esp_err_t wifi_provision_stop_active_transport(void)
{
    if (wifi_provision_is_ble_active()) { return wifi_provision_stop_blecfg(); }
    if (wifi_provision_is_ap_active()) { ws_server_stop(); return wifi_manager_stop_ap(); }
    return ESP_OK;
}
```

- [ ] **Step 7: 重新运行测试，确认桥接层通过**

Run: `uv run python -m unittest tests.test_network_service_wifi_management_source -v`

Expected: PASS

- [ ] **Step 8: 提交**

```bash
git add components/wifi_provision/src/wifi_driver/wifi_manager.h components/wifi_provision/src/wifi_driver/wifi_manager.c components/wifi_provision/include/wifi_provision.h components/wifi_provision/src/wifi_provision.c tests/test_network_service_wifi_management_source.py
git commit -m "feat: 补齐 WiFi 主动断开与重连桥接"
```

### Task 3: 在 `network_service` 实现 Wi-Fi 语义 façade

**Files:**
- Modify: `main/services/network_service.c`
- Modify: `main/services/network_service.h`
- Test: `tests/test_network_service_wifi_management_source.py`

- [ ] **Step 1: 扩写失败测试，锁定状态机入口**

```python
def test_network_service_source_implements_wifi_actions(self) -> None:
    source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

    self.assertIn("network_service_request_connect_with_saved_credentials", source)
    self.assertIn("network_service_request_disconnect", source)
    self.assertIn("network_service_request_reprovision", source)
    self.assertIn("wifi_provision_set_auto_reconnect_enabled(false)", source)
    self.assertIn("wifi_provision_disconnect_sta()", source)
    self.assertIn("wifi_provision_connect_saved()", source)
    self.assertIn("network_service_store_transport_pref", source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_network_service_wifi_management_source -v`

Expected: FAIL

- [ ] **Step 3: 增加运行时状态与 NVS 偏好**

```c
static bool s_user_disconnect_latched = false;
static network_service_provision_transport_t s_default_transport =
    NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO;
```

- [ ] **Step 4: 实现“再次使用已保存凭据联网”**

```c
esp_err_t network_service_request_connect_with_saved_credentials(void)
{
    s_user_disconnect_latched = false;
    wifi_provision_set_auto_reconnect_enabled(true);
    return wifi_provision_connect_saved();
}
```

- [ ] **Step 5: 实现“断开联网并暂停自动重连”**

```c
esp_err_t network_service_request_disconnect(void)
{
    s_user_disconnect_latched = true;
    wifi_provision_set_auto_reconnect_enabled(false);
    (void)wifi_provision_stop_active_transport();
    return wifi_provision_disconnect_sta();
}
```

- [ ] **Step 6: 实现“重新配网”**

```c
esp_err_t network_service_request_reprovision(void)
{
    s_user_disconnect_latched = false;
    wifi_provision_set_auto_reconnect_enabled(false);
    (void)wifi_provision_stop_active_transport();

    switch (s_default_transport)
    {
    case NETWORK_SERVICE_PROVISION_TRANSPORT_AP:
        return wifi_provision_start_apcfg();
    case NETWORK_SERVICE_PROVISION_TRANSPORT_BLE:
        return wifi_provision_start_blecfg();
    case NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO:
    default:
        return wifi_provision_start_blecfg();
    }
}
```

- [ ] **Step 7: 实现 Wi-Fi 状态快照接口**

```c
esp_err_t network_service_get_wifi_status(network_service_wifi_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->wifi_connected = wifi_provision_is_connected();
    status->has_credentials = wifi_provision_has_credentials();
    status->user_disconnect_latched = s_user_disconnect_latched;
    status->ble_active = wifi_provision_is_ble_active();
    status->ap_active = wifi_provision_is_ap_active();
    status->default_transport = s_default_transport;
    return ESP_OK;
}
```

- [ ] **Step 8: 重新运行测试，确认 façade 接口通过**

Run: `uv run python -m unittest tests.test_network_service_wifi_management_source -v`

Expected: PASS

- [ ] **Step 9: 提交**

```bash
git add main/services/network_service.h main/services/network_service.c tests/test_network_service_wifi_management_source.py
git commit -m "feat: 在 network_service 上收口 WiFi 语义控制面"
```

## Chunk 2: 主界面 Wi-Fi 图标接成真实入口

### Task 4: 先把现有 BLE 主入口测试改成新约束

**Files:**
- Modify: `tests/test_main_screen_ble_toggle_source.py`
- Create: `tests/test_main_screen_wifi_entry_source.py`
- Modify: `tests/main_paths.py`

- [ ] **Step 1: 写失败测试，明确主界面入口已切到 Wi-Fi**

```python
class MainScreenWifiEntrySourceTests(unittest.TestCase):
    def test_events_init_binds_wifi_entry_handler(self) -> None:
        source = UI_EVENTS_INIT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("screen_main_wifi_event_handler", source)
        self.assertIn(
            "lv_obj_add_event_cb(ui->screen_main_Wifi, screen_main_wifi_event_handler, LV_EVENT_ALL, ui);",
            source,
        )

    def test_bluetooth_is_no_longer_bound_as_main_entry(self) -> None:
        source = UI_EVENTS_INIT_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("screen_main_bluetooth_event_handler", source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_main_screen_ble_toggle_source tests.test_main_screen_wifi_entry_source -v`

Expected: FAIL

- [ ] **Step 3: 更新旧 BLE 测试为“历史入口已移除”的断言**

```python
self.assertIn("screen_main_Bluetooth", generated_source)
self.assertIn("LV_OBJ_FLAG_HIDDEN", controller_source)
self.assertNotIn("main_dropdown_controller_handle_bluetooth_click", events_source)
```

- [ ] **Step 4: 保持新 Wi-Fi 入口测试只校验主路径，不绑定页面细节**

```python
self.assertIn('#include "wifi_management_controller.h"', source)
self.assertIn("main_dropdown_controller_handle_wifi_click()", source)
```

- [ ] **Step 5: 重新运行测试，确认测试基线准备完成**

Run: `uv run python -m unittest tests.test_main_screen_ble_toggle_source tests.test_main_screen_wifi_entry_source -v`

Expected: PASS

- [ ] **Step 6: 提交**

```bash
git add tests/main_paths.py tests/test_main_screen_ble_toggle_source.py tests/test_main_screen_wifi_entry_source.py
git commit -m "test: 切换主界面联网入口测试到 WiFi 语义"
```

### Task 5: 扩展下拉菜单控制器，让 Wi-Fi 图标反映真实连接状态

**Files:**
- Modify: `main/ui/custom/main_dropdown_controller.h`
- Modify: `main/ui/custom/main_dropdown_controller.c`
- Modify: `main/ui/custom/custom.h`
- Test: `tests/test_main_screen_wifi_entry_source.py`

- [ ] **Step 1: 扩写失败测试，锁定实时状态同步**

```python
def test_dropdown_controller_syncs_wifi_button_and_opens_page(self) -> None:
    source = UI_MAIN_DROPDOWN_CONTROLLER_SOURCE.read_text(encoding="utf-8")
    self.assertIn("network_service_is_wifi_connected()", source)
    self.assertIn("wifi_management_controller_open()", source)
    self.assertIn("LV_STATE_CHECKED", source)
    self.assertIn('ESP_LOGI(TAG, "sync WiFi button:', source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_main_screen_wifi_entry_source -v`

Expected: FAIL

- [ ] **Step 3: 在 `main_dropdown_controller.h` 暴露 Wi-Fi 点击入口**

```c
void main_dropdown_controller_handle_wifi_click(void);
```

- [ ] **Step 4: 在 `main_dropdown_controller.c` 增加 Wi-Fi 状态同步**

```c
static void main_dropdown_controller_sync_wifi_button(void)
{
    const bool connected = network_service_is_wifi_connected();
    if (connected) { lv_obj_add_state(button, LV_STATE_CHECKED); }
    else { lv_obj_remove_state(button, LV_STATE_CHECKED); }
}
```

- [ ] **Step 5: 点击 Wi-Fi 图标时只打开管理页**

```c
void main_dropdown_controller_handle_wifi_click(void)
{
    wifi_management_controller_open();
}
```

- [ ] **Step 6: 隐藏或禁用主界面蓝牙按钮**

```c
lv_obj_add_flag(s_ui->screen_main_Bluetooth, LV_OBJ_FLAG_HIDDEN);
lv_obj_add_flag(s_ui->screen_main_Bluetooth_label, LV_OBJ_FLAG_HIDDEN);
```

- [ ] **Step 7: 重新运行测试，确认主界面控制器通过**

Run: `uv run python -m unittest tests.test_main_screen_wifi_entry_source -v`

Expected: PASS

- [ ] **Step 8: 提交**

```bash
git add main/ui/custom/main_dropdown_controller.h main/ui/custom/main_dropdown_controller.c main/ui/custom/custom.h tests/test_main_screen_wifi_entry_source.py
git commit -m "feat: 将主界面 WiFi 图标接成真实状态入口"
```

### Task 6: 给 `screen_main_Wifi` 绑定真实点击事件

**Files:**
- Modify: `main/ui/generated/events_init.c`
- Modify: `main/ui/custom/custom.h`
- Modify: `main/CMakeLists.txt`
- Test: `tests/test_main_screen_wifi_entry_source.py`

- [ ] **Step 1: 写失败测试，锁定生成层接缝**

```python
self.assertIn("static void screen_main_wifi_event_handler", source)
self.assertIn(
    "lv_obj_add_event_cb(ui->screen_main_Wifi, screen_main_wifi_event_handler, LV_EVENT_ALL, ui);",
    source,
)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_main_screen_wifi_entry_source -v`

Expected: FAIL

- [ ] **Step 3: 在 `events_init.c` 增加 Wi-Fi handler**

```c
static void screen_main_wifi_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        main_dropdown_controller_handle_wifi_click();
    }
}
```

- [ ] **Step 4: 删除蓝牙主入口绑定或改成 no-op**

```c
/* 不再注册 screen_main_Bluetooth 点击事件。 */
```

- [ ] **Step 5: 重新运行测试，确认主界面接缝通过**

Run: `uv run python -m unittest tests.test_main_screen_wifi_entry_source tests.test_main_screen_ble_toggle_source -v`

Expected: PASS

- [ ] **Step 6: 提交**

```bash
git add main/ui/generated/events_init.c main/CMakeLists.txt tests/test_main_screen_wifi_entry_source.py tests/test_main_screen_ble_toggle_source.py
git commit -m "feat: 绑定主界面 WiFi 入口事件"
```

## Chunk 3: 新增全屏 Wi-Fi 管理页

### Task 7: 先写 Wi-Fi 管理页源码级测试

**Files:**
- Create: `main/ui/custom/wifi_management_controller.h`
- Create: `main/ui/custom/wifi_management_controller.c`
- Modify: `tests/main_paths.py`
- Create: `tests/test_wifi_management_controller_source.py`

- [ ] **Step 1: 写失败测试，锁定页面骨架**

```python
class WifiManagementControllerSourceTests(unittest.TestCase):
    def test_controller_exposes_open_and_init(self) -> None:
        header = WIFI_MANAGEMENT_CONTROLLER_HEADER.read_text(encoding="utf-8")
        self.assertIn("void wifi_management_controller_init(lv_ui *ui);", header)
        self.assertIn("void wifi_management_controller_open(void);", header)

    def test_source_contains_expected_actions(self) -> None:
        source = WIFI_MANAGEMENT_CONTROLLER_SOURCE.read_text(encoding="utf-8")
        self.assertIn("进入配网", source)
        self.assertIn("断开联网", source)
        self.assertIn("重连", source)
        self.assertIn("AUTO", source)
        self.assertIn("BLE", source)
        self.assertIn("AP", source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_wifi_management_controller_source -v`

Expected: FAIL，提示文件或接口不存在。

- [ ] **Step 3: 创建最小头文件和空实现**

```c
void wifi_management_controller_init(lv_ui *ui);
void wifi_management_controller_open(void);
```

- [ ] **Step 4: 重新运行测试，确认文件存在但正文仍待补**

Run: `uv run python -m unittest tests.test_wifi_management_controller_source -v`

Expected: 仍有 FAIL，但已经从“文件不存在”缩小到“缺少页面结构字符串”。

- [ ] **Step 5: 提交**

```bash
git add tests/main_paths.py tests/test_wifi_management_controller_source.py main/ui/custom/wifi_management_controller.h main/ui/custom/wifi_management_controller.c
git commit -m "test: 建立 WiFi 管理页源码测试骨架"
```

### Task 8: 实现页面骨架与导航

**Files:**
- Create: `main/ui/custom/wifi_management_controller.c`
- Modify: `main/ui/custom/wifi_management_controller.h`
- Modify: `main/CMakeLists.txt`
- Test: `tests/test_wifi_management_controller_source.py`

- [ ] **Step 1: 扩写失败测试，锁定 hand-written 页面模式**

```python
self.assertIn("lv_screen_load_anim", source)
self.assertIn("LV_SCR_LOAD_ANIM_MOVE_LEFT", source)
self.assertIn("LV_SCR_LOAD_ANIM_MOVE_RIGHT", source)
self.assertIn("返回主页", source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_wifi_management_controller_source -v`

Expected: FAIL

- [ ] **Step 3: 按 `ai_ui_controller` 模式创建独立 screen**

```c
static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_status_timer = NULL;

static void wifi_management_controller_ensure_screen_created(void)
{
    if (s_screen != NULL) { return; }
    s_screen = lv_obj_create(NULL);
}
```

- [ ] **Step 4: 创建顶部状态区、中间按钮区、下方设置区**

```c
/* 顶部状态 */
s_status_title = lv_label_create(s_screen);
/* 中间按钮 */
s_retry_saved_btn = lv_btn_create(s_screen);
s_disconnect_btn = lv_btn_create(s_screen);
s_reprovision_btn = lv_btn_create(s_screen);
/* 下方设置 */
s_transport_auto_btn = lv_btn_create(s_screen);
s_transport_ble_btn = lv_btn_create(s_screen);
s_transport_ap_btn = lv_btn_create(s_screen);
```

- [ ] **Step 5: 增加“返回主页”按钮**

```c
static void wifi_management_back_event_cb(lv_event_t *e)
{
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        true);
}
```

- [ ] **Step 6: 重新运行测试，确认页面骨架与导航通过**

Run: `uv run python -m unittest tests.test_wifi_management_controller_source -v`

Expected: PASS

- [ ] **Step 7: 提交**

```bash
git add main/ui/custom/wifi_management_controller.h main/ui/custom/wifi_management_controller.c main/CMakeLists.txt tests/test_wifi_management_controller_source.py
git commit -m "feat: 新增 WiFi 管理页骨架与导航"
```

### Task 9: 把页面按钮和设置接到真实 Wi-Fi 控制面

**Files:**
- Modify: `main/ui/custom/wifi_management_controller.c`
- Modify: `main/ui/custom/main_dropdown_controller.c`
- Modify: `main/services/network_service.c`
- Test: `tests/test_wifi_management_controller_source.py`
- Test: `tests/test_network_service_wifi_management_source.py`

- [ ] **Step 1: 扩写失败测试，锁定真实动作映射**

```python
self.assertIn("network_service_request_connect_with_saved_credentials()", source)
self.assertIn("network_service_request_disconnect()", source)
self.assertIn("network_service_request_reprovision()", source)
self.assertIn("network_service_set_default_provision_transport(", source)
self.assertIn("network_service_get_wifi_status(", source)
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `uv run python -m unittest tests.test_wifi_management_controller_source tests.test_network_service_wifi_management_source -v`

Expected: FAIL

- [ ] **Step 3: 绑定三个主操作按钮**

```c
static void wifi_management_retry_saved_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_request_connect_with_saved_credentials();
}
```

```c
static void wifi_management_disconnect_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_request_disconnect();
}
```

```c
static void wifi_management_reprovision_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_request_reprovision();
}
```

- [ ] **Step 4: 绑定默认 transport 设置项**

```c
static void wifi_management_set_transport(
    network_service_provision_transport_t transport)
{
    (void)network_service_set_default_provision_transport(transport);
}
```

- [ ] **Step 5: 周期刷新页面状态文案与按钮使能**

```c
network_service_wifi_status_t status = {0};
network_service_get_wifi_status(&status);
if (status.wifi_connected) { lv_label_set_text(s_status_title, "已连接"); }
else if (status.user_disconnect_latched) { lv_label_set_text(s_status_title, "已断开"); }
else if (status.ble_active || status.ap_active) { lv_label_set_text(s_status_title, "正在重新配网"); }
else if (status.has_credentials) { lv_label_set_text(s_status_title, "未连接"); }
else { lv_label_set_text(s_status_title, "未连接"); }
```

- [ ] **Step 6: 同步主界面 Wi-Fi 图标**

```c
/* Wi-Fi 页执行动作后不直接手搓图标状态，仍由 main_dropdown_controller 定时读取 network_service_is_wifi_connected() 收口。 */
```

- [ ] **Step 7: 重新运行测试，确认页面动作接线通过**

Run: `uv run python -m unittest tests.test_wifi_management_controller_source tests.test_network_service_wifi_management_source tests.test_main_screen_wifi_entry_source -v`

Expected: PASS

- [ ] **Step 8: 提交**

```bash
git add main/ui/custom/wifi_management_controller.c main/ui/custom/main_dropdown_controller.c main/services/network_service.c tests/test_wifi_management_controller_source.py tests/test_network_service_wifi_management_source.py tests/test_main_screen_wifi_entry_source.py
git commit -m "feat: 接通 WiFi 管理页真实联网控制"
```

## Chunk 4: 文档、上下文与验证闭环

### Task 10: 更新上下文库，避免旧 BLE 入口文档继续指挥新实现

**Files:**
- Modify: `docs/context/knowledge/project/ble-provisioning-ui-toggle-behavior.md`
- Create: `docs/context/knowledge/project/wifi-management-ui-behavior.md`
- Modify: `docs/context/CHANGELOG.md`

- [ ] **Step 1: 写文档改动**

```md
- `ble-provisioning-ui-toggle-behavior.md`：
  明确其只描述 2026-04-17 之前阶段性主界面行为，当前主界面入口已迁移到 `screen_main_Wifi`。

- `wifi-management-ui-behavior.md`：
  记录 Wi-Fi 图标蓝/灰两态、Wi-Fi 管理页三按钮语义、默认 transport 设置位置，以及“断开联网后暂停自动重连”的规则。
```

- [ ] **Step 2: 记录 CHANGELOG**

```md
- 2026-04-17：新增 Wi-Fi 管理 UI 设计与实现计划，正式把主界面联网入口从 BLE 专用入口迁移到 `screen_main_Wifi` 的统一 Wi-Fi 语义。
```

- [ ] **Step 3: 提交**

```bash
git add docs/context/knowledge/project/ble-provisioning-ui-toggle-behavior.md docs/context/knowledge/project/wifi-management-ui-behavior.md docs/context/CHANGELOG.md
git commit -m "docs: 更新 WiFi 管理 UI 上下文"
```

### Task 11: 运行源码级测试闭环

**Files:**
- Test only

- [ ] **Step 1: 运行 Wi-Fi 管理相关源码测试**

Run:

```powershell
uv run python -m unittest `
  tests.test_network_service_wifi_management_source `
  tests.test_main_screen_wifi_entry_source `
  tests.test_wifi_management_controller_source `
  tests.test_main_screen_ble_toggle_source -v
```

Expected:

```text
Ran X tests in Ys
OK
```

- [ ] **Step 2: 若有旧测试受影响，补跑现有关联回归**

Run:

```powershell
uv run python -m unittest `
  tests.test_network_service_ble_source `
  tests.test_nonblocking_boot_source `
  tests.test_ai_chat_view_source -v
```

Expected: 全部 PASS，确保没有因为 Wi-Fi 语义收口把现有网络状态消费者打坏。

- [ ] **Step 3: 提交**

```bash
git add .
git commit -m "test: 补齐 WiFi 管理 UI 回归测试"
```

### Task 12: 运行构建验证

**Files:**
- Test only

- [ ] **Step 1: 构建前确认本轮没有改 `sdkconfig`**

Expected: 若未改 `sdkconfig`，本轮不需要 `fullclean`。

- [ ] **Step 2: 运行 ESP-IDF 构建**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

Expected:

```text
Project build complete.
```

- [ ] **Step 3: 记录产物和风险**

```md
- 记录 `build/111.bin` 大小
- 记录是否新增明显 warning
- 若 UI 页新增资源较多，确认 app 分区余量仍安全
```

- [ ] **Step 4: 提交**

```bash
git add .
git commit -m "build: 验证 WiFi 管理 UI 改动可编译"
```

## 风险与回滚

- `wifi_manager` 当前自动重连逻辑在事件回调里；若“暂停自动重连”改动不完整，用户点“断开联网”后可能仍会被底层自动拉回。
  - 回滚方式：先回退 `wifi_manager` 自动重连闸门改动，只保留 UI 页面与状态展示。
- `screen_main_Bluetooth` 来自 generated 布局；若直接删除绑定导致 generated 页面仍保留可交互对象，可能出现视觉残留。
  - 回滚方式：先改成 runtime `hidden + disabled`，不要在本轮改 generated 布局文件。
- `重连` 语义与“清空旧凭据”不是同一件事；本轮默认不做 NVS 擦除，避免扩大不可逆风险。
  - 已知边界：若用户在重连页退出且未完成新配网，重启后设备仍可能继续尝试旧凭据。
- 现有 `official_chat` / AI 页仍依赖旧的 `network_service_state_t`。
  - 回滚方式：保持旧状态枚举不删，只新增 Wi-Fi façade API，避免一次性破坏老消费者。

## 执行顺序建议

1. 先做 Chunk 1，把“断开联网并暂停自动重连”的基础能力做实。
2. 再做 Chunk 2，把 `screen_main_Wifi` 接成真实入口，同时撤掉 BLE 主入口。
3. 最后做 Chunk 3，避免页面先做出来但没有真实动作可接。
4. 文档和验证放在最后统一收口，但不要跳过。

Plan complete and saved to `docs/superpowers/plans/2026-04-17-wifi-management-ui-implementation.md`. Ready to execute?
