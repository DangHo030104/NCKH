#ifndef RX_LORAE32_CONFIG_APPCONFIG_H
#define RX_LORAE32_CONFIG_APPCONFIG_H

#include <stdint.h>

/* Private define/macro and Private variables ------------------------------------------------------------*/

#define MQTT_KEEPALIVE 60
#define MQTT_MAX_PACKET_SIZE 512

/* LORA E32 */
#define TX_PIN 16
#define RX_PIN 17
#define AUX_PIN 27
#define M0_PIN 25
#define M1_PIN 26

/* TFT DISPLAY */
#define TFT_CS 5
#define TFT_RST 19
#define TFT_DC 21
#define TFT_SCLK 18
#define TFT_MOSI 23

/* WiFi configuration portal */
#define WIFI_CONFIG_BUTTON_PIN 0
static const char *wifi_config_ap_ssid = "ESP32_Config";
static const char *wifi_config_ap_password = "12345678";

/* HIVEMQ CLOUD */
static const char *mqtt_server = "10cf23427b77452faec8dc86e09f1bc1.s1.eu.hivemq.cloud";
static const int mqtt_port = 8883;
static const char *mqtt_user = "tienduc";
static const char *mqtt_password = "D@ucffgh123";

/* MQTT TOPIC */
// ESP32 publish data sensor lên Web
static const char *publish_topic = "iot/sensor/data/fb0f45aad57744a4ba70";

// ESP32 nhận command từ Web
static const char *subscribe_topic = "iot/device/control/fb0f45aad57744a4ba70";

// ESP32 publish status CMD để Web hiển thị ngay.
static const char *command_status_topic = "iot/device/control-status/fb0f45aad57744a4ba70";

/* CONFIGURATION PARAMETERS */
const unsigned long REQUEST_INTERVAL = 60000;       // 60s
const unsigned long DATA_TIMEOUT_MS = 5000;         // Time Wait DATA STM32 sau khi gửi REQ
const unsigned long CMD_ACK_TIMEOUT_MS = 1000;      // Time Wait ACK STM32 sau khi gửi CMD
const uint8_t MAX_CMD_RETRIES = 5;

// Độ trễ ngẫu nhiên từ 80 đến 280 ms khi retry -> Tránh retry trùng telemetry STM32
const unsigned long CMD_RETRY_JITTER_MIN_MS = 80;   
const unsigned long CMD_RETRY_JITTER_MAX_MS = 280;
const unsigned long TELEMETRY_TIMEOUT_MS = 2500;    // Resume polling if telemetry drop

// Allow two polling periods and a response window before marking data stale
const unsigned long SENSOR_STALE_TIMEOUT_MS = 2 * REQUEST_INTERVAL + DATA_TIMEOUT_MS;   // 125s

const unsigned long MQTT_RECONNECT_INTERVAL = 5000;
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RESET_HOLD_MS = 5000;

#endif // RX_LORAE32_CONFIG_APPCONFIG_H
