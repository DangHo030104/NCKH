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

/* WiFi */
static const char *ssid = "Pho Tro Tret";
static const char *password = "Ngoc4795";

/* HIVEMQ CLOUD */
static const char *mqtt_server = "10cf23427b77452faec8dc86e09f1bc1.s1.eu.hivemq.cloud";
static const int mqtt_port = 8883;
static const char *mqtt_user = "tienduc";
static const char *mqtt_password = "D@ucffgh123";

/* MQTT TOPIC */
// ESP32 publish data sensor lên Web
static const char *publish_topic = "iot/sensor/data/301b5eb855b7485bb15e";

// ESP32 nhận command từ Web
static const char *subscribe_topic = "iot/device/control/301b5eb855b7485bb15e";


const unsigned long REQUEST_INTERVAL = 30000;  // 30s
const unsigned long DATA_TIMEOUT_MS = 5000;    // Time Wait DATA STM32 sau khi gửi REQ
const unsigned long CMD_ACK_TIMEOUT_MS = 5000; // Time Wait ACK STM32 sau khi gửi CMD
// Allow two polling periods and a response window before marking data stale.
const unsigned long SENSOR_STALE_TIMEOUT_MS = 2 * REQUEST_INTERVAL + DATA_TIMEOUT_MS;   // 65s
const uint8_t MAX_CMD_RETRIES = 3;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
//const unsigned long MQTT_PUBLISH_INTERVAL = 2000;

#endif // RX_LORAE32_CONFIG_APPCONFIG_H
