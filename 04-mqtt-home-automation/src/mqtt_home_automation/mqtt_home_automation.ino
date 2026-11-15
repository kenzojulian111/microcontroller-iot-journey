/**
 * ============================================================================
 * Proyek 04 : MQTT Home Automation Hub
 * Repo      : IoT_lab
 * Board     : ESP32 & ESP8266 (Otomatis mendeteksi arsitektur board)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Arsitektur Publish-Subscribe menggunakan protokol MQTT (PubSubClient).
 * 2. Non-blocking Auto-Reconnect untuk Wi-Fi dan MQTT Broker.
 * 3. Last Will and Testament (LWT) pada topik `home/node01/status`
 *    ("online" / "offline" dengan flag Retained = true).
 * 4. Publikasi telemetri berkala (Suhu & Kelembapan DHT22, Sensor Gerak PIR,
 *    Status Relay, RSSI Wi-Fi, Uptime) dalam format JSON ke `home/node01/telemetry`.
 * 5. Deteksi gerakan instan (Event-Driven PIR) dan kontrol Relay jarak jauh
 *    melalui topik `home/node01/relay/set` (mendukung payload "ON", "OFF",
 *    "TOGGLE", maupun objek JSON `{"state":"ON"}`).
 * 6. Dilengkapi tombol fisik lokal (Manual Override) yang otomatis
 *    menyinkronkan perubahan state ke topik `home/node01/relay/state`.
 *
 * Library yang dibutuhkan di Arduino IDE:
 * - PubSubClient (oleh Nick O'Leary)
 * - DHT sensor library (oleh Adafruit) + Adafruit Unified Sensor
 * ============================================================================
 */

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #define BOARD_NAME "ESP32"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #define BOARD_NAME "ESP8266"
#else
  #error "Board tidak didukung! Pilih board ESP32 atau ESP8266 di Arduino IDE."
#endif

#include <PubSubClient.h>
#include <DHT.h>

// Gunakan secrets.h jika tersedia, atau fallback ke konfigurasi simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "Wokwi-GUEST"
  #define WIFI_PASSWORD ""
  #define MQTT_BROKER   "broker.emqx.io"
  #define MQTT_PORT     1883
  #define MQTT_USER     ""
  #define MQTT_PASSWORD ""
  #define NODE_ID       "node01"
#endif

// ============================================================================
// Konfigurasi Pin Hardware (Otomatis menyesuaikan ESP32 vs ESP8266)
// ============================================================================
#if defined(ESP32)
  const int PIN_DHT        = 4;   // GPIO4  - Data Sensor DHT22
  const int PIN_PIR        = 27;  // GPIO27 - Output Sensor Gerak PIR HC-SR501
  const int PIN_RELAY      = 26;  // GPIO26 - Kontrol Modul Relay
  const int PIN_BUTTON     = 14;  // GPIO14 - Push Button Fisik (INPUT_PULLUP)
  const int PIN_STATUS_LED = 2;   // GPIO2  - Indikator LED Onboard ESP32
#elif defined(ESP8266)
  const int PIN_DHT        = 2;   // D4 (GPIO2)  - Data Sensor DHT22
  const int PIN_PIR        = 12;  // D6 (GPIO12) - Output Sensor Gerak PIR
  const int PIN_RELAY      = 5;   // D1 (GPIO5)  - Kontrol Modul Relay
  const int PIN_BUTTON     = 14;  // D5 (GPIO14) - Push Button Fisik (INPUT_PULLUP)
  const int PIN_STATUS_LED = LED_BUILTIN;
#endif

#define DHT_TYPE DHT22

// Ubah ke `true` jika modul relay fisik bersifat Active-LOW.
// Untuk simulasi Wokwi atau modul relay Active-HIGH, gunakan `false`.
const bool RELAY_ACTIVE_LOW = false;

