# Wheelchair Seat Sensor – Project Handoff Summary

Nov 8, 2026 · @Hunt

## Overview

This is a wheelchair seat detector on an Arduino Uno R4 Minima. It passively tracks time seated versus time out of the seat and counts sit-to-stand reps, for people working toward independence. It is a school project (ECE 198, University of Waterloo, Fall 2026) that will be demoed live, so every feature must be showable in about 2 minutes.

- **Seat sensing:** a passive 2-wire seat-belt pressure mat. It replaced the original ultrasonic sensor on Nov 8, 2026.
- **Timekeeping:** DS3231 real-time clock, for real times of day and midnight rollover.
- **Displays:** a 1.8" TFT for graphs and data, and the Grove RGB LCD as a big-text status light.
- **Sound:** Grove buzzer for long-sit reminders and test beeps.
- **Input:** the Grove touch sensor only (tap = next, hold = select).
- **Storage:** the R4's built-in 8 KB EEPROM. There is no SD card.
- **Sketch file:** `seat_sensor.ino`, in `OneDrive - University of Waterloo\01 School\1A - Fall 2026\ECE 198\05 Projects\seat_sensor`.

No wheelchair is available yet, so everything is tested on a regular chair.

## Hardware inventory

Everything below is on hand. The top group is in the current build.

| Part | Role | Notes |
| --- | --- | --- |
| Arduino Uno R4 Minima | Main board | 5V logic. Each pin safely supplies only about 8 mA. No I2C pull-ups on board. |
| Grove Base Shield | Grove ports | Power switch set to **5V**. |
| Seat-belt pressure mat (2 wires: red, black) | Seat sensor | Passive membrane switch: over 10 MΩ when empty, 150 Ω or less when occupied. Needs a rigid backing on a wheelchair's sling seat. |
| DS3231 RTC module (pins: 32K, SQW, SCL, SDA, VCC, GND) | Clock | I2C address 0x68. Has its own SDA/SCL pull-ups. **No coin cell yet.** |
| 1.8" TFT, ST7735 (8 unnumbered pins) | Graphs and data | 3.3V logic. Pin order 1–8: GND, VCC, SCK, SDA, RES, DC, CS, BL. |
| Grove LCD RGB Backlight, 16x2 | Status light | I2C address 0x3E, plus 0x62 or 0x30. |
| Grove touch sensor | Only input | Capacitive. Triggers very easily. |
| Grove buzzer | Reminders | HIGH = beep. |
| Breadboard, jumper wires | Wiring |  |
| Resistors: 100 Ω, 1 kΩ, 10 kΩ | Dummy load, pull-ups, series protection |  |
| 10 nF ceramic capacitor, marked "A 103J" | Noise filtering | Goes across VCC and GND, never on a signal pin. |
| USB power bank | Power | See Power setup. |
| 4.7 mH inductor, marked "472K" | Not needed | Was mistaken for a capacitor. Should be out of the circuit. |
| HC-SR04 ultrasonic sensor | Retired | Replaced by the seat mat. |
| Small OLED (about 1.5 x 2.5 cm) | Unused | Optional. Don't force it into the design. |
| Two potentiometers (Grove one, breadboard one) | Unused | Decided against: the touch sensor is the only input. |
| 9V battery | Unused | Too weak; lasts only a few hours. |

**DS3231 coin-cell caution:** if the board is the common ZS-042 type, its charging circuit slowly charges the battery. Use a rechargeable LIR2032, or remove the board's charging resistor before fitting a regular CR2032.

## Wiring and pin map (current build)

This is the wiring after the seat mat and DS3231 swap on Nov 8, 2026. Power and ground for breadboard parts come from the breadboard's 5V and GND rails, fed by one jumper each from the Arduino.

| Component | Component pin | Connects to | Notes |
| --- | --- | --- | --- |
| Seat mat | Red | D7 | Uses the internal pull-up, so no resistor. LOW = someone sitting, HIGH = empty. |
| Seat mat | Black | GND | Red and black can't be swapped wrong: it's a plain switch. |
| DS3231 | VCC | 5V |  |
| DS3231 | GND | GND |  |
| DS3231 | SDA | SDA | Shield SDA/SCL header (next to AREF), or a spare Grove I2C port with a Grove-to-jumper cable. Shares the bus with the LCD. |
| DS3231 | SCL | SCL | Same as SDA. |
| DS3231 | 32K, SQW | Not connected |  |
| Grove LCD | Grove cable | Any Grove I2C port | Needs the pull-up fix below. |
| I2C pull-ups | Resistor 1 | 5V to SDA | 4.7 kΩ is ideal; 2.2–10 kΩ works. A4/A5 are the same lines as SDA/SCL. |
| I2C pull-ups | Resistor 2 | 5V to SCL | Same. |
| Touch sensor | Grove cable | Grove D2 port | Uses only SIG (yellow, D2), VCC and GND. The white wire lands on D3 but is unconnected inside the sensor. |
| Buzzer | Grove cable | Grove D3 port | No clash with the touch sensor's white wire. |
| TFT pin 1 | GND | GND |  |
| TFT pin 2 | VCC | 5V | Direct wire, nothing in series. |
| TFT pin 3 | SCK | D13 | Hardware SPI clock. |
| TFT pin 4 | SDA (MOSI/DIN) | D11 | Hardware SPI data. |
| TFT pin 5 | RES (reset) | D6 |  |
| TFT pin 6 | DC (also called A0 or RS) | D9 |  |
| TFT pin 7 | CS | D10 | Loose or wrong = screen stays white or blank. |
| TFT pin 8 | BL (backlight) | 3.3V | Never a digital pin: a pin can't supply the 20–40 mA it needs. |
| Dummy load | 100 Ω resistor | 5V rail to GND rail | Keeps the power bank awake. See Power setup. |

