/*
  Wheelchair Seat Sensor - passive activity tracker
  Board: Arduino Uno R4 Minima + Grove Base Shield (switch at 5V)

  Hardware
    Seat mat switch    : red -> D7, black -> GND (uses the internal pull-up)
    DS3231 clock       : VCC -> 5V, GND -> GND, SDA -> SDA, SCL -> SCL
                         (32K and SQW not connected)
    Grove touch sensor : D2 port   (tap = next view, hold 0.6 s = select)
    Grove buzzer       : D3 port
    Grove LCD RGB      : any I2C port (keep the SDA/SCL pull-up fix)
    1.8" TFT (ST7735)  : 1 GND, 2 VCC -> 5V, 3 SCK -> D13, 4 SDA -> D11,
                         5 RES -> D6, 6 DC -> D9, 7 CS -> D10, 8 BL -> 3.3V

  Libraries (Arduino IDE > Tools > Manage Libraries)
    "Grove - LCD RGB Backlight" (Seeed Studio)
    "Adafruit ST7735 and ST7789 Library" (click "Install all" for Adafruit GFX)
    "RTClib" (Adafruit)                  (click "Install all" for Adafruit BusIO)

  Clock: uploading the sketch sets the clock to the time it was compiled.
  With no coin cell, the clock forgets the time when power is removed. Set
  it again in Menu > Settings > Set clock, by re-uploading, or by typing
  2026-11-08 13:45:00  into the Serial Monitor (115200 baud, "Newline"). Until it's set, the device works
  as before: hours since power-on and a "New day / continue" boot screen.

  Hidden game: on the Exercise page, hold the touch sensor for 2-4 s (the
  hold bar turns butter at 2 s), then let go. Tap to flap, hold to exit.

  The screens match the browser simulator coordinate for coordinate.
*/

#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>
#include "rgb_lcd.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <RTClib.h>

// ============================================================
// PINS
// ============================================================
const int TOUCH_PIN  = 2;
const int BUZZER_PIN = 3;
const int TFT_RST    = 6;
const int SEAT_PIN   = 7;    // seat mat: LOW = someone sitting
const int TFT_DC     = 9;
const int TFT_CS     = 10;

rgb_lcd lcd;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc;

// ============================================================
// COLOURS (RGB565). Slate background, never pure black.
// ============================================================
const uint16_t COL_BG       = 0x1905;  // deep slate (30, 33, 44)
const uint16_t COL_CARD     = 0x2988;  // raised slate
const uint16_t COL_BAND     = 0x31EA;  // "usual" range band
const uint16_t COL_FAINT    = 0x4A8C;  // gridlines, inactive
const uint16_t COL_SUB      = 0xA537;  // secondary text
const uint16_t COL_FG       = 0xF79F;  // primary text
const uint16_t COL_MINT     = 0x9F37;  // out of seat, more active
const uint16_t COL_SKY      = 0x965F;  // about usual, seated
const uint16_t COL_LAVENDER = 0xCDBF;  // trends
const uint16_t COL_BUTTER   = 0xFF31;  // exercise, bests
const uint16_t COL_PEACH    = 0xFDF1;  // less active, long sits
const uint16_t COL_CORAL    = 0xFC71;  // much less active, alerts
const uint16_t COL_MINT_DIM   = 0x42EB;
const uint16_t COL_SKY_DIM    = 0x428D;
const uint16_t COL_LAV_DIM    = 0x526D;
const uint16_t COL_BUTTER_DIM = 0x62E9;

// ============================================================
// SETTINGS AND TIMING
// ============================================================
const uint8_t  GOAL_OPTIONS[]     = {5, 10, 15, 20, 25, 30};
const uint16_t REMIND_OPTIONS_S[] = {30, 900, 1800, 2700, 3600};  // 30 s = demo
const int NUM_GOALS   = sizeof(GOAL_OPTIONS) / sizeof(GOAL_OPTIONS[0]);
const int NUM_REMINDS = sizeof(REMIND_OPTIONS_S) / sizeof(REMIND_OPTIONS_S[0]);

const unsigned long READ_INTERVAL_MS = 20;

// Seat filter: a change only counts once it has lasted this long, so a
// bounce or a weight shift isn't a stand. Measured on real time, even in fast mode.
// Raise these if bounces get counted; lower them if fast reps get missed.
const unsigned long STAND_CONFIRM_MS = 600;   // seat empty this long = a stand
const unsigned long SIT_CONFIRM_MS   = 400;   // seat pressed this long = a sit
const unsigned long TEST_CONFIRM_MS  = 350;   // both, during a sit-to-stand test
const unsigned long MIN_BEEP_GAP_MS  = 10000;  // reminders never beep closer than this (real time)
const unsigned long NEW_DAY_GAP_MS   = 6UL * 3600000UL;
const unsigned long CHECKPOINT_MS    = 10UL * 60000UL;
const unsigned long BOOT_CHOICE_MS   = 5000;
const unsigned long ANSWER_WINDOW_MS = 5UL * 60000UL;

// A day = 120 blocks of 8 minutes (16 h). Block: 0 no data, 1 seated, 2 out.
// With the clock set, block 0 starts at DAY_START_HOUR (6am to 10pm).
const int DAY_START_HOUR = 6;
const long UPLOAD_SECONDS = 15;     // compile-to-running delay added when setting the clock
const unsigned long SLOT_MS = 8UL * 60000UL;
const int SLOTS = 120;
const int SLOT_MIN = 8;
const int SLOT_BYTES = 30;          // 120 blocks x 2 bits
const int LONG_SIT_SLOTS = 6;       // 48 min seated in a row
const int BASELINE_DAYS = 14;

// Touch filter
const unsigned long GLITCH_MS         = 50;    // always ignored
const unsigned long LOCKED_TAP_MS     = 150;   // needed on the locked main screen
const unsigned long HOLD_MS           = 600;
const unsigned long HOLD_BAR_DELAY_MS = 100;
const unsigned long RESTING_MS        = 4000;  // arm resting on the sensor
const unsigned long LONG_HOLD_MS      = 1500;  // Set clock: undo (-1)
const unsigned long SECRET_HOLD_MS    = 2000;  // Exercise: hidden game
const unsigned long GAME_BAR_DELAY_MS = 300;   // in the game, quick flaps don't flash the bar
const unsigned long HOLD_LABEL_MS     = 300;   // after this, the footer says what a hold will do
const unsigned long IDLE_MS           = 30000;

const int MAX_TESTS = 12;
const int MAX_DAYS  = 90;

enum { APP_GLANCE, APP_PATTERN, APP_WEEK, APP_TRENDS, APP_EXERCISE,
       APP_MENU, APP_TEST_RUN, APP_CHECK, APP_BOOT, APP_SETCLOCK, APP_GAME, APP_COUNT };
const int CAROUSEL[] = {APP_GLANCE, APP_PATTERN, APP_WEEK, APP_TRENDS, APP_EXERCISE};
const int NUM_CAROUSEL = 5;

// ============================================================
// EEPROM LAYOUT (8 KB on the R4)
//   0    Settings (68 bytes)
//   128  Checkpoint of today (62 bytes)
//   256  90 days x 42 bytes, as a ring buffer  (ends at 4036)
// ============================================================
struct DayRecord {
  uint16_t seatedMin;
  uint16_t outMin;
  uint16_t longestMin;
  uint16_t reps;
  uint8_t  remAns;
  uint8_t  remTot;
  uint16_t bestTestDs;           // tenths of a second, 0 = none
  uint8_t  slots[SLOT_BYTES];    // 2 bits per 8-minute block
};

struct TestResult {
  uint16_t n;                    // test number
  uint16_t ds;                   // tenths of a second
};

struct Settings {
  uint32_t magic;
  uint8_t  goalIndex;
  uint8_t  remindIndex;
  uint8_t  soundOn;
  uint8_t  testCount;
  uint16_t testNumber;
  uint8_t  dayCount;             // days stored
  uint8_t  dayHead;              // ring position of the oldest day
  uint32_t buildId;              // which upload last set the clock
  uint16_t gameBest;             // hidden game's best score
  uint16_t reserved;             // spare (these 4 bytes keep the old layout)
  TestResult tests[MAX_TESTS];   // oldest first
};

struct Checkpoint {
  uint32_t magic;                // CP_MAGIC when it holds an unfinished day
  uint32_t date;                 // yyyymmdd from the clock, 0 = clock wasn't set
  uint32_t elapsedMs;
  uint32_t seatedMs;
  uint32_t outMs;
  uint32_t longestMs;
  uint32_t bestTestMs;
  uint16_t reps;
  uint8_t  remAns;
  uint8_t  remTot;
  uint8_t  slots[SLOT_BYTES];
};

const uint32_t SETTINGS_MAGIC = 0x5EA70201;
const uint32_t CP_MAGIC       = 0xC0FFEE03;
const int SETTINGS_ADDR = 0;
const int CP_ADDR       = 128;
const int DAYS_ADDR     = 256;

Settings settings;

// ============================================================
// RUNTIME STATE
// ============================================================
bool seated = false;             // confirmed state: what gets counted
bool seatRaw = false;            // the mat right now
unsigned long rawSinceReal = 0, rawSinceData = 0;   // when the mat last changed
bool satToday = false;           // out-of-seat time only counts after the first sit
unsigned long lastRead = 0;
unsigned long stateSince = 0;
unsigned long seatedTotalMs = 0, outTotalMs = 0, longestSitMs = 0;
int reps = 0;
bool goalReached = false;
bool reminderDue = false;
unsigned long reminderAt = 0, lastRemindBeep = 0;
int remindsThisSit = 0;          // how many intervals of this sit have already beeped
int remindersTotal = 0, remindersAnswered = 0;
unsigned long todayBestTestMs = 0;
unsigned long lastCheckpoint = 0;
int currentApp = APP_GLANCE;
int menuReturnApp = APP_GLANCE;
bool locked = true;
bool touchDown = false, touchCancelled = false;
unsigned long touchStart = 0, lastInput = 0, hintUntil = 0;

// Fast mode: a second clock for the data only. At 60x, one real minute
// counts as one hour of sitting/standing data, so a 16 h day takes 16 min.
// At 20x, one real minute is 20 minutes of data.
// Touch, beeps, tests, the seat filter and EEPROM saving stay on real time.
const unsigned long FAST_SPEEDS[] = {1, 20, 60};   // each hold on Fast mode moves to the next
const int NUM_FAST = 3;
int fastIndex = 0;
unsigned long timeScale = 1;          // 1 = normal
unsigned long dataBase = 0;           // data-clock reading when the scale last changed
unsigned long realAnchor = 0;         // millis() when the scale last changed

uint8_t todaySlots[SLOTS];       // unpacked: 0, 1 or 2
int slotIdx = 0;
unsigned int slotSamples = 0, slotSeatedSamples = 0;
unsigned long dayStart = 0, todayOffsetMs = 0;

// Clock
bool rtcFound = false, clockOk = false;
DateTime clockNow;               // refreshed once a second while the clock is set
uint32_t todayDate = 0;          // yyyymmdd of the day being tracked
unsigned long lastClockRead = 0;

// "Usual" = 25th / 50th / 75th percentile of minutes out of seat by each block
uint16_t baseP25[SLOTS], baseP50[SLOTS], baseP75[SLOTS];
bool haveBaseline = false;

// Activity levels
enum { LV_LEARNING, LV_EARLY, LV_MORE, LV_USUAL, LV_LESS, LV_MUCHLESS };
struct Level {
  const char* label;
  const char* lcdText;
  uint16_t color;
  uint8_t r, g, b;
};
const Level LEVELS[] = {
  {"Learning usual",    "LEARNING USUAL",   COL_SUB,   120, 170, 255},
  {"Too early to tell", "EARLY IN THE DAY", COL_SUB,   120, 170, 255},
  {"More active",       "MORE ACTIVE",      COL_MINT,  130, 230, 170},
  {"About usual",       "ABOUT USUAL",      COL_SKY,   120, 170, 255},
  {"Less active",       "LESS ACTIVE",      COL_PEACH, 255, 185, 120},
  {"Much less active",  "MUCH LESS ACTIVE", COL_CORAL, 255, 125, 115},
};
int statusDiffMin = 0;           // filled in by activityLevel()

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void openApp(int id);
void testOnStand(unsigned long now);
bool testActive();
bool gameActive();
void closeDay();
void drawStatusBar();
void drawDots();
void glanceCaption(int lv);

// ============================================================
// SMALL HELPERS
// ============================================================
// The data clock: like millis(), but runs timeScale times faster
unsigned long dataMillis() { return dataBase + (millis() - realAnchor) * timeScale; }
void setTimeScale(unsigned long scale) {
  dataBase = dataMillis();            // carry on from the current data time, no jump
  realAnchor = millis();
  timeScale = scale;
}

