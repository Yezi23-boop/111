# SoftAP Official Provisioning Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将当前 SoftAP 门户从占位 `/api/scan`、`/api/configure` 迁移为“自定义 UI + 浏览器端官方 provisioning client”，让页面直接通过官方 `prov-*` endpoint 完成扫描、会话建立、凭据下发和状态轮询。

**Architecture:** 设备侧继续由 `network_provisioning_adapter` 拉起官方 `network_provisioning` SoftAP service，并通过 `ap_portal_adapter` 提供静态资源。浏览器端拆成 `app.js` UI 层与 `prov_client.js` 协议层，再由 `prov_proto_bundle.js` 提供最小 protobuf/security0 编解码，避免把协议细节散落进 UI。

**Tech Stack:** ESP-IDF 5.5.3, `espressif/network_provisioning`, `protocomm_httpd`, 纯静态 Web 资源（ES modules）, Python source tests via `uv run python -m pytest`

---

## File Structure

- Modify: `D:\esp32S3\111\components\ap_portal_adapter\CMakeLists.txt`
  - 把新增的浏览器端协议资源加入 `EMBED_TXTFILES`
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\index.html`
  - 把脚本入口切到 ES module，并为 UI 状态提示保留现有 DOM
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\app.js`
  - 收敛为 UI 层，只处理 DOM、按钮事件、结果渲染、状态提示
- Create: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`
  - 官方 provisioning client 高层封装：`proto-ver`、`prov-session`、`prov-scan`、`prov-config`
- Create: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_proto_bundle.js`
  - 最小 protobuf/security0 编解码与 HTTP binary transport helper
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\src\ap_portal_routes.c`
  - 保留静态资源与 `/api/status`，把 `/api/scan`、`/api/configure` 收敛为兼容提示路径
- Modify: `D:\esp32S3\111\tests\main_paths.py`
  - 为新 Web 资源和测试文件补路径常量
- Create: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`
  - 为门户资源、协议层分层、endpoint 使用方式和兼容接口收口写源码级测试
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\softap-provisioning-placeholder-api-limit.md`
  - 把“当前仍是 501 占位”更新为“已迁到官方前端 client”
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 追加本轮上下文库变更记录

### Task 1: Asset Plumbing And Source-Test Scaffold

**Files:**
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\CMakeLists.txt`
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\index.html`
- Modify: `D:\esp32S3\111\tests\main_paths.py`
- Create: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`
- Create: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`
- Create: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_proto_bundle.js`

- [ ] **Step 1: Write the failing source test for new portal assets**

```python
import unittest

from tests.main_paths import AP_PORTAL_ADAPTER_DIR
from tests.main_paths import AP_PORTAL_WEB_INDEX
from tests.main_paths import AP_PORTAL_WEB_JS
from tests.main_paths import AP_PORTAL_WEB_PROV_CLIENT
from tests.main_paths import AP_PORTAL_WEB_PROTO_BUNDLE


class ApPortalOfficialClientSourceTests(unittest.TestCase):
    def test_cmake_embeds_official_client_assets(self) -> None:
        cmake = (AP_PORTAL_ADAPTER_DIR / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('"web/prov_client.js"', cmake)
        self.assertIn('"web/prov_proto_bundle.js"', cmake)

    def test_index_loads_app_js_as_module_entry(self) -> None:
        html = AP_PORTAL_WEB_INDEX.read_text(encoding="utf-8")

        self.assertIn('<script type="module" src="/app.js"></script>', html)

    def test_official_client_modules_exist(self) -> None:
        self.assertTrue(AP_PORTAL_WEB_JS.exists())
        self.assertTrue(AP_PORTAL_WEB_PROV_CLIENT.exists())
        self.assertTrue(AP_PORTAL_WEB_PROTO_BUNDLE.exists())
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
FAIL ... AP_PORTAL_WEB_PROV_CLIENT ... does not exist
FAIL ... "web/prov_client.js" not found in CMakeLists.txt
```

