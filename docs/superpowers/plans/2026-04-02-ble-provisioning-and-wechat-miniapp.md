# BLE Provisioning And WeChat Mini Program Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为当前 ESP32-S3 项目增加“BLE 配网主路径 + 现有 AP 网页兜底”能力，第一阶段完成固件端基础代码，第二阶段完成微信小程序最小可用客户端。

**Architecture:** 保持 `components/wifi_provision` 作为唯一配网协调器和 `wifi_manager` 唯一 Wi-Fi owner，在组件内部新增适合 `ESP32-S3` 的 `NimBLE` GATT 传输层和轻量 JSON 协议；`network_service` 只调整启动策略和状态映射，不重写当前后台联网模型。微信小程序端使用自定义 BLE 协议直接与设备通讯，先支持手动输入 SSID/密码，不做设备端 Wi-Fi 扫描列表。

**Tech Stack:** ESP-IDF 5.5.3, ESP32-S3, NimBLE, FreeRTOS, C, WeChat Mini Program (WXML/WXSS/JS), Python `unittest` source-contract tests, `uv`

---

## File Map

### Create

- `D:\esp32S3\111\sdkconfig.defaults`
- 新增适合 `ESP32-S3` 的 `NimBLE` 基线配置，只放本轮新增配置，不搬运整份 `sdkconfig`。
- `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_transport.h`
  - BLE 广播、连接、通知与 GATT 注册接口。
- `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_transport.c`
  - NimBLE 初始化、服务 UUID、RX/TX characteristic 和 notify 发送。
- `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_protocol.h`
  - BLE JSON 协议的命令/状态帮助函数。
- `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_protocol.c`
  - 解析 `hello/status/set_wifi/start_ap_fallback`，构造状态上报 JSON。
- `D:\esp32S3\111\tests\test_wifi_provision_ble_source.py`
  - 校验 BLE transport、对外 API 和协议命令字符串已接上。
- `D:\esp32S3\111\tests\test_network_service_ble_source.py`
  - 校验无凭据时走 BLE 首选，有 AP 兜底 API 保留。
- `D:\esp32S3\111\tests\test_ble_sdkconfig_defaults_source.py`
  - 校验 `sdkconfig.defaults` 含 BLE 最小配置。
- `D:\esp32S3\111\wechat-miniapp\app.js`
- `D:\esp32S3\111\wechat-miniapp\app.json`
- `D:\esp32S3\111\wechat-miniapp\app.wxss`
- `D:\esp32S3\111\wechat-miniapp\pages\index\index.js`
- `D:\esp32S3\111\wechat-miniapp\pages\index\index.wxml`
- `D:\esp32S3\111\wechat-miniapp\pages\index\index.wxss`
- `D:\esp32S3\111\wechat-miniapp\pages\index\index.json`
- `D:\esp32S3\111\wechat-miniapp\utils\ble-client.js`
- `D:\esp32S3\111\wechat-miniapp\utils\ble-protocol.js`
- `D:\esp32S3\111\tests\test_wechat_miniapp_ble_contract.py`
  - 校验小程序协议关键字、页面按钮和 BLE 调用存在。

### Modify

- `D:\esp32S3\111\components\wifi_provision\CMakeLists.txt`
  - 增加 BLE 子目录源码和 `bt` 相关依赖。
- `D:\esp32S3\111\components\wifi_provision\include\wifi_provision.h`
  - 新增 BLE 启停、状态和服务名接口。
- `D:\esp32S3\111\components\wifi_provision\src\wifi_provision.c`
  - 接入 BLE 状态机，与现有 Wi-Fi 成功/失败回调联动。
- `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.h`
  - 暴露本轮需要的最小 helper，例如连接失败原因或凭据连接辅助。
- `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.c`
  - 视需要补最小 helper，不重构现有主逻辑。
- `D:\esp32S3\111\main\network_service.h`
  - 增加 BLE 配网相关状态枚举或辅助接口。
- `D:\esp32S3\111\main\network_service.c`
  - 无凭据时改为自动启动 BLE 配网，而不是直接进入 AP 门户。
- `D:\esp32S3\111\main\hardware_init.c`
  - 只保留 `wifi_provision_init()` 初始化，不在这里直接触发 BLE/AP。
- `D:\esp32S3\111\docs\context\knowledge\project\ble-provisioning-wechat-feasibility.md`
  - 固化本轮方案选择和边界。