int currentGoal() { return GOAL_OPTIONS[settings.goalIndex]; }
unsigned long remindIntervalMs() { return (unsigned long)REMIND_OPTIONS_S[settings.remindIndex] * 1000UL; }
unsigned long totalSeatedMs() { return seatedTotalMs + (seated ? dataMillis() - stateSince : 0); }
unsigned long totalOutMs() { return outTotalMs + (seated || !satToday ? 0 : dataMillis() - stateSince); }
unsigned long longestSitNowMs() {
  unsigned long cur = seated ? dataMillis() - stateSince : 0;
  return cur > longestSitMs ? cur : longestSitMs;
}
bool todayHasData() { return reps > 0 || totalSeatedMs() > 60000UL; }
unsigned long dayElapsedMs() { return dataMillis() - dayStart + todayOffsetMs; }

// "12:34" under an hour, "2h05m" after
void fmtDur(unsigned long ms, char* out, int len) {
  unsigned long s = ms / 1000;
  if (s < 3600) snprintf(out, len, "%lu:%02lu", s / 60, s % 60);
  else snprintf(out, len, "%luh%02lum", s / 3600, (s / 60) % 60);
}
// 14 -> "2pm"
void fmtHour(int h, char* out) {
  h %= 24;
  snprintf(out, 6, "%d%s", h % 12 == 0 ? 12 : h % 12, h < 12 ? "am" : "pm");
}
// "12.3 s" (no float printf on the R4)
void fmtTenths(unsigned long ms, char* out, int len) {
  snprintf(out, len, "%lu.%lu s", ms / 1000, (ms / 100) % 10);
}

uint8_t getSlot(const uint8_t* packed, int k) { return (packed[k >> 2] >> ((k & 3) * 2)) & 3; }
void packSlots(const uint8_t* unpacked, uint8_t* packed) {
  for (int i = 0; i < SLOT_BYTES; i++) packed[i] = 0;
  for (int k = 0; k < SLOTS; k++) packed[k >> 2] |= (unpacked[k] & 3) << ((k & 3) * 2);
}
void unpackSlots(const uint8_t* packed, uint8_t* unpacked) {
  for (int k = 0; k < SLOTS; k++) unpacked[k] = getSlot(packed, k);
}

// Small insertion sort, used for medians and percentiles (n <= 30)
void sortU16(uint16_t* v, int n) {
  for (int i = 1; i < n; i++) {
    uint16_t x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
}

// ============================================================
// EEPROM
// ============================================================
void saveSettings() { EEPROM.put(SETTINGS_ADDR, settings); }

void setDefaults() {
  settings.magic = SETTINGS_MAGIC;
  settings.goalIndex = 3;
  settings.remindIndex = 2;
  settings.soundOn = 1;
  settings.testCount = 0;
  settings.testNumber = 0;
  settings.dayCount = 0;
  settings.dayHead = 0;
  settings.buildId = 0;
  settings.gameBest = 0;
  settings.reserved = 0;
  for (int i = 0; i < MAX_TESTS; i++) { settings.tests[i].n = 0; settings.tests[i].ds = 0; }
}

void loadSettings() {
  EEPROM.get(SETTINGS_ADDR, settings);
  bool bad = settings.magic != SETTINGS_MAGIC ||
             settings.goalIndex >= NUM_GOALS ||
             settings.remindIndex >= NUM_REMINDS ||
             settings.testCount > MAX_TESTS ||
             settings.dayCount > MAX_DAYS || settings.dayHead >= MAX_DAYS;
  if (!bad && settings.gameBest > 9999) settings.gameBest = 0;   // old firmware's bytes
  if (bad) {
    setDefaults();
    saveSettings();
    uint32_t zero = 0;
    EEPROM.put(CP_ADDR, zero);
  }
}

// Day i: 0 = oldest stored day, dayCount - 1 = newest
int dayAddr(int i) { return DAYS_ADDR + ((settings.dayHead + i) % MAX_DAYS) * (int)sizeof(DayRecord); }
void readDay(int i, DayRecord& rec) { EEPROM.get(dayAddr(i), rec); }

void pushDay(const DayRecord& rec) {
  if (settings.dayCount < MAX_DAYS) {
    EEPROM.put(dayAddr(settings.dayCount), rec);
    settings.dayCount++;
  } else {
    EEPROM.put(dayAddr(0), rec);                           // overwrite the oldest
    settings.dayHead = (settings.dayHead + 1) % MAX_DAYS;  // it becomes the newest
  }
  saveSettings();
}

void clearCheckpoint() { uint32_t zero = 0; EEPROM.put(CP_ADDR, zero); }

// ============================================================
// BUZZER (non-blocking)
// ============================================================
int beepsLeft = 0;
bool buzzerOn = false;
unsigned long beepOnMs = 80, beepOffMs = 120, nextBeepChange = 0;

void beepPattern(int count, unsigned long onMs, unsigned long offMs) {
  if (!settings.soundOn) return;
  beepsLeft = count; beepOnMs = onMs; beepOffMs = offMs;
  buzzerOn = false;
  digitalWrite(BUZZER_PIN, LOW);
  nextBeepChange = millis();
}

void serviceBuzzer() {
  if (beepsLeft == 0 && !buzzerOn) return;
  unsigned long now = millis();
  if ((long)(now - nextBeepChange) < 0) return;
  if (buzzerOn) {
    digitalWrite(BUZZER_PIN, LOW); buzzerOn = false; beepsLeft--;
    nextBeepChange = now + beepOffMs;
  } else {
    digitalWrite(BUZZER_PIN, HIGH); buzzerOn = true;
    nextBeepChange = now + beepOnMs;
  }
}

// ============================================================
// ACTIVITY VS USUAL
// ============================================================
void computeBaseline() {
  int n = settings.dayCount < BASELINE_DAYS ? settings.dayCount : BASELINE_DAYS;
  if (n < 3) { haveBaseline = false; return; }

  static uint16_t cum[BASELINE_DAYS][SLOTS];   // 3.4 KB, static so it isn't on the stack
  DayRecord rec;
  for (int d = 0; d < n; d++) {
    readDay(settings.dayCount - n + d, rec);
    uint16_t sum = 0;
    for (int k = 0; k < SLOTS; k++) {
      if (getSlot(rec.slots, k) == 2) sum += SLOT_MIN;
      cum[d][k] = sum;
    }
  }
  uint16_t v[BASELINE_DAYS];
  int i25 = (int)((n - 1) * 0.25f + 0.5f);
  int i50 = (int)((n - 1) * 0.50f + 0.5f);
  int i75 = (int)((n - 1) * 0.75f + 0.5f);
  for (int k = 0; k < SLOTS; k++) {
    for (int d = 0; d < n; d++) v[d] = cum[d][k];
    sortU16(v, n);
    baseP25[k] = v[i25]; baseP50[k] = v[i50]; baseP75[k] = v[i75];
  }
  haveBaseline = true;
}

int activityLevel() {
  statusDiffMin = 0;
  if (!haveBaseline) return LV_LEARNING;
  if (dayElapsedMs() < 3600000UL) return LV_EARLY;
  int k = slotIdx < SLOTS ? slotIdx : SLOTS - 1;
  float usual = baseP50[k];
  float today = totalOutMs() / 60000.0f;
  float ratio = (today + 5) / (usual + 5);     // +5 min keeps early-day ratios calm
  statusDiffMin = (int)(today - usual + (today >= usual ? 0.5f : -0.5f));
  if (ratio >= 1.15f) return LV_MORE;
  if (ratio < 0.6f) return LV_MUCHLESS;
  if (ratio < 0.85f) return LV_LESS;
  return LV_USUAL;
}

// ============================================================
// TFT DRAWING HELPERS (128 x 160)
//   y 0-13 status · y 15-145 content · y 147-153 dots/footer · y 156-159 hold bar
// ============================================================
void drawTextBg(int x, int y, const char* s, uint16_t color, uint8_t size, uint16_t bg) {
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setCursor(x, y);
  tft.print(s);
}
void drawText(int x, int y, const char* s, uint16_t color, uint8_t size) { drawTextBg(x, y, s, color, size, COL_BG); }
void drawCentered(int y, const char* s, uint16_t color, uint8_t size) {
  drawText((128 - (int)strlen(s) * 6 * size) / 2, y, s, color, size);
}
void drawRight(int xEnd, int y, const char* s, uint16_t color, uint8_t size) {
  drawText(xEnd - (int)strlen(s) * 6 * size, y, s, color, size);
}

uint16_t viewAccent(int app) {
  switch (app) {
    case APP_GLANCE:   return COL_SKY;
    case APP_PATTERN:  return COL_PEACH;
    case APP_WEEK:     return COL_MINT;
    case APP_TRENDS:   return COL_LAVENDER;
    default:           return COL_BUTTER;
  }
}

void drawTitle(const char* s, uint16_t accent) {
  tft.fillRoundRect(6, 20, 4, 6, 2, accent);
  drawText(14, 19, s, COL_SUB, 1);
}
void clearContent() { tft.fillRect(0, 15, 128, 131, COL_BG); }
// The footer remembers what it shows (dots or text) so a hold label can be put back
char footerSaved[24] = "";
bool footerDots = false;
void drawFooterText(const char* s, uint16_t color) { tft.fillRect(0, 145, 128, 10, COL_BG); drawCentered(147, s, color, 1); }
void drawFooter(const char* s) {
  drawFooterText(s, COL_FAINT);
  snprintf(footerSaved, sizeof(footerSaved), "%s", s);
  footerDots = false;
}

void restoreFooter() {
  if (footerDots) drawDots();
  else if (footerSaved[0]) drawFooterText(footerSaved, COL_FAINT);
  else tft.fillRect(0, 145, 128, 10, COL_BG);
}

int carouselIndex(int app) {
  for (int i = 0; i < NUM_CAROUSEL; i++) if (CAROUSEL[i] == app) return i;
  return -1;
}

void drawDots() {
  footerDots = true;
  tft.fillRect(0, 145, 128, 10, COL_BG);
  int idx = carouselIndex(currentApp);
  const int gap = 10, x0 = 64 - 2 * gap;
  for (int i = 0; i < NUM_CAROUSEL; i++) {
    if (i == idx) tft.fillRoundRect(x0 + i * gap - 4, 148, 9, 4, 2, viewAccent(currentApp));
    else tft.fillCircle(x0 + i * gap, 150, 1, COL_FAINT);
  }
}

void drawRow(int y, const char* label, bool selected) {
  uint16_t bg = selected ? COL_CARD : COL_BG;
  tft.fillRoundRect(4, y, 120, 14, 4, bg);
  drawTextBg(12, y + 3, label, selected ? COL_FG : COL_SUB, 1, bg);
  if (selected) tft.fillRoundRect(6, y + 4, 3, 6, 1, COL_SKY);
}

// Label left, value right; only the gap between them is cleared
void drawStat(int y, const char* label, const char* value, uint16_t valueColor) {
  drawText(6, y, label, COL_SUB, 1);
  int labelEnd = 6 + (int)strlen(label) * 6;
  tft.fillRect(labelEnd, y, 122 - labelEnd, 8, COL_BG);
  drawRight(122, y, value, valueColor, 1);
}

// Text is drawn with its background and padded to a fixed width,
// so the old text is overwritten in one pass without a flicker
// Thin line under the status bar; butter-coloured while fast mode is on
void drawDivider() { tft.drawFastHLine(0, 13, 128, timeScale > 1 ? COL_BUTTER : COL_CARD); }

// Left: seat state and how long. Right: the time, or "--:--" in peach if the
// clock isn't set. 14 characters on the left leaves room for "12:45".
void drawStatusBar() {
  unsigned long period = dataMillis() - stateSince;
  char t[10], s[24], padded[24];
  fmtDur(period, t, sizeof(t));
  uint16_t c;
  if (reminderDue) { snprintf(s, sizeof(s), "Stand up %s", t); c = COL_CORAL; }
  else if (seated) {
    snprintf(s, sizeof(s), "Seated %s", t);
    c = period >= remindIntervalMs() * 8 / 10 ? COL_PEACH : COL_SKY;
  } else { snprintf(s, sizeof(s), "Out %s", t); c = COL_MINT; }
  snprintf(padded, sizeof(padded), "%-14.14s", s);
  drawText(6, 3, padded, c, 1);

  char clk[8];
  if (clockOk) {
    int h = clockNow.hour() % 12;
    snprintf(clk, sizeof(clk), "%2d:%02d", h == 0 ? 12 : h, clockNow.minute());   // " 2:15", 12-hour like a phone
    drawRight(124, 3, clk, COL_SUB, 1);
  } else {
    drawRight(124, 3, "--:--", COL_PEACH, 1);
  }
}

int holdBarPx = 0;
void drawHoldBar(unsigned long held, uint16_t color) {
  long px = (long)(held - HOLD_BAR_DELAY_MS) * 120 / (long)(HOLD_MS - HOLD_BAR_DELAY_MS);
  if (px > 120) px = 120;
  if (px > holdBarPx) { tft.fillRect(4 + holdBarPx, 157, px - holdBarPx, 2, color); holdBarPx = px; }
}
void clearHoldBar() { if (holdBarPx > 0) { tft.fillRect(0, 156, 128, 4, COL_BG); holdBarPx = 0; } }

// Line chart: faint gridlines, area fill, emphasised endpoint
void drawLineChart(int x, int y, int w, int h, const float* values, int n, uint16_t color, uint16_t areaColor) {
  tft.fillRect(x, y, w, h, COL_BG);
  tft.drawFastHLine(x, y, w, COL_CARD);
  tft.drawFastHLine(x, y + h / 2, w, COL_CARD);
  tft.drawFastHLine(x, y + h - 1, w, COL_FAINT);
  if (n < 2) return;
  float lo = values[0], hi = values[0];
  for (int i = 1; i < n; i++) { if (values[i] < lo) lo = values[i]; if (values[i] > hi) hi = values[i]; }
  if (hi - lo < 0.001f) { hi += 1; lo -= 1; }
  float pad = (hi - lo) * 0.12f; lo -= pad; hi += pad;

  int prevX = 0, prevY = 0;
  for (int i = 0; i < n; i++) {
    int px = x + (int)((long)i * (w - 1) / (n - 1));
    int py = y + (int)((hi - values[i]) * (h - 1) / (hi - lo) + 0.5f);
    if (i > 0) {
      for (int xx = prevX; xx <= px; xx++) {           // area under the segment
        int yy = prevY + (px == prevX ? 0 : (int)((long)(py - prevY) * (xx - prevX) / (px - prevX)));
        tft.drawFastVLine(xx, yy + 1, y + h - 2 - yy, areaColor);
      }
    }
    prevX = px; prevY = py;
  }
  for (int i = 0; i < n - 1; i++) {
    int xa = x + (int)((long)i * (w - 1) / (n - 1));
    int xb = x + (int)((long)(i + 1) * (w - 1) / (n - 1));
    int ya = y + (int)((hi - values[i]) * (h - 1) / (hi - lo) + 0.5f);
    int yb = y + (int)((hi - values[i + 1]) * (h - 1) / (hi - lo) + 0.5f);
    tft.drawLine(xa, ya, xb, yb, color);
  }
  tft.fillCircle(prevX, prevY, 2, color);
}

// ============================================================
// SENSING
// ============================================================
bool readSeatPressed() { return digitalRead(SEAT_PIN) == LOW; }

// ---------- Clock ----------
uint32_t dateKey(const DateTime& t) { return t.year() * 10000UL + t.month() * 100UL + t.day(); }
// Changes with every compile, so a fresh upload can be told apart from a power cut
uint32_t buildId() {
  const char* parts[2] = {__DATE__, __TIME__};
  uint32_t h = 2166136261UL;                       // FNV-1a hash
  for (int i = 0; i < 2; i++)
    for (const char* c = parts[i]; *c; c++) { h ^= (uint8_t)*c; h *= 16777619UL; }
  return h;
}
// Reads the clock; a garbage reading (clock unplugged, or reset to 2000) marks it not set
bool readClock() {
  DateTime t = rtc.now();
  lastClockRead = millis();
  if (t.year() < 2024 || t.year() > 2099 || t.month() < 1 || t.month() > 12 ||
      t.day() < 1 || t.day() > 31 || t.hour() > 23 || t.minute() > 59) {
    clockOk = false;
    return false;
  }
  clockNow = t;
  return true;
}
bool clockMode() { return clockOk && timeScale == 1; }   // fast mode runs a pretend day
bool beforeWindow() { return clockMode() && clockNow.hour() < DAY_START_HOUR; }
// Line the day up with the clock: block 0 = DAY_START_HOUR
void anchorToClock() {
  long ms = ((long)(clockNow.hour() - DAY_START_HOUR) * 3600L + clockNow.minute() * 60L + clockNow.second()) * 1000L;
  todayOffsetMs = ms > 0 ? ms : 0;
  dayStart = dataMillis();
}
int clockSlot() {
  int k = (int)(todayOffsetMs / SLOT_MS);
  return k < SLOTS ? k : SLOTS - 1;
}
// Jump today's position back to the real time (after fast mode, or a clock change)
void snapToClock() {
  readClock();
  if (!clockOk) return;
  if (dateKey(clockNow) != todayDate) { closeDay(); return; }
  anchorToClock();
  int k = clockSlot();
  for (int j = k + 1; j < SLOTS; j++) todaySlots[j] = 0;   // drop blocks from the pretend future
  slotIdx = k; slotSamples = 0; slotSeatedSamples = 0;
}

// Once a second: midnight rollover, and keep millis() drift from adding up
void serviceClock() {
  if (!clockOk || millis() - lastClockRead < 1000) return;
  if (!readClock()) return;
  if (timeScale != 1) return;
  if (dateKey(clockNow) != todayDate) { closeDay(); return; }
  anchorToClock();
}

// Sets the clock and lines today up with it (used by Set clock and the Serial Monitor)
bool applyClock(const DateTime& t) {
  rtc.adjust(t);
  clockOk = true;
  if (!readClock()) return false;
  todayDate = dateKey(clockNow);     // today carries on under the new date
  if (timeScale == 1) snapToClock();
  drawStatusBar();
  return true;
}

// Serial Monitor: "2026-11-08 13:45:00" + Enter sets the clock
char serialBuf[24];
int serialLen = 0;
void serviceSerialClock() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (serialLen < (int)sizeof(serialBuf) - 1) serialBuf[serialLen++] = c; continue; }
    serialBuf[serialLen] = 0;
    serialLen = 0;
    int y, mo, d, h, mi, sec;
    if (sscanf(serialBuf, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) {
      Serial.println("To set the clock, type: 2026-11-08 13:45:00");
      continue;
    }
    if (!rtcFound) { Serial.println("No DS3231 found - check SDA/SCL/VCC/GND"); continue; }
    if (!applyClock(DateTime(y, mo, d, h, mi, sec))) { Serial.println("That time didn't work - try again"); continue; }
    Serial.println("Clock set");
  }
}