- [ ] **Step 3: Add path constants, module script entry, and empty asset stubs**

`D:\esp32S3\111\tests\main_paths.py`

```python
AP_PORTAL_WEB_PROV_CLIENT = AP_PORTAL_WEB_DIR / "prov_client.js"
AP_PORTAL_WEB_PROTO_BUNDLE = AP_PORTAL_WEB_DIR / "prov_proto_bundle.js"
```

`D:\esp32S3\111\components\ap_portal_adapter\CMakeLists.txt`

```cmake
    EMBED_TXTFILES
        "web/index.html"
        "web/app.js"
        "web/app.css"
        "web/prov_client.js"
        "web/prov_proto_bundle.js"
```

`D:\esp32S3\111\components\ap_portal_adapter\web\index.html`

```html
    <script type="module" src="/app.js"></script>
</body>
```

`D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`

```javascript
export function createProvisioningClient() {
    return {
        async getPortalInfo() {
            throw new Error("not_implemented");
        },
        async initSession() {
            throw new Error("not_implemented");
        },
        async scanWifi() {
            throw new Error("not_implemented");
        },
        async sendWifiConfig(_ssid, _password) {
            throw new Error("not_implemented");
        },
    };
}
```

`D:\esp32S3\111\components\ap_portal_adapter\web\prov_proto_bundle.js`

```javascript
export function encodeSessionSetup0Request() {
    throw new Error("not_implemented");
}

export function decodeSessionSetup0Response(_buffer) {
    throw new Error("not_implemented");
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
3 passed
```

- [ ] **Step 5: Commit**

```powershell
git add tests/main_paths.py tests/test_ap_portal_official_client_source.py components/ap_portal_adapter/CMakeLists.txt components/ap_portal_adapter/web/index.html components/ap_portal_adapter/web/prov_client.js components/ap_portal_adapter/web/prov_proto_bundle.js
git commit -m "规划 SoftAP 门户官方客户端资源骨架"
```

### Task 2: Split UI From Protocol Layer

**Files:**
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\app.js`
- Modify: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`
- Test: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`

- [ ] **Step 1: Extend failing test for UI/protocol split**

Append to `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`:

```python
    def test_app_js_imports_provisioning_client_and_stops_calling_json_bridge(self) -> None:
        source = AP_PORTAL_WEB_JS.read_text(encoding="utf-8")

        self.assertIn('import { createProvisioningClient } from "./prov_client.js";', source)
        self.assertIn("const provClient = createProvisioningClient();", source)
        self.assertNotIn('fetch("/api/scan"', source)
        self.assertNotIn('fetch("/api/configure"', source)
        self.assertNotIn("callPendingApi(", source)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
FAIL ... import { createProvisioningClient } from "./prov_client.js";
```

- [ ] **Step 3: Rewrite app.js as pure UI entry**

Replace `D:\esp32S3\111\components\ap_portal_adapter\web\app.js` with:

```javascript
import { createProvisioningClient } from "./prov_client.js";

const provClient = createProvisioningClient();
const apiDot = document.getElementById("api-dot");
const apiText = document.getElementById("api-text");
const apiNote = document.getElementById("api-note");
const scanCapability = document.getElementById("scan-capability");
const wifiList = document.getElementById("wifi-list");
const feedback = document.getElementById("feedback");
const scanBtn = document.getElementById("scan-btn");
const refreshBtn = document.getElementById("refresh-btn");
const configureBtn = document.getElementById("configure-btn");
const ssidInput = document.getElementById("ssid");
const passwordInput = document.getElementById("password");

function setFeedback(type, message) {
    feedback.className = `feedback ${type}`;
    feedback.textContent = message;
}

function clearFeedback() {
    feedback.className = "feedback hidden";
    feedback.textContent = "";
}

