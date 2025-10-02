#include <Arduino.h>
#include "LoRa_E32.h"

// Khai báo LoRa E32 (TX=16, RX=17, M0=4, M1=5, Not auxPin = -1 )
LoRa_E32 e32ttl100(16, 17, &Serial2, -1, 4, 5, UART_BPS_RATE_9600, SERIAL_8N1);

// Định nghĩa chân LED.
#define LED_PIN 2

void setup() {
  Serial.begin(9600);
  e32ttl100.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);               // Tắt LED ban đầu
  Serial.println("LoRa Receiver Started");
}

void loop() {
  // Kiểm tra dữ liệu đến
  if (e32ttl100.available() > 1) {
    ResponseContainer rc = e32ttl100.receiveMessage();

    rc.data.trim(); // xóa khoảng trắng thừa ở đầu/cuối

    // Tìm vị trí dấu cách
    int spaceIndex = rc.data.indexOf(' ');
  
    if (spaceIndex != -1) {
      // Cắt nhiệt độ từ đầu đến dấu cách
      String tempStr = rc.data.substring(0, spaceIndex);
      // Cắt độ ẩm từ sau dấu cách đến hết
      String humiStr = rc.data.substring(spaceIndex + 1);

      // Chuyển sang float để xử lý số liệu
      float temp = tempStr.toFloat();
      float humi = humiStr.toFloat();

      // In kết quả
      Serial.print("Nhiệt độ: ");
      Serial.println(temp, 2);     // In 2 chữ số thập phân
      Serial.print("Độ ẩm: ");
      Serial.println(humi, 2);

      if(temp > 30.0) {
        digitalWrite(LED_PIN, HIGH); 
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }
  }
}