// ============================================================================
// Topik MQTT (Mengikuti konvensi hierarki `home/<node_id>/...`)
// ============================================================================
const char* TOPIC_STATUS      = "home/" NODE_ID "/status";      // LWT: "online" / "offline"
const char* TOPIC_TELEMETRY   = "home/" NODE_ID "/telemetry";   // JSON Telemetri Berkala
const char* TOPIC_RELAY_SET   = "home/" NODE_ID "/relay/set";   // Subscribe Perintah Relay
const char* TOPIC_RELAY_STATE = "home/" NODE_ID "/relay/state"; // Publish State Relay ("ON"/"OFF")
const char* TOPIC_MOTION      = "home/" NODE_ID "/motion";      // Publish Event PIR ("DETECTED"/"CLEAR")

// ============================================================================
// Objek Global & Variabel State
// ============================================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
DHT dht(PIN_DHT, DHT_TYPE);

bool relayState = false;
bool motionDetected = false;
bool lastMotionState = false;
float lastTempC = NAN;
float lastHumidity = NAN;

// Variabel Non-Blocking Timers
unsigned long lastTelemetryMs     = 0;
unsigned long lastMqttRetryMs     = 0;
unsigned long lastWifiCheckMs     = 0;
unsigned long lastDebounceMs      = 0;

const unsigned long TELEMETRY_INTERVAL_MS  = 5000; // Kirim telemetri setiap 5 detik
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000; // Coba reconnect MQTT setiap 5 detik
const unsigned long WIFI_CHECK_INTERVAL_MS = 5000; // Cek status Wi-Fi setiap 5 detik
const unsigned long DEBOUNCE_DELAY_MS      = 50;

bool lastBtnReading = HIGH;
String mqttClientId;

// ============================================================================
// Deklarasi Fungsi
// ============================================================================
void applyRelayOutput(bool state);
void setRelayState(bool newState, const char* triggerSource);
void publishRelayState();
void publishTelemetry();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ============================================================================
// Kontrol Hardware Relay & Publikasi State
// ============================================================================
void applyRelayOutput(bool state) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(PIN_RELAY, state ? LOW : HIGH);
  } else {
    digitalWrite(PIN_RELAY, state ? HIGH : LOW);
  }
}

void publishRelayState() {
  if (mqttClient.connected()) {
    const char* payload = relayState ? "ON" : "OFF";
    // Publikasikan dengan flag retained = true agar dashboard yang baru buka tahu status terakhir
    mqttClient.publish(TOPIC_RELAY_STATE, payload, true);
  }
}

void setRelayState(bool newState, const char* triggerSource) {
  relayState = newState;
  applyRelayOutput(relayState);
  Serial.printf("[RELAY] State -> %s (Sumber: %s)\n",
                relayState ? "ON" : "OFF", triggerSource);
  publishRelayState();
}

// ============================================================================
// Callback Pesan Masuk MQTT (Subscribe Handler)
// ============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  Serial.printf("[MQTT RX] Topik: %s | Payload: %s\n", topic, message.c_str());

  if (String(topic) == TOPIC_RELAY_SET) {
    String upperMsg = message;
    upperMsg.toUpperCase();

    // Mendukung perintah plain string ("ON", "OFF", "TOGGLE", "1", "0")
    // maupun payload JSON (contoh: {"state":"ON"} atau {"relay":true})
    if (upperMsg == "ON" || upperMsg == "1" || upperMsg == "TRUE" ||
        upperMsg.indexOf("\"STATE\":\"ON\"") >= 0 ||
        upperMsg.indexOf("\"STATE\": \"ON\"") >= 0 ||
        upperMsg.indexOf("\"RELAY\":TRUE") >= 0 ||
        upperMsg.indexOf("\"RELAY\": TRUE") >= 0) {
      setRelayState(true, "MQTT");
    } else if (upperMsg == "OFF" || upperMsg == "0" || upperMsg == "FALSE" ||
               upperMsg.indexOf("\"STATE\":\"OFF\"") >= 0 ||
               upperMsg.indexOf("\"STATE\": \"OFF\"") >= 0 ||
               upperMsg.indexOf("\"RELAY\":FALSE") >= 0 ||
               upperMsg.indexOf("\"RELAY\": FALSE") >= 0) {
      setRelayState(false, "MQTT");
    } else if (upperMsg == "TOGGLE" ||
               upperMsg.indexOf("\"STATE\":\"TOGGLE\"") >= 0 ||
               upperMsg.indexOf("\"STATE\": \"TOGGLE\"") >= 0) {
      setRelayState(!relayState, "MQTT-TOGGLE");
    } else {
      Serial.println("[MQTT RX] Perintah tidak dikenali. Gunakan ON, OFF, TOGGLE, atau JSON {\"state\":\"ON\"}.");
    }
  }
}

