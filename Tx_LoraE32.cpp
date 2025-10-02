#include <Arduino.h>
#include "LoRa_E32.h"

// Khai báo LoRa E32 (TX=16, RX=17, M0=4, M1=5, Not auxPin = -1 )
LoRa_E32 e32ttl100(16, 17, &Serial2, -1, 4, 5, UART_BPS_RATE_9600, SERIAL_8N1);

void setup() {
  Serial.begin(9600);
  e32ttl100.begin();

  Serial.println("LoRa Sender Started");
}

void loop() {
  String message = "Hello - I am BLACK!";
  ResponseStatus rs = e32ttl100.sendMessage(message);
  
  Serial.print("Send status: ");
  Serial.println(rs.getResponseDescription());

  delay(2000); // Gửi mỗi 2 giây
}