**Free pins:** D8 was freed when the ultrasonic came out (it used Trig D7 and Echo D8).

**Old diagrams may disagree.** An early diagram put TFT reset on D8; it moved to D6 when the ultrasonic needed D8. The table above is the current version.

**Optional extras on the breadboard:**

- 10 nF capacitor across the TFT's VCC and GND, or across the breadboard's 5V and GND rails near where power comes in.
- 1 kΩ series resistors on the TFT's signal wires (pins 3–7), bridging two breadboard rows. They protect the 3.3V screen from the R4's 5V signals. With only one spare, put it on RES first.
- A 100–470 µF electrolytic capacitor across 5V and GND for steadier power (not bought yet). Its striped leg goes to GND.

## Power setup

The device runs from a USB power bank, kept awake by a 100 Ω dummy-load resistor across 5V and GND. Laptop USB also works for testing.

- **Why the dummy load:** the power bank shut itself off after a few seconds because the circuit draws too little current. Many banks need about 50–150 mA to stay on.
- **What it draws:** 5 V across 100 Ω is about 50 mA and 0.25 W. The resistor gets warm but not hot, which is normal. 0.25 W is at the limit of a ¼ W resistor, so check its rating.
- **Cooler alternative:** 1 kΩ resistors in parallel from the + rail to the − rail. Each draws 5 mA at 0.025 W, so 10 give 50 mA and 20 give 100 mA.
- **Other options considered:** a power bank with a low-current or "IoT" mode; a 6xAA holder into the barrel jack (the R4 accepts 6–24 V there); or pulsing the load through an NPN transistor (no transistor on hand).
- **Current budget:** everything together is about 200 mA including the extra resistors. That's well within the board's 5V supply.
- **Extra 5V points:** use the breadboard rails or the shield's 5V/GND header first. If taking power from a Grove port, use only its red and black wires; the yellow and white wires are real Arduino pins. With the shield switch at 5V, every Grove port's VCC gives 5V.
- **Battery-power side effect:** a power bank isn't grounded, so the capacitive touch sensor gets weaker. Test it by touching GND with your other hand. A fix is a strip of foil or copper tape tied to GND where the hand naturally rests.
- **Clock:** with no coin cell, the DS3231 forgets the time whenever power is removed. Keep it powered for the demo.

## Wiring problems solved so far

These are the hardware issues already found and how each was fixed, so they aren't re-diagnosed from scratch.

| Problem | Cause | Fix |
| --- | --- | --- |
| Grove LCD dark; I2C scanner said "No I2C devices found" | Neither the R4 Minima nor the LCD module has I2C pull-up resistors, so SDA/SCL float. | Two pull-ups: 5V to SDA and 5V to SCL. The DS3231 now adds its own; if the LCD glitches, remove the extra pull-ups first. |
| TFT dim or flickering | Backlight on a digital pin, or competing for the weak 3.3V supply. | BL straight to the 3.3V pin. VCC straight to 5V. |
| "472K" part in series with TFT VCC | It's a 4.7 mH inductor, not a capacitor. Its coil resistance can drop about 1 V, leaving the screen around 4 V. | Take it out and wire VCC directly to 5V. |
| Capacitor wired in series with power | A capacitor blocks DC, so in series it cuts power. | Capacitors go across VCC and GND as a side branch, never in line. |
| Where to put the 10 nF capacitor | The touch sensor uses Grove cables, so it can't go there. | Across the TFT's VCC and GND, or across the breadboard rails. Never on SDA, SCL or other signal pins. |
| TFT goes fully white until restart | A power or signal glitch resets the screen's controller. SPI had been raised to 24 MHz, too fast for breadboard jumpers. | SPI back to 8 MHz, plus automatic screen re-setup in the sketch. Wiggle-test VCC, GND and RES. Add 1 kΩ in series on RES if it persists. |
| 5V signals into a 3.3V screen | Out of spec; can cause glitchy drawing over time. | Optional 1 kΩ series resistor on each TFT signal wire (pins 3–7). |
| Display dimming from crowded parts | Resistor legs or wires touching a 5V or signal row. | Space resistors apart. Use a wiggle test and multimeter to find shorts. |
| Touch sensor stopped responding on battery | Floating ground on the power bank. It also measures its "untouched" level at power-up. | GND foil near the hand. Power-cycle with hands away from the pad. |

## How the device behaves