void closeCurrentPeriod(unsigned long now) {
  if ((long)(now - stateSince) < 0) now = stateSince;    // change began before a day reset
  unsigned long dur = now - stateSince;
  if (seated) { seatedTotalMs += dur; if (dur > longestSitMs) longestSitMs = dur; }
  else outTotalMs += dur;
  stateSince = now;
}

// Passive: stand-ups are counted quietly. realAt = when the seat emptied (millis)
void onRep(unsigned long realAt) {
  reps++;
  if (reminderDue) {
    if (millis() - reminderAt <= ANSWER_WINDOW_MS) remindersAnswered++;
    reminderDue = false;
  }
  remindsThisSit = 0;
  if (!goalReached && reps >= currentGoal()) goalReached = true;
  testOnStand(realAt);            // the test timer always runs on real time
}

unsigned long confirmMs(bool toSeated) {
  if (testActive()) return TEST_CONFIRM_MS;
  return toSeated ? SIT_CONFIRM_MS : STAND_CONFIRM_MS;
}

// A change only counts once it has lasted its confirm time. A bounce flips
// back before then and is ignored. Once confirmed, it's dated from when the
// mat actually changed, so no time is lost to the wait.
void runSeatStateMachine(unsigned long realNow, unsigned long dataNow) {
  bool raw = readSeatPressed();
  if (raw != seatRaw) { seatRaw = raw; rawSinceReal = realNow; rawSinceData = dataNow; }
  if (seatRaw == seated) return;
  if (realNow - rawSinceReal < confirmMs(seatRaw)) return;
  if (seatRaw && !satToday) {               // first sit: time before it isn't "out of seat"
    satToday = true;
    stateSince = (long)(rawSinceData - stateSince) > 0 ? rawSinceData : stateSince;
  } else {
    closeCurrentPeriod(rawSinceData);
  }
  seated = seatRaw;
  if (!seated) onRep(rawSinceReal);
}

// Beeps at every full interval you keep sitting (30, 60, 90 min ...), counted on
// the data clock so every speed behaves the same. Standing up starts over.
// The "answered within 5 min" window and the minimum gap use real time, so
// 60x fast mode with the 30 s demo setting beeps every 10 s, not nonstop.
void checkReminder(unsigned long dataNow) {
  if (!seated || testActive() || gameActive()) return;
  int due = (int)((dataNow - stateSince) / remindIntervalMs());
  if (due <= remindsThisSit) return;
  unsigned long realNow = millis();
  if (lastRemindBeep != 0 && realNow - lastRemindBeep < MIN_BEEP_GAP_MS) return;
  remindsThisSit = due;
  lastRemindBeep = realNow;
  if (!reminderDue) {
    reminderDue = true; reminderAt = realNow; remindersTotal++;
    beepPattern(3, 150, 150);
  } else {
    beepPattern(2, 150, 150);
  }
}

// Each block records what it mostly was. Blocks before the first sit of the
// day, while switched off, or outside 6am-10pm stay 0 = no data.
void storeCurrentSlot() {
  if (slotSamples > 0 && slotIdx < SLOTS) todaySlots[slotIdx] = slotSeatedSamples * 2 >= slotSamples ? 1 : 2;
}
void serviceSlots() {
  int idx = (int)(dayElapsedMs() / SLOT_MS);
  if (idx > SLOTS - 1) idx = SLOTS - 1;
  if (idx != slotIdx) {
    storeCurrentSlot();
    slotSamples = 0; slotSeatedSamples = 0;
    slotIdx = idx;
  }
  bool inWindow = dayElapsedMs() < (unsigned long)SLOTS * SLOT_MS && !beforeWindow();
  if (inWindow && satToday) {
    slotSamples++;
    if (seated) slotSeatedSamples++;
  }
}

void fillDayRecord(DayRecord& rec, bool excludeCurrentOut) {
  storeCurrentSlot();
  rec.seatedMin = totalSeatedMs() / 60000UL;
  rec.outMin = (excludeCurrentOut && !seated ? outTotalMs : totalOutMs()) / 60000UL;
  rec.longestMin = longestSitNowMs() / 60000UL;
  rec.reps = reps;
  rec.remAns = remindersAnswered;
  rec.remTot = remindersTotal;
  rec.bestTestDs = todayBestTestMs / 100;
  packSlots(todaySlots, rec.slots);
}

void resetToday() {
  unsigned long now = dataMillis();
  reps = 0; goalReached = false;
  seatedTotalMs = 0; outTotalMs = 0; longestSitMs = 0;
  stateSince = now;
  reminderDue = false; remindersTotal = 0; remindersAnswered = 0; remindsThisSit = 0;
  todayBestTestMs = 0;
  for (int k = 0; k < SLOTS; k++) todaySlots[k] = 0;
  slotIdx = 0; slotSamples = 0; slotSeatedSamples = 0;
  dayStart = now; todayOffsetMs = 0;
  satToday = seated;               // already sitting at midnight counts as a sit
  if (clockOk && readClock()) {
    todayDate = dateKey(clockNow);
    anchorToClock();
    slotIdx = clockSlot();
  }
}

// End of day. The long empty gap itself is not counted as out-of-seat time.
void closeDay() {
  if (todayHasData()) {
    DayRecord rec;
    fillDayRecord(rec, true);
    pushDay(rec);
  }
  clearCheckpoint();
  resetToday();
  computeBaseline();
}

void checkDayRollover(unsigned long now) {
  if (!seated && now - stateSince >= NEW_DAY_GAP_MS) {
    if (todayHasData()) closeDay();
    else stateSince = now;
  }
}

void saveCheckpoint() {
  Checkpoint cp;
  storeCurrentSlot();
  cp.magic = CP_MAGIC;
  cp.date = clockOk ? todayDate : 0;
  cp.elapsedMs = dayElapsedMs();
  cp.seatedMs = totalSeatedMs();
  cp.outMs = totalOutMs();
  cp.longestMs = longestSitNowMs();
  cp.bestTestMs = todayBestTestMs;
  cp.reps = reps;
  cp.remAns = remindersAnswered;
  cp.remTot = remindersTotal;
  packSlots(todaySlots, cp.slots);
  EEPROM.put(CP_ADDR, cp);
}

void checkCheckpoint(unsigned long now) {
  if (now - lastCheckpoint < CHECKPOINT_MS) return;
  lastCheckpoint = now;
  if (todayHasData()) saveCheckpoint();
}

void serviceSensor() {
  unsigned long now = millis();
  if (now - lastRead < READ_INTERVAL_MS) return;
  lastRead = now;
  unsigned long dataNow = dataMillis();
  runSeatStateMachine(now, dataNow);
  serviceSlots();
  checkReminder(dataNow);
  if (!clockMode()) checkDayRollover(dataNow);   // with the clock, days end at midnight
  checkCheckpoint(now);        // real time, so fast mode doesn't wear out the EEPROM
}

