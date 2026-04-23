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
const togglePasswordBtn = document.getElementById("toggle-password-btn");
const ssidInput = document.getElementById("ssid");
const passwordInput = document.getElementById("password");
const currentStepText = document.getElementById("current-step-text");
const scanStep = document.getElementById("scan-step");
const sendStep = document.getElementById("send-step");

let selectedNetworkButton = null;

/**
 * @brief 更新当前配网阶段，让用户知道下一步该做什么。
 * @param {string} message 当前状态文案。
 */
function setCurrentStep(message) {
    currentStepText.textContent = message;
}

/**
 * @brief 更新页面反馈横幅。
 * @param {"info"|"warning"|"error"|"success"} type 反馈类型。
 * @param {string} message 显示给用户的提示文本。
 */
function setFeedback(type, message) {
    feedback.className = `feedback ${type}`;
    feedback.textContent = message;
}

/**
 * @brief 清空页面反馈横幅，避免旧结果干扰下一步操作。
 */
function clearFeedback() {
    feedback.className = "feedback hidden";
    feedback.textContent = "";
}

/**
 * @brief 切换按钮忙碌态，防止同一轮 provisioning 请求被重复提交。
 * @param {boolean} isBusy 当前是否正在执行耗时操作。
 */
function setBusyState(isBusy) {
    scanBtn.disabled = isBusy;
    refreshBtn.disabled = isBusy;
    configureBtn.disabled = isBusy;
}

/**
 * @brief 更新扫描能力徽标。
 * @param {boolean} isReady 当前是否支持官方扫描接口。
 */
function setPortalCapability(isReady) {
    scanCapability.textContent = isReady ? "可扫描" : "等待接口";
    scanCapability.className = `capability-chip ${isReady ? "ready" : "pending"}`;
}

/**
 * @brief 渲染门户信息加载态。
 */
function setPortalLoadingState() {
    apiDot.className = "status-dot";
    apiText.textContent = "正在检测设备门户...";
    apiNote.textContent = "请保持手机或电脑连接在手表热点下。";
    setCurrentStep("正在检测设备门户");
    setPortalCapability(false);
    clearFeedback();
}

/**
 * @brief 渲染门户信息就绪态。
 * @param {{apiVersion: string, scanSupported: boolean}} info 门户信息。
 */
function setPortalReadyState(info) {
    apiDot.className = "status-dot online";
    apiText.textContent = "设备门户已连接";
    apiNote.textContent = info.scanSupported ? "连接正常，可以扫描附近 Wi-Fi。" : "连接正常，可以手动输入网络名称。";
    setCurrentStep(info.scanSupported ? "可以开始扫描 Wi-Fi" : "请手动输入网络名称");
    setPortalCapability(info.scanSupported);
}

/**
 * @brief 渲染门户信息错误态。
 * @param {string} message 要展示的错误信息。
 */
function setPortalErrorState(message) {
    apiDot.className = "status-dot error";
    apiText.textContent = "设备门户暂不可用";
    apiNote.textContent = "请确认手机或电脑仍连接在设备热点下，然后刷新状态。";
    setCurrentStep("请先连接设备热点");
    setPortalCapability(false);
    setFeedback("error", `门户检测失败：${message}`);
}

/**
 * @brief 根据 RSSI 返回面向用户的信号强度描述。
 * @param {number} rssi RSSI 信号强度，单位 dBm。
 * @returns {string} 中文信号强度描述。
 */
function formatSignalStrength(rssi) {
    if (rssi >= -55) {
        return "信号强";
    }

    if (rssi >= -70) {
        return "信号中";
    }

    return "信号弱";
}

/**
 * @brief 将底层安全类型转换成中文说明。
 * @param {string} security 底层返回的安全类型。
 * @returns {string} 中文安全类型。
 */
function formatSecurity(security) {
    const lowerSecurity = String(security || "").toLowerCase();

    if (lowerSecurity.includes("open") || lowerSecurity.includes("none")) {
        return "开放网络";
    }

    if (lowerSecurity.includes("unknown")) {
        return "加密未知";
    }

    return "需要密码";
}

/**
 * @brief 清理 Wi-Fi 列表并重置选中状态。
 */
function resetNetworkList() {
    selectedNetworkButton = null;
    wifiList.replaceChildren();
}

/**
 * @brief 渲染空 Wi-Fi 列表提示。
 * @param {string} title 空态标题。
 * @param {string} detail 空态说明。
 */
function renderEmptyNetworkList(title, detail) {
    resetNetworkList();
    wifiList.classList.add("empty");

    const empty = document.createElement("article");
    empty.className = "empty-state";

    const titleNode = document.createElement("strong");
    titleNode.textContent = title;

    const detailNode = document.createElement("span");
    detailNode.textContent = detail;

    empty.append(titleNode, detailNode);
    wifiList.append(empty);
}

/**
 * @brief 创建单个 Wi-Fi 网络按钮。
 * @param {{ssid: string, security: string, rssi: number}} network 归一化后的网络信息。
 * @returns {HTMLButtonElement} 可点击选择的网络按钮。
 */
