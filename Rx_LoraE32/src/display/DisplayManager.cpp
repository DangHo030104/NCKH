#include "DisplayManager.h"
#include "../config/AppConfig.h"
#include "../common/SharedState.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>
#include <time.h>

/* TFT DISPLAY */
static SPIClass spi = SPIClass(VSPI);
static Adafruit_ST7735 tft = Adafruit_ST7735(&spi, TFT_CS, TFT_DC, TFT_RST);

/* RTOS QUEUES */
static QueueHandle_t displayQueue;

// Render off-screen, then transfer only changed dashboard regions.
// One RGB565 frame uses 40 KiB; no clear operation is sent to the TFT.
static GFXcanvas16 dashboardFrame(160, 128);

static void flushDashboardRegion(int x, int y, int width, int height)
{
    uint16_t *pixels = dashboardFrame.getBuffer();
    if (x == 0 && width == 160)
        tft.drawRGBBitmap(x, y, pixels + y * 160, width, height);
    else
        for (int row = y; row < y + height; ++row)
            tft.drawRGBBitmap(x, row, pixels + row * 160 + x, width, 1);
}

// Compact dashboard for the landscape 160 x 128 TFT.
static void drawSensorCard(int x, int y, const char *label, float value, bool temperature, uint16_t accent)
{
    const uint16_t card = 0x10E4;
    dashboardFrame.fillRoundRect(x, y, 74, 38, 4, card);
    if (temperature)
    {
        dashboardFrame.drawRoundRect(x + 7, y + 5, 5, 10, 2, accent);
        dashboardFrame.fillCircle(x + 9, y + 15, 3, accent);
        dashboardFrame.drawFastVLine(x + 9, y + 8, 7, accent);
    }
    else
    {
        dashboardFrame.fillTriangle(x + 9, y + 5, x + 5, y + 12, x + 13, y + 12, accent);
        dashboardFrame.fillCircle(x + 9, y + 13, 4, accent);
        dashboardFrame.drawPixel(x + 7, y + 13, card);
    }
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(0x8C92);
    dashboardFrame.setCursor(x + 20, y + 7);
    dashboardFrame.print(label);
    char number[20];
    if (isfinite(value))
        snprintf(number, sizeof(number), "%.1f", value);
    else
        snprintf(number, sizeof(number), "--");
    dashboardFrame.setTextSize(strlen(number) <= 5 ? 2 : 1);
    dashboardFrame.setTextColor(ST77XX_WHITE);
    dashboardFrame.setCursor(x + 5, y + 21);
    dashboardFrame.print(number);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(accent);
    // Draw the degree mark directly so it does not depend on UTF-8 font support.
    if (temperature)
        dashboardFrame.drawCircle(x + 62, y + 26, 1, accent);
    dashboardFrame.setCursor(x + 65, y + 27);
    dashboardFrame.print(temperature ? "C" : "%");
}

