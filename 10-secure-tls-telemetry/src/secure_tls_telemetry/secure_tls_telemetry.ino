/**
 * ============================================================================
 * Proyek 10 : Secure End-to-End Encrypted Telemetry (TLS/SSL & mTLS MQTTS)
 * Repo      : IoT_lab
 * Board     : ESP32
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Koneksi terenkripsi ujung-ke-ujung menggunakan protokol MQTTS (Port 8883)
 *    melalui `WiFiClientSecure.h` dan `PubSubClient.h`.
 * 2. Sinkronisasi Waktu NTP (`configTime`) wajib sebelum memulai TLS Handshake
 *    untuk memverifikasi masa berlaku (`NotBefore` & `NotAfter`) sertifikat X.509.
 * 3. Mendukung 3 Profil Keamanan TLS:
 *    - Mutual TLS (mTLS) X.509 (`ROOT_CA_PEM` + `CLIENT_CERT_PEM` + `PRIVATE_KEY_PEM`)
 *      untuk AWS IoT Core.
 *    - Server-Verified TLS + Username/Password untuk HiveMQ Cloud Serverless.
 *    - Mode Demo Wokwi Otomatis (jika `secrets.h` belum dibuat) ke broker MQTTS
 *      publik (`broker.emqx.io:8883`) agar enkripsi TLS dapat langsung diuji.
 * 4. Publikasi telemetri suhu & kelembapan DHT22 ber-timestamp ISO-8601 / Unix Epoch
 *    serta penerimaan perintah jarak jauh (Device Shadow / Remote Actuator Control).
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <time.h>

// Jika secrets.h tersedia, gunakan kredensial AWS IoT Core / HiveMQ Cloud milik pengguna.
// Jika belum ada (misal dijalankan langsung di simulator Wokwi), gunakan fallback MQTTS.
#if __has_include("secrets.h")
  #include "secrets.h"
  #define HAS_CUSTOM_SECRETS 1
#else
  #define HAS_CUSTOM_SECRETS      0
  #define WIFI_SSID               "Wokwi-GUEST"
  #define WIFI_PASSWORD           ""
  #define USE_MUTUAL_TLS_X509     0
  #define MQTTS_BROKER_HOST       "broker.emqx.io"
  #define MQTTS_BROKER_PORT       8883
  #define DEVICE_THING_NAME       "esp32-secure-node-01"
  #define MQTTS_USERNAME          ""
  #define MQTTS_PASSWORD          ""
  #define TOPIC_TELEMETRY_PUB     "iotlab/devices/esp32-secure-node-01/telemetry"
  #define TOPIC_COMMAND_SUB       "iotlab/devices/esp32-secure-node-01/command"
#endif

// ============================================================================
// Konfigurasi Pin Hardware (ESP32)
// ============================================================================
const int PIN_DHT             = 4;  // Sensor DHT22
const int PIN_PUBLISH_BUTTON  = 14; // Tombol fisik pemicu publish instan (INPUT_PULLUP)
const int PIN_TLS_STATUS_LED  = 2;  // LED Biru/Cyan: Menyala penuh saat sesi MQTTS 8883 aktif
const int PIN_ACTUATOR_LED    = 26; // LED Hijau: Aktuator yang dikontrol via perintah Cloud

// ============================================================================
// Konfigurasi Server NTP & Timer
// ============================================================================
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";
const long  GMT_OFFSET_S = 7 * 3600; // WIB (UTC+7)
const int   DST_OFFSET_S = 0;

const unsigned long TELEMETRY_INTERVAL_MS = 10000UL; // Kirim telemetri tiap 10 detik
const unsigned long DEBOUNCE_DELAY_MS     = 50UL;

#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

WiFiClientSecure tlsClient;
PubSubClient mqttClient(tlsClient);

unsigned long lastTelemetryTime = 0;
unsigned long lastDebounceTime  = 0;
bool lastButtonReading          = HIGH;
bool remoteActuatorState        = false;
uint32_t telemetrySeqNumber     = 0;

// ============================================================================
// Sinkronisasi Waktu NTP (Krusal untuk Validasi Sertifikat X.509 TLS!)
// ============================================================================
void synchronizeNtpTime() {
  Serial.print("[NTP] Menyinkronkan jam sistem dengan server NTP ");
  configTime(GMT_OFFSET_S, DST_OFFSET_S, NTP_SERVER_1, NTP_SERVER_2);

  time_t now = time(nullptr);
  int retry = 0;
  while (now < 1700000000L && retry < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retry++;
  }
  Serial.println();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S WIB", &timeinfo);
    Serial.printf("[NTP] Waktu Sistem Tervalidasi: %s (Epoch: %ld)\n", timeBuf, (long)now);
  } else {
    Serial.println("[NTP] Peringatan: Sinkronisasi NTP timeout, menggunakan jam internal.");
  }
}

// ============================================================================
// Format Timestamp ISO-8601 UTC untuk Payload Telemetri Bertanda Tangan Waktu
// ============================================================================
String getIso8601Timestamp() {
  time_t now = time(nullptr);
  struct tm utcTime;
  gmtime_r(&now, &utcTime);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
  return String(buf);
}

// ============================================================================
// Konfigurasi Kredensial X.509 pada WiFiClientSecure
// ============================================================================
void configureTlsCredentials() {
#if HAS_CUSTOM_SECRETS
  // Gunakan Root CA yang disimpan di PROGMEM dari secrets.h
  tlsClient.setCACert(ROOT_CA_PEM);

  #if (USE_MUTUAL_TLS_X509 == 1)
    // Mode AWS IoT Core: Mutual TLS (Verifikasi Server + Sertifikat Klien + Private Key)
    tlsClient.setCertificate(CLIENT_CERT_PEM);
    tlsClient.setPrivateKey(PRIVATE_KEY_PEM);
    Serial.println("[TLS] Mode Keamanan: Mutual TLS (mTLS X.509 Client Cert + Private Key)");
  #else
    // Mode HiveMQ Cloud: Server Root CA Verification + Username/Password
    Serial.println("[TLS] Mode Keamanan: Server X.509 Verification + MQTT Username/Password");
  #endif
#else
  // Mode Simulasi Wokwi (Tanpa secrets.h): Tetap mengenkripsi trafik pada port 8883
  // menggunakan cipher suite TLS, tanpa memblokir koneksi karena placeholder cert.
  tlsClient.setInsecure();
  Serial.println("[TLS] Mode Demo Wokwi: Enkripsi Kanal MQTTS Port 8883 (Tanpa secrets.h)");
  Serial.println("      Salin secrets.example.h -> secrets.h untuk mengaktifkan mTLS X.509 penuh.");
#endif
}

// ============================================================================
// Callback Penerimaan Pesan Terenkripsi dari Cloud (Downlink Command)
// ============================================================================
void onSecureMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();

  Serial.println("\n------------------------------------------------------------");
  Serial.printf("[MQTTS-RX] Perintah Terenkripsi Diterima pada Topik: %s\n", topic);
  Serial.printf("[MQTTS-RX] Payload: %s\n", msg.c_str());

  // Parsing perintah sederhana: {"actuator":true} / {"actuator":false} atau "ON"/"OFF"
  if (msg.indexOf("\"actuator\":true") >= 0 || msg.equalsIgnoreCase("ON") || msg == "1") {
    remoteActuatorState = true;
    digitalWrite(PIN_ACTUATOR_LED, HIGH);
    Serial.println("[SHADOW] Aktuator Keamanan -> ON (LED Hijau Menyala)");
  } else if (msg.indexOf("\"actuator\":false") >= 0 || msg.equalsIgnoreCase("OFF") || msg == "0") {
    remoteActuatorState = false;
    digitalWrite(PIN_ACTUATOR_LED, LOW);
    Serial.println("[SHADOW] Aktuator Keamanan -> OFF (LED Hijau Padam)");
  }
  Serial.println("------------------------------------------------------------");
}

// ============================================================================
// Koneksi & Handshake TLS ke Broker MQTTS Port 8883
// ============================================================================
void ensureSecureMqttConnected() {
  if (mqttClient.connected()) return;

  digitalWrite(PIN_TLS_STATUS_LED, LOW);
  Serial.printf("\n[MQTTS] Memulai TLS Handshake ke %s:%d ...\n", MQTTS_BROKER_HOST, MQTTS_BROKER_PORT);
  unsigned long tStart = millis();

  // Buat Client ID unik
  String clientId = String(DEVICE_THING_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  bool connected = false;
  if (String(MQTTS_USERNAME).length() > 0) {
    connected = mqttClient.connect(clientId.c_str(), MQTTS_USERNAME, MQTTS_PASSWORD);
  } else {
    connected = mqttClient.connect(clientId.c_str());
  }

  if (connected) {
    unsigned long handshakeMs = millis() - tStart;
    digitalWrite(PIN_TLS_STATUS_LED, HIGH);
    Serial.printf("[MQTTS] TLS Handshake & Koneksi Broker BERHASIL dalam %lu ms!\n", handshakeMs);
    Serial.printf("[MQTTS] Sisa Free Heap selama sesi TLS: %u bytes\n", ESP.getFreeHeap());

    mqttClient.subscribe(TOPIC_COMMAND_SUB);
    Serial.printf("[MQTTS] Berlangganan ke topik perintah: %s\n", TOPIC_COMMAND_SUB);
  } else {
    int state = mqttClient.state();
    char errBuf[128] = {0};
    tlsClient.lastError(errBuf, sizeof(errBuf));
    Serial.printf("[MQTTS] Gagal Terhubung! MQTT State = %d | mbedTLS Error: %s\n", state, errBuf);
    Serial.println("[MQTTS] Mencoba ulang dalam 5 detik...");
    delay(5000);
  }
}

// ============================================================================
// Publikasi Payload Telemetri Terenkripsi
// ============================================================================
void publishEncryptedTelemetry(const char* triggerReason) {
  if (!mqttClient.connected()) {
    ensureSecureMqttConnected();
    if (!mqttClient.connected()) return;
  }

  float tempC  = dht.readTemperature();
  float humPct = dht.readHumidity();
  if (isnan(tempC))  tempC  = 26.5f;
  if (isnan(humPct)) humPct = 68.0f;

  telemetrySeqNumber++;
  time_t epochNow = time(nullptr);
  String isoTime  = getIso8601Timestamp();

  char payload[320];
  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\",\"seq\":%lu,\"timestamp\":\"%s\",\"epoch\":%ld,"
           "\"trigger\":\"%s\",\"temperature_c\":%.2f,\"humidity_pct\":%.2f,"
           "\"actuator\":%s,\"rssi_dbm\":%d,\"free_heap\":%u}",
           DEVICE_THING_NAME,
           (unsigned long)telemetrySeqNumber,
           isoTime.c_str(),
           (long)epochNow,
           triggerReason,
           tempC,
           humPct,
           remoteActuatorState ? "true" : "false",
           WiFi.RSSI(),
           ESP.getFreeHeap());

  bool ok = mqttClient.publish(TOPIC_TELEMETRY_PUB, payload);
  Serial.printf("[MQTTS-TX] (%s) Topik: %s\n", ok ? "TERKIRIM" : "GAGAL", TOPIC_TELEMETRY_PUB);
  Serial.printf("           Payload: %s\n", payload);
}

// ============================================================================
// Pembacaan Tombol Fisik Publish Instan
// ============================================================================
void checkPublishButton() {
  int reading = digitalRead(PIN_PUBLISH_BUTTON);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    static bool stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        Serial.println("\n[BUTTON] Tombol Publish Instan ditekan!");
        publishEncryptedTelemetry("manual_button");
      }
    }
  }
  lastButtonReading = reading;
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(PIN_PUBLISH_BUTTON, INPUT_PULLUP);
  pinMode(PIN_TLS_STATUS_LED, OUTPUT);
  pinMode(PIN_ACTUATOR_LED, OUTPUT);
  digitalWrite(PIN_TLS_STATUS_LED, LOW);
  digitalWrite(PIN_ACTUATOR_LED, LOW);

  dht.begin();

  Serial.println("\n============================================================");
  Serial.println("  IoT Lab - 10 Secure End-to-End Encrypted Telemetry (TLS)  ");
  Serial.printf("  Device Thing Name : %s\n", DEVICE_THING_NAME);
  Serial.printf("  Target Broker     : %s:%d (MQTTS)\n", MQTTS_BROKER_HOST, MQTTS_BROKER_PORT);
  Serial.println("============================================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_TLS_STATUS_LED, !digitalRead(PIN_TLS_STATUS_LED));
    delay(350);
    Serial.print(".");
  }
  digitalWrite(PIN_TLS_STATUS_LED, LOW);
  Serial.printf("\n[WIFI] Terhubung! IP: %s (RSSI: %d dBm)\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // 1. Wajib sinkronisasi jam NTP sebelum verifikasi sertifikat X.509
  synchronizeNtpTime();

  // 2. Muat kredensial X.509 ke stack mbedTLS (WiFiClientSecure)
  configureTlsCredentials();

  // 3. Konfigurasi klien MQTT di atas soket TLS
  mqttClient.setServer(MQTTS_BROKER_HOST, MQTTS_BROKER_PORT);
  mqttClient.setCallback(onSecureMqttMessage);
  mqttClient.setBufferSize(512); // Perbesar buffer untuk payload JSON bertanda tangan waktu

  // 4. Lakukan TLS Handshake pertama & kirim paket telemetri awal
  ensureSecureMqttConnected();
  publishEncryptedTelemetry("startup");
  lastTelemetryTime = millis();
}

void loop() {
  if (!mqttClient.connected()) {
    ensureSecureMqttConnected();
  }
  mqttClient.loop();

  checkPublishButton();

  if (millis() - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = millis();
    publishEncryptedTelemetry("periodic");
  }
}
