/**
 * ============================================================================
 * Proyek 03 : Smart Plant Watering System (Automated & Notified)
 * Repo      : IoT_lab
 * Board     : ESP32 & ESP8266 (Otomatis mendeteksi arsitektur board)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur:
 * 1. Membaca sensor kelembapan tanah kapasitif (Capacitive Soil Moisture Sensor)
 *    via ADC dengan filter rata-rata bergerak (Moving Average / Multi-Sampling)
 *    dan konversi terkalibrasi (ADC_DRY vs ADC_WET) ke persentase 0-100%.
 * 2. Kontrol pompa air DC (via Modul Relay) berbasis Histeresis:
 *    - Pompa MENYALA otomatis saat kelembapan tanah < 35% (THRESHOLD_DRY_PCT).
 *    - Pompa MATI otomatis saat kelembapan tanah >= 65% (THRESHOLD_WET_PCT).
 * 3. Proteksi Anti-Overwatering (Keamanan Ganda):
 *    - Durasi penyiraman maksimum per siklus (MAX_WATERING_DURATION_MS).
 *    - Masa tunggu / Cooldown (WATERING_COOLDOWN_MS) agar air sempat meresap
 *      ke dalam tanah sebelum pompa diizinkan menyala kembali.
 * 4. Notifikasi Real-Time ke HP melalui Telegram Bot API dan/atau Discord Webhook
 *    menggunakan HTTPS POST JSON (WiFiClientSecure + HTTPClient).
 * ============================================================================
 */

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <HTTPClient.h>
  #define BOARD_NAME "ESP32"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
  #include <ESP8266HTTPClient.h>
  #define BOARD_NAME "ESP8266"
#else
  #error "Board tidak didukung! Pilih board ESP32 atau ESP8266 di Arduino IDE."
#endif

// Gunakan secrets.h jika tersedia, atau fallback ke Wi-Fi simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID            "Wokwi-GUEST"
  #define WIFI_PASSWORD        ""
  #define TELEGRAM_BOT_TOKEN   "GANTI_DENGAN_BOT_TOKEN"
  #define TELEGRAM_CHAT_ID     "GANTI_DENGAN_CHAT_ID"
  #define DISCORD_WEBHOOK_URL  ""
#endif

// ============================================================================
// Konfigurasi Pin & Kalibrasi ADC (ESP32 vs ESP8266)
// ============================================================================
#if defined(ESP32)
  const int PIN_SOIL_ADC   = 34;   // GPIO34 (ADC1_CH6) - Input-only, aman saat Wi-Fi aktif
  const int PIN_PUMP_RELAY = 26;   // GPIO26 untuk kontrol Relay Pompa Air
  const int PIN_STATUS_LED = 2;    // Built-in LED ESP32
  // Kalibrasi ADC 12-bit ESP32 (0 - 4095)
  // Sensor kapasitif menghasilkan tegangan TINGGI saat kering, RENDAH saat basah
  const int ADC_DRY_VALUE  = 3200; // Nilai ADC saat sensor kering di udara
  const int ADC_WET_VALUE  = 1400; // Nilai ADC saat sensor dicelupkan ke air
#elif defined(ESP8266)
  const int PIN_SOIL_ADC   = A0;   // Pin A0 (ADC 10-bit tunggal pada ESP8266)
  const int PIN_PUMP_RELAY = 5;    // D1 (GPIO5) untuk kontrol Relay Pompa Air
  const int PIN_STATUS_LED = LED_BUILTIN;
  // Kalibrasi ADC 10-bit ESP8266 (0 - 1023)
  const int ADC_DRY_VALUE  = 820;  // Nilai ADC saat sensor kering di udara
  const int ADC_WET_VALUE  = 380;  // Nilai ADC saat sensor dicelupkan ke air
#endif

// Ubah ke `true` jika menggunakan modul relay fisik Active-LOW.
// Untuk simulasi Wokwi (diagram.json) atau relay Active-HIGH, gunakan `false`.
const bool RELAY_ACTIVE_LOW = false;

// ============================================================================
// Parameter Histeresis & Proteksi Anti-Overwatering
// ============================================================================
const int THRESHOLD_DRY_PCT = 35; // Mulai menyiram jika kelembapan < 35%
const int THRESHOLD_WET_PCT = 65; // Berhenti menyiram jika kelembapan >= 65%

const unsigned long SENSOR_POLL_INTERVAL_MS  = 2000;  // Cek sensor setiap 2 detik
const unsigned long MAX_WATERING_DURATION_MS = 15000; // Batas durasi pompa menyala (15 detik)
const unsigned long WATERING_COOLDOWN_MS     = 30000; // Jeda resapan air sebelum boleh menyiram lagi (30 detik)

