#include <Arduino.h>
// Version 3.0 - Dual Shift Support with Named Shifts

#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH 1
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RTC_DS3231 rtc;
bool rtcAvailable = true;

#define ENGINE_INPUT_PIN 15
#define LED_PIN 25

#define EEPROM_SIZE 512
#define NAME_ADDR 0
#define ENGINE_HOURS_ADDR 16
#define SHIFT1_CUTOFF_ADDR 20
#define SHIFT2_CUTOFF_ADDR 21
#define SHIFT1_HOUR_ADDR 22
#define SHIFT2_HOUR_ADDR 26
#define LAST_SHIFT_RESET_DAY_ADDR 30 //Not used
#define SHIFT1_NAME_ADDR 31
#define SHIFT2_NAME_ADDR 41

#define MAX_NAME_LENGTH 10

#define LAST_SHIFT1_RESET_DAY_ADDR 52  // 1 byte (day of last reset for shift 1)
#define LAST_SHIFT2_RESET_DAY_ADDR 53  // 1 byte (day of last reset for shift 2)


const int EEPROM_ADDR_INTERVAL = 51;
unsigned long SAVE_INTERVAL = 10 * 60 * 1000UL; // Default 10 minutes

const unsigned long DISPLAY_UPDATE_INTERVAL = 1000;

String engine_name = "";
bool engineRunning = false;
unsigned long engineStartTime = 0;
unsigned long lastSaveTime = 0;
unsigned long lastDisplayUpdate = 0;
float totalEngineHours = 0;
float shift1Hours = 0;
float shift2Hours = 0;
uint8_t shift1Cutoff = 8;
uint8_t shift2Cutoff = 20;
int lastShiftResetDay = -1;
String shift1Name = "Shift1";
String shift2Name = "Shift2";

bool wasEngineRunning = false;

int lastShift1ResetDay = -1;
int lastShift2ResetDay = -1;

void saveEngineHours();  // ✅ Add this prototype

void loadSaveInterval() {
  unsigned long intervalSec;
  EEPROM.get(EEPROM_ADDR_INTERVAL, intervalSec);
  if (intervalSec == 0xFFFFFFFF || intervalSec == 0 || intervalSec > 86400) {
    intervalSec = 600; // fallback to default 600 seconds
  }
  SAVE_INTERVAL = intervalSec * 1000UL;
}

void showMessage(const String& message) {
  static bool visible = true;
  static unsigned long lastBlink = 0;
  const unsigned long blinkInterval = 500;  // milliseconds

  unsigned long now = millis();
  if (now - lastBlink >= blinkInterval) {
    visible = !visible;
    lastBlink = now;

    display.clearDisplay();
    display.fillScreen(SSD1306_WHITE);                 // White background

    if (visible) {
      display.setTextColor(SSD1306_BLACK);             // Black text
      display.setTextSize(2);

      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);

      int16_t x = (display.width() - w) / 2;
      int16_t y = (display.height() - h) / 2;
      display.setCursor(x, y);
      display.print(message);
    }

    display.display();
  }
}


bool isInShift1(uint8_t hour) {
  return (shift1Cutoff <= hour && hour < shift2Cutoff);
}

bool isInShift2(uint8_t hour) {
  return (hour < shift1Cutoff || hour >= shift2Cutoff);
}
void updateEngineHours() {
  unsigned long currentTime = rtc.now().unixtime();
  unsigned long elapsed = currentTime - engineStartTime;
  if (elapsed >= 1) {
    float hours = elapsed / 3600.0;
    totalEngineHours += hours;
    uint8_t currentHour = rtc.now().hour();
    if (isInShift1(currentHour)) shift1Hours += hours;
    else if (isInShift2(currentHour)) shift2Hours += hours;
    engineStartTime = currentTime;
  }
}

void startEngine() {
  engineRunning = true;
  engineStartTime = rtc.now().unixtime();
  lastSaveTime = millis();
}

void stopEngine() {
  if (engineRunning) {
    updateEngineHours();
    saveEngineHours();
    engineRunning = false;
    digitalWrite(LED_PIN, HIGH);
  }
}





void handleShiftReset(DateTime now) {
  int currentHour = now.hour();
  int currentDay = now.day();

  // --- Shift 1 ---
  if (currentHour >= shift1Cutoff && lastShift1ResetDay != currentDay) {
    shift1Hours = 0;
    EEPROM.put(SHIFT1_HOUR_ADDR, shift1Hours);
    EEPROM.write(LAST_SHIFT1_RESET_DAY_ADDR, currentDay);
    EEPROM.commit();
    lastShift1ResetDay = currentDay;
  }

  // --- Shift 2 ---
  if (currentHour >= shift2Cutoff && lastShift2ResetDay != currentDay) {
    shift2Hours = 0;
    EEPROM.put(SHIFT2_HOUR_ADDR, shift2Hours);
    EEPROM.write(LAST_SHIFT2_RESET_DAY_ADDR, currentDay);
    EEPROM.commit();
    lastShift2ResetDay = currentDay;
  }
}

