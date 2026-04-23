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

/**
 * @brief 更新页面反馈横幅。
 * @param {string} type 反馈类型。
 * @param {string} message 显示给用户的提示文本。
 */
function setFeedback(type, message) {
    feedback.className = `feedback ${type}`;
    feedback.textContent = message;
}

/**
 * @brief 清空页面反馈横幅。
 */
function clearFeedback() {
    feedback.className = "feedback hidden";
    feedback.textContent = "";
}

/**
 * @brief 更新扫描能力徽标。
 * @param {boolean} isReady 当前是否支持官方扫描。
 */
function setPortalCapability(isReady) {
    scanCapability.textContent = isReady ? "Scan ready" : "Scan pending";
    scanCapability.style.background = isReady ? "#daf6e7" : "#fff3d6";
    scanCapability.style.color = isReady ? "#11663b" : "#7e5b00";
}

/**
 * @brief 渲染门户信息加载态。
 */
function setPortalLoadingState() {
    apiDot.className = "status-dot";
    apiText.textContent = "Checking device portal...";
    apiNote.textContent = "Loading portal state from the provisioning client.";
    clearFeedback();
}

/**
 * @brief 渲染门户信息就绪态。
 * @param {{title: string, note: string, apiVersion: string, scanSupported: boolean}} info 门户信息。
 */
function setPortalReadyState(info) {
    apiDot.className = "status-dot online";
    apiText.textContent = info.title;
    apiNote.textContent = info.note;
    setPortalCapability(info.scanSupported);
}

/**
 * @brief 渲染门户信息错误态。
 * @param {string} message 要展示的错误信息。
 */
function setPortalErrorState(message) {
    apiDot.className = "status-dot error";
    apiText.textContent = "Device portal is unavailable";
    apiNote.textContent = "The AP page is ready, but the provisioning client is not responding yet.";
    setPortalCapability(false);
    setFeedback("error", message);
}

/**
 * @brief 渲染空 Wi-Fi 列表提示。
 * @param {string} message 空态提示文本。
 */
function renderEmptyNetworkList(message) {
    wifiList.classList.add("empty");
    wifiList.innerHTML = `
        <article class="wifi-item">
            <div>
                <strong>${message}</strong>
                <span>The browser portal now asks the provisioning client for Wi-Fi data.</span>
            </div>
        </article>
    `;
}

/**
 * @brief 渲染稳定的 Wi-Fi 列表。
 * @param {{ssid: string, security: string, rssi: number}[]} networks 归一化后的网络列表。
 */
function renderNetworkList(networks) {
    if (networks.length === 0) {
        renderEmptyNetworkList("No Wi-Fi networks were returned.");
        return;
    }

    wifiList.classList.remove("empty");
    wifiList.innerHTML = networks
        .map((network) => {
            return `
                <article class="wifi-item">
                    <div>
                        <strong>${network.ssid}</strong>
                        <span>${network.security}</span>
                    </div>
                    <small>${network.rssi} dBm</small>
                </article>
            `;
        })
        .join("");
}

/**
 * @brief 刷新门户摘要信息。
 */
async function refreshPortalInfo() {
    setPortalLoadingState();

    try {
        const info = await provClient.getPortalInfo();
        setPortalReadyState(info);
    } catch (error) {
        setPortalErrorState(error.message);
    }
}

scanBtn.addEventListener("click", async () => {
    clearFeedback();
    renderEmptyNetworkList("Scanning Wi-Fi networks...");

    try {
        const networks = await provClient.scanWifi();
        renderNetworkList(networks);
        setFeedback("info", `Scan complete: ${networks.length} network(s).`);
    } catch (error) {
        renderEmptyNetworkList("Wi-Fi scan failed.");
        setFeedback("error", error.message);
    }
});

refreshBtn.addEventListener("click", () => {
    refreshPortalInfo();
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
        await provClient.sendWifiConfig(ssid, password);
        setFeedback("info", "Credentials sent. Waiting for connection status...");

        const waitResult = await provClient.waitForWifiConnection();

        // 成功和失败是设备给出的明确终态，优先直接向用户说明最终结果。
        if (waitResult.phase === "connected") {
            setFeedback("success", "Device connected successfully.");
            return;
        }

        if (waitResult.phase === "failed") {
            setFeedback("error", "Device reported a Wi-Fi connection failure.");
            return;
        }

        // SoftAP 成功后设备会主动关闭门户，浏览器看到的往往是网络断开而不是最终 HTTP 响应。
        if (waitResult.phase === "portal_closed") {
            setFeedback(
                "success",
                "Provisioning request accepted. The device likely left the hotspot and is switching to your Wi-Fi.",
            );
            return;
        }

        // 仍处于 connecting/timeout 时，说明这轮轮询窗口内设备还没收敛到终态。
        setFeedback("warning", "Device is still connecting. Please check status again shortly.");
    } catch (error) {
        setFeedback("error", error.message);
    }
});

refreshPortalInfo();