// ============================================================
// APPS
// ============================================================
void nextCarousel() {
  int i = carouselIndex(currentApp);
  openApp(CAROUSEL[(i + 1) % NUM_CAROUSEL]);
}
void openMenuFromHere() { menuReturnApp = currentApp; openApp(APP_MENU); }

// ---------- Glance: today vs usual ----------
const int GX = 6, GY = 38, GW = 116, GH = 76;
int glanceLevel = -1, glanceSlot = -1;
unsigned long glanceLastDraw = 0;

int gYv(float m, float ymax) {
  if (m > ymax) m = ymax;
  return GY + GH - 1 - (int)(m * (GH - 1) / ymax + 0.5f);
}
int gXk(float k) {
  if (k > SLOTS) k = SLOTS;
  return GX + (int)(k * (GW - 1) / SLOTS + 0.5f);
}

void glanceDraw() {
  int lv = activityLevel();
  const Level& L = LEVELS[lv];
  glanceLevel = lv; glanceSlot = slotIdx; glanceLastDraw = millis();

  // Status chip
  tft.fillRoundRect(4, 17, 120, 16, 8, COL_CARD);
  tft.fillCircle(14, 25, 3, L.color);
  drawTextBg(22, 21, L.label, L.color == COL_SUB ? COL_FG : L.color, 1, COL_CARD);

  // Chart
  tft.fillRect(GX, GY, GW, GH, COL_BG);
  float todayNow = totalOutMs() / 60000.0f;
  float ymax = 60;
  if (todayNow > ymax) ymax = todayNow;
  if (haveBaseline && baseP75[SLOTS - 1] > ymax) ymax = baseP75[SLOTS - 1];
  ymax *= 1.1f;
  tft.drawFastHLine(GX, GY + GH / 2, GW, COL_CARD);
  tft.drawFastHLine(GX, GY + GH - 1, GW, COL_FAINT);

  if (haveBaseline) {
    for (int col = 0; col < GW; col++) {
      int k = col * SLOTS / GW;
      int yt = gYv(baseP75[k], ymax), yb = gYv(baseP25[k], ymax);
      tft.drawFastVLine(GX + col, yt, (yb - yt + 1) > 1 ? yb - yt + 1 : 1, COL_BAND);
      if (col % 3 == 0) tft.drawPixel(GX + col, gYv(baseP50[k], ymax), COL_SUB);
    }
  }

  // Today so far: thick line ending in a dot at "now"
  int px = gXk(0), py = gYv(0, ymax);
  int cum = 0;
  for (int k = 0; k <= slotIdx; k++) {
    int nx, ny;
    if (k < slotIdx) {
      if (todaySlots[k] == 2) cum += SLOT_MIN;
      nx = gXk(k + 1); ny = gYv(cum, ymax);
    } else {
      nx = gXk(dayElapsedMs() / (float)SLOT_MS); ny = gYv(todayNow, ymax);
    }
    tft.drawLine(px, py, nx, ny, L.color);
    tft.drawLine(px, py - 1, nx, ny - 1, L.color);
    px = nx; py = ny;
  }
  tft.fillCircle(px, py, 3, L.color);

  tft.fillRect(0, GY + GH + 2, 128, 8, COL_BG);
  char h0[6], h8[6], h16[6];
  if (clockMode()) { fmtHour(DAY_START_HOUR, h0); fmtHour(DAY_START_HOUR + 8, h8); fmtHour(DAY_START_HOUR + 16, h16); }
  else { strcpy(h0, "0h"); strcpy(h8, "8h"); strcpy(h16, "16h"); }
  drawText(GX, GY + GH + 3, h0, COL_FAINT, 1);
  drawCentered(GY + GH + 3, h8, COL_FAINT, 1);
  drawRight(GX + GW, GY + GH + 3, h16, COL_FAINT, 1);

  glanceCaption(lv);
}
// Caption is padded to a fixed width so it redraws in place
void glanceCaption(int lv) {
  char s[24], padded[24];
  uint16_t c = COL_FAINT;
  if (lv == LV_LEARNING) snprintf(s, sizeof(s), "Needs 3 days of data");
  else if (lv == LV_EARLY) snprintf(s, sizeof(s), "Compares after 1 h");
  else {
    snprintf(s, sizeof(s), "%s%d min out vs usual", statusDiffMin >= 0 ? "+" : "", statusDiffMin);
    c = LEVELS[lv].color;
  }
  int len = strlen(s), padL = (21 - len) / 2;
  snprintf(padded, sizeof(padded), "%*s%s%*s", padL, "", s, 21 - len - padL, "");
  drawText(1, 130, padded, c, 1);
  glanceLastDraw = millis();
}
void glanceEnter() { glanceDraw(); drawDots(); }
void glanceUpdate() {
  int lv = activityLevel();
  if (lv != glanceLevel || slotIdx != glanceSlot) glanceDraw();
  else if (millis() - glanceLastDraw >= 10000UL) glanceCaption(lv);
}

// ---------- Pattern: 7 days of sitting, block by block ----------
const int PAT_X = 4, PAT_Y = 30, PAT_ROW = 14, PAT_H = 10;
int patternSlot = -1;

uint16_t slotColor(uint8_t s, int runLen) {
  if (s == 2) return COL_MINT;
  if (s == 1) return runLen >= LONG_SIT_SLOTS ? COL_PEACH : COL_SKY_DIM;
  return COL_CARD;
}
void drawPatternRow(int y, const uint8_t* slots, int upto) {
  tft.fillRect(PAT_X, y, SLOTS, PAT_H, COL_CARD);
  int k = 0;
  while (k < upto) {
    int j = k;
    while (j < upto && slots[j] == slots[k]) j++;
    tft.fillRect(PAT_X + k, y, j - k, PAT_H, slotColor(slots[k], j - k));
    k = j;
  }
}
void patternRows() {
  uint8_t un[SLOTS];
  DayRecord rec;
  int have = settings.dayCount < 6 ? settings.dayCount : 6;
  for (int i = 0; i < 6; i++) {
    int y = PAT_Y + i * PAT_ROW;
    int dayIndex = settings.dayCount - 6 + i;
    if (i >= 6 - have && dayIndex >= 0) {
      readDay(dayIndex, rec);
      unpackSlots(rec.slots, un);
      drawPatternRow(y, un, SLOTS);
    } else {
      tft.fillRect(PAT_X, y, SLOTS, PAT_H, COL_CARD);
    }
  }
  for (int k = 0; k < SLOTS; k++) un[k] = todaySlots[k];
  if (slotIdx < SLOTS && satToday) un[slotIdx] = seated ? 1 : 2;
  int upto = slotIdx + 1 < SLOTS ? slotIdx + 1 : SLOTS;
  drawPatternRow(PAT_Y + 6 * PAT_ROW, un, upto);
  tft.fillRect(0, PAT_Y + 6 * PAT_ROW, 2, PAT_H, COL_FG);
  patternSlot = slotIdx;
}
void patternEnter() {
  drawTitle("Sitting pattern", COL_PEACH);
  patternRows();
  const int ly = 128;
  tft.fillRect(4, ly + 1, 6, 6, COL_MINT);     drawText(12, ly, "out", COL_SUB, 1);
  tft.fillRect(36, ly + 1, 6, 6, COL_SKY_DIM); drawText(44, ly, "sit", COL_SUB, 1);
  tft.fillRect(68, ly + 1, 6, 6, COL_PEACH);   drawText(76, ly, "long sit", COL_SUB, 1);
  drawDots();
}
void patternUpdate() { if (slotIdx != patternSlot) patternRows(); }

// ---------- Week: share of time out of seat ----------
int weekShownSlot = -1;

float shareOf(const DayRecord& d) {
  unsigned int tot = d.seatedMin + d.outMin;
  return tot ? (float)d.outMin / tot : 0;
}
// Median share of time out of seat over the last 14 days
float usualShare() {
  int n = settings.dayCount < BASELINE_DAYS ? settings.dayCount : BASELINE_DAYS;
  if (n == 0) return 0;
  uint16_t v[BASELINE_DAYS];
  DayRecord rec;
  for (int i = 0; i < n; i++) { readDay(settings.dayCount - n + i, rec); v[i] = (uint16_t)(shareOf(rec) * 1000); }
  sortU16(v, n);
  return v[n / 2] / 1000.0f;
}

void weekDraw() {
  tft.fillRect(0, 28, 128, 116, COL_BG);
  float v[7];
  bool has[7];
  DayRecord rec;
  for (int i = 0; i < 6; i++) {
    int dayIndex = settings.dayCount - 6 + i;
    has[i] = dayIndex >= 0;
    if (has[i]) { readDay(dayIndex, rec); v[i] = shareOf(rec); }
  }
  // Today's share is only fair once some of the day has passed
  unsigned long tot = totalSeatedMs() + totalOutMs();
  has[6] = tot > 0 && dayElapsedMs() >= 3600000UL;
  v[6] = has[6] ? (float)totalOutMs() / tot : 0;
  float usual = usualShare();

  const int top = 36, base = 118, h = base - top;
  float max = 0.5f;
  for (int i = 0; i < 7; i++) if (has[i] && v[i] > max) max = v[i];
  max *= 1.15f;
  int lv = activityLevel();
  for (int i = 0; i < 7; i++) {
    int x = 10 + i * 16;
    if (!has[i]) { tft.fillRect(x, base - 1, 12, 1, COL_FAINT); continue; }
    int bh = (int)(v[i] / max * h + 0.5f);
    if (bh < 2) bh = 2;
    uint16_t c = v[i] >= usual ? COL_MINT : COL_FAINT;
    if (i == 6) c = LEVELS[lv].color == COL_SUB ? COL_SKY : LEVELS[lv].color;
    tft.fillRoundRect(x, base - bh, 12, bh, bh >= 6 ? 3 : 1, c);
  }
  if (settings.dayCount > 0) {
    int gy = base - (int)(usual / max * h + 0.5f);
    for (int x = 8; x < 122; x += 4) tft.drawFastHLine(x, gy, 2, COL_SUB);
  }
  drawText(10 + 6 * 16 + 3, base + 3, "T", COL_SUB, 1);

  char line[24];
  int u = (int)(usual * 100 + 0.5f), t = (int)(v[6] * 100 + 0.5f);
  if (!has[6]) {
    if (settings.dayCount > 0) snprintf(line, sizeof(line), "Usual %d%% out of seat", u);
    else snprintf(line, sizeof(line), "Today shows after 1 h");
  } else if (settings.dayCount > 0) snprintf(line, sizeof(line), "Now %d%% - usual %d%%", t, u);
  else snprintf(line, sizeof(line), "Out of seat: %d%%", t);
  drawCentered(132, line, COL_SUB, 1);
  weekShownSlot = slotIdx;
}
void weekEnter() { drawTitle("Time out of seat", COL_MINT); weekDraw(); drawDots(); }
void weekUpdate() { if (slotIdx != weekShownSlot) weekDraw(); }

// ---------- Trends: last 30 days ----------
void trendsEnter() {
  drawTitle("Last 30 days", COL_LAVENDER);
  int n = settings.dayCount < 30 ? settings.dayCount : 30;
  if (n < 2) {
    drawCentered(66, "Not enough data yet", COL_SUB, 1);
    drawCentered(80, "Hold, then Data >", COL_FAINT, 1);
    drawCentered(92, "Load demo data", COL_FAINT, 1);
  } else {
    float share[30], longest[30], sumS = 0, sumL = 0;
    DayRecord rec;
    for (int i = 0; i < n; i++) {
      readDay(settings.dayCount - n + i, rec);
      share[i] = shareOf(rec) * 100; longest[i] = rec.longestMin;
      sumS += share[i]; sumL += longest[i];
    }
    char s[16];
    drawText(6, 32, "Out of seat", COL_SUB, 1);
    snprintf(s, sizeof(s), "%d%%", (int)(sumS / n + 0.5f));
    drawRight(122, 32, s, COL_MINT, 1);
    drawLineChart(6, 42, 116, 30, share, n, COL_MINT, COL_MINT_DIM);

    drawText(6, 80, "Longest sit", COL_SUB, 1);
    snprintf(s, sizeof(s), "%d min", (int)(sumL / n + 0.5f));
    drawRight(122, 80, s, COL_LAVENDER, 1);
    drawLineChart(6, 90, 116, 30, longest, n, COL_LAVENDER, COL_LAV_DIM);

    if (n >= 10) {
      float recent = 0, before = 0;
      for (int i = n - 7; i < n; i++) recent += share[i];
      for (int i = 0; i < n - 7; i++) before += share[i];
      int pts = (int)(recent / 7 - before / (n - 7) + (recent / 7 >= before / (n - 7) ? 0.5f : -0.5f));
      char line[24];
      snprintf(line, sizeof(line), "This week %s%d pts out", pts >= 0 ? "+" : "", pts);
      drawCentered(128, line, pts >= 0 ? COL_MINT : COL_PEACH, 1);
    } else {
      drawCentered(128, "Trend after 10 days", COL_FAINT, 1);
    }
  }
  drawDots();
}