The device quietly collects data and shows how active today is compared with a normal day. Interaction is minimal: one touch sensor, a carousel of pages, and a menu behind a hold.

**Seat detection rules**

- Contact bounce under 50 ms is ignored.
- Passive tracking: the seat must stay empty 1.5 s to count as a stand, and occupied 1 s to count as a sit. This stops hops and bounces on the chair from counting as reps.
- Sit-to-stand exercise test: planned shorter stand time of about 0.4–0.6 s, so fast reps aren't missed. Final values depend on measured bounce lengths.
- Data is only recorded from the first sit of the day, between 6 am and 10 pm.
- Mat check with a multimeter: 150 Ω or less when pressed, open circuit when empty.

**Touch rules (the sensor triggers very easily)**

- Tap = next page. Hold 0.8 s = select.
- Presses under 0.08 s are always ignored.
- When the main screen is locked (idle), a tap must last 0.2 s. A padlock shows in the status bar.
- Holds over 4 s are ignored (a resting arm).
- The screen relocks and returns to the main page after 30 s idle.

**TFT pages, in carousel order with page dots**

1. **Today vs usual (main screen):** a shaded band shows the normal range of time out of seat by this hour, from the last 14 days. Today's line is drawn on top. A label reads More active, About usual, Less active or Much less active.
2. **Sitting pattern:** 7 rows, one per day. Blocks are mint for out of seat, muted blue for sitting, and peach for sits over 48 minutes.
3. **Time out of seat:** 7 bars with a dashed line at the usual level.
4. **Last 30 days:** trend lines for share of time out of seat and longest sit.
5. **Exercise:** stand-up count versus goal, plus sit-to-stand test results. Holding here starts a test.

A hold opens the menu: settings (rep goal, reminder interval, sound), Data > Load demo data, and Fast mode (Off, 20x, 60x). A hidden Flappy game was also added.

**Grove LCD:** a status light. Its colour follows today's status (mint, sky, peach or coral) with big text like "LESS ACTIVE". It turns coral with "TIME TO STAND" when a sit runs too long.

**Buzzer:** beeps only for long-sit reminders and during a test. Reminders repeat at every interval while still seated. Stand-ups are counted silently.

**Clock:** after a fresh upload the clock is set from the compile time. If it isn't set, the device shows "Clock not set" instead of saving data with wrong times.

**Data storage (EEPROM, 8 KB):** each day is 42 bytes: totals plus 120 blocks of 8 minutes covering 16 hours. 90 days take about 3.8 KB, and the oldest day is overwritten after that. Settings and test results survive power loss, and today's totals are checkpointed regularly.

**Durations** display as 45s, 12m or 2h05m, so they can't be mistaken for a time of day.

## Design direction

The device is supplementary to the user's phone: they glance at it and rarely interact, but a brief check should be engaging. Long-term data and a clean UI system matter more than interactive features.

- **Passive first:** the main screen should feel like data collection. It shows graphs of time standing versus previous days, so sedentary days stand out. Reps and workouts live on a separate page.
- **Cut as gimmicky:** different sounds per action, pop-up banners, pulsing numbers and personalised encouragement messages.
- **Look:** clean and Apple-esque. Smooth transitions, polished rounded edges, muted colours, with bright colours reserved for what matters.
- **TFT colours:** pastel splashes; avoid pure black backgrounds. Keep the page dots.
- **Display roles:** the Grove LCD shows big text; the TFT handles smaller text and data. The OLED is optional and shouldn't be forced in.
- **Demo:** every feature must be showable in about 2 minutes. Demo data and fast mode exist for this.
- **Workflow:** UI changes are tried first in an HTML mock-up or simulator, then converted to Arduino code once approved.

## Current status and open items

The seat mat and DS3231 are wired and passed a standalone test; the clock keeps time while powered. The main sketch has been converted from ultrasonic to seat mat plus clock, and was checked in simulation but not yet confirmed on the board.

**Most recent work (Nov 8):** a report that fast mode showed "sitting" while off the seat. The fast-mode speed was correct; durations shown as `0:20` were being read as 20 minutes. Durations now read like `20s` or `20m`. Two more fixes went in: 8-minute blocks occasionally recorded wrong, and one bad clock reading marking the clock as not set.

**Known limitations**

- A loose mat wire reads as an empty seat. A switch can't tell "unplugged" from "nobody sitting".
- Without a coin cell, the clock forgets the time on every power loss.
- Capacitive touch is weaker on the power bank.

**To do**

- [ ] Test the updated main sketch on the real board.
- [ ] Measure bounce and fast-rep durations on the chair, then set the passive and exercise stand times.
- [ ] Buy a coin cell for the DS3231 (LIR2032, or CR2032 with the charging resistor removed).
- [ ] Confirm the 472K inductor is out and TFT VCC goes straight to 5V.
- [ ] Check the 100 Ω dummy-load resistor's wattage rating.
- [ ] Optional: buy a 100–470 µF electrolytic capacitor for the 5V rail.
- [ ] When a wheelchair is available, mount the mat on a rigid backing on the sling seat.
