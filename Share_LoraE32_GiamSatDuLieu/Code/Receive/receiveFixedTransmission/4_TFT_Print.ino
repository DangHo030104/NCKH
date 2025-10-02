void TFT_Print() {

  tft.drawRect(1, 2, 158, 126, TFT_WHITE);
  tft.drawLine(40, 0, 40, 128, TFT_WHITE);
  tft.drawLine(100, 0, 100, 128, TFT_WHITE);

  tft.drawLine(0, 29, 158, 29, TFT_WHITE);
  tft.drawLine(0, 54, 158, 54, TFT_WHITE);
  tft.drawLine(0, 79, 158, 79, TFT_WHITE);
  tft.drawLine(0, 103, 158, 103, TFT_WHITE);
  tft.setSwapBytes(true);
  tft.pushImage(2, 30, 24, 24, nhietdo);
  tft.pushImage(5, 55, 24, 24, iconDoAm);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextFont(1);
  tft.setCursor(6, 87);
  tft.print("PM2.5");
  tft.pushImage(7, 104, 23, 23, iconPin);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(1);
  tft.setCursor(21, 35); tft.print("(C)");
  tft.setCursor(29, 60); tft.print('%');

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(1);
  tft.setCursor(7, 8); tft.print((String)hour()); tft.print(":");
  if (minute() < 10) {
    tft.print("0");
  }
  else {}
  tft.print(String(minute()));
  tft.setCursor(15, 17);
  if (second() < 10) {
    tft.print("0");
  }
  else {}
  tft.print((String)second());

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("KV.1", 50, 7); tft.drawString("KV.2", 110, 7);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextFont(2);
  tft.setCursor(44, 31); tft.print(nhietdo1, 2); tft.setCursor(104, 31); tft.print(nhietdo2, 2);
  tft.setCursor(44, 57); tft.print(doam1, 2); tft.setCursor(104, 57); tft.print(doam2, 2);
  tft.setCursor(44, 107); tft.print(dienap1, 2); tft.print("V");
  tft.setCursor(104, 107); tft.print(dienap2, 2); tft.print("V");

  if ( mauCanhBao == 1) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }
  else tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(44, 84); tft.print(bui1, 2); tft.print(" ");     tft.setCursor(104, 84); tft.print(bui2, 2); tft.print(" ");

}