// ---------- Exercise: stand-ups and sit-to-stand tests ----------
int exShownReps = -1;
const int EX_N = 24, EX_CX = 30, EX_CY = 56, EX_R = 20;

void exerciseRing() {
  int goal = currentGoal();
  int filled = (int)((reps < goal ? reps : goal) * (long)EX_N / goal);
  for (int i = 0; i < EX_N; i++) {
    float a = -PI / 2 + i * 2 * PI / EX_N;
    int x = EX_CX + (int)(EX_R * cos(a) + (cos(a) >= 0 ? 0.5f : -0.5f));
    int y = EX_CY + (int)(EX_R * sin(a) + (sin(a) >= 0 ? 0.5f : -0.5f));
    tft.fillCircle(x, y, 1, i < filled ? (goalReached ? COL_BUTTER : COL_MINT) : COL_FAINT);
  }
  tft.fillRect(EX_CX - 13, EX_CY - 8, 26, 16, COL_BG);
  char s[8];
  snprintf(s, sizeof(s), "%d", reps);
  drawText(EX_CX - (int)strlen(s) * 6, EX_CY - 7, s, goalReached ? COL_BUTTER : COL_FG, 2);
  drawText(60, 44, "stand-ups", COL_SUB, 1);
  char g[12];
  snprintf(g, sizeof(g), "goal %-3d", goal);
  drawText(60, 56, g, goalReached ? COL_BUTTER : COL_FAINT, 1);
  exShownReps = reps;
}

void exerciseEnter() {
  drawTitle("Exercise", COL_BUTTER);
  exerciseRing();
  drawText(6, 86, "Last test", COL_SUB, 1);
  int n = settings.testCount;
  if (n == 0) {
    drawCentered(106, "No tests yet", COL_FAINT, 1);
  } else {
    float v[MAX_TESTS];
    uint16_t best = settings.tests[0].ds;
    for (int i = 0; i < n; i++) { v[i] = settings.tests[i].ds / 10.0f; if (settings.tests[i].ds < best) best = settings.tests[i].ds; }
    uint16_t latest = settings.tests[n - 1].ds;
    char t[12];
    fmtTenths((unsigned long)latest * 100, t, sizeof(t));
    drawRight(122, 86, t, latest == best ? COL_BUTTER : COL_FG, 1);
    drawLineChart(6, 96, 116, 26, v, n, COL_BUTTER, COL_BUTTER_DIM);
  }
  drawCentered(130, "Hold to start a test", COL_FAINT, 1);
  drawDots();
}
void exerciseUpdate() { if (reps != exShownReps) exerciseRing(); }
void exerciseHold() { menuReturnApp = APP_EXERCISE; openApp(APP_TEST_RUN); }

// ---------- Demo data: 30 believable days and a slow start to today ----------
float rnd() { return random(10000) / 10000.0f; }

void genSlots(float activity, int length, uint8_t* out) {
  for (int k = 0; k < SLOTS; k++) out[k] = 0;
  int k = 0;
  while (k < length) {
    int sit = (int)((2 + rnd() * 5) / activity + 0.5f);
    if (sit < 1) sit = 1;
    for (int j = 0; j < sit && k < length; j++) out[k++] = 1;
    int outB = (int)((0.6f + rnd() * 1.6f) * activity + 0.5f);
    if (outB < 1) outB = 1;
    for (int j = 0; j < outB && k < length; j++) out[k++] = 2;
  }
}

void summarizeSlots(const uint8_t* s, uint16_t& seatedMin, uint16_t& outMin, uint16_t& longestMin, uint16_t& ups) {
  int seatedB = 0, outB = 0, longest = 0, run = 0;
  ups = 0;
  for (int k = 0; k < SLOTS; k++) {
    if (s[k] == 1) { seatedB++; run++; if (run > longest) longest = run; }
    else { if (s[k] == 2) { outB++; if (run > 0) ups++; } run = 0; }
  }
  seatedMin = seatedB * SLOT_MIN; outMin = outB * SLOT_MIN; longestMin = longest * SLOT_MIN;
}

void loadDemoData() {
  settings.dayCount = 0; settings.dayHead = 0;
  settings.testCount = 0; settings.testNumber = 0;
  uint8_t un[SLOTS];
  DayRecord rec;
  for (int i = 0; i < 30; i++) {
    float trend = i / 29.0f;
    genSlots(0.85f + 0.35f * trend + (rnd() - 0.5f) * 0.2f, 100 + random(12), un);
    uint16_t sm, om, lm, ups;
    summarizeSlots(un, sm, om, lm, ups);
    rec.seatedMin = sm; rec.outMin = om; rec.longestMin = lm; rec.reps = ups * 2;
    rec.remAns = 3 + random(3); rec.remTot = 5 + random(2); rec.bestTestDs = 0;
    if (i % 3 == 0 && settings.testCount < MAX_TESTS) {
      settings.testNumber++;
      uint16_t ds = (uint16_t)((16.5f - 4 * trend + (rnd() - 0.5f)) * 10);
      settings.tests[settings.testCount].n = settings.testNumber;
      settings.tests[settings.testCount].ds = ds;
      settings.testCount++;
      rec.bestTestDs = ds;
    }
    packSlots(un, rec.slots);
    EEPROM.put(dayAddr(settings.dayCount), rec);   // settings saved once at the end
    settings.dayCount++;
  }
  saveSettings();

  // Today: quieter than usual so far (up to the real time if the clock is set)
  resetToday();
  int startSlots = 38;
  if (clockMode()) { startSlots = clockSlot(); if (startSlots < 1) startSlots = 1; }
  genSlots(0.7f, startSlots, todaySlots);
  uint16_t sm, om, lm, ups;
  summarizeSlots(todaySlots, sm, om, lm, ups);
  if (!clockMode()) todayOffsetMs = startSlots * SLOT_MS;
  slotIdx = startSlots < SLOTS ? startSlots : SLOTS - 1;
  satToday = true;
  seatedTotalMs = sm * 60000UL; outTotalMs = om * 60000UL; longestSitMs = lm * 60000UL;
  reps = ups * 2; goalReached = reps >= currentGoal();
  computeBaseline();
}

// ---------- CSV export over USB ----------
void exportCsv() {
  Serial.println("day,out_of_seat_min,seated_min,out_pct,longest_sit_min,stand_ups,long_sit_alerts,alerts_answered,best_test_s,pattern_8min");
  DayRecord rec;
  char line[80];
  for (int i = 0; i < settings.dayCount; i++) {
    readDay(i, rec);
    snprintf(line, sizeof(line), "%d,%u,%u,%d,%u,%u,%u,%u,", i + 1, rec.outMin, rec.seatedMin,
             (int)(shareOf(rec) * 100 + 0.5f), rec.longestMin, rec.reps, rec.remTot, rec.remAns);
    Serial.print(line);
    if (rec.bestTestDs) { Serial.print(rec.bestTestDs / 10); Serial.print('.'); Serial.print(rec.bestTestDs % 10); }
    Serial.print(',');
    for (int k = 0; k < SLOTS; k++) Serial.print(getSlot(rec.slots, k));
    Serial.println();
  }
}

// ---------- Menu: three short pages ----------
enum { IT_TEST, IT_CHECK, IT_SETTINGS, IT_DATA, IT_BACK, IT_GOAL, IT_REMIND, IT_SOUND,
       IT_FAST, IT_CLOCK, IT_DEMO, IT_ENDDAY, IT_EXPORT, IT_RESET, IT_NONE };
enum { PAGE_MAIN, PAGE_SETTINGS, PAGE_DATA };
const int PAGE_ITEMS[3][6] = {
  {IT_TEST, IT_CHECK, IT_SETTINGS, IT_DATA, IT_BACK, IT_NONE},
  {IT_GOAL, IT_REMIND, IT_SOUND, IT_FAST, IT_CLOCK, IT_BACK},
  {IT_DEMO, IT_ENDDAY, IT_EXPORT, IT_RESET, IT_BACK, IT_NONE}
};
const int PAGE_LEN[3] = {5, 6, 5};
const char* const PAGE_TITLES[3] = {"Menu", "Settings", "Data"};

int menuPage = PAGE_MAIN, menuSel = 0;
int menuConfirm = IT_NONE, menuFlash = IT_NONE;
int menuReturnItem = IT_NONE;     // reopen the menu with this item selected
unsigned long menuConfirmUntil = 0, menuFlashUntil = 0;

void menuLabel(int item, char* buf, int len) {
  if (item == menuFlash && millis() < menuFlashUntil) {
    const char* f = item == IT_DEMO ? "Demo data loaded" : item == IT_ENDDAY ? "Day saved" :
                    item == IT_EXPORT ? "Sent over USB" : item == IT_CLOCK ? "Clock set" : "Today cleared";
    snprintf(buf, len, "%s", f);
    return;
  }
  if (item == menuConfirm) { snprintf(buf, len, "Hold to confirm"); return; }
  uint16_t r = REMIND_OPTIONS_S[settings.remindIndex];
  switch (item) {
    case IT_TEST:     snprintf(buf, len, "Start test"); break;
    case IT_CHECK:    snprintf(buf, len, "Sensor & clock"); break;
    case IT_SETTINGS: snprintf(buf, len, "Settings  >"); break;
    case IT_DATA:     snprintf(buf, len, "Data  >"); break;
    case IT_GOAL:     snprintf(buf, len, "Exercise goal: %d", currentGoal()); break;
    case IT_REMIND:
      if (r < 60) snprintf(buf, len, "Long sit: %us demo", r);      // 18 chars fits the row
      else snprintf(buf, len, "Long sit: %u min", r / 60);
      break;
    case IT_SOUND:    snprintf(buf, len, "Sound: %s", settings.soundOn ? "On" : "Off"); break;
    case IT_FAST:
      snprintf(buf, len, "%s", timeScale == 60 ? "Fast: 1 min = 1 h" : timeScale == 20 ? "Fast: 1 min=20 min" : "Fast mode: Off");
      break;
    case IT_CLOCK:    snprintf(buf, len, "Set clock  >"); break;
    case IT_DEMO:     snprintf(buf, len, "Load demo data"); break;
    case IT_ENDDAY:   snprintf(buf, len, "End day now"); break;
    case IT_EXPORT:   snprintf(buf, len, "Export over USB"); break;
    case IT_RESET:    snprintf(buf, len, "Clear today"); break;
    default:          snprintf(buf, len, "Back"); break;
  }
}

void drawMenuRow(int i) {
  char label[24];
  menuLabel(PAGE_ITEMS[menuPage][i], label, sizeof(label));
  drawRow(30 + i * 17, label, i == menuSel);
}
void drawMenuPage() {
  clearContent();
  drawTitle(PAGE_TITLES[menuPage], COL_SKY);
  for (int i = 0; i < PAGE_LEN[menuPage]; i++) drawMenuRow(i);
  drawFooter("tap: next - hold: ok");
}
void menuGo(int page) { menuPage = page; menuSel = 0; menuConfirm = IT_NONE; drawMenuPage(); }
void menuEnter() {
  if (menuReturnItem == IT_NONE) { menuGo(PAGE_MAIN); return; }
  for (int pg = 0; pg < 3; pg++)
    for (int i = 0; i < PAGE_LEN[pg]; i++)
      if (PAGE_ITEMS[pg][i] == menuReturnItem) { menuPage = pg; menuSel = i; }
  menuReturnItem = IT_NONE;
  menuConfirm = IT_NONE;
  drawMenuPage();
}
void menuRedrawItem(int item) {
  for (int i = 0; i < PAGE_LEN[menuPage]; i++) if (PAGE_ITEMS[menuPage][i] == item) drawMenuRow(i);
}
void menuUpdate() {
  if (menuConfirm != IT_NONE && millis() >= menuConfirmUntil) { int c = menuConfirm; menuConfirm = IT_NONE; menuRedrawItem(c); }
  if (menuFlash != IT_NONE && millis() >= menuFlashUntil) { int f = menuFlash; menuFlash = IT_NONE; menuRedrawItem(f); }
}
void menuTap() {
  if (menuConfirm != IT_NONE) { int c = menuConfirm; menuConfirm = IT_NONE; menuRedrawItem(c); }
  int old = menuSel;
  menuSel = (menuSel + 1) % PAGE_LEN[menuPage];
  drawMenuRow(old); drawMenuRow(menuSel);
}
void flash(int item) { menuFlash = item; menuFlashUntil = millis() + 2500; }
// Destructive items need a second hold within 3 seconds
bool confirmed(int item) {
  if (menuConfirm == item && millis() < menuConfirmUntil) { menuConfirm = IT_NONE; return true; }
  menuConfirm = item; menuConfirmUntil = millis() + 3000;
  return false;
}
void menuHold() {
  int item = PAGE_ITEMS[menuPage][menuSel];
  switch (item) {
    case IT_TEST: openApp(APP_TEST_RUN); return;
    case IT_CHECK: openApp(APP_CHECK); return;
    case IT_CLOCK: openApp(APP_SETCLOCK); return;
    case IT_SETTINGS: menuGo(PAGE_SETTINGS); return;
    case IT_DATA: menuGo(PAGE_DATA); return;
    case IT_GOAL:
      settings.goalIndex = (settings.goalIndex + 1) % NUM_GOALS;
      goalReached = reps >= currentGoal();
      saveSettings();
      break;
    case IT_REMIND: settings.remindIndex = (settings.remindIndex + 1) % NUM_REMINDS; saveSettings(); break;
    case IT_SOUND: settings.soundOn = !settings.soundOn; saveSettings(); beepPattern(1, 80, 50); break;
    case IT_FAST:                 // not saved: every power-up starts at normal speed
      fastIndex = (fastIndex + 1) % NUM_FAST;
      setTimeScale(FAST_SPEEDS[fastIndex]);
      if (timeScale == 1 && clockOk) snapToClock();   // back to the real time of day
      drawDivider();
      break;
    case IT_DEMO:
      drawRow(30 + menuSel * 17, "Saving...", true);   // EEPROM writes take a few seconds
      loadDemoData(); saveCheckpoint(); flash(IT_DEMO); beepPattern(1, 80, 50);
      break;
    case IT_ENDDAY: if (confirmed(IT_ENDDAY)) { closeDay(); flash(IT_ENDDAY); beepPattern(2, 80, 80); } break;
    case IT_EXPORT: exportCsv(); flash(IT_EXPORT); break;
    case IT_RESET:   // clear the checkpoint too, or a restart would bring the day back
      if (confirmed(IT_RESET)) { resetToday(); clearCheckpoint(); flash(IT_RESET); beepPattern(2, 80, 80); }
      break;
    default:
      if (menuPage == PAGE_MAIN) openApp(menuReturnApp); else menuGo(PAGE_MAIN);
      return;
  }
  drawMenuRow(menuSel);
}