void updateDisplay(DateTime now) {
  display.clearDisplay();

  // Line 1: "Running" (if running) and engine name
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (engineRunning) display.print("Running");
  display.setCursor(96, 0);
  display.print(engine_name);

  // Line 2: Big Total Hour in center
  display.setTextSize(2);
  char hourStr[12];
  sprintf(hourStr, "%010.2f", totalEngineHours);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(hourStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, 25);
  display.print(hourStr);

  // Last line:
  // Shift Hour (Big) + Time & Shift name (Small)
  bool inShift1 = isInShift1(now.hour());
  String shiftLabel = inShift1 ? shift1Name : shift2Name;
  float currentShiftHour = inShift1 ? shift1Hours : shift2Hours;

  // Show shift hour in big font
  display.setTextSize(2);
  char shiftHourStr[8];
  sprintf(shiftHourStr, "%.2f", currentShiftHour);
  display.setCursor(0, 48);
  display.print(shiftHourStr);
  display.print("h");

  // Show time and shift name in small font
  display.setTextSize(1);
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());
  display.setCursor(90, 54);  // adjust X for alignment if needed
  // display.setCursor(65, 48);
  display.print(timeStr);
  // display.print(" ");
  // display.print(shiftLabel);

  display.display();
}


void blinkLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  if (millis() - lastBlink >= 500) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    lastBlink = millis();
  }
}

void saveEngineHours() {
  EEPROM.put(ENGINE_HOURS_ADDR, totalEngineHours);
  EEPROM.put(SHIFT1_HOUR_ADDR, shift1Hours);
  EEPROM.put(SHIFT2_HOUR_ADDR, shift2Hours);
  EEPROM.commit();
}

float readFloat(int addr) {
  float val;
  EEPROM.get(addr, val);
  return (isnan(val) || val < 0) ? 0.0 : val;
}

String readName() {
  char buf[MAX_NAME_LENGTH + 1];
  for (int i = 0; i < MAX_NAME_LENGTH; i++) buf[i] = EEPROM.read(NAME_ADDR + i);
  buf[MAX_NAME_LENGTH] = '\0';
  return String(buf);
}

bool writeName(String name) {
  for (int i = 0; i < MAX_NAME_LENGTH; i++) EEPROM.write(NAME_ADDR + i, name[i]);
  bool ok = EEPROM.commit();
  engine_name = name;
  return ok;
}

String readShiftName(int addr) {
  char buf[MAX_NAME_LENGTH + 1];
  for (int i = 0; i < MAX_NAME_LENGTH; i++) buf[i] = EEPROM.read(addr + i);
  buf[MAX_NAME_LENGTH] = '\0';
  return String(buf);
}

void writeShiftName(int addr, String name) {
  for (int i = 0; i < MAX_NAME_LENGTH; i++) EEPROM.write(addr + i, name[i]);
  EEPROM.commit();
}

void clearEEPROM() {
  if (engineRunning) {
    Serial.println("Stop engine before clearing EEPROM");
    return;
  }
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0xFF);
  EEPROM.commit();
  totalEngineHours = 0;
  // dailyEngineHours = 0;
  Serial.println("EEPROM cleared and hours reset");
}

void set_time(String input) {
  input.remove(0, 8);
  int yyyy = input.substring(0, 4).toInt();
  int mm = input.substring(5, 7).toInt();
  int dd = input.substring(8, 10).toInt();
  int hh = input.substring(11, 13).toInt();
  int min = input.substring(14, 16).toInt();
  int ss = input.substring(17, 19).toInt();
  rtc.adjust(DateTime(yyyy, mm, dd, hh, min, ss));
  Serial.println("Time set.");
}

void show_time() {
  DateTime now = rtc.now();
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
}

