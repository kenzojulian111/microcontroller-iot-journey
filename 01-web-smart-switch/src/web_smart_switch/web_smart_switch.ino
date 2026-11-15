/**
 * ============================================================================
 * Proyek 01 : Web-Controlled Smart Switch (Local Web Server)
 * Repo      : IoT_lab
 * Board     : ESP32 & ESP8266 (Otomatis mendeteksi arsitektur board)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur:
 * 1. Kontrol 2 Channel Relay via Web Dashboard responsif (tanpa reload halaman).
 * 2. Mendukung kontrol tombol fisik (Push Button) dengan software debounce.
 * 3. Endpoint REST API JSON (/api/status, /api/toggle, /api/all).
 * 4. Kompatibel dengan ESP32 maupun ESP8266 (NodeMCU / Wemos D1 Mini).
 * ============================================================================
 */

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  WebServer server(80);
  #define BOARD_NAME "ESP32"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  ESP8266WebServer server(80);
  #define BOARD_NAME "ESP8266"
#else
  #error "Board tidak didukung! Pilih board ESP32 atau ESP8266 di Arduino IDE."
#endif

// Gunakan secrets.h jika tersedia, atau fallback ke Wi-Fi simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "Wokwi-GUEST"
  #define WIFI_PASSWORD ""
#endif

#include "webpage.h"

// ============================================================================
// Konfigurasi Pin Hardware (Otomatis menyesuaikan ESP32 vs ESP8266)
// ============================================================================
#if defined(ESP32)
  const int PIN_RELAY_1  = 26;  // GPIO26 untuk Relay Channel 1
  const int PIN_RELAY_2  = 27;  // GPIO27 untuk Relay Channel 2
  const int PIN_BUTTON_1 = 14;  // GPIO14 untuk Push Button 1 (INPUT_PULLUP)
  const int PIN_BUTTON_2 = 12;  // GPIO12 untuk Push Button 2 (INPUT_PULLUP)
  const int PIN_STATUS_LED = 2; // Built-in LED ESP32
#elif defined(ESP8266)
  const int PIN_RELAY_1  = 5;   // D1 (GPIO5) untuk Relay Channel 1
  const int PIN_RELAY_2  = 4;   // D2 (GPIO4) untuk Relay Channel 2
  const int PIN_BUTTON_1 = 14;  // D5 (GPIO14) untuk Push Button 1 (INPUT_PULLUP)
  const int PIN_BUTTON_2 = 12;  // D6 (GPIO12) untuk Push Button 2 (INPUT_PULLUP)
  const int PIN_STATUS_LED = LED_BUILTIN;
#endif

// Ubah ke `true` jika modul relay kamu Active-LOW (nyala saat diberi logika LOW).
// Untuk simulasi LED/Relay di Wokwi atau modul Active-HIGH, gunakan `false`.
const bool RELAY_ACTIVE_LOW = false;

// ============================================================================
// Variabel State & Debounce
// ============================================================================
bool relay1State = false;
bool relay2State = false;

bool lastBtn1Reading = HIGH;
bool lastBtn2Reading = HIGH;
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long DEBOUNCE_DELAY_MS = 50;

// ============================================================================
// Fungsi Helper Kontrol Relay
// ============================================================================
void applyRelayOutput(int pin, bool state) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, state ? LOW : HIGH);
  } else {
    digitalWrite(pin, state ? HIGH : LOW);
  }
}

void setRelayState(int channel, bool state) {
  if (channel == 1) {
    relay1State = state;
    applyRelayOutput(PIN_RELAY_1, relay1State);
    Serial.printf("[SWITCH] Relay 1 -> %s\n", relay1State ? "ON" : "OFF");
  } else if (channel == 2) {
    relay2State = state;
    applyRelayOutput(PIN_RELAY_2, relay2State);
    Serial.printf("[SWITCH] Relay 2 -> %s\n", relay2State ? "ON" : "OFF");
  }
}