// ---------- Test run (5x sit-to-stand) ----------
enum { T_WAIT_SIT, T_COUNTDOWN, T_RUNNING, T_RESULT };
int testState = T_WAIT_SIT, testStands = 0, testLastShown = -1;
unsigned long testStart = 0, testPhaseStart = 0, testLastDraw = 0;

bool testActive() { return currentApp == APP_TEST_RUN && testState != T_RESULT; }
// Always busy, so the idle timer never sends a test to the main screen.
// The result screen returns to Exercise by itself after RESULT_SHOW_MS.
bool testBusy() { return true; }
const unsigned long RESULT_SHOW_MS = 6000;
unsigned long testResultAt = 0;

void testRunEnter() {
  testState = T_WAIT_SIT;
  drawTitle("Sit-to-stand x5", COL_BUTTER);
  drawCentered(58, "Sit down", COL_FG, 2);
  drawCentered(80, "to begin", COL_SUB, 1);
  drawFooter("hold to cancel");
}

void testRunUpdate() {
  unsigned long now = millis();
  if (testState == T_RESULT) {
    if (now - testResultAt >= RESULT_SHOW_MS) openApp(APP_EXERCISE);
    return;
  }
  if (testState == T_WAIT_SIT && seated) {
    testState = T_COUNTDOWN; testPhaseStart = now; testLastShown = -1;
    clearContent(); drawTitle("Sit-to-stand x5", COL_BUTTER);
    drawCentered(108, "Stand up 5 times", COL_SUB, 1);
    drawCentered(120, "as fast as is safe", COL_SUB, 1);
  } else if (testState == T_COUNTDOWN) {
    int left = 3 - (int)((now - testPhaseStart) / 1000);
    if (left <= 0) {
      testState = T_RUNNING; testStart = now; testStands = 0; testLastShown = -1; testLastDraw = 0;
      tft.fillRect(0, 28, 128, 70, COL_BG);
      beepPattern(1, 400, 50);
    } else if (left != testLastShown) {
      testLastShown = left;
      char n[12];
      snprintf(n, sizeof(n), "%d", left);
      tft.fillRect(0, 36, 128, 44, COL_BG);
      drawCentered(42, n, COL_FG, 4);
      beepPattern(1, 80, 50);
    }
  } else if (testState == T_RUNNING) {
    if (testStands != testLastShown) {
      testLastShown = testStands;
      for (int i = 0; i < 5; i++) tft.fillCircle(40 + i * 12, 40, 3, i < testStands ? COL_MINT : COL_FAINT);
    }
    if (now - testLastDraw >= 100) {
      testLastDraw = now;
      char t[12], p[12];
      fmtTenths(now - testStart, t, sizeof(t));
      snprintf(p, sizeof(p), "%7s", t);
      drawCentered(60, p, COL_FG, 2);
    }
  }
}

void testOnStand(unsigned long now) {
  if (currentApp != APP_TEST_RUN || testState != T_RUNNING) return;
  if ((long)(now - testStart) < 0) return;   // left the seat before "go": a false start
  beepPattern(1, 60, 40);
  if (++testStands < 5) return;

  unsigned long ms = now - testStart;   // now is real time (millis)
  uint16_t ds = ms / 100;
  uint16_t prevBest = 0;
  for (int i = 0; i < settings.testCount; i++)
    if (prevBest == 0 || settings.tests[i].ds < prevBest) prevBest = settings.tests[i].ds;
  if (settings.testCount == MAX_TESTS) {               // drop the oldest
    for (int i = 0; i < MAX_TESTS - 1; i++) settings.tests[i] = settings.tests[i + 1];
    settings.testCount--;
  }
  settings.testNumber++;
  settings.tests[settings.testCount].n = settings.testNumber;
  settings.tests[settings.testCount].ds = ds;
  settings.testCount++;
  saveSettings();
  if (todayBestTestMs == 0 || ms < todayBestTestMs) todayBestTestMs = ms;

  testState = T_RESULT;
  testResultAt = millis();
  clearContent(); drawTitle("Result", COL_BUTTER);
  bool best = prevBest == 0 || ds < prevBest;
  char t[12], line[24];
  fmtTenths(ms, t, sizeof(t));
  drawCentered(44, t, best ? COL_BUTTER : COL_FG, 3);
  snprintf(line, sizeof(line), "Test #%u saved", settings.testNumber);
  drawCentered(76, line, COL_SUB, 1);
  if (prevBest == 0) drawCentered(92, "First result", COL_SUB, 1);
  else if (best) drawCentered(92, "New personal best", COL_BUTTER, 1);
  else {
    char p[12];
    fmtTenths((unsigned long)prevBest * 100, p, sizeof(p));
    snprintf(line, sizeof(line), "Best is %s", p);
    drawCentered(92, line, COL_SUB, 1);
  }
  drawFooter("tap to finish");
}
// Tap or hold on the result, or hold during a test to cancel: back to Exercise
void testRunHold() { openApp(APP_EXERCISE); }
void testRunTap() { if (testState == T_RESULT) openApp(APP_EXERCISE); }

// ---------- Sensor & clock: live check, handy for setup and the demo ----------
unsigned long checkLastDraw = 0;
int checkShownClock = -1;

void checkDraw() {
  checkLastDraw = millis();
  drawCentered(38, seatRaw ? "Pressed" : " Empty ", seatRaw ? COL_SKY : COL_MINT, 2);
  char s[24];
  snprintf(s, sizeof(s), "%-17s", seated ? "Counted: seated" : "Counted: out");
  drawCentered(60, s, COL_SUB, 1);

  int c = clockOk ? 1 : 0;
  if (c != checkShownClock) { tft.fillRect(0, 86, 128, 40, COL_BG); checkShownClock = c; }
  if (clockOk) {
    snprintf(s, sizeof(s), "%04d-%02d-%02d", clockNow.year(), clockNow.month(), clockNow.day());
    drawCentered(90, s, COL_SUB, 1);
    snprintf(s, sizeof(s), "%02d:%02d:%02d", clockNow.hour(), clockNow.minute(), clockNow.second());
    drawCentered(104, s, COL_FG, 2);
  } else {
    drawCentered(92, rtcFound ? "Clock not set" : "Clock not found", COL_CORAL, 1);
    drawCentered(106, rtcFound ? "Menu > Settings >" : "Check SDA/SCL wires", COL_FAINT, 1);
    if (rtcFound) drawCentered(116, "Set clock", COL_FAINT, 1);
  }
}
void checkEnter() {
  drawTitle("Sensor & clock", COL_LAVENDER);
  char s[24];
  snprintf(s, sizeof(s), "Stand: %lu.%lu s empty", STAND_CONFIRM_MS / 1000, (STAND_CONFIRM_MS / 100) % 10);
  drawCentered(72, s, COL_FAINT, 1);
  checkShownClock = -1;
  checkDraw();
  drawFooter("hold: back");
}
void checkUpdate() { if (millis() - checkLastDraw >= 200) checkDraw(); }
void checkHold() { openApp(menuReturnApp); }

// ---------- Set clock ----------
//   tap        +1 on the underlined field
//   hold       next field
//   long hold  -1 (undo), the bar turns butter at 1.5 s
// Starts from the clock's time, or the upload time if it isn't set, so it
// usually needs only a few taps. Minutes are two digits so no field needs
// more than 23 taps, and the year only spans 6 years. The last step offers
// Save, Edit again (back to the year) and Cancel. Seconds start at :00.
enum { SC_YEAR, SC_MONTH, SC_DAY, SC_HOUR, SC_MIN10, SC_MIN1, SC_ACTION };
enum { SA_SAVE, SA_EDIT, SA_CANCEL };
const int SC_YEARS = 6;
int scY = 2026, scMo = 1, scD = 1, scH = 0, scMi = 0, scField = SC_YEAR, scAction = SA_SAVE;
int scYearMin = 2026;

