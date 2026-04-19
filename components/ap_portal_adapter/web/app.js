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

function renderPlaceholderNetworks() {
    wifiList.classList.remove("empty");
    wifiList.innerHTML = `
        <article class="wifi-item">
            <div>
                <strong>Wi-Fi scan API is not connected yet</strong>
                <span>The browser portal has moved to the new adapter structure. The next step is wiring scan/configure handlers.</span>
            </div>
        </article>
    `;
}

async function fetchPortalStatus() {
    apiDot.className = "status-dot";
    apiText.textContent = "Checking device portal...";
    apiNote.textContent = "Querying the new AP portal adapter HTTP API.";
    clearFeedback();

    try {
        const response = await fetch("/api/status", {
            method: "GET",
            cache: "no-store",
        });

        if (!response.ok) {
            throw new Error(`Status request failed with ${response.status}`);
        }

        const data = await response.json();
        apiDot.className = "status-dot online";
        apiText.textContent = "Device portal is reachable";
        apiNote.textContent = `API version: ${data.api_version}. Scan supported: ${data.scan_supported ? "yes" : "no"}.`;
        scanCapability.textContent = data.scan_supported ? "Scan ready" : "Scan API pending";
        scanCapability.style.background = data.scan_supported ? "#daf6e7" : "#fff3d6";
        scanCapability.style.color = data.scan_supported ? "#11663b" : "#7e5b00";
    } catch (error) {
        apiDot.className = "status-dot error";
        apiText.textContent = "Device portal is unavailable";
        apiNote.textContent = "The AP page structure is ready, but the device API is not responding yet.";
        setFeedback("error", error.message);
    }
}

async function callPendingApi(path, payload) {
    const response = await fetch(path, {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
        },
        body: JSON.stringify(payload),
    });

    const data = await response.json();
    return {
        ok: response.ok,
        status: response.status,
        data,
    };
}

scanBtn.addEventListener("click", async () => {
    clearFeedback();
    renderPlaceholderNetworks();

    try {
        const result = await callPendingApi("/api/scan", {});
        if (!result.ok) {
            setFeedback(
                "warning",
                `Scan API not ready yet: ${result.data.message || `HTTP ${result.status}`}`
            );
            return;
        }

        setFeedback("info", "Scan API is ready.");
    } catch (error) {
        setFeedback("error", error.message);
    }
});

refreshBtn.addEventListener("click", () => {
    fetchPortalStatus();
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
        const result = await callPendingApi("/api/configure", { ssid, password });
        if (!result.ok) {
            setFeedback(
                "warning",
                `Configure API not ready yet: ${result.data.message || `HTTP ${result.status}`}`
            );
            return;
        }

        setFeedback("info", "Credentials sent.");
    } catch (error) {
        setFeedback("error", error.message);
    }
});

fetchPortalStatus();
