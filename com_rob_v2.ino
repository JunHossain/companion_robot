#include <U8g2lib.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include "animations.h"
#include <WiFi.h>
#include <time.h>

const char* WIFI_SSID = "HONOR 200";
const char* WIFI_PASS = "gheugheu";

// --- Info screen ---
unsigned long infoScreenStart = 0;
bool showingInfoScreen = false;

// --- Hardware ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_BME280 bme;
const int BUTTON_PIN = 14;
const int BUZZER_PIN = 25;
const int TOUCH_PIN  = 27;
bool excitedSoundPlayed = false;

// --- Temperature thresholds ---
float HOT_THRESHOLD  = 31.0;
float COLD_THRESHOLD = 20.0;

// --- State definitions ---
enum FaceState { NEUTRAL, HAPPY, EXCITED, HOT, COLD, SAD, SLEEPY };
FaceState currentState   = NEUTRAL;
FaceState weatherState   = NEUTRAL;
FaceState displayedState = NEUTRAL;

// --- Timing ---
unsigned long pressStartTime      = 0;
unsigned long lastTempCheck       = 0;
unsigned long lastAnimFrame       = 0;
unsigned long excitedStartTime    = 0;
unsigned long lastInteractionTime = 0;
int  sadLoopCount = 0;
bool displayOff   = false;
bool buttonHeld   = false;
bool touchActive  = false;

// --- Animation ---
int animFrame = 0;
const int ANIM_INTERVAL = 133; // ms per frame

// ============================================================
//  BOOT SCREEN HELPER
// ============================================================
void showBootMessage(const char* line1, const char* line2 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  int w1 = u8g2.getStrWidth(line1);
  u8g2.drawStr((128 - w1) / 2, 28, line1);
  if (strlen(line2) > 0) {
    int w2 = u8g2.getStrWidth(line2);
    u8g2.drawStr((128 - w2) / 2, 44, line2);
  }
  u8g2.sendBuffer();
}

// ============================================================
//  WIFI + NTP SYNC (with OLED feedback)
// ============================================================
void syncTime() {
  showBootMessage("Connecting...");
  Serial.print("Connecting to WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    char line2[22];
    snprintf(line2, sizeof(line2), "%.21s", WIFI_SSID);
    showBootMessage("Connected to", line2);
    delay(1500);

    configTime(6 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    Serial.print("Waiting for NTP sync");
    struct tm timeinfo;
    int syncTries = 0;
    while (!getLocalTime(&timeinfo) && syncTries < 10) {
      delay(500);
      Serial.print(".");
      syncTries++;
    }
    Serial.println(getLocalTime(&timeinfo) ? " Synced!" : " Timeout.");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

  } else {
    Serial.println(" Failed.");
    showBootMessage("Couldn't connect", "to WiFi");
    delay(2000);
  }
}

// ============================================================
//  INFO SCREEN — button only
// ============================================================
void drawInfoScreen() {
  struct tm timeinfo;
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_logisoso16_tf);
  if (getLocalTime(&timeinfo)) {
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    int tw = u8g2.getStrWidth(timeBuf);
    u8g2.drawStr((128 - tw) / 2, 26, timeBuf);
  } else {
    u8g2.drawStr(40, 26, "--:--");
  }

  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  char infoBuf[20];
  snprintf(infoBuf, sizeof(infoBuf), "%.1fC  %.0f%%", temp, hum);
  u8g2.setFont(u8g2_font_6x10_tf);
  int sw = u8g2.getStrWidth(infoBuf);
  u8g2.drawStr((128 - sw) / 2, 50, infoBuf);
  u8g2.sendBuffer();
}

// ============================================================
//  FACE FUNCTIONS
// ============================================================
void drawNeutral(int frame) {
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, neutralFrames + (frame % NEUTRAL_FRAME_COUNT) * 1024);
  u8g2.sendBuffer();
}

void drawHappy(int frame) {
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, happyFrames + (frame % HAPPY_FRAME_COUNT) * 1024);
  u8g2.sendBuffer();
}

void drawExcited(int frame) {
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, excitedFrames + (frame % EXCITED_FRAME_COUNT) * 1024);
  u8g2.sendBuffer();
}

void drawSad(int frame) {
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, sadFrames + (frame % SAD_FRAME_COUNT) * 1024);
  u8g2.sendBuffer();
}

void drawSleepy(int frame) {
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, sleepyFrames + (frame % SLEEPY_FRAME_COUNT) * 1024);
  u8g2.sendBuffer();
}