int daysInMonth(int y, int m) {
  static const uint8_t d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

void scDraw() {
  char s[16];
  snprintf(s, sizeof(s), "%04d-%02d-%02d", scY, scMo, scD);
  drawText(4, 40, s, COL_FG, 2);                 // size 2 = 12 px per character
  snprintf(s, sizeof(s), "%02d:%02d", scH, scMi);
  drawText(34, 70, s, COL_FG, 2);

  // Underline the field being changed
  tft.fillRect(0, 57, 128, 2, COL_BG);
  tft.fillRect(0, 87, 128, 2, COL_BG);
  int x = -1, y = 0, w = 0;
  switch (scField) {
    case SC_YEAR:  x = 4;           y = 57; w = 48; break;
    case SC_MONTH: x = 4 + 5 * 12;  y = 57; w = 24; break;
    case SC_DAY:   x = 4 + 8 * 12;  y = 57; w = 24; break;
    case SC_HOUR:  x = 34;          y = 87; w = 24; break;
    case SC_MIN10: x = 34 + 3 * 12; y = 87; w = 12; break;
    case SC_MIN1:  x = 34 + 4 * 12; y = 87; w = 12; break;
  }
  if (x >= 0) tft.fillRect(x, y, w - 2, 2, COL_BUTTER);

  tft.fillRect(0, 96, 128, 49, COL_BG);
  if (scField == SC_ACTION) {
    drawRow(98, "Save", scAction == SA_SAVE);
    drawRow(114, "Edit again", scAction == SA_EDIT);
    drawRow(130, "Cancel", scAction == SA_CANCEL);
    drawFooter("tap: next - hold: ok");
  } else {
    drawCentered(104, "tap: +1   hold: next", COL_FAINT, 1);
    drawFooter("long hold: -1 (undo)");
  }
}
void setClockEnter() {
  drawTitle("Set clock", COL_LAVENDER);
  if (!rtcFound) {
    drawCentered(60, "Clock not found", COL_CORAL, 1);
    drawCentered(74, "Check SDA/SCL wires", COL_FAINT, 1);
    drawFooter("hold: back");
    return;
  }
  DateTime t = (clockOk && readClock()) ? clockNow : DateTime(__DATE__, __TIME__);
  scYearMin = atoi(__DATE__ + 7);                 // the upload's year: "Nov  8 2026"
  scY = t.year(); scMo = t.month(); scD = t.day(); scH = t.hour(); scMi = t.minute();
  if (scY < scYearMin || scY >= scYearMin + SC_YEARS) scY = scYearMin;
  scField = SC_YEAR; scAction = SA_SAVE;
  scDraw();
}
// step = +1 or -1 on the current field, wrapping around
void scStep(int step) {
  switch (scField) {
    case SC_YEAR:  scY = scYearMin + (scY - scYearMin + step + SC_YEARS) % SC_YEARS; break;
    case SC_MONTH: scMo = (scMo - 1 + step + 12) % 12 + 1; break;
    case SC_DAY:   { int n = daysInMonth(scY, scMo); scD = (scD - 1 + step + n) % n + 1; } break;
    case SC_HOUR:  scH = (scH + step + 24) % 24; break;
    case SC_MIN10: scMi = ((scMi / 10 + step + 6) % 6) * 10 + scMi % 10; break;
    case SC_MIN1:  scMi = (scMi / 10) * 10 + (scMi % 10 + step + 10) % 10; break;
    default:       scAction = (scAction + step + 3) % 3; break;
  }
  if (scD > daysInMonth(scY, scMo)) scD = daysInMonth(scY, scMo);   // e.g. Jan 31 -> Feb
  scDraw();
}
void setClockTap() { if (rtcFound) scStep(+1); }
void setClockUndo() { if (rtcFound) scStep(-1); }
void setClockHold() {
  if (!rtcFound) { openApp(APP_MENU); return; }
  if (scField < SC_ACTION) { scField++; scDraw(); return; }
  if (scAction == SA_EDIT) { scField = SC_YEAR; scAction = SA_SAVE; scDraw(); return; }
  menuReturnItem = IT_CLOCK;
  if (scAction == SA_SAVE && applyClock(DateTime(scY, scMo, scD, scH, scMi, 0))) { flash(IT_CLOCK); beepPattern(1, 80, 50); }
  openApp(APP_MENU);
}

// ---------- Hidden game: "Glide" ----------
// Tap to flap through the gaps. Uses the touch sensor, so seat tracking keeps
// running underneath; only the long-sit beeps wait until you stop.
// Drawn incrementally (only the pixels that change) so it stays smooth at 8 MHz.
const int GM_TOP = 28, GM_BOT = 140;              // play area rows
const int GM_PIPE_W = 12, GM_GAP = 46, GM_SPACING = 64, GM_SPEED = 2;
const int GM_BIRD_X = 30, GM_BIRD = 7;
const int GM_PIPES = 3;
const unsigned long GM_FRAME_MS = 33;             // about 30 frames a second
const float GM_GRAVITY = 0.32f, GM_FLAP = -3.4f, GM_MAX_FALL = 4.5f;
const uint16_t GM_COLOURS[4] = {COL_MINT, COL_SKY, COL_LAVENDER, COL_PEACH};

enum { G_READY, G_PLAY, G_OVER };
int gState = G_READY, gScore = 0, gDrawnY = -1;
float gY = 0, gVy = 0;
int gPipeX[GM_PIPES], gPipeGap[GM_PIPES];         // gap = top row of the opening
uint16_t gPipeCol[GM_PIPES];
bool gScored[GM_PIPES];
unsigned long gLastFrame = 0, gOverAt = 0;

bool gameActive() { return currentApp == APP_GAME && gState == G_PLAY; }

int gRandGap() { return GM_TOP + 10 + (int)random(GM_BOT - GM_TOP - GM_GAP - 20); }
void gNewPipe(int i, int x) {
  gPipeX[i] = x; gPipeGap[i] = gRandGap(); gScored[i] = false; gPipeCol[i] = GM_COLOURS[random(4)];
}
// Fills columns [x0, x1) of pipe i outside its opening, clipped to the screen
void gPipeCols(int i, int x0, int x1, uint16_t c) {
  if (x0 < 0) x0 = 0;
  if (x1 > 128) x1 = 128;
  if (x1 <= x0) return;
  int top = gPipeGap[i], bot = top + GM_GAP;
  tft.fillRect(x0, GM_TOP, x1 - x0, top - GM_TOP, c);
  tft.fillRect(x0, bot, x1 - x0, GM_BOT - bot, c);
}
// Moves the bird by erasing only the strip it left, so it doesn't flicker
void gDrawBird() {
  int y = (int)(gY + 0.5f);
  if (y == gDrawnY) return;
  if (gDrawnY >= 0) {
    int dy = y - gDrawnY;
    if (dy >= GM_BIRD || -dy >= GM_BIRD) tft.fillRect(GM_BIRD_X, gDrawnY, GM_BIRD, GM_BIRD, COL_BG);
    else if (dy > 0) tft.fillRect(GM_BIRD_X, gDrawnY, GM_BIRD, dy, COL_BG);
    else tft.fillRect(GM_BIRD_X, y + GM_BIRD, GM_BIRD, -dy, COL_BG);
  }
  tft.fillRoundRect(GM_BIRD_X, y, GM_BIRD, GM_BIRD, 2, COL_BUTTER);
  tft.drawPixel(GM_BIRD_X + 5, y + 2, COL_BG);    // eye
  gDrawnY = y;
}
void gDrawScore() {
  char s[8];
  snprintf(s, sizeof(s), "%4d", gScore);
  drawRight(122, 19, s, COL_FG, 1);
}
void gDrawFooter() {
  char s[24];
  snprintf(s, sizeof(s), "best %u - hold: exit", settings.gameBest);
  drawFooter(s);
}
void gReset() {
  tft.fillRect(0, GM_TOP, 128, GM_BOT - GM_TOP, COL_BG);
  tft.drawFastHLine(0, GM_BOT, 128, COL_FAINT);
  gState = G_READY; gScore = 0; gVy = 0; gDrawnY = -1;
  gY = (GM_TOP + GM_BOT) / 2 - GM_BIRD / 2;
  for (int i = 0; i < GM_PIPES; i++) gNewPipe(i, 128 + 40 + i * GM_SPACING);
  gDrawBird();
  gDrawScore();
  drawCentered(112, "tap to fly", COL_SUB, 1);
  gDrawFooter();
}
void gGameOver() {
  gState = G_OVER; gOverAt = millis();
  beepPattern(3, 90, 60);
  bool best = gScore > (int)settings.gameBest;
  if (best) { settings.gameBest = gScore; saveSettings(); }
  tft.fillRoundRect(14, 60, 100, 42, 8, COL_CARD);
  char s[12];
  snprintf(s, sizeof(s), "%d", gScore);
  drawTextBg((128 - (int)strlen(s) * 12) / 2, 66, s, best && gScore > 0 ? COL_BUTTER : COL_FG, 2, COL_CARD);
  const char* line = best && gScore > 0 ? "new best" : "tap to retry";
  drawTextBg((128 - (int)strlen(line) * 6) / 2, 88, line, best && gScore > 0 ? COL_BUTTER : COL_SUB, 1, COL_CARD);
  gDrawFooter();
}
void gamePress() {
  if (gState == G_READY) {
    tft.fillRect(0, 110, 128, 10, COL_BG);        // clear "tap to fly"
    gState = G_PLAY; gLastFrame = millis(); gVy = GM_FLAP;
  } else if (gState == G_PLAY) {
    gVy = GM_FLAP;
  } else if (millis() - gOverAt >= 600) {         // a frantic last flap doesn't skip the score
    gReset();
  }
}
void gameEnter() { drawTitle("Glide", COL_BUTTER); gReset(); }
void gameHold() { openApp(APP_EXERCISE); }
void gameUpdate() {
  unsigned long now = millis();
  if (gState != G_PLAY || now - gLastFrame < GM_FRAME_MS) return;
  gLastFrame = now;

  gVy += GM_GRAVITY;
  if (gVy > GM_MAX_FALL) gVy = GM_MAX_FALL;
  gY += gVy;
  bool hit = false;
  if (gY < GM_TOP) { gY = GM_TOP; gVy = 0; }      // the ceiling stops you, it doesn't end the game
  if (gY + GM_BIRD > GM_BOT) { gY = GM_BOT - GM_BIRD; hit = true; }

  for (int i = 0; i < GM_PIPES; i++) {
    int ox = gPipeX[i], nx = ox - GM_SPEED;
    gPipeCols(i, nx, ox, gPipeCol[i]);                      // new left edge
    gPipeCols(i, nx + GM_PIPE_W, ox + GM_PIPE_W, COL_BG);   // old right edge
    gPipeX[i] = nx;
    if (nx < GM_BIRD_X + GM_BIRD && nx + GM_PIPE_W > GM_BIRD_X &&
        (gY < gPipeGap[i] || gY + GM_BIRD > gPipeGap[i] + GM_GAP)) hit = true;
    if (!gScored[i] && nx + GM_PIPE_W <= GM_BIRD_X) {
      gScored[i] = true; gScore++; gDrawScore(); beepPattern(1, 15, 10);
    }
    if (nx + GM_PIPE_W <= 0) {                              // off the left: send it round again
      int far = 0;
      for (int j = 0; j < GM_PIPES; j++) if (gPipeX[j] > far) far = gPipeX[j];
      gNewPipe(i, far + GM_SPACING);
    }
  }
  gDrawBird();
  if (hit) gGameOver();
}

// ---------- Boot: new day, or hold to continue the previous one ----------
unsigned long bootStart = 0;
int bootLastShown = -1;

void bootEnter() {
  bootStart = millis(); bootLastShown = -1;
  drawTitle("Starting up", COL_SKY);
  drawCentered(44, "New day", COL_FG, 2);
  drawCentered(72, "Hold to continue", COL_SUB, 1);
  drawCentered(84, "the previous day", COL_SUB, 1);
  drawFooter("tap to start now");
}
// Saves an unfinished day from the checkpoint as a day record
void checkpointToDay(const Checkpoint& cp) {
  DayRecord rec;
  rec.seatedMin = cp.seatedMs / 60000UL; rec.outMin = cp.outMs / 60000UL; rec.longestMin = cp.longestMs / 60000UL;
  rec.reps = cp.reps; rec.remAns = cp.remAns; rec.remTot = cp.remTot; rec.bestTestDs = cp.bestTestMs / 100;
  for (int i = 0; i < SLOT_BYTES; i++) rec.slots[i] = cp.slots[i];
  pushDay(rec);
}
void bootNewDay() {
  Checkpoint cp;
  EEPROM.get(CP_ADDR, cp);
  if (cp.magic == CP_MAGIC) checkpointToDay(cp);
  clearCheckpoint();
  resetToday();
  computeBaseline();
  openApp(APP_GLANCE);
}
void bootUpdate() {
  unsigned long el = millis() - bootStart;
  int left = el >= BOOT_CHOICE_MS ? 0 : (int)((BOOT_CHOICE_MS - el + 999) / 1000);
  if (left != bootLastShown) {
    bootLastShown = left;
    char s[20];
    snprintf(s, sizeof(s), "Starting in %d", left);
    tft.fillRect(0, 104, 128, 10, COL_BG);
    drawCentered(106, s, COL_FAINT, 1);
  }
  if (left == 0) bootNewDay();
}
// Carries on a saved day. With the clock, blocks while it was off stay "no data".
void restoreCheckpoint(const Checkpoint& cp) {
  resetToday();                      // with the clock, this already lines up with the time of day
  reps = cp.reps; seatedTotalMs = cp.seatedMs; outTotalMs = cp.outMs; longestSitMs = cp.longestMs;
  remindersAnswered = cp.remAns; remindersTotal = cp.remTot; todayBestTestMs = cp.bestTestMs;
  unpackSlots(cp.slots, todaySlots);
  satToday = cp.seatedMs > 0 || cp.reps > 0;
  if (!clockOk) {
    todayOffsetMs = cp.elapsedMs;
    slotIdx = (int)(cp.elapsedMs / SLOT_MS);
    if (slotIdx > SLOTS - 1) slotIdx = SLOTS - 1;
  }
  goalReached = reps >= currentGoal();
}
void bootContinue() {
  Checkpoint cp;
  EEPROM.get(CP_ADDR, cp);
  restoreCheckpoint(cp);
  openApp(APP_GLANCE);
}
bool alwaysBusy() { return true; }

// ============================================================
// APP TABLE
// ============================================================
struct App {
  void (*enter)();
  void (*update)();
  void (*onTap)();
  void (*onHold)();
  bool (*busy)();      // true = don't auto-return to the main screen
};
void noUpdate() {}
void noTap() {}
bool notBusy() { return false; }

const App apps[APP_COUNT] = {
  // enter          update          tap            hold              busy
  {glanceEnter,   glanceUpdate,   nextCarousel,  openMenuFromHere, notBusy},     // APP_GLANCE
  {patternEnter,  patternUpdate,  nextCarousel,  openMenuFromHere, notBusy},     // APP_PATTERN
  {weekEnter,     weekUpdate,     nextCarousel,  openMenuFromHere, notBusy},     // APP_WEEK
  {trendsEnter,   noUpdate,       nextCarousel,  openMenuFromHere, notBusy},     // APP_TRENDS
  {exerciseEnter, exerciseUpdate, nextCarousel,  exerciseHold,     notBusy},     // APP_EXERCISE
  {menuEnter,     menuUpdate,     menuTap,       menuHold,         notBusy},     // APP_MENU
  {testRunEnter,  testRunUpdate,  testRunTap,    testRunHold,      testBusy},    // APP_TEST_RUN
  {checkEnter,    checkUpdate,    noTap,         checkHold,        notBusy},     // APP_CHECK
  {bootEnter,     bootUpdate,     bootNewDay,    bootContinue,     alwaysBusy},  // APP_BOOT
  {setClockEnter, noUpdate,       setClockTap,   setClockHold,     notBusy},     // APP_SETCLOCK
  {gameEnter,     gameUpdate,     noTap,         gameHold,         notBusy},     // APP_GAME
};

void openApp(int id) {
  currentApp = id;
  lastInput = millis();
  footerSaved[0] = 0; footerDots = false;     // the new page sets its own footer         // a screen opened by the program restarts the idle timer
  tft.fillRect(0, 14, 128, 146, COL_BG);   // the status bar stays; less to redraw
  drawDivider();
  drawStatusBar();
  holdBarPx = 0;
  apps[id].enter();
}

// ============================================================
// TOUCH: filtered against glitches, brushes and resting arms
// ============================================================
void unlockScreen() { if (locked) { locked = false; drawStatusBar(); } }
void showLockedHint() {
  hintUntil = millis() + 1200;
  tft.fillRect(0, 145, 128, 10, COL_BG);
  drawCentered(147, "locked: press longer", COL_PEACH, 1);
}

// Every action happens when the finger lifts, judged by how long it was down:
//   < 0.05 s            noise, ignored
//   < 0.15 s (locked)   brush on the idle main screen, ignored
//   < 0.6 s             tap
//   0.6 s - 4 s         hold (the bar is full once a hold is ready)
//   long hold           on pages that have one, the bar turns butter
//   > 4 s               arm resting on the sensor, cancelled
// The game is the exception: it flaps the moment the finger lands.

// Long-hold time for the current page, 0 = none
unsigned long longHoldMs() {
  if (currentApp == APP_EXERCISE) return SECRET_HOLD_MS;
  if (currentApp == APP_SETCLOCK) return LONG_HOLD_MS;
  return 0;
}
void onLongHold() {
  if (currentApp == APP_EXERCISE) openApp(APP_GAME);
  else if (currentApp == APP_SETCLOCK) setClockUndo();
}

// What letting go will do, shown in the footer while holding
const char* holdName(bool longHold) {
  if (longHold) return currentApp == APP_SETCLOCK ? "-1 (undo)" : "?";    // the game stays a secret
  switch (currentApp) {
    case APP_GLANCE: case APP_PATTERN: case APP_WEEK: case APP_TRENDS: return "Menu";
    case APP_EXERCISE: return "Start test";
    case APP_MENU:     return "select";
    case APP_TEST_RUN: return testActive() ? "cancel test" : "Exercise";
    case APP_CHECK:    return "back";
    case APP_BOOT:     return "continue day";
    case APP_GAME:     return "exit";
    case APP_SETCLOCK:
      if (!rtcFound) return "back";
      if (scField < SC_ACTION) return "next field";
      return scAction == SA_SAVE ? "save" : scAction == SA_EDIT ? "edit again" : "cancel";
    default: return "select";
  }
}
// 0 none, 1 "hold: X" (filling), 2 "release: X" (ready), 3 long hold ready
int labelShown = 0;
void showHoldLabel(int state) {
  if (state == labelShown) return;
  labelShown = state;
  char s[24];
  if (state == 1) { snprintf(s, sizeof(s), "hold: %s", holdName(false)); drawFooterText(s, COL_FAINT); }
  else if (state == 2) { snprintf(s, sizeof(s), "release: %s", holdName(false)); drawFooterText(s, COL_SKY); }
  else if (state == 3) { snprintf(s, sizeof(s), "release: %s", holdName(true)); drawFooterText(s, COL_BUTTER); }
}
void clearHoldLabel() { if (labelShown) { labelShown = 0; restoreFooter(); } }

bool longShown = false;
void serviceTouch() {
  unsigned long now = millis();
  bool touched = (digitalRead(TOUCH_PIN) == HIGH);

  if (touched && !touchDown) {                      // finger landed
    touchDown = true; touchCancelled = false; touchStart = now; longShown = false;
    if (currentApp == APP_GAME) { lastInput = now; gamePress(); }
  } else if (touched && touchDown && !touchCancelled) {
    unsigned long held = now - touchStart;
    if (held >= RESTING_MS) { touchCancelled = true; clearHoldBar(); clearHoldLabel(); return; }
    bool lockedHere = locked && currentApp == APP_GLANCE;
    unsigned long barDelay = currentApp == APP_GAME ? GAME_BAR_DELAY_MS : HOLD_BAR_DELAY_MS;
    if (held >= barDelay) drawHoldBar(held, lockedHere && held < LOCKED_TAP_MS ? COL_FAINT : COL_SKY);
    unsigned long lh = longHoldMs();
    if (lh && held >= lh && !longShown) {           // the bar turns butter
      longShown = true;
      tft.fillRect(4, 157, 120, 2, COL_BUTTER);
      holdBarPx = 120;
    }
    // Locked main screen: no label until the press is long enough to count
    if (held >= HOLD_LABEL_MS && !(lockedHere && held < LOCKED_TAP_MS))
      showHoldLabel(longShown ? 3 : held >= HOLD_MS ? 2 : 1);
  } else if (!touched && touchDown) {               // finger lifted
    touchDown = false;
    unsigned long held = now - touchStart;
    clearHoldBar();
    clearHoldLabel();                               // before the action, which may redraw the footer
    if (touchCancelled || held < GLITCH_MS) return;
    if (held >= HOLD_MS) {
      lastInput = now;
      unlockScreen();
      unsigned long lh = longHoldMs();
      if (lh && held >= lh) onLongHold();
      else apps[currentApp].onHold();
      return;
    }
    if (locked && currentApp == APP_GLANCE && held < LOCKED_TAP_MS) { showLockedHint(); return; }
    lastInput = now;
    unlockScreen();
    apps[currentApp].onTap();
  }
}

// ============================================================
// GROVE LCD: an ambient light for the day
// ============================================================
char lcdShown1[17] = "", lcdShown2[17] = "";
int lcdR = -1, lcdG = -1, lcdB = -1;

void lcdColor(int r, int g, int b) {
  if (r != lcdR || g != lcdG || b != lcdB) { lcd.setRGB(r, g, b); lcdR = r; lcdG = g; lcdB = b; }
}
void lcdShow(const char* a, const char* b) {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "%-16s", a);
  snprintf(l2, sizeof(l2), "%-16s", b);
  if (strcmp(l1, lcdShown1) != 0) { lcd.setCursor(0, 0); lcd.print(l1); strcpy(lcdShown1, l1); }
  if (strcmp(l2, lcdShown2) != 0) { lcd.setCursor(0, 1); lcd.print(l2); strcpy(lcdShown2, l2); }
}