function renderWifiNetworks(entries) {
    if (!entries.length) {
        wifiList.className = "wifi-list empty";
        wifiList.innerHTML = `
            <div class="empty-state">
                <strong>No scan results yet</strong>
                <span>Tap "Scan Wi-Fi" to query the device.</span>
            </div>
        `;
        return;
    }

    wifiList.className = "wifi-list";
    wifiList.innerHTML = entries.map((entry) => `
        <article class="wifi-item" data-ssid="${entry.ssid}">
            <div>
                <strong>${entry.ssid}</strong>
                <span>RSSI ${entry.rssi} dBm • Channel ${entry.channel}</span>
            </div>
        </article>
    `).join("");
}

async function refreshPortalStatus() {
    apiDot.className = "status-dot";
    apiText.textContent = "Checking provisioning endpoint...";
    apiNote.textContent = "Probing proto-ver and provisioning session readiness.";
    clearFeedback();

    try {
        const info = await provClient.getPortalInfo();
        apiDot.className = "status-dot online";
        apiText.textContent = "Provisioning portal is reachable";
        apiNote.textContent = `Version: ${info.version}. Security: ${info.security}.`;
        scanCapability.textContent = "Official client";
    } catch (error) {
        apiDot.className = "status-dot error";
        apiText.textContent = "Provisioning portal is unavailable";
        apiNote.textContent = "The page loaded, but the official provisioning endpoints are not ready.";
        setFeedback("error", error.message);
    }
}

scanBtn.addEventListener("click", async () => {
    clearFeedback();
    setFeedback("info", "Starting Wi-Fi scan...");
    try {
        const entries = await provClient.scanWifi();
        renderWifiNetworks(entries);
        setFeedback("info", `Scan complete: ${entries.length} network(s).`);
    } catch (error) {
        setFeedback("error", error.message);
    }
});

configureBtn.addEventListener("click", async () => {
    clearFeedback();
    const ssid = ssidInput.value.trim();
    const password = passwordInput.value;

    if (!ssid) {
        setFeedback("warning", "Please enter a Wi-Fi name first.");
        return;
    }

    try {
        setFeedback("info", "Sending credentials to the device...");
        await provClient.sendWifiConfig(ssid, password);
        setFeedback("info", "Credentials sent. Waiting for connection status...");
    } catch (error) {
        setFeedback("error", error.message);
    }
});

refreshBtn.addEventListener("click", refreshPortalStatus);
refreshPortalStatus();
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
4 passed
```

- [ ] **Step 5: Commit**

```powershell
git add components/ap_portal_adapter/web/app.js tests/test_ap_portal_official_client_source.py
git commit -m "拆分 SoftAP 门户 UI 与协议层入口"
```

### Task 3: Implement Minimal Protobuf And Security0 Codec

**Files:**
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_proto_bundle.js`
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`
- Modify: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`

- [ ] **Step 1: Add failing tests for official endpoint usage and codec helpers**

Append to `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`:

```python
    def test_prov_client_targets_official_endpoints(self) -> None:
        source = AP_PORTAL_WEB_PROV_CLIENT.read_text(encoding="utf-8")

        self.assertIn('"/proto-ver"', source)
        self.assertIn('"/prov-session"', source)
        self.assertIn('"/prov-scan"', source)
        self.assertIn('"/prov-config"', source)

    def test_proto_bundle_exposes_security0_and_wifi_codec_helpers(self) -> None:
        source = AP_PORTAL_WEB_PROTO_BUNDLE.read_text(encoding="utf-8")

        self.assertIn("export function encodeSessionSetup0Request()", source)
        self.assertIn("export function decodeSessionSetup0Response(buffer)", source)
        self.assertIn("export function encodeScanStartRequest()", source)
        self.assertIn("export function decodeScanResultResponse(buffer)", source)
        self.assertIn("export function encodeSetWifiConfigRequest(", source)
        self.assertIn("export function decodeGetWifiStatusResponse(buffer)", source)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
FAIL ... "/prov-scan" not found
FAIL ... encodeScanStartRequest not found
```

