// Step 3: first full seat sensor
// Ultrasonic decides seated / standing, counts reps, tracks seated time,
// shows it all on the Grove LCD. Touch sensor resets everything.
//
// Wiring: ultrasonic Trig -> D7, Echo -> D8 (jumpers, 5V + GND)
//         Grove LCD -> I2C port, touch -> D2 port, buzzer -> D3 port
//         (keep your pull-up fix in place)

#include <Wire.h>
#include "rgb_lcd.h"

rgb_lcd lcd;

const int TRIG_PIN   = 7;
const int ECHO_PIN   = 8;
const int TOUCH_PIN  = 2;
const int BUZZER_PIN = 3;

// Two thresholds (hysteresis): the gap in between is a "no change" zone,
// so a reading wobbling around one number can't flip the state rapidly.
const float SEATED_BELOW_CM = 25.0;  // closer than this -> becomes seated
const float EMPTY_ABOVE_CM  = 35.0;  // farther than this -> becomes standing

// A new state must show up this many readings in a row before we accept it
// (3 readings x 100 ms = 0.3 s). Filters out single bad echoes.
const int CONFIRM_READINGS = 3;
const unsigned long READ_INTERVAL_MS = 100;

bool seated = false;
int pendingCount = 0;
int reps = 0;
unsigned long seatedMs = 0;      // seated time from finished sitting periods
unsigned long seatedSince = 0;   // when the current sitting period started
unsigned long lastRead = 0;
float lastDistance = -1;
bool wasTouched = false;

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) {
    return -1;   // no echo
  }
  return duration * 0.0343 / 2.0;
}

void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

unsigned long totalSeatedMs() {
  if (seated) {
    return seatedMs + (millis() - seatedSince);  // include the current sit
  }
  return seatedMs;
}

void updateDisplay() {
  char line1[17];   // 16 characters + the '\0' that ends a C string
  char line2[17];
  unsigned long secs = totalSeatedMs() / 1000;

  // Fixed-width formatting overwrites old characters, so no lcd.clear()
  // is needed (clearing every update makes the screen flicker).
  snprintf(line1, sizeof(line1), "Reps:%-4d %s", reps, seated ? "SEATED" : "UP    ");
  snprintf(line2, sizeof(line2), "Sit %02lu:%02lu %4dcm", secs / 60, secs % 60, (int)lastDistance);

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.begin(16, 2);
  lcd.setRGB(0, 255, 80);   // green = standing / empty
  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  // Touch = reset (counted once per tap, same edge idea as step 2)
  bool isTouched = (digitalRead(TOUCH_PIN) == HIGH);
  if (isTouched && !wasTouched) {
    reps = 0;
    seatedMs = 0;
    seatedSince = now;
    beep(200);
  }
  wasTouched = isTouched;

  // Read the sensor every 100 ms without using delay(), so the touch
  // sensor above is still checked constantly
  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;
    float d = readDistanceCm();

    if (d > 0) {
      lastDistance = d;
      Serial.println(d);   // watch the numbers while you tune thresholds

      bool looksSeated;
      if (seated) {
        looksSeated = (d < EMPTY_ABOVE_CM);    // stay seated until clearly gone
      } else {
        looksSeated = (d < SEATED_BELOW_CM);   // need to be clearly close to sit
      }

      if (looksSeated != seated) {
        pendingCount++;
        if (pendingCount >= CONFIRM_READINGS) {
          seated = looksSeated;
          pendingCount = 0;

          if (seated) {                        // just sat down
            seatedSince = now;
            lcd.setRGB(0, 120, 255);           // blue = seated
          } else {                             // just stood up = one rep
            seatedMs += now - seatedSince;
            reps++;
            beep(80);
            lcd.setRGB(0, 255, 80);
          }
        }
      } else {
        pendingCount = 0;   // streak broken, start counting again
      }
    }

    updateDisplay();
  }
}
