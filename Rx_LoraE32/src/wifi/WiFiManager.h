#ifndef RX_LORAE32_WIFI_WIFIMANAGER_H
#define RX_LORAE32_WIFI_WIFIMANAGER_H

void WiFiManager_Begin(void);
// Returns true once when WiFi transitions to connected; call from MQTT task.
bool WiFiManager_Maintain(void);
bool WiFiManager_IsConnected(void);

#endif // RX_LORAE32_WIFI_WIFIMANAGER_H