- [ ] **Step 3: Fill in low-level codec and transport helpers**

`D:\esp32S3\111\components\ap_portal_adapter\web\prov_proto_bundle.js`

```javascript
function encodeVarint(value) {
    const bytes = [];
    let current = value >>> 0;
    while (current > 0x7f) {
        bytes.push((current & 0x7f) | 0x80);
        current >>>= 7;
    }
    bytes.push(current);
    return Uint8Array.from(bytes);
}

function encodeFieldHeader(fieldNumber, wireType) {
    return encodeVarint((fieldNumber << 3) | wireType);
}

function concatBytes(...chunks) {
    const total = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
    const merged = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        merged.set(chunk, offset);
        offset += chunk.length;
    }
    return merged;
}

function encodeLengthDelimitedField(fieldNumber, payload) {
    return concatBytes(
        encodeFieldHeader(fieldNumber, 2),
        encodeVarint(payload.length),
        payload,
    );
}

function encodeEnumField(fieldNumber, value) {
    return concatBytes(encodeFieldHeader(fieldNumber, 0), encodeVarint(value));
}

function encodeBytesField(fieldNumber, text) {
    return encodeLengthDelimitedField(fieldNumber, new TextEncoder().encode(text));
}

function readVarint(bytes, offset) {
    let value = 0;
    let shift = 0;
    let index = offset;
    while (index < bytes.length) {
        const current = bytes[index];
        value |= (current & 0x7f) << shift;
        index += 1;
        if ((current & 0x80) === 0) {
            return { value, offset: index };
        }
        shift += 7;
    }
    throw new Error("Malformed protobuf varint");
}

function readLengthDelimited(bytes, offset) {
    const lengthInfo = readVarint(bytes, offset);
    const end = lengthInfo.offset + lengthInfo.value;
    return {
        value: bytes.slice(lengthInfo.offset, end),
        offset: end,
    };
}

export function encodeSessionSetup0Request() {
    const sec0Payload = concatBytes(
        encodeEnumField(1, 0),
        encodeLengthDelimitedField(20, new Uint8Array(0)),
    );
    return concatBytes(
        encodeEnumField(2, 0),
        encodeLengthDelimitedField(10, sec0Payload),
    );
}

export function decodeSessionSetup0Response(buffer) {
    const bytes = new Uint8Array(buffer);
    let offset = 0;
    let secVer = null;
    while (offset < bytes.length) {
        const header = readVarint(bytes, offset);
        const fieldNumber = header.value >> 3;
        const wireType = header.value & 0x07;
        offset = header.offset;

        if (wireType === 0) {
            const field = readVarint(bytes, offset);
            if (fieldNumber === 2) {
                secVer = field.value;
            }
            offset = field.offset;
            continue;
        }

        if (wireType === 2) {
            const field = readLengthDelimited(bytes, offset);
            offset = field.offset;
            continue;
        }

        throw new Error("Unsupported wire type in SessionData");
    }

    if (secVer !== 0) {
        throw new Error("Invalid security0 session response");
    }
    return { security: "sec0" };
}

export function encodeScanStartRequest() {
    const payload = concatBytes(
        encodeEnumField(1, 1),
        encodeEnumField(2, 0),
        encodeEnumField(3, 5),
        encodeEnumField(4, 120),
    );
    return concatBytes(
        encodeEnumField(1, 0),
        encodeEnumField(2, 0),
        encodeLengthDelimitedField(10, payload),
    );
}

export function encodeScanStatusRequest() {
    return concatBytes(
        encodeEnumField(1, 2),
        encodeEnumField(2, 0),
        encodeLengthDelimitedField(12, new Uint8Array(0)),
    );
}

export function encodeScanResultRequest(startIndex, count) {
    const payload = concatBytes(
        encodeEnumField(1, startIndex),
        encodeEnumField(2, count),
    );
    return concatBytes(
        encodeEnumField(1, 4),
        encodeEnumField(2, 0),
        encodeLengthDelimitedField(14, payload),
    );
}

export function decodeScanStatusResponse(buffer) {
    const bytes = new Uint8Array(buffer);
    let offset = 0;
    let result = { scanFinished: false, resultCount: 0 };

    while (offset < bytes.length) {
        const header = readVarint(bytes, offset);
        const fieldNumber = header.value >> 3;
        const wireType = header.value & 0x07;
        offset = header.offset;

        if (fieldNumber === 13 && wireType === 2) {
            const nested = readLengthDelimited(bytes, offset);
            let nestedOffset = 0;
            while (nestedOffset < nested.value.length) {
                const nestedHeader = readVarint(nested.value, nestedOffset);
                const nestedFieldNumber = nestedHeader.value >> 3;
                nestedOffset = nestedHeader.offset;
                const nestedValue = readVarint(nested.value, nestedOffset);
                if (nestedFieldNumber === 1) {
                    result.scanFinished = nestedValue.value === 1;
                }
                if (nestedFieldNumber === 2) {
                    result.resultCount = nestedValue.value;
                }
                nestedOffset = nestedValue.offset;
            }
            offset = nested.offset;
            continue;
        }

        if (wireType === 0) {
            offset = readVarint(bytes, offset).offset;
            continue;
        }
        if (wireType === 2) {
            offset = readLengthDelimited(bytes, offset).offset;
            continue;
        }
        throw new Error("Unsupported wire type in scan status");
    }

    return result;
}

export function decodeScanResultResponse(buffer) {
    const bytes = new Uint8Array(buffer);
    let offset = 0;
    const entries = [];

    while (offset < bytes.length) {
        const header = readVarint(bytes, offset);
        const fieldNumber = header.value >> 3;
        const wireType = header.value & 0x07;
        offset = header.offset;

        if (fieldNumber === 15 && wireType === 2) {
            const nested = readLengthDelimited(bytes, offset);
            let nestedOffset = 0;
            const entry = { ssid: "", channel: 0, rssi: 0, auth: 0 };
            while (nestedOffset < nested.value.length) {
                const nestedHeader = readVarint(nested.value, nestedOffset);
                const nestedFieldNumber = nestedHeader.value >> 3;
                const nestedWireType = nestedHeader.value & 0x07;
                nestedOffset = nestedHeader.offset;

                if (nestedWireType === 2) {
                    const field = readLengthDelimited(nested.value, nestedOffset);
                    if (nestedFieldNumber === 1) {
                        entry.ssid = new TextDecoder().decode(field.value);
                    }
                    nestedOffset = field.offset;
                    continue;
                }

                const field = readVarint(nested.value, nestedOffset);
                if (nestedFieldNumber === 2) {
                    entry.channel = field.value;
                }
                if (nestedFieldNumber === 3) {
                    entry.rssi = field.value;
                }
                if (nestedFieldNumber === 5) {
                    entry.auth = field.value;
                }
                nestedOffset = field.offset;
            }
            entries.push(entry);
            offset = nested.offset;
            continue;
        }

        if (wireType === 0) {
            offset = readVarint(bytes, offset).offset;
            continue;
        }
        if (wireType === 2) {
            offset = readLengthDelimited(bytes, offset).offset;
            continue;
        }
        throw new Error("Unsupported wire type in scan result");
    }

    return entries;
}

export function encodeSetWifiConfigRequest(ssid, passphrase) {
    const payload = concatBytes(
        encodeBytesField(1, ssid),
        encodeBytesField(2, passphrase),
    );
    return concatBytes(
        encodeEnumField(1, 2),
        encodeLengthDelimitedField(12, payload),
    );
}

export function encodeApplyWifiConfigRequest() {
    return concatBytes(
        encodeEnumField(1, 4),
        encodeLengthDelimitedField(14, new Uint8Array(0)),
    );
}

export function encodeGetWifiStatusRequest() {
    return concatBytes(
        encodeEnumField(1, 0),
        encodeLengthDelimitedField(10, new Uint8Array(0)),
    );
}

export function decodeGetWifiStatusResponse(buffer) {
    const bytes = new Uint8Array(buffer);
    let offset = 0;
    let state = "unknown";

    while (offset < bytes.length) {
        const header = readVarint(bytes, offset);
        const fieldNumber = header.value >> 3;
        const wireType = header.value & 0x07;
        offset = header.offset;

        if (fieldNumber === 11 && wireType === 2) {
            const nested = readLengthDelimited(bytes, offset);
            let nestedOffset = 0;
            while (nestedOffset < nested.value.length) {
                const nestedHeader = readVarint(nested.value, nestedOffset);
                const nestedFieldNumber = nestedHeader.value >> 3;
                nestedOffset = nestedHeader.offset;

                if (nestedFieldNumber === 2) {
                    const nestedValue = readVarint(nested.value, nestedOffset);
                    if (nestedValue.value === 0) {
                        state = "connected";
                    } else if (nestedValue.value === 1) {
                        state = "connecting";
                    } else if (nestedValue.value === 3) {
                        state = "failed";
                    } else {
                        state = "disconnected";
                    }
                    nestedOffset = nestedValue.offset;
                    continue;
                }

                if ((nestedHeader.value & 0x07) === 0) {
                    nestedOffset = readVarint(nested.value, nestedOffset).offset;
                } else {
                    nestedOffset = readLengthDelimited(nested.value, nestedOffset).offset;
                }
            }
            offset = nested.offset;
            continue;
        }

        if (wireType === 0) {
            offset = readVarint(bytes, offset).offset;
            continue;
        }
        if (wireType === 2) {
            offset = readLengthDelimited(bytes, offset).offset;
            continue;
        }
        throw new Error("Unsupported wire type in wifi status");
    }

    return { state };
}
```