void handleSerialCommand(String cmd) {
  if (cmd == "1") startEngine();
  else if (cmd == "0") stopEngine();
  else if (cmd == "hour") Serial.println(totalEngineHours, 2);
  else if (cmd.startsWith("sethour")) {
    totalEngineHours = cmd.substring(8).toFloat();
    EEPROM.put(ENGINE_HOURS_ADDR, totalEngineHours);
    EEPROM.commit();
    Serial.print("Set hour: "); Serial.println(totalEngineHours);
  }
  else if (cmd == "name") Serial.println(engine_name);
  else if (cmd.startsWith("setname")) writeName(cmd.substring(8, 18));
  else if (cmd == "cutoff") {
    Serial.printf("Shift1 cutoff: %02d\n", shift1Cutoff);
    Serial.printf("Shift2 cutoff: %02d\n", shift2Cutoff);
  }
  else if (cmd.startsWith("setcutoff1")) {
    shift1Cutoff = cmd.substring(11).toInt();
    EEPROM.write(SHIFT1_CUTOFF_ADDR, shift1Cutoff);
    EEPROM.commit();
    Serial.printf("Set shift1 cutoff: %02d\n", shift1Cutoff);
  }
  else if (cmd.startsWith("setcutoff2")) {
    shift2Cutoff = cmd.substring(11).toInt();
    EEPROM.write(SHIFT2_CUTOFF_ADDR, shift2Cutoff);
    EEPROM.commit();
    Serial.printf("Set shift2 cutoff: %02d\n", shift1Cutoff);
  }
  else if (cmd == "shifthour1") Serial.println(shift1Hours, 2);
  else if (cmd == "shifthour2") Serial.println(shift2Hours, 2);
  else if (cmd == "shiftname1") Serial.println(shift1Name);
  else if (cmd == "shiftname2") Serial.println(shift2Name);
  else if (cmd.startsWith("setshiftname1")) {
    shift1Name = cmd.substring(14, 24);
    writeShiftName(SHIFT1_NAME_ADDR, shift1Name);
    Serial.print("Set shift1 name: ");
    Serial.println(shift1Name);
  }
  else if (cmd.startsWith("setshiftname2")) {
    shift2Name = cmd.substring(14, 24);
    writeShiftName(SHIFT2_NAME_ADDR, shift2Name);
    Serial.print("Set shift2 name: ");
    Serial.println(shift2Name);
  }
  else if (cmd == "clear") clearEEPROM();
  else if (cmd == "time") show_time();
  else if (cmd.startsWith("settime")) set_time(cmd);
 else if (cmd == "interval") {
    Serial.printf("Current save interval: %lu seconds\n", SAVE_INTERVAL / 1000UL);
  }
  else if (cmd.startsWith("setinterval ")) {
    unsigned long sec = cmd.substring(12).toInt();
    if (sec > 0 && sec <= 86400) {
      SAVE_INTERVAL = sec * 1000UL;
      EEPROM.put(EEPROM_ADDR_INTERVAL, sec);
      EEPROM.commit();
      Serial.printf("Save interval set to: %lu seconds\n", sec);
    } else {
      Serial.println("Invalid value. Use 1–86400 seconds.");
    }
    }
    
  }

  void setup() {
    Serial.begin(115200);
    Wire.begin();
  
    pinMode(LED_PIN, OUTPUT);
    pinMode(ENGINE_INPUT_PIN, INPUT_PULLUP);
  
    EEPROM.begin(EEPROM_SIZE);
  
    loadSaveInterval();
  
    if (!rtc.begin()) {
      Serial.println("RTC not found!");
      rtcAvailable = false;
    }
  
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      Serial.println("SSD1306 OLED failed");
      while (1);
    }
  
    engine_name = readName();
    totalEngineHours = readFloat(ENGINE_HOURS_ADDR);
    shift1Cutoff = EEPROM.read(SHIFT1_CUTOFF_ADDR);
    shift2Cutoff = EEPROM.read(SHIFT2_CUTOFF_ADDR);
    shift1Hours = readFloat(SHIFT1_HOUR_ADDR);
    shift2Hours = readFloat(SHIFT2_HOUR_ADDR);
    lastShiftResetDay = EEPROM.read(LAST_SHIFT_RESET_DAY_ADDR);
    shift1Name = readShiftName(SHIFT1_NAME_ADDR);
    shift2Name = readShiftName(SHIFT2_NAME_ADDR);
  
    lastShift1ResetDay = EEPROM.read(LAST_SHIFT1_RESET_DAY_ADDR);//EEPROM.get(LAST_SHIFT1_RESET_DAY_ADDR, lastShift1ResetDay);
    lastShift2ResetDay = EEPROM.read(LAST_SHIFT2_RESET_DAY_ADDR);//EEPROM.get(LAST_SHIFT2_RESET_DAY_ADDR, lastShift2ResetDay);
  
  
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
  }


  void loop() {
    if (!rtcAvailable) {
      showMessage("No Clock");
      delay(1000);
      return;
    }
    DateTime now = rtc.now();
    handleShiftReset(now);
  
  
  
    bool engineSignal = digitalRead(ENGINE_INPUT_PIN) == LOW;
    if (engineSignal && !engineRunning) startEngine();
    else if (!engineSignal && engineRunning) {
      stopEngine();
    }
  
  
    if (engineRunning) {
      blinkLED();
      updateEngineHours();
      if (millis() - lastSaveTime >= SAVE_INTERVAL) {
        saveEngineHours();
        lastSaveTime = millis();
      }
    }
  
    if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
      updateDisplay(now);
      lastDisplayUpdate = millis();
    }
  
    if (Serial.available()) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      handleSerialCommand(cmd);
    }
  }