/**
 * ============================================================================
 * Project 23: Smart Access Control & SQLAlchemy Attendance System
 * File: rfid_attendance_node.ino
 * Target MCU: ESP32 (DOIT ESP32 DEVKIT V1) / ESP8266 (NodeMCU v3)
 * ============================================================================
 *
 * HARDWARE ARCHITECTURE:
 *   - RFID Transceiver : MFRC522 13.56 MHz Contactless Reader (SPI Bus)
 *                        SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23, SS=GPIO5, RST=GPIO27
 *   - Local HMI Display: SSD1306 128x64 Monochrome OLED (I2C Bus: SDA=21, SCL=22)
 *   - Door Strike Relay: Active-HIGH Opto-Isolated Relay Module (GPIO26)
 *   - Acoustic Feedback: Piezoelectric Buzzer (GPIO25)
 *   - Status LEDs      : Green Granted LED (GPIO32), Red Denied LED (GPIO33)
 *   - Wokwi Simulation :
 *       * Button 1 (GPIO14, INPUT_PULLUP) -> Simulates Authorized RFID Card Tap
 *       * Button 2 (GPIO12, INPUT_PULLUP) -> Simulates Unregistered RFID Card Tap
 *
 * PROTOCOL & FALLBACK FLOW:
 *   1. Polls MFRC522 ISO/IEC 14443A PICC or Wokwi simulation pushbuttons.
 *   2. Applies a 2500 ms non-blocking debounce cooldown per UID.
 *   3. Transmits an HTTP POST JSON payload to `/api/v1/attendance/punch`:
 *        {"uid": "04:A3:9B:12", "terminal_id": "GATE-01", "rssi": -54}
 *   4. If HTTP transport fails (or in offline simulation), falls back to a
 *      deterministic Serial/Offline parser so Wokwi users can test the full
 *      OLED + Relay + Buzzer state machine without an external bridge.
 *   5. Renders employee name, role, action (CHECK_IN / CHECK_OUT / DENIED),
 *      and timestamp on the SSD1306 OLED, energizes the door strike relay for
 *      3000 ms on authorization, and plays multi-tone acoustic feedback.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#endif

// Optional MFRC522 hardware inclusion (gracefully handled if running in Wokwi without MFRC522 library)
#if __has_include(<MFRC522.h>)
  #include <MFRC522.h>
  #define HAS_MFRC522_LIB 1
#else
  #define HAS_MFRC522_LIB 0
#endif

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

// ============================================================================
// Pin Assignments (ESP32 Standard Layout)
// ============================================================================
constexpr uint8_t PIN_I2C_SDA        = 21;
constexpr uint8_t PIN_I2C_SCL        = 22;

constexpr uint8_t PIN_RFID_SS        = 5;
constexpr uint8_t PIN_RFID_RST       = 27;

constexpr uint8_t PIN_RELAY_LOCK     = 26;
constexpr uint8_t PIN_BUZZER         = 25;
constexpr uint8_t PIN_LED_GRANTED    = 32;
constexpr uint8_t PIN_LED_DENIED     = 33;

// Simulated RFID Trigger Buttons for Wokwi / Bench Testing
constexpr uint8_t PIN_BTN_VALID_UID  = 14;
constexpr uint8_t PIN_BTN_ROGUE_UID  = 12;

// ============================================================================
// Constants & Timing Parameters
// ============================================================================
constexpr uint16_t OLED_WIDTH        = 128;
constexpr uint16_t OLED_HEIGHT       = 64;
constexpr int8_t   OLED_RESET        = -1;
constexpr uint8_t  OLED_I2C_ADDR     = 0x3C;

constexpr uint32_t CARD_COOLDOWN_MS  = 2500;
constexpr uint32_t RELAY_UNLOCK_MS   = 3000;
constexpr uint32_t SCREEN_RESET_MS   = 4500;
constexpr uint32_t HTTP_TIMEOUT_MS   = 2500;

// Pre-provisioned simulation UIDs matching backend seed database
const char* SIM_VALID_UIDS[] = {
  "04:A3:9B:12",  // Kenzo Julian - Lead Systems Engineer
  "A1:7F:44:C8",  // Elena Vance  - Embedded Security Architect
  "7B:2E:90:55"   // Marcus Chen  - Firmware QA Specialist
};
constexpr size_t SIM_VALID_COUNT = sizeof(SIM_VALID_UIDS) / sizeof(SIM_VALID_UIDS[0]);
const char* SIM_ROGUE_UID = "DE:AD:BE:EF";

// ============================================================================
// Peripheral Instances & State Tracking
// ============================================================================
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

#if HAS_MFRC522_LIB
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);
#endif

struct PunchResponse {
  bool        allowed;
  String      action;     // "CHECK_IN", "CHECK_OUT", or "DENIED"
  String      user;
  String      role;
  String      timestamp;
  String      shiftHours;
};

uint32_t lastReadTimestamp   = 0;
uint32_t relayUnlockDeadline = 0;
uint32_t screenResetDeadline = 0;
bool     relayActive         = false;
bool     showingResultScreen = false;
size_t   simUidIndex         = 0;
bool     simToggleCheckOut   = false;

// ============================================================================
// Acoustic Feedback Helper Routines
// ============================================================================
void playTone(uint16_t freqHz, uint16_t durationMs) {
#if defined(ESP32)
  tone(PIN_BUZZER, freqHz, durationMs);
  delay(durationMs);
  noTone(PIN_BUZZER);
#else
  tone(PIN_BUZZER, freqHz, durationMs);
  delay(durationMs);
#endif
}

void playGrantedChime(bool isCheckOut) {
  if (!isCheckOut) {
    // Ascending two-tone chime for CHECK_IN
    playTone(1760, 90);
    delay(25);
    playTone(2637, 160);
  } else {
    // Descending warm chime for CHECK_OUT
    playTone(2349, 90);
    delay(25);
    playTone(1760, 160);
  }
}

void playDeniedAlarm() {
  // Low-frequency double buzz for unauthorized UID
  playTone(330, 180);
  delay(60);
  playTone(262, 260);
}

// ============================================================================
// Lightweight JSON Value Extractor (Zero Heap-Fragmentation Parser)
// ============================================================================
String extractJsonString(const String& json, const char* key, const char* defaultVal = "") {
  String pattern = String("\"") + key + "\":";
  int keyIdx = json.indexOf(pattern);
  if (keyIdx < 0) return String(defaultVal);

  int valStart = keyIdx + pattern.length();
  while (valStart < (int)json.length() && (json[valStart] == ' ' || json[valStart] == '\t')) {
    valStart++;
  }
  if (valStart >= (int)json.length()) return String(defaultVal);

  if (json[valStart] == '"') {
    valStart++;
    int valEnd = json.indexOf('"', valStart);
    if (valEnd > valStart) {
      return json.substring(valStart, valEnd);
    }
  } else {
    int valEnd = valStart;
    while (valEnd < (int)json.length() &&
           json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != ' ') {
      valEnd++;
    }
    return json.substring(valStart, valEnd);
  }
  return String(defaultVal);
}

// ============================================================================
// OLED Graphical User Interface Routines
// ============================================================================
void renderStandbyScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header Banner
  display.fillRect(0, 0, OLED_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 3);
  display.print("GATE-01 ACCESS CTRL");

  // Network indicator
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("Net: ");
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP());
  } else {
    display.print("LOCAL/SIM MODE");
  }

  // Center prompt box
  display.drawRoundRect(6, 33, 116, 28, 4, SSD1306_WHITE);
  display.setCursor(16, 39);
  display.print("TAP RFID BADGE");
  display.setCursor(18, 49);
  display.print("OR PRESS BTN 1/2");

  display.display();
}

void renderScanningScreen(const String& uid) {
  display.clearDisplay();
  display.fillRect(0, 0, OLED_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(6, 3);
  display.print("VERIFYING CREDENTIAL");

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print("UID: ");
  display.println(uid);

  display.setCursor(0, 38);
  display.println("Querying SQLAlchemy");
  display.setCursor(0, 48);
  display.println("ORM Backend...");
  display.display();
}

void renderPunchResultScreen(const String& uid, const PunchResponse& res) {
  display.clearDisplay();

  // Top Invert Banner
  display.fillRect(0, 0, OLED_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 3);

  if (res.allowed) {
    display.print("GRANTED: ");
    display.print(res.action);
  } else {
    display.print("ACCESS DENIED!");
  }

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 18);
  display.print("Name: ");
  display.println(res.user.substring(0, 15));

  display.setCursor(0, 29);
  display.print("Role: ");
  display.println(res.role.substring(0, 15));

  display.setCursor(0, 40);
  display.print("UID : ");
  display.println(uid);

  display.drawFastHLine(0, 51, OLED_WIDTH, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.print("T:");
  display.print(res.timestamp);
  if (res.shiftHours.length() > 0 && res.action == "CHECK_OUT") {
    display.print(" (");
    display.print(res.shiftHours);
    display.print("h)");
  } else if (res.allowed) {
    display.print(" LOCK:OPEN");
  } else {
    display.print(" LOCK:SHUT");
  }

  display.display();
}

// ============================================================================
// HTTP REST Client + Offline Deterministic Fallback
// ============================================================================
PunchResponse submitAttendancePunch(const String& uid) {
  PunchResponse result;
  result.allowed    = false;
  result.action     = "DENIED";
  result.user       = "Unknown Badge";
  result.role       = "Unauthorized";
  result.timestamp  = "00:00:00";
  result.shiftHours = "";

  // Construct JSON Request Payload
  int32_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -48;
  String payload = String("{\"uid\":\"") + uid +
                   "\",\"terminal_id\":\"" + TERMINAL_ID +
                   "\",\"rssi\":" + String(rssi) + "}";

  Serial.print(F("[TX -> FLASK] POST "));
  Serial.print(PUNCH_ENDPOINT);
  Serial.print(F(" | Payload: "));
  Serial.println(payload);

  bool httpHandled = false;

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    String url = String("http://") + FLASK_HOST + ":" + String(FLASK_PORT) + PUNCH_ENDPOINT;

    if (http.begin(client, url)) {
      http.setTimeout(HTTP_TIMEOUT_MS);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-Terminal-Key", API_SHARED_KEY);

      int httpCode = http.POST(payload);
      if (httpCode > 0) {
        String responseBody = http.getString();
        Serial.printf("[RX <- FLASK] HTTP %d: %s\n", httpCode, responseBody.c_str());

        String statusStr = extractJsonString(responseBody, "status", "denied");
        result.allowed    = (statusStr == "allowed");
        result.action     = extractJsonString(responseBody, "action", result.allowed ? "CHECK_IN" : "DENIED");
        result.user       = extractJsonString(responseBody, "user", "Unknown");
        result.role       = extractJsonString(responseBody, "role", "N/A");
        result.timestamp  = extractJsonString(responseBody, "timestamp", "08:59:12");
        result.shiftHours = extractJsonString(responseBody, "shift_hours", "");
        httpHandled       = true;
      } else {
        Serial.printf("[WARN] HTTP POST failed (%s). Engaging local edge verifier.\n",
                      http.errorToString(httpCode).c_str());
      }
      http.end();
    }
  }

  // Fallback simulation response when Flask server is unreachable (e.g. standalone Wokwi browser run)
  if (!httpHandled) {
    uint32_t upSec = millis() / 1000;
    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "08:%02lu:%02lu", (upSec / 60) % 60, upSec % 60);
    result.timestamp = String(timeBuf);

    if (uid == "04:A3:9B:12") {
      result.allowed = true;
      result.user    = "Kenzo Julian";
      result.role    = "Lead Engineer";
    } else if (uid == "A1:7F:44:C8") {
      result.allowed = true;
      result.user    = "Elena Vance";
      result.role    = "Sec Architect";
    } else if (uid == "7B:2E:90:55") {
      result.allowed = true;
      result.user    = "Marcus Chen";
      result.role    = "Firmware QA";
    }

    if (result.allowed) {
      result.action = simToggleCheckOut ? "CHECK_OUT" : "CHECK_IN";
      if (simToggleCheckOut) {
        result.shiftHours = "8.42";
      }
      simToggleCheckOut = !simToggleCheckOut;
    }

    Serial.print(F("[SIMULATED FLASK RESPONSE] "));
    Serial.printf("{\"status\":\"%s\",\"action\":\"%s\",\"user\":\"%s\",\"role\":\"%s\",\"timestamp\":\"%s\"}\n",
                  result.allowed ? "allowed" : "denied",
                  result.action.c_str(),
                  result.user.c_str(),
                  result.role.c_str(),
                  result.timestamp.c_str());
  }

  return result;
}

// ============================================================================
// Card Processing & Actuator State Machine
// ============================================================================
void processCardTap(const String& uid) {
  lastReadTimestamp = millis();
  renderScanningScreen(uid);

  PunchResponse res = submitAttendancePunch(uid);
  renderPunchResultScreen(uid, res);

  showingResultScreen = true;
  screenResetDeadline = millis() + SCREEN_RESET_MS;

  if (res.allowed) {
    digitalWrite(PIN_LED_GRANTED, HIGH);
    digitalWrite(PIN_LED_DENIED, LOW);
    digitalWrite(PIN_RELAY_LOCK, HIGH);
    relayActive         = true;
    relayUnlockDeadline = millis() + RELAY_UNLOCK_MS;
    playGrantedChime(res.action == "CHECK_OUT");
  } else {
    digitalWrite(PIN_LED_GRANTED, LOW);
    digitalWrite(PIN_LED_DENIED, HIGH);
    digitalWrite(PIN_RELAY_LOCK, LOW);
    relayActive = false;
    playDeniedAlarm();
  }
}

// ============================================================================
// Hardware Initialization
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n================================================================"));
  Serial.println(F("Project 23: Smart RFID Access Control & SQLAlchemy Attendance"));
  Serial.println(F("================================================================"));

  pinMode(PIN_RELAY_LOCK, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GRANTED, OUTPUT);
  pinMode(PIN_LED_DENIED, OUTPUT);
  pinMode(PIN_BTN_VALID_UID, INPUT_PULLUP);
  pinMode(PIN_BTN_ROGUE_UID, INPUT_PULLUP);

  digitalWrite(PIN_RELAY_LOCK, LOW);
  digitalWrite(PIN_LED_GRANTED, LOW);
  digitalWrite(PIN_LED_DENIED, LOW);

  // Initialize I2C Bus & SSD1306 OLED
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("[ERR] SSD1306 OLED allocation failed on I2C 0x3C"));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Booting GATE-01...");
    display.display();
  }

  // Initialize SPI Bus & MFRC522 Reader
  SPI.begin();
#if HAS_MFRC522_LIB
  mfrc522.PCD_Init();
  Serial.println(F("[INFO] MFRC522 SPI Transceiver Initialized."));
#else
  Serial.println(F("[INFO] MFRC522 library not linked; using Pushbutton/Serial UID injection."));
#endif

  // Connect to Wi-Fi with non-blocking timeout so offline simulation never hangs
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to SSID: %s ", WIFI_SSID);

  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 4000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WIFI] Connected! IPv4: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[WIFI] No AP link; running with deterministic edge fallback."));
  }

  renderStandbyScreen();
  Serial.println(F("[READY] Tap an ISO14443A tag, press BTN1 (Valid) / BTN2 (Rogue), or send 'UID:04:A3:9B:12' via Serial."));
}

// ============================================================================
// Main Non-Blocking Event Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // 1. De-energize solenoid door lock relay after unlock window expires
  if (relayActive && (int32_t)(now - relayUnlockDeadline) >= 0) {
    digitalWrite(PIN_RELAY_LOCK, LOW);
    digitalWrite(PIN_LED_GRANTED, LOW);
    relayActive = false;
    Serial.println(F("[LOCK] Solenoid strike re-engaged (LOCKED)."));
  }

  // 2. Restore standby OLED screen after result display window expires
  if (showingResultScreen && (int32_t)(now - screenResetDeadline) >= 0) {
    digitalWrite(PIN_LED_GRANTED, LOW);
    digitalWrite(PIN_LED_DENIED, LOW);
    showingResultScreen = false;
    renderStandbyScreen();
  }

  // Enforce debounce cooldown between consecutive card reads
  if ((now - lastReadTimestamp) < CARD_COOLDOWN_MS) {
    return;
  }

  // 3. Check Wokwi Pushbutton 1 (Simulated Authorized Employee Card Tap)
  if (digitalRead(PIN_BTN_VALID_UID) == LOW) {
    String uid = String(SIM_VALID_UIDS[simUidIndex]);
    simUidIndex = (simUidIndex + 1) % SIM_VALID_COUNT;
    Serial.printf("\n[EVENT] Simulated Authorized RFID Tap -> UID: %s\n", uid.c_str());
    processCardTap(uid);
    return;
  }

  // 4. Check Wokwi Pushbutton 2 (Simulated Unregistered Card Tap)
  if (digitalRead(PIN_BTN_ROGUE_UID) == LOW) {
    String uid = String(SIM_ROGUE_UID);
    Serial.printf("\n[EVENT] Simulated Unauthorized RFID Tap -> UID: %s\n", uid.c_str());
    processCardTap(uid);
    return;
  }

  // 5. Check UART Serial Command Injection (e.g., `UID:04:A3:9B:12`)
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("UID:")) {
      String uid = line.substring(4);
      uid.trim();
      uid.toUpperCase();
      if (uid.length() > 0) {
        processCardTap(uid);
        return;
      }
    }
  }

  // 6. Poll Physical MFRC522 SPI Transceiver if present
#if HAS_MFRC522_LIB
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uidStr = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (i > 0) uidStr += ":";
      if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
      uidStr += String(mfrc522.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    Serial.printf("\n[MFRC522] ISO14443A Tag Detected -> UID: %s\n", uidStr.c_str());
    processCardTap(uidStr);
  }
#endif
}
