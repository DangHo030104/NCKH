#include "WiFiManager.h"
#include "../config/AppConfig.h"
#include "../common/SharedState.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

namespace
{
constexpr const char *PREFERENCES_NAMESPACE = "wifi-config";
constexpr const char *SSID_KEY = "ssid";
constexpr const char *PASSWORD_KEY = "password";

WebServer webServer(80);
String savedSsid;
String savedPassword;

bool webServerStarted = false;
bool configPortalActive = false;
bool wifiConnected = false;
bool connectionAttemptActive = false;
bool credentialsApplyPending = false;
bool restartPending = false;
bool resetButtonHandled = false;

uint32_t connectionAttemptStartedAt = 0;
uint32_t lastWiFiReconnectAttempt = 0;
uint32_t credentialsApplyAt = 0;
uint32_t restartAt = 0;
uint32_t resetButtonPressedAt = 0;

const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Cấu hình WiFi - Rx LoRa E32</title>
  <style>
    *{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:18px;font-family:Arial,sans-serif;background:linear-gradient(135deg,#0f172a,#1e3a5f);color:#e2e8f0}
    .card{width:min(460px,100%);padding:24px;border:1px solid #ffffff1f;border-radius:20px;background:#0f172ae8;box-shadow:0 22px 60px #0006}
    h1{font-size:22px;margin:0 0 8px}.hint{color:#94a3b8;font-size:13px;line-height:1.5;margin-bottom:20px}
    label{display:block;margin:14px 0 7px;font-size:14px;font-weight:700}select,input{width:100%;height:46px;padding:0 12px;border-radius:11px;border:1px solid #334155;background:#111827;color:#f8fafc;font-size:15px}
    .actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:18px}button{height:44px;border:0;border-radius:11px;color:white;font-weight:700;cursor:pointer;background:#2563eb}button.save{background:#059669}button.secondary{background:#475569}.wide{grid-column:1/-1}
    #status{margin-top:16px;padding:12px;border-radius:10px;background:#1e293b;color:#cbd5e1;font-size:14px;line-height:1.45}.ok{background:#064e3b!important;color:#a7f3d0!important}.err{background:#7f1d1d!important;color:#fecaca!important}
  </style>
</head>
<body><main class="card">
  <h1>Cấu hình WiFi Rx LoRa E32</h1>
  <div class="hint">Chọn mạng 2.4 GHz, nhập mật khẩu và lưu. ESP32 sẽ thử kết nối ngay mà không cần nạp lại chương trình.</div>
  <label for="ssid">Mạng WiFi</label><select id="ssid"><option value="">Đang tải danh sách...</option></select>
  <label for="password">Mật khẩu</label><input id="password" type="password" maxlength="63" placeholder="Để trống nếu là mạng mở">
  <div class="actions">
    <button onclick="scan()">Quét lại</button><button class="save" onclick="save()">Lưu và kết nối</button>
    <button class="secondary wide" onclick="restartDevice()">Khởi động lại ESP32</button>
  </div>
  <div id="status">Đang đọc trạng thái...</div>
</main>
<script>
const statusBox=document.getElementById('status');
function statusText(text,type=''){statusBox.textContent=text;statusBox.className=type}
async function scan(){statusText('Đang quét WiFi...');try{const r=await fetch('/api/scan');if(!r.ok)throw Error();const nets=await r.json();const s=document.getElementById('ssid');s.innerHTML='<option value="">-- Chọn mạng WiFi --</option>';nets.forEach(n=>s.add(new Option(n,n)));statusText(`Tìm thấy ${nets.length} mạng WiFi.`,'ok')}catch(e){statusText('Không thể quét WiFi. Hãy thử lại.','err')}}
async function loadStatus(){try{const r=await fetch('/api/status');const d=await r.json();statusText(d.connected?`Đã kết nối ${d.ssid} - IP ${d.ip}`:`Chưa kết nối. AP cấu hình: ${d.apSsid} - IP ${d.apIp}`,d.connected?'ok':'')}catch(e){statusText('Không đọc được trạng thái.','err')}}
async function save(){const ssid=document.getElementById('ssid').value;const password=document.getElementById('password').value;if(!ssid){statusText('Vui lòng chọn mạng WiFi.','err');return}statusText('Đang lưu cấu hình...');const body=new URLSearchParams({ssid,password});try{const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const text=await r.text();statusText(text,r.ok?'ok':'err')}catch(e){statusText('Mất kết nối với ESP32. Hãy kết nối lại AP cấu hình.','err')}}
async function restartDevice(){statusText('ESP32 đang khởi động lại...');await fetch('/api/restart',{method:'POST'}).catch(()=>{});}
scan();loadStatus();
</script></body></html>
)rawliteral";

void loadCredentials()
{
    Preferences preferences;
    /* Open read-write once so the namespace is created cleanly on first boot. */
    if (!preferences.begin(PREFERENCES_NAMESPACE, false))
    {
        Serial.println("[WIFI] Cannot open NVS for reading");
        savedSsid = "";
        savedPassword = "";
        return;
    }

    savedSsid = preferences.getString(SSID_KEY, "");
    savedPassword = preferences.getString(PASSWORD_KEY, "");
    preferences.end();
    savedSsid.trim();
}

bool saveCredentials(const String &newSsid, const String &newPassword)
{
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE, false)) return false;

    const size_t ssidBytes = preferences.putString(SSID_KEY, newSsid);
    const size_t passwordBytes = preferences.putString(PASSWORD_KEY, newPassword);
    preferences.end();
    return ssidBytes > 0U && (newPassword.length() == 0U || passwordBytes > 0U);
}

void clearCredentials()
{
    Preferences preferences;
    if (preferences.begin(PREFERENCES_NAMESPACE, false))
    {
        preferences.clear();
        preferences.end();
    }
    savedSsid = "";
    savedPassword = "";
}

void startConfigPortal()
{
    if (configPortalActive) return;

    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAP(wifi_config_ap_ssid, wifi_config_ap_password))
    {
        configPortalActive = true;
        Serial.println("[WIFI] Configuration portal started");
        Serial.print("[WIFI] AP: ");
        Serial.println(wifi_config_ap_ssid);
        Serial.print("[WIFI] Portal: http://");
        Serial.println(WiFi.softAPIP());
    }
    else
    {
        Serial.println("[WIFI] Failed to start configuration AP");
    }
}

void stopConfigPortal()
{
    if (!configPortalActive) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    configPortalActive = false;
    Serial.println("[WIFI] Configuration AP stopped");
}

void startStationConnection()
{
    if (savedSsid.length() == 0U)
    {
        startConfigPortal();
        return;
    }

    WiFi.mode(configPortalActive ? WIFI_AP_STA : WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    connectionAttemptActive = true;
    connectionAttemptStartedAt = millis();
    lastWiFiReconnectAttempt = connectionAttemptStartedAt;

    Serial.print("[WIFI] Connecting to: ");
    Serial.println(savedSsid);
}

void scheduleCredentialApply()
{
    credentialsApplyPending = true;
    credentialsApplyAt = millis() + 750U;
}

void setupWebServer()
{
    if (webServerStarted) return;

    webServer.on("/", HTTP_GET, []()
    {
        webServer.send_P(200, "text/html; charset=utf-8", CONFIG_PAGE);
    });

    webServer.on("/api/status", HTTP_GET, []()
    {
        JsonDocument document;
        document["connected"] = WiFi.status() == WL_CONNECTED;
        document["ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : savedSsid;
        document["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
        document["apSsid"] = wifi_config_ap_ssid;
        document["apIp"] = configPortalActive ? WiFi.softAPIP().toString() : "";
        String response;
        serializeJson(document, response);
        webServer.send(200, "application/json", response);
    });

    webServer.on("/api/scan", HTTP_GET, []()
    {
        const int networkCount = WiFi.scanNetworks(false, true);
        JsonDocument document;
        JsonArray networks = document.to<JsonArray>();

        for (int index = 0; index < networkCount; ++index)
        {
            const String networkSsid = WiFi.SSID(index);
            bool duplicate = false;
            for (JsonVariant value : networks)
            {
                if (networkSsid == value.as<String>())
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && networkSsid.length() > 0U) networks.add(networkSsid);
        }

        WiFi.scanDelete();
        String response;
        serializeJson(document, response);
        webServer.send(200, "application/json", response);
    });

    webServer.on("/api/save", HTTP_POST, []()
    {
        String newSsid = webServer.arg("ssid");
        const String newPassword = webServer.arg("password");
        newSsid.trim();

        if (newSsid.length() == 0U || newSsid.length() > 32U || newPassword.length() > 63U)
        {
            webServer.send(400, "text/plain; charset=utf-8", "SSID hoặc mật khẩu không hợp lệ.");
            return;
        }

        if (!saveCredentials(newSsid, newPassword))
        {
            webServer.send(500, "text/plain; charset=utf-8", "Không thể lưu cấu hình vào NVS.");
            return;
        }

        savedSsid = newSsid;
        savedPassword = newPassword;
        webServer.send(200, "text/plain; charset=utf-8", "Đã lưu. ESP32 đang thử kết nối WiFi mới.");
        scheduleCredentialApply();
    });

    webServer.on("/api/restart", HTTP_POST, []()
    {
        webServer.send(200, "text/plain; charset=utf-8", "ESP32 đang khởi động lại.");
        restartPending = true;
        restartAt = millis() + 750U;
    });

    webServer.onNotFound([]()
    {
        webServer.sendHeader("Location", "/");
        webServer.send(302, "text/plain", "");
    });

    webServer.begin();
    webServerStarted = true;
    Serial.println("[WIFI] Configuration WebServer started");
}

void checkResetButton()
{
    if (digitalRead(WIFI_CONFIG_BUTTON_PIN) == LOW)
    {
        if (resetButtonPressedAt == 0U) resetButtonPressedAt = millis();

        if (!resetButtonHandled && millis() - resetButtonPressedAt >= WIFI_RESET_HOLD_MS)
        {
            resetButtonHandled = true;
            Serial.println("[WIFI] BOOT held for 5 seconds -> credentials cleared");
            clearCredentials();
            WiFi.disconnect(true, true);
            configPortalActive = false;
            wifiConnected = false;
            connectionAttemptActive = false;
            setWiFiDisplayState(false);
            startConfigPortal();
        }
    }
    else
    {
        resetButtonPressedAt = 0U;
        resetButtonHandled = false;
    }
}
} // namespace

void WiFiManager_Begin(void)
{
    pinMode(WIFI_CONFIG_BUTTON_PIN, INPUT_PULLUP);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname("rx-lora-e32");
    setWiFiDisplayState(false);

    loadCredentials();

    /* WiFi.mode()/WiFi.begin() must initialize lwIP before WebServer.begin(). */
    if (savedSsid.length() == 0U)
    {
        Serial.println("[WIFI] No saved credentials");
        startConfigPortal();
    }
    else
    {
        startStationConnection();
    }

    setupWebServer();
}

bool WiFiManager_IsConnected(void)
{
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager_Maintain(void)
{
    checkResetButton();
    webServer.handleClient();

    const uint32_t now = millis();

    if (restartPending && static_cast<int32_t>(now - restartAt) >= 0)
    {
        ESP.restart();
    }

    if (credentialsApplyPending && static_cast<int32_t>(now - credentialsApplyAt) >= 0)
    {
        credentialsApplyPending = false;
        wifiConnected = false;
        setWiFiDisplayState(false);
        startConfigPortal();
        startStationConnection();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        connectionAttemptActive = false;
        if (!wifiConnected)
        {
            wifiConnected = true;
            setWiFiDisplayState(true);
            Serial.println("[WIFI] Connected");
            Serial.print("[WIFI] SSID: ");
            Serial.println(WiFi.SSID());
            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());
            stopConfigPortal();
            return true;
        }
        return false;
    }

    if (wifiConnected)
    {
        wifiConnected = false;
        setWiFiDisplayState(false);
        Serial.println("[WIFI] Connection lost");
        connectionAttemptActive = false;
        lastWiFiReconnectAttempt = now - WIFI_RECONNECT_INTERVAL;
    }

    if (savedSsid.length() == 0U)
    {
        startConfigPortal();
        return false;
    }

    if (connectionAttemptActive)
    {
        if (now - connectionAttemptStartedAt >= WIFI_CONNECT_TIMEOUT_MS)
        {
            connectionAttemptActive = false;
            Serial.println("[WIFI] Connection timeout -> configuration portal available");
            startConfigPortal();
            lastWiFiReconnectAttempt = now;
        }
        return false;
    }

    if (now - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL)
    {
        startStationConnection();
    }

    return false;
}
