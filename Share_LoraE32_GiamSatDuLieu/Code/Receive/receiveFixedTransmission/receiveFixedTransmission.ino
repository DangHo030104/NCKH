/*
   LoRa E32-TTL-100
   Receive fixed transmission message as a specified point.
   https://www.mischianti.org/2019/11/10/lora-e32-device-for-arduino-esp32-or-esp8266-fixed-transmission-part-4/
  
  Update:
          + 04/04/2024: Sửa lại code nhằm phù hợp với BLynk phiên bản mới
  Địa chỉ:
          - configuration.ADDL = 6;
          - configuration.ADDH = 0;
          - configuration.CHAN = 0x09;

   LIBRARY
   - TFT_eSPI (2.5.43)

*/

TaskHandle_t Task2;

#include "iconSuDung.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

#include "LoRa_E32.h"
LoRa_E32 e32ttl(&Serial2); //  RX AUX M0 M1 (ESP32)

//===== Blynk IOT =====//
#define BLYNK_TEMPLATE_ID           "TMPxxxxxx"
#define BLYNK_TEMPLATE_NAME         "Device"
#define BLYNK_AUTH_TOKEN            "YourAuthToken"

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <WidgetRTC.h>

char auth[] = BLYNK_AUTH_TOKEN;

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Do Go Tinh Hop";
char pass[] = "12345679";
TaskHandle_t Core2;
BlynkTimer timer;
WidgetRTC rtc;

void clockDisplay()
{
  String currentTime = String(hour()) + ":" + minute() + ":" + second();
  String currentDate = String(day()) + " " + month() + " " + year();
}
BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
}


#define ledRun   21
#define ledAlarm 5
#define buzz     12


// -------------------------------------
void printParameters(struct Configuration configuration);
void printModuleInformation(struct ModuleInformation moduleInformation);

long dv, chuc, tram, nghin, chucnghin;
int s;
long t1, t2, t3, t4, t5;
String str = "";

float
nhietdo1,
doam1   ,
bui1    ,
dienap1 ,
nhietdo2,
doam2   ,
bui2    ,
dienap2 ,
buiHigh = 120.00,
batLow  = 3.60;

unsigned long
previousMillis        = 0,
currentMillis         = 0,
prevSerialMillis      = 0,
currentSerialMillis   = 0,
prevBlynkMillis      = 0,
currentBlynkMillis = 0;

int
ledState   = 0,
mauCanhBao = 0,
ERR        = 0,
ERR2       = 0;


void setup()
{
  Serial.begin(115200);

  pinMode(ledRun, OUTPUT);
  pinMode(ledAlarm, OUTPUT);
  pinMode(buzz, OUTPUT);

  digitalWrite(ledRun, HIGH);
  digitalWrite(ledAlarm, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  e32ttl.begin();

  // ---------------------------


  xTaskCreatePinnedToCore(
    Task2code,   /* Task function. */
    "Task2",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &Task2,      /* Task handle to keep track of created task */
    1);          /* pin task to core 1 */

} // end Setup

void Task2code( void * pvParameters ) {
  Blynk.begin(auth, ssid, pass);
  setSyncInterval(5); // Sync interval in seconds (10 minutes)
  while (1) {
    senBlynk();
    Blynk.run();
    timer.run();
  }
}

void loop()
{
  NhanDuLieu();
  TFT_Print();
  Onboard_Telemetry();
  CanhBao();
} // end loop