void updateLcd() {
  unsigned long now = millis();
  unsigned long period = dataMillis() - stateSince;
  char a[17], b[17], t[12];
  fmtDur(period, t, sizeof(t));

  if (currentApp == APP_BOOT) {
    lcdColor(120, 170, 255); lcdShow("NEW DAY", "HOLD: CONTINUE"); return;
  }
  if (currentApp == APP_GAME) {
    lcdColor(185, 160, 255);
    snprintf(a, sizeof(a), "GLIDE  SCORE %d", gScore);
    snprintf(b, sizeof(b), "BEST %u", settings.gameBest);
  } else if (testActive()) {
    lcdColor(185, 160, 255);
    if (testState == T_RUNNING) {
      char tt[12];
      fmtTenths(now - testStart, tt, sizeof(tt));
      snprintf(a, sizeof(a), "TEST  STAND %d/5", testStands);
      snprintf(b, sizeof(b), "TIME  %s", tt);
    } else if (testState == T_COUNTDOWN) { snprintf(a, sizeof(a), "TEST STARTING"); snprintf(b, sizeof(b), "GET READY"); }
    else { snprintf(a, sizeof(a), "TEST: SIT DOWN"); snprintf(b, sizeof(b), "TO BEGIN"); }
  } else if (reminderDue) {
    lcdColor(255, 125, 115);
    snprintf(a, sizeof(a), "TIME TO STAND");
    snprintf(b, sizeof(b), "SEATED %9s", t);
  } else {
    const Level& L = LEVELS[activityLevel()];
    lcdColor(L.r, L.g, L.b);
    snprintf(a, sizeof(a), "%s", L.lcdText);
    if (seated) snprintf(b, sizeof(b), "SEATED %9s", t);
    else snprintf(b, sizeof(b), "OUT %12s", t);
  }
  lcdShow(a, b);
}

// ============================================================
// TFT RECOVERY
// A noisy wire can corrupt a command and leave the screen white (display
// off or asleep). Every few seconds the display settings are sent again,
// which is invisible when nothing is wrong. Every few minutes, while the
// main screen sits idle, it is also redrawn in case its memory was lost.
// ============================================================
const unsigned long TFT_SPI_HZ = 8000000;              // 8 MHz is reliable on jumper wires
const unsigned long TFT_KEEPALIVE_MS = 3000;
const unsigned long TFT_REPAINT_MS = 5UL * 60000UL;
unsigned long lastTftKeepAlive = 0, lastTftRepaint = 0;

void tftKeepAlive() {
  uint8_t colmod = 0x05;                     // 16-bit colour
  tft.sendCommand(ST77XX_SLPOUT);            // no effect if already awake
  delay(5);
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1);
  tft.sendCommand(ST77XX_NORON);
  tft.sendCommand(ST77XX_INVOFF);
  tft.setRotation(0);                        // re-sends the memory direction
  tft.sendCommand(ST77XX_DISPON);
}
void serviceTft() {
  unsigned long now = millis();
  if (now - lastTftKeepAlive >= TFT_KEEPALIVE_MS) { lastTftKeepAlive = now; tftKeepAlive(); }
  if (now - lastTftRepaint >= TFT_REPAINT_MS) {
    lastTftRepaint = now;
    if (locked && currentApp == APP_GLANCE && !touchDown) openApp(APP_GLANCE);
  }
}

// ============================================================
// SETUP AND LOOP
// ============================================================
unsigned long lastLcdUpdate = 0, lastStatusUpdate = 0;

void setup() {
  Serial.begin(115200);              // Export over USB, and setting the clock
  pinMode(SEAT_PIN, INPUT_PULLUP);
  seatRaw = readSeatPressed();
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  randomSeed(analogRead(A0));        // only used for demo data

  loadSettings();

  lcd.begin(16, 2);
  lcdColor(120, 170, 255);

  // Clock: a fresh upload sets it to the compile time. Otherwise it's only
  // trusted if it kept running (it won't through a power cut without a coin cell).
  rtcFound = rtc.begin();
  if (rtcFound) {
    uint32_t id = buildId();
    if (settings.buildId != id) {
      rtc.adjust(DateTime(__DATE__, __TIME__) + TimeSpan(UPLOAD_SECONDS));
      settings.buildId = id;
      saveSettings();
      clockOk = true;
    } else {
      clockOk = !rtc.lostPower();
    }
    if (clockOk) readClock();
  }
  if (!clockOk) Serial.println("Clock not set. Type the time like 2026-11-08 13:45:00");

  Wire.setClock(400000);             // fast I2C (after rtc.begin, which can reset it)
  lcdShow("SEAT SENSOR", "STARTING");

  // If colours look wrong or there's a noisy stripe at one edge,
  // try INITR_GREENTAB or INITR_REDTAB instead of INITR_BLACKTAB.
  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(0);
  tft.fillScreen(COL_BG);
  drawCentered(70, "Seat Sensor", COL_SKY, 1);

  resetToday();
  computeBaseline();
  lastInput = millis();
  rawSinceReal = millis(); rawSinceData = dataMillis();

  // An unfinished day: with the clock, carry it on if it's from today, or
  // file it if it's from an earlier day. Without the clock, ask.
  Checkpoint cp;
  EEPROM.get(CP_ADDR, cp);
  if (cp.magic != CP_MAGIC) {
    openApp(APP_GLANCE);
  } else if (clockOk && cp.date != 0) {
    if (cp.date == todayDate) restoreCheckpoint(cp);
    else { checkpointToDay(cp); clearCheckpoint(); computeBaseline(); }
    openApp(APP_GLANCE);
  } else {
    openApp(APP_BOOT);
  }
}

void loop() {
  unsigned long now = millis();

  serviceTouch();
  serviceSensor();
  serviceClock();
  serviceSerialClock();
  serviceBuzzer();
  serviceTft();

  if (now - lastLcdUpdate >= 250) { lastLcdUpdate = now; updateLcd(); }
  if (now - lastStatusUpdate >= 1000) { lastStatusUpdate = now; drawStatusBar(); }

  if (hintUntil && now >= hintUntil) {
    hintUntil = 0;
    if (carouselIndex(currentApp) >= 0) drawDots();
  }

  apps[currentApp].update();

  // Idle: back to the main screen and lock it again
  // Fresh millis(), not `now`: lastInput may have been set later in this same
  // pass (opening a page), and now - lastInput would wrap to a huge number.
  if (!touchDown && !apps[currentApp].busy() && millis() - lastInput >= IDLE_MS) {
    if (!locked || currentApp != APP_GLANCE) {
      locked = true;
      if (currentApp != APP_GLANCE) openApp(APP_GLANCE);
    }
  }
}
