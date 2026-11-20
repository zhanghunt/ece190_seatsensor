// I2C scanner: lists the address of every I2C device the Arduino can see.
// Open Serial Monitor at 9600 baud.
// Grove LCD RGB Backlight should show 0x3E (text chip) plus
// 0x62 (v4 backlight chip) or 0x30 (v5 backlight chip).

#include <Wire.h>

void setup() {
  Serial.begin(9600);
  while (!Serial) { }   // R4 waits for the Serial Monitor to open
  Wire.begin();
}

void loop() {
  int found = 0;
  Serial.println("Scanning...");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();   // 0 means a device replied

    if (error == 0) {
      Serial.print("  Device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("  No I2C devices found - check the cable, port and 5V switch");
  }
  Serial.println();
  delay(3000);
}
