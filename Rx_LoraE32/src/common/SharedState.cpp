#include "SharedState.h"
#include <Arduino.h>

static DashboardStatus dashboardStatus;
static portMUX_TYPE dashboardMux = portMUX_INITIALIZER_UNLOCKED;


// Copy shared status under a short lock; never draw or use MQTT under the lock.
DashboardStatus readDashboardStatus(void)
{
    portENTER_CRITICAL(&dashboardMux);
    DashboardStatus snapshot = dashboardStatus;
    portEXIT_CRITICAL(&dashboardMux);
    return snapshot;
}

void setCommandDisplayState(CommandDisplayState state, uint8_t attempt)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.command = state;
    dashboardStatus.attempt = attempt;
    portEXIT_CRITICAL(&dashboardMux);
}

void setMqttDisplayState(bool online)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.mqttOnline = online;
    portEXIT_CRITICAL(&dashboardMux);
}

void setCommandDetails(uint8_t zone, bool irrigationOn)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.zone = zone;
    dashboardStatus.irrigationOn = irrigationOn;
    portEXIT_CRITICAL(&dashboardMux);
}

void setWiFiDisplayState(bool online)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.wifiOnline = online;
    portEXIT_CRITICAL(&dashboardMux);
}