// ============================================================================
// Pembuat Payload JSON Status
// ============================================================================
String buildStatusJson() {
  String json = "{";
  json += "\"board\":\"" + String(BOARD_NAME) + "\",";
  json += "\"relay1\":" + String(relay1State ? "true" : "false") + ",";
  json += "\"relay2\":" + String(relay2State ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap());
  json += "}";
  return json;
}

// ============================================================================
// HTTP Route Handlers
// ============================================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiStatus() {
  server.send(200, "application/json", buildStatusJson());
}

void handleApiToggle() {
  if (!server.hasArg("ch")) {
    server.send(400, "application/json", "{\"error\":\"Parameter 'ch' diperlukan\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  if (ch == 1) {
    setRelayState(1, !relay1State);
  } else if (ch == 2) {
    setRelayState(2, !relay2State);
  } else {
    server.send(400, "application/json", "{\"error\":\"Channel tidak valid (gunakan 1 atau 2)\"}");
    return;
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleApiAll() {
  if (!server.hasArg("state")) {
    server.send(400, "application/json", "{\"error\":\"Parameter 'state' diperlukan (0 atau 1)\"}");
    return;
  }
  bool targetState = (server.arg("state").toInt() == 1);
  setRelayState(1, targetState);
  setRelayState(2, targetState);
  server.send(200, "application/json", buildStatusJson());
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"Endpoint tidak ditemukan\"}");
}

// ============================================================================
// Pembacaan Tombol Fisik (Push Button Override)
// ============================================================================
void checkPhysicalButtons() {
  int reading1 = digitalRead(PIN_BUTTON_1);
  if (reading1 != lastBtn1Reading) {
    lastDebounceTime1 = millis();
  }
  if ((millis() - lastDebounceTime1) > DEBOUNCE_DELAY_MS) {
    static bool stableBtn1 = HIGH;
    if (reading1 != stableBtn1) {
      stableBtn1 = reading1;
      if (stableBtn1 == LOW) { // Tombol ditekan (Active-LOW)
        setRelayState(1, !relay1State);
      }
    }
  }
  lastBtn1Reading = reading1;

  int reading2 = digitalRead(PIN_BUTTON_2);
  if (reading2 != lastBtn2Reading) {
    lastDebounceTime2 = millis();
  }
  if ((millis() - lastDebounceTime2) > DEBOUNCE_DELAY_MS) {
    static bool stableBtn2 = HIGH;
    if (reading2 != stableBtn2) {
      stableBtn2 = reading2;
      if (stableBtn2 == LOW) { // Tombol ditekan (Active-LOW)
        setRelayState(2, !relay2State);
      }
    }
  }
  lastBtn2Reading = reading2;
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n============================================");
  Serial.println("  IoT Lab - 01 Web-Controlled Smart Switch  ");
  Serial.printf("  Target Board: %s\n", BOARD_NAME);
  Serial.println("============================================");

  // Inisialisasi Pin Output & Input
  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);

  // Pastikan semua relay mati saat startup
  setRelayState(1, false);
  setRelayState(2, false);

  // Koneksi ke Jaringan Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    delay(400);
    Serial.print(".");
  }

  // Nyalakan indikator terkoneksi
  digitalWrite(PIN_STATUS_LED, HIGH);
  Serial.println("\n[WIFI] Terhubung!");
  Serial.print("[WIFI] Alamat IP Web Server : http://");
  Serial.println(WiFi.localIP());
  Serial.printf("[WIFI] Kekuatan Sinyal      : %d dBm\n", WiFi.RSSI());

  // Registrasi Endpoint Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/toggle", HTTP_POST, handleApiToggle);
  server.on("/api/toggle", HTTP_GET, handleApiToggle); // Memudahkan tes dari browser bar
  server.on("/api/all", HTTP_POST, handleApiAll);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] Web Server aktif pada port 80.");
}

void loop() {
  server.handleClient();
  checkPhysicalButtons();
}