static void updateDisplay(const SensorData &data, bool hasData, uint32_t receivedAt)
{
    const uint16_t bg = 0x0862, green = 0x4ED2, muted = 0x8C92;

    if (!dashboardFrame.getBuffer())
        return;

    dashboardFrame.setTextWrap(false);
    const DashboardStatus status = readDashboardStatus();
    const bool wifiOnline = status.wifiOnline;
    const uint32_t age = millis() - receivedAt;
    const bool stale = hasData && age >= SENSOR_STALE_TIMEOUT_MS;

    // Update the header and footer independently from the sensor cards.
    dashboardFrame.fillRect(0, 0, 160, 21, bg);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(green);
    dashboardFrame.setCursor(5, 7);
    char clockText[6] = "--:--";
    const time_t now = time(nullptr);
    struct tm localTime = {};
    // Read the system clock without waiting for an NTP response.
    // Before initial synchronization ESP32 time is near the Unix epoch.
    if (now >= 1704067200 && localtime_r(&now, &localTime) != nullptr)
        strftime(clockText, sizeof(clockText), "%H:%M", &localTime);
    dashboardFrame.print(clockText);

    const uint16_t wifiColor = wifiOnline ? green : ST77XX_RED;
    // Compact 13 x 10 Wi-Fi fan, centered vertically beside the label.
    // Fixed shape indicates connectivity, not RSSI.
    dashboardFrame.drawFastHLine(51, 6, 7, wifiColor);
    dashboardFrame.drawLine(48, 8, 50, 7, wifiColor);
    dashboardFrame.drawLine(58, 7, 60, 8, wifiColor);
    dashboardFrame.drawFastHLine(52, 9, 5, wifiColor);
    dashboardFrame.drawPixel(51, 10, wifiColor);
    dashboardFrame.drawPixel(57, 10, wifiColor);
    dashboardFrame.drawFastHLine(53, 12, 3, wifiColor);
    dashboardFrame.drawPixel(54, 15, wifiColor);
    dashboardFrame.setTextColor(wifiColor);
    dashboardFrame.setCursor(65, 7);
    dashboardFrame.print("WiFi");

    const uint16_t radioColor = !hasData ? 0xFD68 : (stale ? ST77XX_RED : green);
    // Compact 13 x 13 antenna with paired waves and a stable base.
    dashboardFrame.drawLine(115, 4, 113, 6, radioColor);
    dashboardFrame.drawFastVLine(113, 7, 2, radioColor);
    dashboardFrame.drawLine(113, 9, 115, 11, radioColor);
    dashboardFrame.drawLine(123, 4, 125, 6, radioColor);
    dashboardFrame.drawFastVLine(125, 7, 2, radioColor);
    dashboardFrame.drawLine(125, 9, 123, 11, radioColor);
    dashboardFrame.drawPixel(117, 6, radioColor);
    dashboardFrame.drawFastVLine(116, 7, 2, radioColor);
    dashboardFrame.drawPixel(117, 9, radioColor);
    dashboardFrame.drawPixel(121, 6, radioColor);
    dashboardFrame.drawFastVLine(122, 7, 2, radioColor);
    dashboardFrame.drawPixel(121, 9, radioColor);
    dashboardFrame.drawPixel(119, 7, radioColor);
    dashboardFrame.drawFastVLine(119, 8, 5, radioColor);
    dashboardFrame.drawLine(119, 11, 117, 16, radioColor);
    dashboardFrame.drawLine(119, 11, 121, 16, radioColor);
    dashboardFrame.drawFastHLine(118, 14, 3, radioColor);
    dashboardFrame.drawFastHLine(116, 16, 7, radioColor);
    dashboardFrame.setTextColor(radioColor);
    dashboardFrame.setCursor(130, 7);
    dashboardFrame.print("LoRa");

    {
        drawSensorCard(4, 22, "TEMP", hasData ? data.temperature : NAN, true, 0xFD68);
        drawSensorCard(82, 22, "HUMI", hasData ? data.humidity : NAN, false, 0x4DDF);
        drawSensorCard(4, 63, "SOIL 1", hasData ? data.soil1 : NAN, false, green);
        drawSensorCard(82, 63, "SOIL 2", hasData ? data.soil2 : NAN, false, green);
    }

    dashboardFrame.fillRect(0, 103, 160, 11, bg);
    dashboardFrame.setTextSize(1);
    // One shared irrigation mode; show only fresh status reported by STM32.
    /* V1:ON V2:OFF P:ON AUTO */ 
    const ValveState valves[] = {
        hasData && !stale ? data.valve1 : VALVE_UNKNOWN,
        hasData && !stale ? data.valve2 : VALVE_UNKNOWN};
    const PumpState pump = hasData && !stale ? data.pump : PUMP_UNKNOWN;    
    const IrrigationMode mode = hasData && !stale ? data.irrigationMode : MODE_UNKNOWN;
    for (int i = 0; i < 2; ++i)
    {
        dashboardFrame.setCursor(i == 0 ? 3 : 43, 105);
        dashboardFrame.setTextColor(muted);
        dashboardFrame.print(i == 0 ? "V1:" : "V2:");
        dashboardFrame.setTextColor(valves[i] == VALVE_ON ? green : (valves[i] == VALVE_OFF ? muted : 0xFD68));
        dashboardFrame.print(valves[i] == VALVE_ON ? "ON" : (valves[i] == VALVE_OFF ? "OFF" : "--"));
    }
    dashboardFrame.setCursor(83, 105);
    dashboardFrame.setTextColor(muted);
    dashboardFrame.print("P:");
    dashboardFrame.setTextColor(pump == PUMP_ON ? green : (pump == PUMP_OFF ? muted : 0xFD68));
    dashboardFrame.print(pump == PUMP_ON ? "ON" : (pump == PUMP_OFF ? "OFF" : "--"));
    dashboardFrame.setCursor(124, 105);
    dashboardFrame.setTextColor(mode == MODE_AUTO ? green : (mode == MODE_MANUAL ? 0x4DDF : 0xFD68));
    dashboardFrame.print(mode == MODE_AUTO ? "AUTO" : (mode == MODE_MANUAL ? "MANUAL" : "--"));
    dashboardFrame.fillRect(0, 114, 160, 14, bg);
    dashboardFrame.fillCircle(7, 121, 2, radioColor);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(radioColor);
    dashboardFrame.setCursor(14, 118);
    char liveText[20] = "WAIT";
    if (hasData && stale)
        snprintf(liveText, sizeof(liveText), "OLD");
    else if (hasData && !stale && data.irrigationPhase == IRRIGATION_PHASE_FAILED)
    {
        snprintf(liveText, sizeof(liveText), "Z%u FAIL C%u",
                 (unsigned int)data.activeZone, (unsigned int)data.irrigationCycle);
    }
    else if (hasData && data.streaming)
    {
        const char *phase = data.irrigationPhase == IRRIGATION_PHASE_WATERING ? "WATER" :
                            data.irrigationPhase == IRRIGATION_PHASE_SOAK ? "SOAK" :
                            data.irrigationPhase == IRRIGATION_PHASE_MEASURING ? "MEASURE" :
                            data.irrigationPhase == IRRIGATION_PHASE_FAILED ? "FAIL" : "IDLE";
        if (data.irrigationMode == MODE_AUTO)
            snprintf(liveText, sizeof(liveText), "Z%u %s C%u",
                     (unsigned int)data.activeZone, phase, (unsigned int)data.irrigationCycle);
        else
            snprintf(liveText, sizeof(liveText), "Z%u %s",
                     (unsigned int)data.activeZone, phase);
    }
    else if (hasData)
        snprintf(liveText, sizeof(liveText), "LIVE %lus", (unsigned long)(age / 1000));
    dashboardFrame.print(liveText);

    // Pin thuộc về thiết bị (STM32) từ xa; ẩn mức pin (--%) khi dữ liệu đã cũ.
    const bool batteryValid = hasData && !stale && data.batteryPercent <= 100;
    const uint16_t batteryColor = !batteryValid ? muted : (data.batteryPercent <= 20 ? ST77XX_RED :           // Đỏ ≤ 20%
                                                               (data.batteryPercent <= 50 ? 0xFD68 : green)); // Xanh > 50%, cam từ 21–50%
    char batteryText[5] = "--%";
    if (batteryValid)
    {
        snprintf(batteryText, sizeof(batteryText), "%u%%", (unsigned int)data.batteryPercent);
    }

    // Canh cả cụm pin theo mép phải màn hình: 3 px lề, 3 px giữa icon và chữ.
    const int batteryTextX = 157 - strlen(batteryText) * 6;
    const int batteryX = batteryTextX - 24;
    const int batteryLabelX = batteryX - 17;
    constexpr int batteryY = 116;
    constexpr int batteryInnerWidth = 12;
    dashboardFrame.setTextColor(muted);
    dashboardFrame.setCursor(batteryLabelX, 118);
    dashboardFrame.print("TX");
    dashboardFrame.drawRect(batteryX, batteryY, 18, 11, batteryColor);
    dashboardFrame.fillRect(batteryX + 18, batteryY + 3, 2, 5, batteryColor);
    if (batteryValid && data.batteryPercent > 0)
    {
        // Làm tròn lên để mức pin thấp vẫn nhìn thấy được một cột màu.
        const int levelWidth = (data.batteryPercent * batteryInnerWidth + 99) / 100;
        dashboardFrame.fillRoundRect(batteryX + 3, batteryY + 3, levelWidth, 5, 1, batteryColor);
    }
    dashboardFrame.setTextColor(batteryColor);
    dashboardFrame.setCursor(batteryTextX, 118);
    dashboardFrame.print(batteryText);

    static bool initialized = false;
    static char previousClock[6] = "";
    static uint16_t previousWifi = 0, previousRadio = 0;
    static ValveState previousValves[2] = {VALVE_UNKNOWN, VALVE_UNKNOWN};
    static PumpState previousPump = PUMP_UNKNOWN;
    static IrrigationMode previousMode = MODE_UNKNOWN;
    static bool previousHasData = false;
    static uint32_t previousAgeSeconds = 0;
    static uint8_t previousBatteryPercent = 0;
    static uint8_t previousActiveZone = 0;
    static IrrigationPhase previousPhase = IRRIGATION_PHASE_UNKNOWN;
    static uint8_t previousCycle = 0;
    static bool previousStreaming = false;
    static char previousValues[4][20] = {};

    if (!initialized || strcmp(previousClock, clockText) != 0)
        flushDashboardRegion(0, 0, 40, 21);
    if (!initialized || previousWifi != wifiColor)
        flushDashboardRegion(40, 0, 60, 21);
    if (!initialized || previousRadio != radioColor)
        flushDashboardRegion(100, 0, 60, 21);

    const float values[] = {data.temperature, data.humidity, data.soil1, data.soil2};
    for (int i = 0; i < 4; ++i)
    {
        char valueText[20];
        if (hasData && isfinite(values[i]))
            snprintf(valueText, sizeof(valueText), "%.1f", values[i]);
        else
            snprintf(valueText, sizeof(valueText), "--");
        if (!initialized || strcmp(previousValues[i], valueText) != 0)
            flushDashboardRegion(i % 2 == 0 ? 4 : 82, i < 2 ? 22 : 63, 74, 38);
        strcpy(previousValues[i], valueText);
    }

    if (!initialized || previousValves[0] != valves[0] ||
        previousValves[1] != valves[1] || previousPump != pump || previousMode != mode)
        flushDashboardRegion(0, 103, 160, 11);
    if (!initialized || previousHasData != hasData || previousRadio != radioColor ||
        previousBatteryPercent != data.batteryPercent ||
        previousActiveZone != data.activeZone || previousPhase != data.irrigationPhase ||
        previousCycle != data.irrigationCycle || previousStreaming != data.streaming ||
        (hasData && previousAgeSeconds != age / 1000))
        flushDashboardRegion(0, 114, 160, 14);

    strcpy(previousClock, clockText);
    previousWifi = wifiColor;
    previousRadio = radioColor;
    previousValves[0] = valves[0];
    previousValves[1] = valves[1];
    previousPump = pump;
    previousMode = mode;
    previousHasData = hasData;
    previousAgeSeconds = age / 1000;
    previousBatteryPercent = data.batteryPercent;
    previousActiveZone = data.activeZone;
    previousPhase = data.irrigationPhase;
    previousCycle = data.irrigationCycle;
    previousStreaming = data.streaming;
    initialized = true;
}

void DisplayManager_Begin(QueueHandle_t displayData)
{
    displayQueue = displayData;
    /* TFT INIT */
    spi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 20);
    tft.println("SMART FARM IoT");
    vTaskDelay(pdMS_TO_TICKS(1000));    
}

void DisplayManager_Run(void *pvParameters)
{
    Serial.println("[RTOS] DisplayTask started");

    SensorData data = {};
    bool hasData = false;
    uint32_t receivedAt = 0;

    if (!dashboardFrame.getBuffer())
    {
        Serial.println("[DISPLAY] Framebuffer allocation failed");
        vTaskDelete(nullptr);
        return;
    }

    dashboardFrame.fillScreen(0x0862);
    tft.fillScreen(0x0862);
    updateDisplay(data, hasData, receivedAt);

    for (;;)
    {
        // Refresh connection status and data age even when no sensor data arrives.
        if (xQueueReceive(displayQueue, &data, pdMS_TO_TICKS(200)) == pdPASS)
        {
            Serial.println("[DISPLAY] Update TFT");

            hasData = true;
            receivedAt = data.receivedAt;
        }
        updateDisplay(data, hasData, receivedAt);

        /* Nhường CPU */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