- `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 增加本轮文档沉淀记录。

---

### Task 1: 先补固件侧源码契约测试，锁定 BLE 方案边界

**Files:**
- Create: `D:\esp32S3\111\tests\test_wifi_provision_ble_source.py`
- Create: `D:\esp32S3\111\tests\test_network_service_ble_source.py`
- Create: `D:\esp32S3\111\tests\test_ble_sdkconfig_defaults_source.py`

- [ ] **Step 1: 写 `wifi_provision` BLE API 失败测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class WifiProvisionBleSourceTests(unittest.TestCase):
    def test_wifi_provision_public_ble_apis_exist(self) -> None:
        header = (REPO_ROOT / "components" / "wifi_provision" / "include" / "wifi_provision.h").read_text(encoding="utf-8")
        source = (REPO_ROOT / "components" / "wifi_provision" / "src" / "wifi_provision.c").read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_blecfg", header)
        self.assertIn("wifi_provision_stop_blecfg", header)
        self.assertIn("wifi_provision_is_ble_active", header)
        self.assertIn("wifi_provision_get_ble_service_name", header)
        self.assertIn("start_blecfg", source)
```

- [ ] **Step 2: 运行测试确认失败**

Run: `uv run python -m unittest tests.test_wifi_provision_ble_source -v`
Expected: FAIL，提示 `wifi_provision_start_blecfg` 不存在。

- [ ] **Step 3: 写 `network_service` BLE 首选策略失败测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class NetworkServiceBleSourceTests(unittest.TestCase):
    def test_network_service_prefers_ble_when_no_credentials(self) -> None:
        source = (REPO_ROOT / "main" / "network_service.c").read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_blecfg()", source)
        self.assertIn("wifi_provision_start_auto()", source)
        self.assertIn("NETWORK_SERVICE_STATE_BLE_PROVISIONING", source)
        self.assertIn("network_service_request_portal", source)
```

- [ ] **Step 4: 运行测试确认失败**

Run: `uv run python -m unittest tests.test_network_service_ble_source -v`
Expected: FAIL，提示 `NETWORK_SERVICE_STATE_BLE_PROVISIONING` 不存在。

- [ ] **Step 5: 写 BLE 配置基线失败测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class BleSdkconfigDefaultsSourceTests(unittest.TestCase):
    def test_sdkconfig_defaults_contains_minimum_ble_flags(self) -> None:
        source = (REPO_ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")

        self.assertIn("CONFIG_BT_ENABLED=y", source)
self.assertIn("CONFIG_BT_ENABLED=y", source)
self.assertIn("CONFIG_BT_NIMBLE_ENABLED=y", source)
```

- [ ] **Step 6: 运行测试确认失败**

Run: `uv run python -m unittest tests.test_ble_sdkconfig_defaults_source -v`
Expected: FAIL，提示 `sdkconfig.defaults` 不存在。

- [ ] **Step 7: 提交**

```bash
git add tests/test_wifi_provision_ble_source.py tests/test_network_service_ble_source.py tests/test_ble_sdkconfig_defaults_source.py
git commit -m "测试: 增加 BLE 配网方案的源码契约"
```

### Task 2: 建立 BLE 构建基线和最小配置

**Files:**
- Create: `D:\esp32S3\111\sdkconfig.defaults`
- Modify: `D:\esp32S3\111\components\wifi_provision\CMakeLists.txt`
- Test: `D:\esp32S3\111\tests\test_ble_sdkconfig_defaults_source.py`

- [ ] **Step 1: 新建 `sdkconfig.defaults` 只放 BLE 基线**

```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
```

不要在第一阶段加入：

```ini
CONFIG_BT_BLUEDROID_ENABLED=y
```

- [ ] **Step 2: 扩展 `wifi_provision` 组件依赖**

在 `D:\esp32S3\111\components\wifi_provision\CMakeLists.txt` 的 `SRCS` 增加：

```cmake
        "src/ble_server/ble_provision_transport.c"
        "src/ble_server/ble_provision_protocol.c"
```

在 `REQUIRES` 增加：

```cmake
        bt
```

- [ ] **Step 3: 运行配置测试确认通过**

Run: `uv run python -m unittest tests.test_ble_sdkconfig_defaults_source -v`
Expected: PASS