// ============================================================================
// Variabel Filter Moving Average & State Machine Penyiraman
// ============================================================================
const int FILTER_WINDOW_SIZE = 8;
int adcSamples[FILTER_WINDOW_SIZE];
int sampleIndex = 0;
bool filterPrimed = false;

int currentRawAdc      = 0;
int currentMoisturePct = 50;
int moistureAtStartPct = 0;

bool isPumpRunning     = false;
bool isInCooldown      = false;

unsigned long lastSensorPollTime = 0;
unsigned long pumpStartTime      = 0;
unsigned long cooldownStartTime  = 0;

// ============================================================================
// Fungsi Helper Kontrol Relay Pompa Air
// ============================================================================
void applyPumpRelay(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(PIN_PUMP_RELAY, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(PIN_PUMP_RELAY, turnOn ? HIGH : LOW);
  }
}

// ============================================================================
// Pembacaan Sensor Kelembapan Tanah dengan Moving Average
// ============================================================================
void readSoilMoisture() {
  // Multi-sampling cepat (4x baca per siklus) untuk meredam derau ADC
  long burstSum = 0;
  const int BURST_COUNT = 4;
  for (int i = 0; i < BURST_COUNT; i++) {
    burstSum += analogRead(PIN_SOIL_ADC);
    delayMicroseconds(250);
  }
  int instantAdc = burstSum / BURST_COUNT;

  // Inisialisasi buffer saat pertama kali berjalan
  if (!filterPrimed) {
    for (int i = 0; i < FILTER_WINDOW_SIZE; i++) {
      adcSamples[i] = instantAdc;
    }
    filterPrimed = true;
  } else {
    adcSamples[sampleIndex] = instantAdc;
    sampleIndex = (sampleIndex + 1) % FILTER_WINDOW_SIZE;
  }

  // Hitung rata-rata bergerak (Moving Average)
  long movingSum = 0;
  for (int i = 0; i < FILTER_WINDOW_SIZE; i++) {
    movingSum += adcSamples[i];
  }
  currentRawAdc = movingSum / FILTER_WINDOW_SIZE;

  // Konversi terbalik: ADC_DRY_VALUE -> 0%, ADC_WET_VALUE -> 100%
  long mappedPct = map(currentRawAdc, ADC_DRY_VALUE, ADC_WET_VALUE, 0, 100);
  currentMoisturePct = constrain((int)mappedPct, 0, 100);
}

// ============================================================================
// Escape String Sederhana untuk Payload JSON
// ============================================================================
String escapeJsonString(const String& input) {
  String out = "";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

// ============================================================================
// Pengiriman Notifikasi ke Telegram Bot API & Discord Webhook (HTTPS JSON)
// ============================================================================
void sendNotification(const String& message) {
  Serial.println("--------------------------------------------------");
  Serial.printf("[NOTIFY] %s\n", message.c_str());
  Serial.println("--------------------------------------------------");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NOTIFY] Wi-Fi tidak terhubung, lewati pengiriman notifikasi online.");
    return;
  }

  String tgToken = String(TELEGRAM_BOT_TOKEN);
  String tgChat  = String(TELEGRAM_CHAT_ID);
  bool hasTelegram = (tgToken.length() > 10 && tgToken != "GANTI_DENGAN_BOT_TOKEN" &&
                      tgChat.length() > 1 && tgChat != "GANTI_DENGAN_CHAT_ID");

  String discordUrl = String(DISCORD_WEBHOOK_URL);
  bool hasDiscord = (discordUrl.startsWith("https://"));

  if (!hasTelegram && !hasDiscord) {
    Serial.println("[NOTIFY] Mode Simulasi/Lokal: Token Telegram/Discord belum diisi di secrets.h.");
    return;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // Memudahkan koneksi HTTPS ke api.telegram.org & discord.com tanpa bundle CA
  HTTPClient https;

  String escapedMsg = escapeJsonString(message);

  // 1. Kirim ke Telegram Bot API jika dikonfigurasi
  if (hasTelegram) {
    String url = "https://api.telegram.org/bot" + tgToken + "/sendMessage";
    String payload = "{\"chat_id\":\"" + tgChat + "\",\"text\":\"" + escapedMsg + "\"}";

    if (https.begin(secureClient, url)) {
      https.addHeader("Content-Type", "application/json");
      int httpCode = https.POST(payload);
      if (httpCode >= 200 && httpCode < 300) {
        Serial.printf("[TELEGRAM] Notifikasi terkirim! (HTTP %d)\n", httpCode);
      } else {
        Serial.printf("[TELEGRAM] Gagal mengirim notifikasi. Kode HTTP: %d\n", httpCode);
      }
      https.end();
    }
  }

  // 2. Kirim ke Discord Webhook jika dikonfigurasi
  if (hasDiscord) {
    String payload = "{\"content\":\"" + escapedMsg + "\"}";

    if (https.begin(secureClient, discordUrl)) {
      https.addHeader("Content-Type", "application/json");
      int httpCode = https.POST(payload);
      if (httpCode >= 200 && httpCode < 300) {
        Serial.printf("[DISCORD] Webhook terkirim! (HTTP %d)\n", httpCode);
      } else {
        Serial.printf("[DISCORD] Gagal mengirim webhook. Kode HTTP: %d\n", httpCode);
      }
      https.end();
    }
  }
}

