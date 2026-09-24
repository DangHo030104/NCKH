#include "WiFiManager.h"
#include "../config/AppConfig.h"
#include "../common/SharedState.h"
#include <Arduino.h>
#include <WiFi.h>

static unsigned long lastWiFiReconnectAttempt = 0;
static bool wifiConnected = false;


void WiFiManager_Begin(void)
{
    /* WiFi */
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    lastWiFiReconnectAttempt = millis();
    wifiConnected = false;
    Serial.println("[WIFI] Connecting in background...");

}

bool WiFiManager_IsConnected(void)
{
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager_Maintain(void)
{
    wl_status_t status = WiFi.status();

    /* WIFI CONNECTED */
    if (status == WL_CONNECTED)
    {
        if (!wifiConnected)
        {
            wifiConnected = true;
            Serial.println();
            Serial.println("[WIFI] Connected");

            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());

            setWiFiDisplayState(true);
            return true;
        }
        return false;
    }

    /* WIFI LOST */
    if (wifiConnected)
    {
        wifiConnected = false;

        Serial.println();
        Serial.println("[WIFI] Connection lost");

        setWiFiDisplayState(false);
    }

    /* NON-BLOCKING WIFI RECONNECT */
    if (millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL)
    {
        lastWiFiReconnectAttempt = millis();
        Serial.println("[WIFI] Reconnecting...");
        WiFi.reconnect();
    }
    return false;
}