- [ ] **Step 4: 先做一次配置级构建验证**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py fullclean
idf.py build
```

Expected: 允许因为 BLE 源文件尚未实现而编译失败，但不能再报 `CONFIG_BT_ENABLED` 未配置或 `bt` 依赖缺失。

- [ ] **Step 5: 提交**

```bash
git add sdkconfig.defaults components/wifi_provision/CMakeLists.txt
git commit -m "配置: 建立 BLE 配网构建基线"
```

### Task 3: 在 `wifi_provision` 内实现最小 BLE transport 和 JSON 协议

**Files:**
- Create: `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_transport.h`
- Create: `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_transport.c`
- Create: `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_protocol.h`
- Create: `D:\esp32S3\111\components\wifi_provision\src\ble_server\ble_provision_protocol.c`
- Modify: `D:\esp32S3\111\components\wifi_provision\include\wifi_provision.h`
- Modify: `D:\esp32S3\111\components\wifi_provision\src\wifi_provision.c`
- Test: `D:\esp32S3\111\tests\test_wifi_provision_ble_source.py`

- [ ] **Step 1: 补 `wifi_provision` 对外 BLE 接口**

在 `wifi_provision.h` 新增：

```c
esp_err_t wifi_provision_start_blecfg(void);
esp_err_t wifi_provision_stop_blecfg(void);
bool wifi_provision_is_ble_active(void);
esp_err_t wifi_provision_get_ble_service_name(char *service_name, size_t service_name_len);
```

- [ ] **Step 2: 定义协议命令与状态帮助函数**

在 `ble_provision_protocol.h` 定义：

```c
typedef enum {
    BLE_PROV_CMD_INVALID = 0,
    BLE_PROV_CMD_HELLO,
    BLE_PROV_CMD_STATUS,
    BLE_PROV_CMD_SET_WIFI,
    BLE_PROV_CMD_START_AP_FALLBACK,
} ble_prov_cmd_t;

typedef struct {
    ble_prov_cmd_t cmd;
    char ssid[33];
    char password[65];
} ble_prov_request_t;
```

- [ ] **Step 3: 先实现最小 JSON 解析**

要求支持解析：

```json
{"cmd":"hello"}
{"cmd":"status"}
{"cmd":"set_wifi","ssid":"MyWiFi","password":"12345678"}
{"cmd":"start_ap_fallback"}
```

- [ ] **Step 4: 实现 transport 的最小 GATT 接口**

transport 侧至少要有：

```c
esp_err_t ble_provision_transport_start(const char *device_name);
esp_err_t ble_provision_transport_stop(void);
bool ble_provision_transport_is_active(void);
esp_err_t ble_provision_transport_notify_json(const char *json_payload);
```

- [ ] **Step 5: 在 `wifi_provision.c` 接入 BLE 事件处理**

处理逻辑要求：

- `hello`：返回设备名和版本
- `status`：返回当前配网状态
- `set_wifi`：保存凭据并触发 `wifi_manager_connect()`
- `start_ap_fallback`：调用 `wifi_provision_start_apcfg()`

- [ ] **Step 6: 运行 BLE API 测试确认通过**

Run: `uv run python -m unittest tests.test_wifi_provision_ble_source -v`
Expected: PASS

- [ ] **Step 7: 构建验证**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py fullclean
idf.py build
```

Expected: 编译进入 `main` 链路，不再停在 BLE transport 缺符号阶段。

- [ ] **Step 8: 提交**

```bash
git add components/wifi_provision/include/wifi_provision.h components/wifi_provision/src/wifi_provision.c components/wifi_provision/src/ble_server
git commit -m "功能: 为 wifi_provision 增加 BLE 配网传输层"
```

### Task 4: 把 BLE 首选策略接进后台联网主流程，并保留 AP 兜底

**Files:**
- Modify: `D:\esp32S3\111\main\network_service.h`
- Modify: `D:\esp32S3\111\main\network_service.c`
- Modify: `D:\esp32S3\111\main\hardware_init.c`
- Test: `D:\esp32S3\111\tests\test_network_service_ble_source.py`

- [ ] **Step 1: 扩展 `network_service` 状态枚举**

在 `network_service.h` 增加：

```c
    NETWORK_SERVICE_STATE_BLE_PROVISIONING,
```

保持已有：

```c
    NETWORK_SERVICE_STATE_PORTAL_REQUIRED,
```

用于表示 AP 兜底仍然存在。

- [ ] **Step 2: 调整无凭据时的启动策略**

在 `network_service_task()` 中改成：

