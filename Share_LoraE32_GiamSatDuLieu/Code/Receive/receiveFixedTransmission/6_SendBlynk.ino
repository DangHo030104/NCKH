void senBlynk() {
  
  Blynk.virtualWrite(0, nhietdo1);
  Blynk.virtualWrite(1, doam1);
  Blynk.virtualWrite(2, bui1);
  Blynk.virtualWrite(3, dienap1);
  Blynk.virtualWrite(4, nhietdo2);
  Blynk.virtualWrite(5, doam2);
  Blynk.virtualWrite(6, bui2);
  Blynk.virtualWrite(7, dienap2);
   Blynk.virtualWrite(8, (String)second());
}
