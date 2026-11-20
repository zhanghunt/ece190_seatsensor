/*
  Test: seat mat switch + DS3231 clock on Arduino Uno R4 Minima
  Run this before changing the main seat_sensor sketch.

  Wiring
    Seat mat   : red -> D7, black -> GND   (no resistor needed, uses the internal pull-up)
    DS3231     : VCC -> 5V, GND -> GND, SDA -> SDA, SCL -> SCL
                 32K and SQW are not connected

  Library (Tools > Manage Libraries): "RTClib" by Adafruit
    When it asks about Adafruit BusIO, click "Install all".

  Serial Monitor: 115200 baud, line ending "Newline".
  To set the clock, type the time like this and press Enter:
    2026-11-08 13:45:00
*/

#include <Wire.h>
#include <RTClib.h>

const int SEAT_PIN = 7;
const unsigned long SEAT_DEBOUNCE_MS = 50;   // ignore contact bounce shorter than this

RTC_DS3231 rtc;
bool rtcFound = false;

bool seatStable = false;          // debounced state: true = someone sitting
bool seatLastRaw = false;
unsigned long seatRawChangedAt = 0;
unsigned long lastPrint = 0;

// Mat closes (about 150 ohm) when sat on, which pulls the pin LOW.
bool readSeatRaw() {
  return digitalRead(SEAT_PIN) == LOW;
}

void printTime(const DateTime &t) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  Serial.print(buf);
}

// Reads "YYYY-MM-DD HH:MM:SS" from the Serial Monitor and sets the clock.
void checkSerialTimeSet() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  int y, mo, d, h, mi, s;
  if (sscanf(line.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
    rtc.adjust(DateTime(y, mo, d, h, mi, s));
    Serial.print("Clock set to ");
    printTime(rtc.now());
    Serial.println();
  } else {
    Serial.println("Format: 2026-11-08 13:45:00");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}   // wait up to 3 s for the Serial Monitor

  pinMode(SEAT_PIN, INPUT_PULLUP);
  seatLastRaw = seatStable = readSeatRaw();

  Wire.begin();
  rtcFound = rtc.begin();
  if (!rtcFound) {
    Serial.println("DS3231 NOT FOUND - check SDA/SCL/VCC/GND");
    return;
  }

  // No coin cell, so the clock forgets the time every time power is removed.
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("Clock had lost power: set to the time this sketch was compiled.");
    Serial.println("If that's wrong, type the correct time (2026-11-08 13:45:00).");
  }
}

void loop() {
  // Debounce the seat mat
  bool raw = readSeatRaw();
  if (raw != seatLastRaw) {
    seatLastRaw = raw;
    seatRawChangedAt = millis();
  }
  if (raw != seatStable && millis() - seatRawChangedAt >= SEAT_DEBOUNCE_MS) {
    seatStable = raw;
    Serial.print(">>> ");
    if (rtcFound) { printTime(rtc.now()); Serial.print("  "); }
    Serial.println(seatStable ? "SAT DOWN" : "STOOD UP");
  }

  // Once a second: time, seat state, clock temperature
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    if (rtcFound) {
      printTime(rtc.now());
      Serial.print("  temp ");
      Serial.print(rtc.getTemperature(), 1);
      Serial.print(" C  ");
    }
    Serial.println(seatStable ? "seat: OCCUPIED" : "seat: empty");
  }

  if (rtcFound) checkSerialTimeSet();
}