```c
    if (wifi_provision_has_credentials()) {
        s_network_state = NETWORK_SERVICE_STATE_CONNECTING;
        ret = wifi_provision_start_auto();
    } else {
        s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
        ret = wifi_provision_start_blecfg();
    }
```

- [ ] **Step 3: 保留 AP 入口**

继续保留：

```c
void network_service_request_portal(void) {
    wifi_provision_start_apcfg();
    s_network_state = NETWORK_SERVICE_STATE_PORTAL_REQUIRED;
}
```

不要移除现有按钮触发 AP 的路径。

- [ ] **Step 4: 运行源码测试确认通过**

Run: `uv run python -m unittest tests.test_network_service_ble_source -v`
Expected: PASS

- [ ] **Step 5: 执行固件完整构建验证**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py fullclean
idf.py build
```

Expected: PASS

- [ ] **Step 6: 真机日志验证**

Run:

```powershell
$env:ESP_IDF_MONITOR_TEST='1'
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py -p COM3 flash monitor
```

Expected log:

- 无凭据时打印 BLE 广播已启动
- 小程序写入后打印 `开始连接 Wi-Fi`
- 成功后打印 `获取到 STA IP`

- [ ] **Step 7: 清理 monitor 进程**

Run:

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*idf_monitor*' -or $_.ProcessName -like '*python*' } | Out-String
```

Expected: 确认无残留占用串口的 `idf_monitor` 相关进程，必要时手工停止。

- [ ] **Step 8: 提交**

```bash
git add main/network_service.h main/network_service.c main/hardware_init.c
git commit -m "功能: 将 BLE 设为默认配网入口并保留 AP 兜底"
```

### Task 5: 沉淀上下文和最小使用说明

**Files:**
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\ble-provisioning-wechat-feasibility.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 写知识卡**

记录：

- 为什么阶段 1 不选官方 `wifi_prov_mgr`
- 为什么 BLE 先不做 Wi-Fi 扫描列表
- BLE 主路径和 AP 兜底的启动规则
- 第一阶段安全边界

- [ ] **Step 2: 在 `CHANGELOG` 增加一行**

格式参考现有文件，增加：

```markdown
- 2026-04-02：新增 BLE 配网与微信小程序方案设计，确定“自定义 BLE GATT 主路径 + 现有 AP 网页兜底 + 先固件后小程序”的实施边界。
```

- [ ] **Step 3: 提交**

```bash
git add docs/context/knowledge/project/ble-provisioning-wechat-feasibility.md docs/context/CHANGELOG.md
git commit -m "文档: 固化 BLE 配网方案与边界"
```

### Task 6: 搭建微信小程序骨架和 BLE 协议工具

**Files:**
- Create: `D:\esp32S3\111\wechat-miniapp\app.js`
- Create: `D:\esp32S3\111\wechat-miniapp\app.json`
- Create: `D:\esp32S3\111\wechat-miniapp\app.wxss`
- Create: `D:\esp32S3\111\wechat-miniapp\pages\index\index.js`
- Create: `D:\esp32S3\111\wechat-miniapp\pages\index\index.wxml`
- Create: `D:\esp32S3\111\wechat-miniapp\pages\index\index.wxss`
- Create: `D:\esp32S3\111\wechat-miniapp\pages\index\index.json`
- Create: `D:\esp32S3\111\wechat-miniapp\utils\ble-client.js`
- Create: `D:\esp32S3\111\wechat-miniapp\utils\ble-protocol.js`
- Create: `D:\esp32S3\111\tests\test_wechat_miniapp_ble_contract.py`

- [ ] **Step 1: 写小程序协议辅助层**

`ble-protocol.js` 至少导出：

```javascript
export function encodeHello() {
  return JSON.stringify({ cmd: 'hello' })
}

export function encodeStatus() {
  return JSON.stringify({ cmd: 'status' })
}

export function encodeSetWifi(ssid, password) {
  return JSON.stringify({ cmd: 'set_wifi', ssid, password })
}
```

- [ ] **Step 2: 写 BLE 客户端最小接口**

`ble-client.js` 至少封装：

```javascript
export async function startDiscovery() {}
export async function connectDevice(deviceId) {}
export async function subscribeStatus(deviceId) {}
export async function sendPayload(deviceId, text) {}
```

- [ ] **Step 3: 写页面基础结构**

页面至少有：

- 扫描按钮
- 设备列表
- SSID 输入框
- 密码输入框
- 发送配网按钮
- 状态文本