`D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`

```javascript
import {
    decodeGetWifiStatusResponse,
    decodeScanResultResponse,
    decodeScanStatusResponse,
    decodeSessionSetup0Response,
    encodeApplyWifiConfigRequest,
    encodeGetWifiStatusRequest,
    encodeScanResultRequest,
    encodeScanStartRequest,
    encodeScanStatusRequest,
    encodeSessionSetup0Request,
    encodeSetWifiConfigRequest,
} from "./prov_proto_bundle.js";

async function postBinary(path, payload, cookie) {
    const headers = { "Content-Type": "application/x-www-form-urlencoded", "Accept": "text/plain" };
    if (cookie) {
        headers.Cookie = cookie;
    }

    const response = await fetch(path, { method: "POST", headers, body: payload });
    const setCookie = response.headers.get("set-cookie");
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (!response.ok) {
        throw new Error(`${path} failed with ${response.status}`);
    }
    return { bytes, cookie: setCookie };
}

export function createProvisioningClient() {
    let cookie = null;

    return {
        async getPortalInfo() {
            const response = await fetch("/proto-ver", { cache: "no-store" });
            const version = await response.text();
            if (!response.ok) {
                throw new Error(`proto-ver failed with ${response.status}`);
            }
            return { version, security: "sec0" };
        },
        async initSession() {
            const payload = encodeSessionSetup0Request();
            const result = await postBinary("/prov-session", payload, cookie);
            cookie = result.cookie || cookie;
            return decodeSessionSetup0Response(result.bytes.buffer);
        },
        async getWifiStatus() {
            const result = await postBinary("/prov-config", encodeGetWifiStatusRequest(), cookie);
            return decodeGetWifiStatusResponse(result.bytes.buffer);
        },
    };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
6 passed
```

