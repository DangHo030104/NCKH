void CanhBao() {
  currentMillis = millis();
  if (currentMillis - previousMillis >= 500) {
    previousMillis = currentMillis;
    if (ledState == LOW) {
      ledState = HIGH;
    } else {
      ledState = LOW;
    }
  }
  ERR2 =0;
  ERR = 0;
  if (bui1 > buiHigh)       { ERR =1; ERR2 = 1; }
  if (dienap1 < batLow)     { ERR =1; } 
 

  
  if (ERR == 1) {
    digitalWrite(ledRun, HIGH);        // tắt LED Xanh
    digitalWrite(ledAlarm, ledState);  // Blink LED Đỏ và còi Buzz
  }
  else {
    digitalWrite(ledRun, LOW);         // bật LED xanh
    digitalWrite(ledAlarm, HIGH);      // tắt LED đỏ
  }
  if (ERR2 == 1 && ERR == 1){
    digitalWrite(buzz, !ledState);     
    mauCanhBao = 1;
  }
  else {
    digitalWrite(buzz, LOW);       // tắt còi buzz
    mauCanhBao = 0;
  }
  
}