- [ ] **Step 4: 写小程序源码契约测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class WechatMiniappBleContractTests(unittest.TestCase):
    def test_miniapp_contains_minimum_ble_contract(self) -> None:
        index_js = (REPO_ROOT / "wechat-miniapp" / "pages" / "index" / "index.js").read_text(encoding="utf-8")
        protocol_js = (REPO_ROOT / "wechat-miniapp" / "utils" / "ble-protocol.js").read_text(encoding="utf-8")

        self.assertIn("set_wifi", protocol_js)
        self.assertIn("startDiscovery", index_js)
        self.assertIn("connectDevice", index_js)
        self.assertIn("sendWifiCredentials", index_js)
```

- [ ] **Step 5: 运行测试确认通过**

Run: `uv run python -m unittest tests.test_wechat_miniapp_ble_contract -v`
Expected: PASS

- [ ] **Step 6: 提交**

```bash
git add wechat-miniapp tests/test_wechat_miniapp_ble_contract.py
git commit -m "功能: 创建微信小程序 BLE 配网骨架"
```

### Task 7: 完成微信小程序配网页和状态联调

**Files:**
- Modify: `D:\esp32S3\111\wechat-miniapp\pages\index\index.js`
- Modify: `D:\esp32S3\111\wechat-miniapp\pages\index\index.wxml`
- Modify: `D:\esp32S3\111\wechat-miniapp\pages\index\index.wxss`
- Modify: `D:\esp32S3\111\wechat-miniapp\utils\ble-client.js`

- [ ] **Step 1: 页面加载时支持扫描设备**

要求支持按设备名前缀筛选，例如：

```javascript
const DEVICE_NAME_PREFIX = 'ESP32S3-'
```

- [ ] **Step 2: 建立连接后自动发送 `hello` 与 `status`**

连接成功后执行：

```javascript
await subscribeStatus(deviceId)
await sendPayload(deviceId, encodeHello())
await sendPayload(deviceId, encodeStatus())
```

- [ ] **Step 3: 提交 Wi-Fi 凭据**

按钮点击时执行：

```javascript
async function sendWifiCredentials() {
  const payload = encodeSetWifi(this.data.ssid, this.data.password)
  await sendPayload(this.data.deviceId, payload)
}
```

- [ ] **Step 4: 状态映射**

至少处理：

- `idle`
- `connecting`
- `connected`
- `failed`
- `ap_fallback`

其中 `ap_fallback` 页面要提示：

```text
可按设备按键进入 AP 网页配网，默认地址 http://192.168.100.1/
```

- [ ] **Step 5: 手工联调**

使用微信开发者工具验证：

1. 扫描设备
2. 连接设备
3. 写入 SSID/密码
4. 收到连接结果
5. 错误时显示 AP 兜底提示

- [ ] **Step 6: 提交**

```bash
git add wechat-miniapp/pages/index wechat-miniapp/utils/ble-client.js
git commit -m "功能: 完成微信小程序 BLE 配网页与状态联调"
```

### Task 8: 做完整闭环验证并准备后续增强

**Files:**
- Modify: `D:\esp32S3\111\docs\superpowers\specs\2026-04-02-ble-provisioning-wechat-miniapp-design.md`
- Modify: `D:\esp32S3\111\docs\superpowers\plans\2026-04-02-ble-provisioning-and-wechat-miniapp.md`

- [ ] **Step 1: 固件闭环验证**

验证组合：

1. 新板首次上电，无凭据，自动 BLE 广播
2. 小程序发送正确凭据，设备成功联网
3. 小程序发送错误凭据，设备返回失败
4. 用户按按键进入 AP 网页配网，旧路径仍可用

- [ ] **Step 2: 小程序闭环验证**

验证：

1. 多次扫描后仍可发现设备
2. 断开重连可恢复
3. 重复发送配网命令不会让页面卡死

- [ ] **Step 3: 记录剩余风险**

至少记录：

- 第一阶段没有安全握手
- 第一阶段没有设备端 Wi-Fi 扫描
- 未来若要兼容官方客户端，需要单独迁移协议

- [ ] **Step 4: 提交**

```bash
git add docs/superpowers/specs/2026-04-02-ble-provisioning-wechat-miniapp-design.md docs/superpowers/plans/2026-04-02-ble-provisioning-and-wechat-miniapp.md
git commit -m "文档: 完成 BLE 配网项目计划收尾"
```