- [ ] **Step 5: Commit**

```powershell
git add components/ap_portal_adapter/web/prov_proto_bundle.js components/ap_portal_adapter/web/prov_client.js tests/test_ap_portal_official_client_source.py
git commit -m "接入 SoftAP 官方协议编码与 security0 会话"
```

### Task 4: Finish Scan / Configure / Status Workflows

**Files:**
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\web\app.js`
- Modify: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`

- [ ] **Step 1: Add failing tests for complete scan/config/status workflow**

Append to `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`:

```python
    def test_prov_client_exposes_scan_and_config_workflow(self) -> None:
        source = AP_PORTAL_WEB_PROV_CLIENT.read_text(encoding="utf-8")

        self.assertIn("async scanWifi()", source)
        self.assertIn("async sendWifiConfig(ssid, password)", source)
        self.assertIn("async applyWifiConfig()", source)
        self.assertIn("async waitForWifiConnection()", source)

    def test_app_js_renders_official_scan_results_and_connection_states(self) -> None:
        source = AP_PORTAL_WEB_JS.read_text(encoding="utf-8")

        self.assertIn("renderWifiNetworks(entries)", source)
        self.assertIn('setFeedback("info", `Scan complete: ${entries.length} network(s).`);', source)
        self.assertIn('setFeedback("info", "Credentials sent. Waiting for connection status...");', source)
        self.assertIn('setFeedback("info", "Device connected successfully.");', source)
        self.assertIn('setFeedback("warning", "Device reported a Wi-Fi connection failure.");', source)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
FAIL ... async scanWifi()
FAIL ... Device connected successfully.
```

