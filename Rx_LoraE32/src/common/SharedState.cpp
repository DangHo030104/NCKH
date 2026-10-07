#include "SharedState.h"
#include <Arduino.h>

static DashboardStatus dashboardStatus;
static portMUX_TYPE dashboardMux = portMUX_INITIALIZER_UNLOCKED;    // Spinlock để bảo vệ dashboardStatus khi đọc/ghi từ nhiều task khác nhau.
static uint32_t latestZoneCommand[2] = {0, 0};                      // Lưu cmd mới nhất của từng zone


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

// Tăng revision khi nhận một CMD mới.
uint32_t nextZoneCommand(uint8_t zone)
{
    if (zone < 1 || zone > 2) return 0;
    portENTER_CRITICAL(&dashboardMux);
    uint32_t revision = ++latestZoneCommand[zone - 1];
    portEXIT_CRITICAL(&dashboardMux);
    return revision;
}

// LoRa task dùng để kiểm tra cmd sắp gửi or retry còn hợp lệ không.
uint32_t readLatestZoneCommand(uint8_t zone)
{
    if (zone < 1 || zone > 2) return 0;
    portENTER_CRITICAL(&dashboardMux);
    uint32_t revision = latestZoneCommand[zone - 1];
    portEXIT_CRITICAL(&dashboardMux);
    return revision;
}
