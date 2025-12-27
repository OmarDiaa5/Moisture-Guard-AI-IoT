#include <Servo.h>
#include <LiquidCrystal.h>

Servo valve;

// LCD: RS, EN, D4, D5, D6, D7
// RS->7, EN->6, D4->5, D5->10, D6->A1, D7->A2
LiquidCrystal lcd(7, 6, 5, 10, A1, A2);

// ---- Pins ----
const int sensorPin = A0;

const int ledR = 11;
const int ledY = 12;
const int ledG = 13;

const int buzzerPin = 8;
const int servoPin  = 9;

// Buttons (INPUT_PULLUP)
const int btnDry = 2;
const int btnWet = 3;
const int btnMan = 4;

// ---- Calibration (Tinkercad range) ----
int DRY_RAW = 1023;
int WET_RAW = 0;

// ---- Thresholds ----
const int START_WATER_BELOW = 30; // يبدأ ري تحت 30%
const int STOP_WATER_ABOVE  = 40; // يوقف فوق 40%

// ---- Timings ----
bool watering = false;
bool cooldown = false;

unsigned long t0 = 0;
const unsigned long WATER_MS    = 3000;
const unsigned long COOLDOWN_MS = 8000;

// LCD update (avoid flicker)
unsigned long lastLcd = 0;

// ---- LOG THROTTLE (حل مشكلة المسح في Serial Monitor) ----
const unsigned long LOG_EVERY_MS = 250;  // خليها 250 أو 500 لو عايز أبطأ
unsigned long lastLog = 0;

// ---- Helpers ----
int smoothRead() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(sensorPin);
    delay(3);
  }
  return sum / 10;
}

int moisturePercent(int raw) {
  if (DRY_RAW == WET_RAW) return 0;
  long p = (long)(DRY_RAW - raw) * 100L / (DRY_RAW - WET_RAW); // Dry=0, Wet=100
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return (int)p;
}

void setLeds(bool r, bool y, bool g) {
  digitalWrite(ledR, r);
  digitalWrite(ledY, y);
  digitalWrite(ledG, g);
}

void beep(bool on) {
  if (on) tone(buzzerPin, 1200);
  else noTone(buzzerPin);
}

void valveOpen(bool open) {
  if (open) valve.write(90);
  else valve.write(0);
}

bool pressed(int pin) {
  return digitalRead(pin) == LOW; // INPUT_PULLUP
}

void lcdShow(int percent, const char* stateText) {
  lcd.setCursor(0, 0);
  lcd.print("Moist: ");
  if (percent < 100) lcd.print(" ");
  if (percent < 10)  lcd.print(" ");
  lcd.print(percent);
  lcd.print("%     ");

  lcd.setCursor(0, 1);
  lcd.print("State: ");
  lcd.print(stateText);
  lcd.print("       ");
}

void lcdMsg(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

// CSV logger: ms,raw,percent,stateId
void logCSV(unsigned long ms, int raw, int percent, int stateId) {
  Serial.print(ms);      Serial.print(",");
  Serial.print(raw);     Serial.print(",");
  Serial.print(percent); Serial.print(",");
  Serial.println(stateId);
}

// print only every LOG_EVERY_MS
void maybeLog(unsigned long ms, int raw, int percent, int stateId) {
  if (ms - lastLog >= LOG_EVERY_MS) {
    logCSV(ms, raw, percent, stateId);
    lastLog = ms;
  }
}

void setup() {
  pinMode(ledR, OUTPUT);
  pinMode(ledY, OUTPUT);
  pinMode(ledG, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  pinMode(btnDry, INPUT_PULLUP);
  pinMode(btnWet, INPUT_PULLUP);
  pinMode(btnMan, INPUT_PULLUP);

  Serial.begin(9600);

  valve.attach(servoPin);
  valveOpen(false);

  lcd.begin(16, 2);
  lcdMsg("Moisture Guard", "Ready");
  delay(700);
  lcd.clear();

  // Optional: header مرة واحدة (لو عايز)
  // Serial.println("ms,raw,percent,stateId");
}

void loop() {
  int raw = smoothRead();
  int percent = moisturePercent(raw);
  unsigned long now = millis();

  // stateId mapping:
  // 0=WET, 1=NORMAL, 2=DRY, 3=WATERING, 4=COOLDOWN
  int stateId = 1;
  const char* stateText = "NORMAL";

  // ---- Buttons ----
  if (pressed(btnDry)) {
    DRY_RAW = raw;
    lcdMsg("Saved DRY_RAW", "OK");
    delay(350);
    lcd.clear();
  }

  if (pressed(btnWet)) {
    WET_RAW = raw;
    lcdMsg("Saved WET_RAW", "OK");
    delay(350);
    lcd.clear();
  }

  if (pressed(btnMan) && !watering && !cooldown) {
    watering = true;
    t0 = now;
    delay(250);
  }

  // ---- WATERING ----
  if (watering) {
    stateId = 3;
    stateText = "WATERING";

    setLeds(true, false, false);
    beep(false);
    valveOpen(true);

    // log throttled
    maybeLog(now, raw, percent, stateId);

    // stop watering when moisture reached stop threshold OR timer ends
    if (percent >= STOP_WATER_ABOVE || now - t0 >= WATER_MS) {
      watering = false;
      valveOpen(false);
      cooldown = true;
      t0 = now;
    }
  }
  // ---- COOLDOWN ----
  else if (cooldown) {
    stateId = 4;
    stateText = "COOLDOWN";

    setLeds(false, true, false);
    beep(false);
    valveOpen(false);

    maybeLog(now, raw, percent, stateId);

    if (now - t0 >= COOLDOWN_MS) cooldown = false;
  }
  // ---- NORMAL STATES ----
  else {
    if (percent >= 70) {
      stateId = 0;
      stateText = "WET";

      setLeds(false, false, true);
      beep(false);
      valveOpen(false);
    }
    else if (percent > START_WATER_BELOW) {
      stateId = 1;
      stateText = "NORMAL";

      setLeds(false, true, false);
      beep(false);
      valveOpen(false);
    }
    else {
      stateId = 2;
      stateText = "DRY";

      setLeds(true, false, false);
      beep(true);
      valveOpen(false);

      // auto start watering
      watering = true;
      t0 = now;
      beep(false);
    }

    maybeLog(now, raw, percent, stateId);
  }

  // LCD update (avoid flicker)
  if (now - lastLcd >= 200) {
    lcdShow(percent, stateText);
    lastLcd = now;
  }

  delay(50);
}