void drawHot(int frame) {
  u8g2.clearBuffer();
  u8g2.drawLine(28, 20, 44, 16);
  u8g2.drawLine(84, 16, 100, 20);
  u8g2.drawCircle(36, 30, 8);
  u8g2.setDrawColor(0); u8g2.drawBox(26, 20, 22, 10); u8g2.setDrawColor(1);
  u8g2.drawLine(26, 28, 48, 28);
  u8g2.drawCircle(92, 30, 8);
  u8g2.setDrawColor(0); u8g2.drawBox(82, 20, 22, 10); u8g2.setDrawColor(1);
  u8g2.drawLine(82, 28, 104, 28);
  u8g2.drawPixel(62, 38); u8g2.drawPixel(66, 38);
  u8g2.drawLine(44, 52, 84, 52);
  u8g2.drawLine(44, 52, 46, 54); u8g2.drawLine(84, 52, 82, 54);
  int dropY = (frame % 2 == 0) ? 10 : 16;
  u8g2.drawPixel(52, dropY); u8g2.drawLine(50, dropY+2, 54, dropY+2); u8g2.drawPixel(52, dropY+4);
  u8g2.drawPixel(76, dropY); u8g2.drawLine(74, dropY+2, 78, dropY+2); u8g2.drawPixel(76, dropY+4);
  u8g2.sendBuffer();
}

void drawCold(int frame) {
  u8g2.clearBuffer();
  int shake = (frame % 2 == 0) ? -2 : 2;
  u8g2.drawLine(28+shake, 12, 36+shake, 16); u8g2.drawLine(36+shake, 16, 44+shake, 12);
  u8g2.drawLine(84+shake, 12, 92+shake, 16); u8g2.drawLine(92+shake, 16, 100+shake, 12);
  u8g2.drawCircle(36+shake, 28, 8); u8g2.drawCircle(92+shake, 28, 8);
  u8g2.drawDisc(36+shake, 28, 3);   u8g2.drawDisc(92+shake, 28, 3);
  u8g2.drawPixel(62+shake, 36); u8g2.drawPixel(66+shake, 36);
  if (frame % 2 == 0) {
    u8g2.drawLine(44+shake, 50, 84+shake, 50); u8g2.drawLine(44+shake, 54, 84+shake, 54);
    u8g2.drawLine(54+shake, 50, 54+shake, 54); u8g2.drawLine(64+shake, 50, 64+shake, 54);
    u8g2.drawLine(74+shake, 50, 74+shake, 54);
  } else {
    u8g2.drawLine(44+shake, 48, 84+shake, 48); u8g2.drawLine(44+shake, 56, 84+shake, 56);
    u8g2.drawLine(54+shake, 48, 54+shake, 56); u8g2.drawLine(64+shake, 48, 64+shake, 56);
    u8g2.drawLine(74+shake, 48, 74+shake, 56);
  }
  u8g2.drawPixel(8+shake, 20); u8g2.drawLine(6+shake, 22, 10+shake, 22); u8g2.drawPixel(8+shake, 24);
  u8g2.drawPixel(118+shake, 36); u8g2.drawLine(116+shake, 38, 120+shake, 38); u8g2.drawPixel(118+shake, 40);
  u8g2.sendBuffer();
}