function createNetworkButton(network) {
    const item = document.createElement("button");
    item.className = "wifi-item";
    item.type = "button";

    const textWrap = document.createElement("span");

    const name = document.createElement("strong");
    name.className = "wifi-name";
    name.textContent = network.ssid;

    const meta = document.createElement("span");
    meta.className = "wifi-meta";
    meta.textContent = `${formatSecurity(network.security)} · ${network.rssi} dBm`;

    const signal = document.createElement("span");
    signal.className = "signal-pill";
    signal.textContent = formatSignalStrength(network.rssi);

    textWrap.append(name, meta);
    item.append(textWrap, signal);

    item.addEventListener("click", () => {
        if (selectedNetworkButton) {
            selectedNetworkButton.classList.remove("selected");
        }

        selectedNetworkButton = item;
        item.classList.add("selected");
        ssidInput.value = network.ssid;
        passwordInput.focus();
        sendStep.classList.add("active");
        setCurrentStep(`已选择 ${network.ssid}`);
        setFeedback("info", `已选择「${network.ssid}」，请输入密码后发送到手表。`);
    });

    return item;
}

/**
 * @brief 渲染稳定的 Wi-Fi 列表。
 * @param {{ssid: string, security: string, rssi: number}[]} networks 归一化后的网络列表。
 */
function renderNetworkList(networks) {
    if (networks.length === 0) {
        setCurrentStep("没有扫描到网络");
        renderEmptyNetworkList("没有扫描到 Wi-Fi", "可以靠近路由器后再试，或直接手动输入 SSID。");
        return;
    }

    resetNetworkList();
    wifiList.classList.remove("empty");
    scanStep.classList.add("active");
    setCurrentStep("请选择要连接的 Wi-Fi");

    networks.forEach((network) => {
        wifiList.append(createNetworkButton(network));
    });
}

/**
 * @brief 刷新门户摘要信息。
 */
async function refreshPortalInfo() {
    setBusyState(true);
    setPortalLoadingState();

    try {
        const info = await provClient.getPortalInfo();
        setPortalReadyState(info);
    } catch (error) {
        setPortalErrorState(error.message);
    } finally {
        setBusyState(false);
    }
}

scanBtn.addEventListener("click", async () => {
    clearFeedback();
    setBusyState(true);
    setCurrentStep("正在扫描附近 Wi-Fi");
    renderEmptyNetworkList("正在扫描附近 Wi-Fi...", "扫描期间请保持浏览器连接在设备热点下。");

    try {
        const networks = await provClient.scanWifi();
        renderNetworkList(networks);
        setFeedback("success", `扫描完成，共发现 ${networks.length} 个网络。`);
    } catch (error) {
        setCurrentStep("扫描失败，可手动输入");
        renderEmptyNetworkList("Wi-Fi 扫描失败", "当前仍可手动输入 SSID；如果多次失败，请刷新门户状态。");
        setFeedback("error", `扫描失败：${error.message}`);
    } finally {
        setBusyState(false);
    }
});

refreshBtn.addEventListener("click", () => {
    refreshPortalInfo();
});

togglePasswordBtn.addEventListener("click", () => {
    const shouldShowPassword = passwordInput.type === "password";
    passwordInput.type = shouldShowPassword ? "text" : "password";
    togglePasswordBtn.textContent = shouldShowPassword ? "隐" : "显";
    togglePasswordBtn.setAttribute("aria-label", shouldShowPassword ? "隐藏密码" : "显示密码");
});

configureBtn.addEventListener("click", async () => {
    clearFeedback();

    const ssid = ssidInput.value.trim();
    const password = passwordInput.value;

    if (!ssid) {
        setCurrentStep("请先选择或输入 Wi-Fi");
        setFeedback("warning", "请先输入或选择 Wi-Fi 名称。");
        ssidInput.focus();
        return;
    }

    setBusyState(true);
    sendStep.classList.add("active");
    setCurrentStep("正在发送 Wi-Fi 信息");

    try {
        await provClient.sendWifiConfig(ssid, password);
        setCurrentStep("正在等待手表连接");
        setFeedback("info", "凭据已发送，正在等待设备切换到目标 Wi-Fi...");

        const waitResult = await provClient.waitForWifiConnection();

        // 成功和失败是设备给出的明确终态，优先直接向用户说明最终结果。
        if (waitResult.phase === "connected") {
            setCurrentStep("手表已连接 Wi-Fi");
            setFeedback("success", "手表已连接 Wi-Fi，可以回到设备查看联网状态。");
            return;
        }

        if (waitResult.phase === "failed") {
            setCurrentStep("Wi-Fi 连接失败");
            setFeedback("error", "设备报告 Wi-Fi 连接失败，请检查密码或路由器信号。");
            return;
        }

        // SoftAP 成功后设备会主动关闭门户，浏览器看到的往往是网络断开而不是最终 HTTP 响应。
        if (waitResult.phase === "portal_closed") {
            setCurrentStep("手表正在切换网络");
            setFeedback("success", "设备已接受凭据并可能正在关闭热点；请切回目标 Wi-Fi 查看手表状态。");
            return;
        }

        // 仍处于 connecting/timeout 时，说明这轮轮询窗口内设备还没收敛到终态。
        setCurrentStep("手表仍在连接中");
        setFeedback("warning", "设备仍在连接中。稍等几秒后刷新状态，或检查路由器是否允许新设备接入。");
    } catch (error) {
        setCurrentStep("发送失败，请重试");
        setFeedback("error", `发送失败：${error.message}`);
    } finally {
        setBusyState(false);
    }
});

renderEmptyNetworkList("还没有扫描结果", "点击“扫描 Wi-Fi”获取附近网络，也可以直接手动输入。");
refreshPortalInfo();