- [ ] **Step 3: Implement high-level workflow methods and UI status loop**

`D:\esp32S3\111\components\ap_portal_adapter\web\prov_client.js`

```javascript
        async ensureSession() {
            if (!cookie) {
                await this.initSession();
            }
        },

        async scanWifi() {
            await this.ensureSession();
            await postBinary("/prov-scan", encodeScanStartRequest(), cookie);
            const statusResult = await postBinary("/prov-scan", encodeScanStatusRequest(), cookie);
            const status = decodeScanStatusResponse(statusResult.bytes.buffer);

            if (!status.scanFinished) {
                throw new Error("Scan did not finish in time");
            }

            if (!status.resultCount) {
                return [];
            }

            const result = await postBinary(
                "/prov-scan",
                encodeScanResultRequest(0, status.resultCount),
                cookie,
            );
            return decodeScanResultResponse(result.bytes.buffer);
        },

        async sendWifiConfig(ssid, password) {
            await this.ensureSession();
            await postBinary("/prov-config", encodeSetWifiConfigRequest(ssid, password), cookie);
            await this.applyWifiConfig();
        },

        async applyWifiConfig() {
            await postBinary("/prov-config", encodeApplyWifiConfigRequest(), cookie);
        },

        async waitForWifiConnection() {
            for (let attempt = 0; attempt < 6; attempt += 1) {
                const state = await this.getWifiStatus();
                if (state.state === "connected") {
                    return state;
                }
                if (state.state === "failed") {
                    return state;
                }
                await new Promise((resolve) => window.setTimeout(resolve, 2000));
            }
            return { state: "connecting" };
        },
```

`D:\esp32S3\111\components\ap_portal_adapter\web\app.js`

```javascript
configureBtn.addEventListener("click", async () => {
    clearFeedback();
    const ssid = ssidInput.value.trim();
    const password = passwordInput.value;

    if (!ssid) {
        setFeedback("warning", "Please enter a Wi-Fi name first.");
        return;
    }

    try {
        setFeedback("info", "Sending credentials to the device...");
        await provClient.sendWifiConfig(ssid, password);
        setFeedback("info", "Credentials sent. Waiting for connection status...");

        const status = await provClient.waitForWifiConnection();
        if (status.state === "connected") {
            setFeedback("info", "Device connected successfully.");
            return;
        }
        if (status.state === "failed") {
            setFeedback("warning", "Device reported a Wi-Fi connection failure.");
            return;
        }
        setFeedback("info", "Device is still connecting. You can refresh status again.");
    } catch (error) {
        setFeedback("error", error.message);
    }
});
```

