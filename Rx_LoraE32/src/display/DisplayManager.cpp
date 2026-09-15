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
        drawSensorCard(82, 22, "HUM", hasData ? data.humidity : NAN, false, 0x4DDF);
        drawSensorCard(4, 63, "SOIL 1", hasData ? data.soil1 : NAN, false, green);
        drawSensorCard(82, 63, "SOIL 2", hasData ? data.soil2 : NAN, false, green);
    }

    dashboardFrame.fillRect(0, 103, 160, 11, bg);
    dashboardFrame.setTextSize(1);
    const uint16_t commandColor = status.command == COMMAND_DISPLAY_FAIL ? ST77XX_RED :
        (status.command == COMMAND_DISPLAY_ACK ? green :
        (status.command == COMMAND_DISPLAY_WAIT ? 0xFD68 : muted));
    dashboardFrame.setTextColor(commandColor);
    dashboardFrame.setCursor(5, 105);
    if (status.command == COMMAND_DISPLAY_NONE)
        dashboardFrame.print("CMD: --");
    else
    {
        char commandText[26];
        const char *state = status.command == COMMAND_DISPLAY_WAIT ? "WAIT" :
            (status.command == COMMAND_DISPLAY_ACK ? "ACK" : "FAIL");
        if (status.command == COMMAND_DISPLAY_WAIT)
            snprintf(commandText, sizeof(commandText), "Z%u %s: %s %u/%u", status.zone,
                     status.irrigationOn ? "ON" : "OFF", state, status.attempt, MAX_CMD_RETRIES);
        else
            snprintf(commandText, sizeof(commandText), "Z%u %s: %s", status.zone,
                     status.irrigationOn ? "ON" : "OFF", state);
        dashboardFrame.print(commandText);
    }
    
    // MQTT shares the command row; reserve its right edge for icon and label.
    const uint16_t mqttColor = status.mqttOnline && wifiOnline ? green : ST77XX_RED;
    // 16 x 10 outline cloud: rounded crown, soft shoulders and a flat base.
    static const uint8_t mqttCloudIcon[] PROGMEM = {
        0x03, 0xC0,
        0x04, 0x20,
        0x08, 0x10,
        0x38, 0x1C,
        0x40, 0x02,
        0x80, 0x01,
        0x80, 0x01,
        0x40, 0x02,
        0x3F, 0xFC,
        0x00, 0x00
    };
    dashboardFrame.drawBitmap(117, 104, mqttCloudIcon, 16, 10, mqttColor);
    dashboardFrame.setTextColor(mqttColor);
    dashboardFrame.setCursor(136, 105);
    dashboardFrame.print("MQTT");
    dashboardFrame.fillRect(0, 114, 160, 14, bg);
    dashboardFrame.fillCircle(7, 121, 2, radioColor);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(radioColor);
    dashboardFrame.setCursor(14, 118);
    dashboardFrame.print(!hasData ? "WAITING FOR DATA" : (stale ? "OLD" : "LIVE"));
    if (hasData)
    {
        char text[24];
        snprintf(text, sizeof(text), "Age Data:%lus", (unsigned long)(age / 1000));
        dashboardFrame.setTextColor(muted);
        dashboardFrame.setCursor(156 - strlen(text) * 6, 118);
        dashboardFrame.print(text);
    }
    static bool initialized = false;
    static char previousClock[6] = "";
    static uint16_t previousWifi = 0, previousRadio = 0, previousMqtt = 0;
    static DashboardStatus previousStatus;
    static bool previousHasData = false;
    static uint32_t previousAgeSeconds = 0;
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

    if (!initialized || previousStatus.command != status.command ||
        previousStatus.zone != status.zone || previousStatus.irrigationOn != status.irrigationOn ||
        (status.command == COMMAND_DISPLAY_WAIT && previousStatus.attempt != status.attempt))
        flushDashboardRegion(0, 103, 117, 11);
    if (!initialized || previousMqtt != mqttColor)
        flushDashboardRegion(117, 103, 43, 11);
    if (!initialized || previousHasData != hasData || previousRadio != radioColor ||
        (hasData && previousAgeSeconds != age / 1000))
        flushDashboardRegion(0, 114, 160, 14);

    strcpy(previousClock, clockText);
    previousWifi = wifiColor;
    previousRadio = radioColor;
    previousMqtt = mqttColor;
    previousStatus = status;
    previousHasData = hasData;
    previousAgeSeconds = age / 1000;
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
    delay(1000);

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

        // Refresh connection and command status even when no sensor data arrives.
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