// ============================================================
//  STATE MACHINE
// ============================================================
void showFace(FaceState state, int frame = 0) {
  switch (state) {
    case NEUTRAL: drawNeutral(frame); break;
    case HAPPY:   drawHappy(frame);   break;
    case EXCITED: drawExcited(frame); break;
    case HOT:     drawHot(frame);     break;
    case COLD:    drawCold(frame);    break;
    case SAD:     drawSad(frame);     break;
    case SLEEPY:  drawSleepy(frame);  break;
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);

  u8g2.begin();
  u8g2.setDrawColor(1);

  syncTime();

  if (!bme.begin(0x76)) {
    Serial.println("BME280 not found! Check wiring.");
    while (1);
  }
  Serial.println("BME280 ready.");
  Serial.println("Type 't' to read temp & humidity.");

  showFace(NEUTRAL);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // --- Serial debug ---
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 't') {
      float temp = bme.readTemperature();
      float hum  = bme.readHumidity();
      Serial.print("Temp: ");     Serial.print(temp); Serial.print(" C  |  ");
      Serial.print("Humidity: "); Serial.print(hum);  Serial.println(" %");
    }
  }

  // --- Temperature check every 5s ---
  // BUG FIX 3: skip if SAD or SLEEPY so temp doesn't stomp the sad/sleep progression
  if (now - lastTempCheck > 5000) {
    lastTempCheck = now;
    if (weatherState != SAD && weatherState != SLEEPY) {
      float temp = bme.readTemperature();
      if      (temp >= HOT_THRESHOLD)  weatherState = HOT;
      else if (temp <= COLD_THRESHOLD) weatherState = COLD;
      else                             weatherState = NEUTRAL;
    }
  }

  // --- SAD idle trigger ---
  // BUG FIX 2: guard against overriding SLEEPY
  if (now - lastInteractionTime > 30000 && currentState == NEUTRAL && weatherState != SLEEPY) {
    weatherState = SAD;
  } else if (weatherState == SAD && (now - lastInteractionTime <= 30000 || currentState != NEUTRAL)) {
    // Interaction happened or emotion overriding — clear SAD
    weatherState = NEUTRAL;
    sadLoopCount = 0;
  }

  // --- Wake from sleep ---
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);
  bool touchPressed  = (digitalRead(TOUCH_PIN) == HIGH);

  if (displayOff) {
    if (buttonPressed || touchPressed) {
      displayOff = false;
      sadLoopCount = 0;
      lastInteractionTime = now;
      u8g2.setPowerSave(0);
      currentState = NEUTRAL;
      weatherState = NEUTRAL;
    }
    return;
  }

  if (touchPressed) Serial.println("Touch detected!");

  // --- Update interaction time ---
  if (buttonPressed || touchPressed) lastInteractionTime = now;
  if (showingInfoScreen)             lastInteractionTime = now;

  // ── BUTTON: info screen only ────────────────────────────────
  if (buttonPressed) {
    if (!buttonHeld) {
      buttonHeld = true;
      showingInfoScreen = true;
      infoScreenStart = now;
      drawInfoScreen();
    }
  } else {
    buttonHeld = false;
    if (showingInfoScreen && now - infoScreenStart >= 3000) {
      showingInfoScreen = false;
      displayedState = (FaceState)-1;
    }
  }

  // ── TOUCH: emotions only ────────────────────────────────────
  if (touchPressed) {
    if (!touchActive) {
      touchActive = true;
      pressStartTime = now;
      if (currentState != EXCITED) {
        currentState = HAPPY;
        sadLoopCount = 0; // BUG FIX 4: reset sad progress if interrupted mid-way
      }
    } else if (now - pressStartTime >= 3000 && currentState != EXCITED) {
      currentState = EXCITED;
      excitedSoundPlayed = false;
      excitedStartTime = now;
    }
  } else {
    if (touchActive) {
      touchActive = false;
      if (currentState == HAPPY) {
        currentState = NEUTRAL;
      }
    }
  }

  // --- Excited auto-return after 1 full loop ---
  if (currentState == EXCITED) {
    unsigned long excitedDuration = 1UL * EXCITED_FRAME_COUNT * ANIM_INTERVAL;
    if (now - excitedStartTime >= excitedDuration) {
      currentState = NEUTRAL;
      animFrame = 0;
    }
  }

  // --- Decide what face to display ---
  FaceState toDisplay;
  if (currentState == HAPPY || currentState == EXCITED) {
    toDisplay = currentState;
  } else {
    toDisplay = weatherState;
  }

  // --- SLEEPY: play 1 full loop then turn display off ---
  if (toDisplay == SLEEPY && animFrame >= SLEEPY_FRAME_COUNT) {
    displayOff = true;
    weatherState = NEUTRAL;
    u8g2.setPowerSave(1);
    animFrame = 0;
    return;
  }

  // --- Excited sound (plays once on entry) ---
  if (toDisplay == EXCITED && !excitedSoundPlayed) {
    excitedSoundPlayed = true;
    int melody[]    = {523, 659, 784, 1047, 784, 1047, 1319};
    int durations[] = {100, 100, 100,  150, 100,  100,  200};
    for (int i = 0; i < 7; i++) {
      tone(BUZZER_PIN, melody[i], durations[i]);
      delay(durations[i] + 30);
    }
    noTone(BUZZER_PIN);
  } else if (toDisplay != EXCITED) {
    excitedSoundPlayed = false;
  }

  // --- Animation tick ---
  if (!showingInfoScreen) {
    if (now - lastAnimFrame > ANIM_INTERVAL) {
      lastAnimFrame = now;
      animFrame++;
      showFace(toDisplay, animFrame);

      // BUG FIX 1: SAD loop counter is INSIDE the tick — fires once per frame, not per loop()
      if (toDisplay == SAD && animFrame % SAD_FRAME_COUNT == 0) {
        sadLoopCount++;
        Serial.print("Sad loop: "); Serial.println(sadLoopCount);
        if (sadLoopCount >= 5) {
          sadLoopCount = 0;
          weatherState = SLEEPY;
          animFrame = 0;
        }
      }
    }
    displayedState = toDisplay;
  }
}