- [ ] **Step 4: Run source tests to verify they pass**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py tests/test_network_service_ble_source.py -v
```

Expected:

```text
... passed
```

- [ ] **Step 5: Commit**

```powershell
git add components/ap_portal_adapter/web/app.js components/ap_portal_adapter/web/prov_client.js tests/test_ap_portal_official_client_source.py
git commit -m "打通 SoftAP 门户官方扫描与配网流程"
```

### Task 5: Route Cleanup, Build Verification, And Context Refresh

**Files:**
- Modify: `D:\esp32S3\111\components\ap_portal_adapter\src\ap_portal_routes.c`
- Modify: `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\softap-provisioning-placeholder-api-limit.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: Update imports and add failing tests for route cleanup and docs refresh**

Update the import block and append to `D:\esp32S3\111\tests\test_ap_portal_official_client_source.py`:

```python
from tests.main_paths import AP_PORTAL_ROUTES_SOURCE

    def test_ap_portal_routes_keep_status_and_demote_legacy_json_endpoints(self) -> None:
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn('"/api/status"', source)
        self.assertIn('"/api/scan"', source)
        self.assertIn('"/api/configure"', source)
        self.assertIn("Legacy JSON API has been replaced by official provisioning client.", source)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py -v
```

Expected:

```text
FAIL ... Legacy JSON API has been replaced by official provisioning client.
```

- [ ] **Step 3: Convert legacy JSON routes to compatibility notice and refresh docs**

`D:\esp32S3\111\components\ap_portal_adapter\src\ap_portal_routes.c`

```c
static esp_err_t ap_portal_legacy_api_handler(httpd_req_t *req)
{
    static const char *kLegacyJson =
        "{"
        "\"ok\":false,"
        "\"error\":\"legacy_api_removed\","
        "\"message\":\"Legacy JSON API has been replaced by official provisioning client.\""
        "}";

    httpd_resp_set_status(req, "410 Gone");
    return ap_portal_send_text(req, "application/json; charset=utf-8",
                               kLegacyJson);
}
```

Update URI registration:

```c
    httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_POST,
        .handler = ap_portal_legacy_api_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t configure_uri = {
        .uri = "/api/configure",
        .method = HTTP_POST,
        .handler = ap_portal_legacy_api_handler,
        .user_ctx = NULL,
    };
```

`D:\esp32S3\111\docs\context\knowledge\project\softap-provisioning-placeholder-api-limit.md`

```md
- 自 `2026-04-22` 起，门户正式主链路已改为浏览器端官方 provisioning client。
- `/api/scan` 与 `/api/configure` 若仍保留，只作为兼容提示接口，不再承载正式配网逻辑。
```

`D:\esp32S3\111\docs\context\CHANGELOG.md`

```md
- 2026-04-22：SoftAP 门户从设备侧 JSON bridge 方向收敛到“自定义 UI + 前端 official provisioning client”路线；页面主路径改为直连官方 `prov-session / prov-scan / prov-config`，旧 `/api/scan`、`/api/configure` 降级为兼容提示接口。
```

- [ ] **Step 4: Run tests and build verification**

Run:

```powershell
uv run python -m pytest tests/test_ap_portal_official_client_source.py tests/test_network_service_ble_source.py tests/test_nonblocking_boot_source.py -v
```

Expected:

```text
... passed
```

Run:

```powershell
& 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'
idf.py build
```

Expected:

```text
Project build complete
```

- [ ] **Step 5: Commit**

```powershell
git add components/ap_portal_adapter/src/ap_portal_routes.c tests/test_ap_portal_official_client_source.py docs/context/knowledge/project/softap-provisioning-placeholder-api-limit.md docs/context/CHANGELOG.md
git commit -m "完成 SoftAP 门户官方客户端路径收口"
```
