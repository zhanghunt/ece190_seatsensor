// Step 2: Grove kit test - touch sensor, buzzer, LCD
// Grove LCD -> any I2C port, touch sensor -> D2 port, buzzer -> D3 port
// Library: in Arduino IDE, Tools > Manage Libraries, search
// "Grove - LCD RGB Backlight" (by Seeed Studio) and install it.

#include <Wire.h>
#include "rgb_lcd.h"

rgb_lcd lcd;

const int TOUCH_PIN = 2;
const int BUZZER_PIN = 3;

int touchCount = 0;
bool wasTouched = false;   // touch state from the previous loop

void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void showCount() {
  lcd.clear();
  lcd.setCursor(0, 0);       // column 0, row 0 (top line)
  lcd.print("Touches:");
  lcd.setCursor(0, 1);       // bottom line
  lcd.print(touchCount);
}

void setup() {
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.begin(16, 2);          // 16 columns, 2 rows
  lcd.setRGB(0, 120, 255);   // backlight colour (red, green, blue: 0-255)
  showCount();
}

void loop() {
  bool isTouched = (digitalRead(TOUCH_PIN) == HIGH);

  // Count only the moment it changes from "not touched" to "touched".
  // Holding your finger down still counts as ONE touch.
  // This is the same idea you'll use to count sit-to-stand reps.
  if (isTouched && !wasTouched) {
    touchCount++;
    beep(80);
    showCount();
    lcd.setRGB(0, 255, 80);  // flash green
  }
  if (!isTouched && wasTouched) {
    lcd.setRGB(0, 120, 255); // back to blue on release
  }

  wasTouched = isTouched;
  delay(20);
}