// ============================================================================
// Pembacaan Sensor & Publikasi Telemetri JSON
// ============================================================================
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    lastTempC = t;
    lastHumidity = h;
  } else {
    Serial.println("[SENSOR] Peringatan: Gagal membaca sensor DHT22!");
  }
}

void publishTelemetry() {
  readSensors();

  char jsonBuffer[320];
  char tempStr[12];
  char humStr[12];

  if (isnan(lastTempC)) {
    snprintf(tempStr, sizeof(tempStr), "null");
  } else {
    snprintf(tempStr, sizeof(tempStr), "%.1f", lastTempC);
  }

  if (isnan(lastHumidity)) {
    snprintf(humStr, sizeof(humStr), "null");
  } else {
    snprintf(humStr, sizeof(humStr), "%.1f", lastHumidity);
  }

  snprintf(
    jsonBuffer,
    sizeof(jsonBuffer),
    "{\"node\":\"%s\",\"board\":\"%s\",\"temperature\":%s,\"humidity\":%s,"
    "\"motion\":%s,\"relay\":\"%s\",\"rssi\":%d,\"uptime\":%lu,\"heap\":%u}",
    NODE_ID,
    BOARD_NAME,
    tempStr,
    humStr,
    motionDetected ? "true" : "false",
    relayState ? "ON" : "OFF",
    WiFi.RSSI(),
    millis() / 1000UL,
    ESP.getFreeHeap()
  );

  if (mqttClient.connected()) {
    bool ok = mqttClient.publish(TOPIC_TELEMETRY, jsonBuffer, false);
    Serial.printf("[MQTT TX] %s -> %s (%s)\n",
                  TOPIC_TELEMETRY, jsonBuffer, ok ? "OK" : "FAILED");
  } else {
    Serial.printf("[LOCAL] Telemetri (MQTT Offline): %s\n", jsonBuffer);
  }
}

// ============================================================================
// Pemantauan Sensor Gerak PIR & Tombol Fisik
// ============================================================================
void checkMotionSensor() {
  motionDetected = (digitalRead(PIN_PIR) == HIGH);
  if (motionDetected != lastMotionState) {
    lastMotionState = motionDetected;
    const char* motionPayload = motionDetected ? "DETECTED" : "CLEAR";
    Serial.printf("[PIR] Status Gerakan berubah -> %s\n", motionPayload);

    if (mqttClient.connected()) {
      mqttClient.publish(TOPIC_MOTION, motionPayload, true);
    }
    // Segera kirim update telemetri saat status gerakan berubah
    publishTelemetry();
  }
}

void checkPhysicalButton() {
  int reading = digitalRead(PIN_BUTTON);
  if (reading != lastBtnReading) {
    lastDebounceMs = millis();
  }
  if ((millis() - lastDebounceMs) > DEBOUNCE_DELAY_MS) {
    static bool stableBtn = HIGH;
    if (reading != stableBtn) {
      stableBtn = reading;
      if (stableBtn == LOW) {
        setRelayState(!relayState, "Tombol Fisik");
        publishTelemetry();
      }
    }
  }
  lastBtnReading = reading;
}