// ============================================================================
// Kontrol Siklus Penyiraman (Start & Stop dengan Alasan)
// ============================================================================
void startWatering() {
  isPumpRunning = true;
  pumpStartTime = millis();
  moistureAtStartPct = currentMoisturePct;
  applyPumpRelay(true);

  String msg = "[Smart Plant] Tanah Kering (" + String(currentMoisturePct) +
               "% < " + String(THRESHOLD_DRY_PCT) + "%). Memulai penyiraman otomatis.";
  sendNotification(msg);
}

void stopWatering(const String& reason) {
  unsigned long durationSec = (millis() - pumpStartTime) / 1000;
  isPumpRunning = false;
  applyPumpRelay(false);

  // Aktifkan masa tenang (cooldown) agar air meresap ke tanah terlebih dahulu
  isInCooldown = true;
  cooldownStartTime = millis();

  String msg = "[Smart Plant] Penyiraman selesai (" + reason + "). "
               "Kelembapan: " + String(moistureAtStartPct) + "% -> " + String(currentMoisturePct) + "%. "
               "Durasi: " + String(durationSec) + " detik. Cooldown aktif " +
               String(WATERING_COOLDOWN_MS / 1000) + " detik.";
  sendNotification(msg);
}

// ============================================================================
// Evaluasi Logika Otomatisasi & Keamanan Pompa
// ============================================================================
void evaluateWateringLogic() {
  unsigned long now = millis();

  // 1. Cek apakah masa Cooldown sudah selesai
  if (isInCooldown) {
    if (now - cooldownStartTime >= WATERING_COOLDOWN_MS) {
      isInCooldown = false;
      Serial.println("[SYSTEM] Masa cooldown selesai. Sistem siap memantau penyiraman berikutnya.");
    }
  }

  // 2. Jika pompa sedang menyala, periksa target kelembapan atau batas waktu maksimum
  if (isPumpRunning) {
    if (currentMoisturePct >= THRESHOLD_WET_PCT) {
      stopWatering("Target kelembapan >= " + String(THRESHOLD_WET_PCT) + "% tercapai");
    } else if (now - pumpStartTime >= MAX_WATERING_DURATION_MS) {
      stopWatering("Batas waktu maksimum " + String(MAX_WATERING_DURATION_MS / 1000) + "s tercapai");
    }
    return;
  }

  // 3. Jika pompa mati dan tidak sedang cooldown, nyalakan saat tanah kering (< THRESHOLD_DRY_PCT)
  if (!isInCooldown && currentMoisturePct < THRESHOLD_DRY_PCT) {
    startWatering();
  }
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n=======================================================");
  Serial.println("  IoT Lab - 03 Smart Plant Watering System             ");
  Serial.printf("  Target Board: %s\n", BOARD_NAME);
  Serial.println("=======================================================");

  pinMode(PIN_PUMP_RELAY, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  applyPumpRelay(false); // Pastikan pompa mati saat pertama kali dinyalakan

#if defined(ESP32)
  analogReadResolution(12); // Pastikan resolusi ADC ESP32 12-bit (0-4095)
#endif

  // Koneksi ke Jaringan Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 15000) {
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_STATUS_LED, HIGH);
    Serial.println("\n[WIFI] Terhubung!");
    Serial.print("[WIFI] Alamat IP Perangkat : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Timeout! Sistem penyiraman otomatis tetap aktif secara lokal (Offline).");
  }

  Serial.printf("[CONFIG] Ambang Kering (ON): < %d%% | Ambang Lembap (OFF): >= %d%%\n",
                THRESHOLD_DRY_PCT, THRESHOLD_WET_PCT);
  Serial.printf("[CONFIG] Durasi Maks Pompa : %lu dtk | Cooldown Resapan: %lu dtk\n",
                MAX_WATERING_DURATION_MS / 1000, WATERING_COOLDOWN_MS / 1000);
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorPollTime >= SENSOR_POLL_INTERVAL_MS) {
    lastSensorPollTime = now;

    readSoilMoisture();

    const char* stateLabel = isPumpRunning ? "MENYIRAM (PUMP ON)"
                           : (isInCooldown ? "COOLDOWN (RESAPAN)" : "STANDBY (MONITOR)");

    Serial.printf("[SOIL] ADC Rata-rata: %4d | Kelembapan: %3d%% | Status: %s\n",
                  currentRawAdc, currentMoisturePct, stateLabel);

    evaluateWateringLogic();
  }
}
