#include "SharedState.h"
#include <Arduino.h>

static DashboardStatus dashboardStatus;
static portMUX_TYPE dashboardMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t latestZoneCommand[2] = {0, 0};  // Lưu trữ cmd mới nhất của từng (zone 1 và zone 2)


// Copy shared status under a short lock; never draw or use MQTT under the lock.
DashboardStatus readDashboardStatus(void)
{
    portENTER_CRITICAL(&dashboardMux);
    DashboardStatus snapshot = dashboardStatus;
    portEXIT_CRITICAL(&dashboardMux);
    return snapshot;
}

void setMqttDisplayState(bool online)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.mqttOnline = online;
    portEXIT_CRITICAL(&dashboardMux);
}

void setWiFiDisplayState(bool online)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.wifiOnline = online;
    portEXIT_CRITICAL(&dashboardMux);
}

// Increment and return the next command revision for a given zone (1 or 2).
uint32_t nextZoneCommand(uint8_t zone)
{
    if (zone < 1 || zone > 2) return 0;
    portENTER_CRITICAL(&dashboardMux);
    uint32_t revision = ++latestZoneCommand[zone - 1];
    portEXIT_CRITICAL(&dashboardMux);
    return revision;
}

// Đọc lệnh mới nhất của từng khu vực
uint32_t readLatestZoneCommand(uint8_t zone)
{
    if (zone < 1 || zone > 2) return 0;
    portENTER_CRITICAL(&dashboardMux);
    uint32_t revision = latestZoneCommand[zone - 1];
    portEXIT_CRITICAL(&dashboardMux);
    return revision;
}