// ============================================================================
// Manajemen Koneksi Wi-Fi & MQTT (Non-Blocking Reconnect)
// ============================================================================
void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (now - lastWifiCheckMs >= WIFI_CHECK_INTERVAL_MS) {
    lastWifiCheckMs = now;
    Serial.printf("[WIFI] Koneksi terputus! Mencoba menghubungkan ulang ke %s...\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void ensureMqttConnected() {
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttRetryMs < MQTT_RETRY_INTERVAL_MS) {
    return;
  }
  lastMqttRetryMs = now;

  Serial.printf("[MQTT] Menghubungkan ke broker %s:%d sebagai %s...\n",
                MQTT_BROKER, MQTT_PORT, mqttClientId.c_str());

  // Koneksi dengan Last Will and Testament (LWT):
  // Jika perangkat kehilangan daya atau koneksi terputus tiba-tiba,
  // broker otomatis mempublikasikan "offline" (retained) ke TOPIC_STATUS.
  bool connected = false;
  if (strlen(MQTT_USER) > 0) {
    connected = mqttClient.connect(
      mqttClientId.c_str(),
      MQTT_USER,
      MQTT_PASSWORD,
      TOPIC_STATUS,
      1,            // QoS 1 untuk LWT
      true,         // Retained = true
      "offline"     // Payload LWT
    );
  } else {
    connected = mqttClient.connect(
      mqttClientId.c_str(),
      TOPIC_STATUS,
      1,            // QoS 1 untuk LWT
      true,         // Retained = true
      "offline"     // Payload LWT
    );
  }

  if (connected) {
    digitalWrite(PIN_STATUS_LED, HIGH);
    Serial.println("[MQTT] Berhasil terhubung ke Broker!");

    // 1. Publikasikan status "online" (retained) untuk menimpa pesan LWT
    mqttClient.publish(TOPIC_STATUS, "online", true);

    // 2. Subscribe ke topik perintah kontrol relay
    mqttClient.subscribe(TOPIC_RELAY_SET);
    Serial.printf("[MQTT] Subscribed ke topik: %s\n", TOPIC_RELAY_SET);

    // 3. Sinkronkan state relay & telemetri awal
    publishRelayState();
    publishTelemetry();
  } else {
    digitalWrite(PIN_STATUS_LED, LOW);
    Serial.printf("[MQTT] Gagal terhubung (rc=%d). Coba lagi dalam 5 detik.\n",
                  mqttClient.state());
  }
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n==================================================");
  Serial.println("  IoT Lab - 04 MQTT Home Automation Hub");
  Serial.printf("  Node ID : %s | Target Board : %s\n", NODE_ID, BOARD_NAME);
  Serial.println("==================================================");

  // Konfigurasi Pin I/O
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  applyRelayOutput(false);
  dht.begin();

  // Buat Client ID unik berdasarkan MAC Address agar tidak bentrok di broker publik
  uint32_t chipId = 0;
  #if defined(ESP32)
    chipId = (uint32_t)(ESP.getEfuseMac() >> 24);
  #elif defined(ESP8266)
    chipId = ESP.getChipId();
  #endif
  mqttClientId = String("iotlab-") + NODE_ID + "-" + String(chipId, HEX);

  // Inisialisasi Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 10000) {
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    delay(350);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Terhubung!");
    Serial.print("[WIFI] Alamat IP : ");
    Serial.println(WiFi.localIP());
    Serial.printf("[WIFI] Sinyal    : %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\n[WIFI] Belum terhubung, sistem tetap berjalan dan akan auto-reconnect di background.");
  }

  // Konfigurasi MQTT Client
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512); // Pastikan cukup untuk payload JSON telemetri

  // Langsung coba koneksi MQTT pertama kali jika Wi-Fi sudah siap
  lastMqttRetryMs = millis() - MQTT_RETRY_INTERVAL_MS;
  ensureMqttConnected();
}

void loop() {
  // 1. Cek tombol fisik & sensor PIR secara responsif tanpa terblokir jaringan
  checkPhysicalButton();
  checkMotionSensor();

  // 2. Pastikan Wi-Fi dan MQTT tetap terhubung (Non-blocking)
  ensureWifiConnected();
  ensureMqttConnected();

  // 3. Proses antrean paket MQTT
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // 4. Publikasi telemetri berkala
  unsigned long now = millis();
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
