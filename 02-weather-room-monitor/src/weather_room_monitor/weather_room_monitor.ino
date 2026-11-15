/**
 * ============================================================================
 * Proyek 02 : Real-Time Weather & Room Monitor
 * Repo      : IoT_lab
 * Board     : ESP32 & ESP8266 (Otomatis mendeteksi arsitektur board)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur:
 * 1. Membaca suhu (°C), kelembapan relatif (%), dan menghitung Heat Index (°C)
 *    dari sensor DHT22 (dilengkapi opsi/komentar integrasi sensor I2C BME280).
 * 2. Menampilkan data secara real-time pada layar OLED 0.96" SSD1306 (128x64, I2C)
 *    lengkap dengan bar status Wi-Fi (RSSI) dan indikator sinkronisasi Cloud.
 * 3. Mengirimkan telemetri secara berkala (non-blocking dengan millis()) ke
 *    ThingSpeak (HTTP POST/GET) dan/atau endpoint HTTP JSON generik (ThingsBoard).
 * 4. Kompatibel penuh dengan ESP32 maupun ESP8266 (NodeMCU / Wemos D1 Mini).
 * ============================================================================
 * Library yang diperlukan di Arduino IDE:
 * - DHT sensor library by Adafruit (+ Adafruit Unified Sensor)
 * - Adafruit SSD1306 by Adafruit
 * - Adafruit GFX Library by Adafruit
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Opsional: Jika ingin menggunakan sensor BME280 (Suhu, Kelembapan, Tekanan Udara)
// #include <Adafruit_BME280.h>
// Adafruit_BME280 bme;

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #define BOARD_NAME "ESP32"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
  #define BOARD_NAME "ESP8266"
#else
  #error "Board tidak didukung! Pilih board ESP32 atau ESP8266 di Arduino IDE."
#endif

// Gunakan secrets.h jika tersedia, atau fallback ke Wi-Fi simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID             "Wokwi-GUEST"
  #define WIFI_PASSWORD         ""
  #define THINGSPEAK_API_KEY    "GANTI_DENGAN_WRITE_API_KEY"
  #define THINGSPEAK_CHANNEL_ID 0000000
  #define CUSTOM_JSON_ENDPOINT  ""
#endif

// ============================================================================
// Konfigurasi Pin Hardware & Sensor (ESP32 vs ESP8266)
// ============================================================================
#if defined(ESP32)
  const int PIN_DHT        = 15; // GPIO15 untuk data DHT22
  const int PIN_I2C_SDA    = 21; // GPIO21 untuk SDA OLED SSD1306
  const int PIN_I2C_SCL    = 22; // GPIO22 untuk SCL OLED SSD1306
  const int PIN_STATUS_LED = 2;  // Built-in LED ESP32
#elif defined(ESP8266)
  const int PIN_DHT        = 14; // D5 (GPIO14) untuk data DHT22
  const int PIN_I2C_SDA    = 4;  // D2 (GPIO4) untuk SDA OLED SSD1306
  const int PIN_I2C_SCL    = 5;  // D1 (GPIO5) untuk SCL OLED SSD1306
  const int PIN_STATUS_LED = LED_BUILTIN;
#endif

#define DHT_TYPE DHT22

// Konfigurasi Layar OLED SSD1306 (128x64 piksel)
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_I2C_ADDR 0x3C

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// Interval Timer Non-Blocking (millis)
// ============================================================================
const unsigned long SENSOR_READ_INTERVAL_MS = 2500;  // Baca sensor & update OLED tiap 2.5 detik
const unsigned long CLOUD_SYNC_INTERVAL_MS  = 15000; // Kirim ke ThingSpeak tiap 15 detik (batas gratis ThingSpeak)

unsigned long lastSensorReadTime = 0;
unsigned long lastCloudSyncTime  = 0;

// ============================================================================
// Variabel Data Telemetri & Status
// ============================================================================
float currentTempC     = NAN;
float currentHumidity  = NAN;
float currentHeatIndex = NAN;
bool  oledAvailable    = false;
String lastCloudStatus = "IDLE";
int    cloudSyncCount  = 0;

// ============================================================================
// Fungsi Helper Kategori Kenyamanan Ruangan
// ============================================================================
const char* getComfortLevel(float tempC, float hum) {
  if (isnan(tempC) || isnan(hum)) return "SENSOR ERR";
  if (tempC >= 32.0 || hum >= 80.0) return "PANAS/LEMBAP";
  if (tempC < 20.0) return "SEJUK/DINGIN";
  if (tempC >= 20.0 && tempC <= 27.5 && hum >= 40.0 && hum <= 65.0) return "NYAMAN (IDEAL)";
  return "NORMAL";
}

// ============================================================================
// Pembacaan Sensor DHT22 (atau opsi BME280)
// ============================================================================
bool readEnvironmentSensor() {
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // Celcius (default)

  // Contoh jika menggunakan BME280:
  // float t = bme.readTemperature();
  // float h = bme.readHumidity();
  // float pressureHpa = bme.readPressure() / 100.0F;

  if (isnan(h) || isnan(t)) {
    Serial.println("[SENSOR] Gagal membaca data dari sensor DHT22! Periksa kabel/pull-up.");
    return false;
  }

  currentTempC     = t;
  currentHumidity  = h;
  currentHeatIndex = dht.computeHeatIndex(t, h, false); // false = satuan Celcius

  Serial.printf("[SENSOR] Suhu: %.1f C | Kelembapan: %.1f %% | Heat Index: %.1f C | Status: %s\n",
                currentTempC, currentHumidity, currentHeatIndex,
                getComfortLevel(currentTempC, currentHumidity));
  return true;
}

// ============================================================================
// Render Tampilan Real-Time pada OLED SSD1306
// ============================================================================
void updateOledDisplay() {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Baris Header: Status Wi-Fi & Status Cloud
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.printf("WiFi:%ddBm", WiFi.RSSI());
  } else {
    display.print("WiFi:OFFLINE");
  }

  display.setCursor(74, 0);
  display.printf("CLD:%s", lastCloudStatus.c_str());
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Jika pembacaan sensor gagal
  if (isnan(currentTempC) || isnan(currentHumidity)) {
    display.setCursor(10, 26);
    display.setTextSize(1);
    display.println("Sensor DHT22 Error!");
    display.setCursor(10, 40);
    display.println("Cek pin & koneksi.");
    display.display();
    return;
  }

  // Baris Utama: Suhu & Kelembapan dengan ukuran font besar
  display.setCursor(0, 15);
  display.setTextSize(1);
  display.print("SUHU");
  display.setCursor(70, 15);
  display.print("LEMBAP");

  display.setTextSize(2);
  display.setCursor(0, 26);
  display.printf("%.1f", currentTempC);
  display.setTextSize(1);
  display.cp437(true);
  display.write(248); // Simbol derajat (°)
  display.print("C");

  display.setTextSize(2);
  display.setCursor(70, 26);
  display.printf("%.0f", currentHumidity);
  display.setTextSize(1);
  display.print("%");

  display.drawLine(0, 45, 127, 45, SSD1306_WHITE);

  // Baris Footer: Heat Index & Kategori Kenyamanan
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.printf("Terasa: %.1f C", currentHeatIndex);

  display.setCursor(0, 56);
  display.printf("Status: %s", getComfortLevel(currentTempC, currentHumidity));

  display.display();
}

// ============================================================================
// Pembuat Payload JSON Telemetri
// ============================================================================
String buildTelemetryJson() {
  String json = "{";
  json += "\"device\":\"" + String(BOARD_NAME) + "\",";
  json += "\"temperature_c\":" + String(currentTempC, 2) + ",";
  json += "\"humidity_pct\":" + String(currentHumidity, 2) + ",";
  json += "\"heat_index_c\":" + String(currentHeatIndex, 2) + ",";
  json += "\"comfort\":\"" + String(getComfortLevel(currentTempC, currentHumidity)) + "\",";
  json += "\"rssi_dbm\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_s\":" + String(millis() / 1000);
  json += "}";
  return json;
}

// ============================================================================
// Pengiriman Data ke ThingSpeak & Endpoint HTTP JSON Generik
// ============================================================================
void sendTelemetryToCloud() {
  if (isnan(currentTempC) || isnan(currentHumidity)) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[CLOUD] Wi-Fi terputus, mencoba menghubungkan ulang...");
    lastCloudStatus = "NO-WIFI";
    WiFi.reconnect();
    return;
  }

  String jsonPayload = buildTelemetryJson();
  Serial.printf("[TELEMETRY] Payload JSON: %s\n", jsonPayload.c_str());

  String apiKey = String(THINGSPEAK_API_KEY);
  bool hasThingSpeakKey = (apiKey.length() > 5 && apiKey != "GANTI_DENGAN_WRITE_API_KEY");
  String customEndpoint = String(CUSTOM_JSON_ENDPOINT);
  bool hasCustomEndpoint = (customEndpoint.length() > 7);

  if (!hasThingSpeakKey && !hasCustomEndpoint) {
    lastCloudStatus = "SIM-OK";
    cloudSyncCount++;
    Serial.println("[CLOUD] Mode Lokal/Simulasi (API Key belum diisi di secrets.h). Lewati HTTP request.");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  // 1. Kirim ke ThingSpeak jika Write API Key dikonfigurasi
  if (hasThingSpeakKey) {
    lastCloudStatus = "SYNC..";
    updateOledDisplay();

    String url = "http://api.thingspeak.com/update";
    String postBody = "api_key=" + apiKey +
                      "&field1=" + String(currentTempC, 2) +
                      "&field2=" + String(currentHumidity, 2) +
                      "&field3=" + String(currentHeatIndex, 2) +
                      "&field4=" + String(WiFi.RSSI());

    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(postBody);

    if (httpCode == 200) {
      String response = http.getString();
      cloudSyncCount++;
      lastCloudStatus = "OK#" + String(cloudSyncCount);
      Serial.printf("[THINGSPEAK] Berhasil kirim data! Entry ID: %s (HTTP %d)\n",
                    response.c_str(), httpCode);
    } else {
      lastCloudStatus = "ERR";
      Serial.printf("[THINGSPEAK] Gagal kirim data. Kode HTTP: %d (%s)\n",
                    httpCode, http.errorToString(httpCode).c_str());
    }
    http.end();
  }

  // 2. Kirim ke Endpoint HTTP JSON Generik (misal ThingsBoard / Webhook) jika diisi
  if (hasCustomEndpoint) {
    http.begin(client, customEndpoint);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(jsonPayload);

    if (httpCode >= 200 && httpCode < 300) {
      lastCloudStatus = "OK";
      Serial.printf("[HTTP-JSON] Berhasil POST ke endpoint kustom (HTTP %d)\n", httpCode);
    } else {
      lastCloudStatus = "ERR";
      Serial.printf("[HTTP-JSON] Gagal POST ke endpoint kustom. Kode HTTP: %d\n", httpCode);
    }
    http.end();
  }
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n==================================================");
  Serial.println("  IoT Lab - 02 Real-Time Weather & Room Monitor   ");
  Serial.printf("  Target Board: %s\n", BOARD_NAME);
  Serial.println("==================================================");

  pinMode(PIN_STATUS_LED, OUTPUT);

  // Inisialisasi Bus I2C dan Layar OLED SSD1306
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 8);
    display.println("IoT Lab - Proyek 02");
    display.println("Weather Room Monitor");
    display.println("--------------------");
    display.println("Menghubungkan WiFi..");
    display.display();
    Serial.printf("[OLED] SSD1306 berhasil diinisialisasi pada alamat I2C 0x%02X.\n", OLED_I2C_ADDR);
  } else {
    Serial.println("[OLED] Peringatan: Layar SSD1306 tidak terdeteksi di 0x3C! Lanjut tanpa OLED.");
  }

  // Inisialisasi Sensor DHT22
  dht.begin();
  Serial.printf("[SENSOR] Sensor DHT22 aktif pada GPIO %d.\n", PIN_DHT);

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
    Serial.printf("[WIFI] Kekuatan Sinyal     : %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\n[WIFI] Timeout! Berjalan dalam mode tampilan lokal (Offline OLED).");
    lastCloudStatus = "OFFLINE";
  }

  // Pembacaan pertama saat boot
  readEnvironmentSensor();
  updateOledDisplay();
  lastSensorReadTime = millis();
}

void loop() {
  unsigned long now = millis();

  // Tugas 1: Baca sensor DHT22 & segarkan tampilan OLED secara berkala
  if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadTime = now;
    if (readEnvironmentSensor()) {
      updateOledDisplay();
    }
  }

  // Tugas 2: Kirim data telemetri ke Cloud secara non-blocking
  if (now - lastCloudSyncTime >= CLOUD_SYNC_INTERVAL_MS) {
    lastCloudSyncTime = now;
    sendTelemetryToCloud();
    updateOledDisplay();
  }
}
