/**
 * ============================================================================
 * Proyek 05 : Smart Energy Meter (Monitoring Daya Listrik & Biaya PLN)
 * Repo      : IoT_lab
 * Board     : ESP32 (Menggunakan HardwareSerial UART2 & I2C OLED SSD1306)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Membaca 6 parameter kelistrikan AC 220V dari sensor PZEM-004T v3.0
 *    melalui komunikasi UART2 (GPIO16 RX2, GPIO17 TX2):
 *    - Tegangan (V), Arus (A), Daya Aktif (W), Energi Akumulasi (kWh),
 *      Frekuensi (Hz), dan Power Factor (PF).
 * 2. Dilengkapi mode simulasi realistis (`SIMULATE_PZEM = true`) yang membaca
 *    potensiometer pada GPIO34 sehingga dapat diuji langsung di Wokwi atau
 *    tanpa perlu terhubung ke tegangan berbahaya AC 220V!
 * 3. Menghitung estimasi tagihan listrik dalam Rupiah (Rp) berdasarkan tarif
 *    PLN per kWh yang dapat dikonfigurasi (default: Rp 1.444,70 / kWh).
 * 4. Menampilkan dashboard lokal secara real-time pada layar OLED SSD1306.
 * 5. Mengirimkan data telemetri ke Time-Series Database InfluxDB v2 menggunakan
 *    format InfluxDB Line Protocol via HTTP POST (`/api/v2/write`).
 *
 * Library yang dibutuhkan di Arduino IDE:
 * - PZEM004Tv30 (oleh Jakub Mandula) — wajib jika SIMULATE_PZEM = false
 * - Adafruit SSD1306 & Adafruit GFX Library (untuk layar OLED I2C)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// Mode Simulasi vs Hardware Fisik PZEM-004T v3.0
// ============================================================================
// Set `SIMULATE_PZEM` ke `false` ketika ESP32 sudah terhubung ke sensor fisik
// PZEM-004T v3.0 pada pin GPIO16 (RX2) dan GPIO17 (TX2).
// Set `SIMULATE_PZEM` ke `true` untuk simulasi Wokwi menggunakan potensiometer.
#define SIMULATE_PZEM true

#if !SIMULATE_PZEM
  #include <PZEM004Tv30.h>
#endif

// Gunakan secrets.h jika tersedia, atau fallback ke konfigurasi simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID       "Wokwi-GUEST"
  #define WIFI_PASSWORD   ""
  #define INFLUXDB_URL    "http://192.168.1.100:8086"
  #define INFLUXDB_TOKEN  "DEV_TOKEN_SIMULASI"
  #define INFLUXDB_ORG    "iot_lab_org"
  #define INFLUXDB_BUCKET "energy_meter"
  #define METER_LOCATION  "panel_utama"
#endif

// ============================================================================
// Konfigurasi Pin Hardware ESP32 & Tarif Listrik PLN
// ============================================================================
const int PIN_PZEM_RX    = 16;  // GPIO16 (RX2) -> terhubung ke pin TX PZEM-004T
const int PIN_PZEM_TX    = 17;  // GPIO17 (TX2) -> terhubung ke pin RX PZEM-004T
const int PIN_POT_SIM    = 34;  // GPIO34 (ADC1_CH6) -> Potensiometer simulasi beban arus
const int PIN_OLED_SDA   = 21;  // GPIO21 -> I2C SDA OLED SSD1306
const int PIN_OLED_SCL   = 22;  // GPIO22 -> I2C SCL OLED SSD1306
const int PIN_STATUS_LED = 2;   // GPIO2  -> LED Indikator Onboard

// Tarif Listrik PLN Golongan R-1/TR 1300 VA - 2200 VA Nonsubsidi (Rp / kWh)
const float TARIF_PLN_PER_KWH = 1444.70f;

// Konfigurasi Layar OLED 128x64
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledAvailable = false;

#if !SIMULATE_PZEM
  PZEM004Tv30 pzem(Serial2, PIN_PZEM_RX, PIN_PZEM_TX);
#endif

// ============================================================================
// Struktur Data Pembacaan Energi
// ============================================================================
struct EnergyMetrics {
  float voltage;      // Volt (V)
  float current;      // Ampere (A)
  float power;        // Watt (W)
  float energyKwh;    // Kilowatt-hour (kWh)
  float frequency;    // Hertz (Hz)
  float powerFactor;  // Cos Phi (0.00 - 1.00)
  float costRupiah;   // Estimasi Biaya (Rp)
  bool  valid;
};

EnergyMetrics metrics = {220.0f, 0.0f, 0.0f, 0.0f, 50.0f, 0.95f, 0.0f, true};

// Variabel Timers Non-Blocking
unsigned long lastSampleMs   = 0;
unsigned long lastInfluxMs   = 0;
unsigned long lastEnergyTick = 0;

const unsigned long SAMPLE_INTERVAL_MS = 2000;  // Baca sensor & update OLED tiap 2 detik
const unsigned long INFLUX_INTERVAL_MS = 10000; // Kirim ke InfluxDB tiap 10 detik

// ============================================================================
// Pembacaan Sensor PZEM-004T v3.0 (Asli atau Simulasi Wokwi)
// ============================================================================
void readEnergyMetrics() {
  unsigned long now = millis();
  float deltaHours = (now - lastEnergyTick) / 3600000.0f;
  lastEnergyTick = now;

#if !SIMULATE_PZEM
  // Pembacaan langsung dari modul fisik PZEM-004T v3.0 via Modbus-RTU UART2
  float v  = pzem.voltage();
  float c  = pzem.current();
  float p  = pzem.power();
  float e  = pzem.energy();
  float hz = pzem.frequency();
  float pf = pzem.pf();

  if (isnan(v) || isnan(c) || isnan(p)) {
    metrics.valid = false;
    Serial.println("[PZEM] ERROR: Gagal membaca sensor PZEM-004T! Periksa kabel RX2/TX2 dan sumber AC 220V.");
    return;
  }

  metrics.voltage     = v;
  metrics.current     = c;
  metrics.power       = p;
  metrics.energyKwh   = e;
  metrics.frequency   = hz;
  metrics.powerFactor = pf;
  metrics.costRupiah  = metrics.energyKwh * TARIF_PLN_PER_KWH;
  metrics.valid       = true;

#else
  // Mode Simulasi Realistis (Menggunakan Potensiometer di GPIO34 untuk mengatur beban arus 0 - 16 A)
  int rawAdc = analogRead(PIN_POT_SIM);
  float jitterV = ((float)random(-15, 16)) / 10.0f; // Fluktuasi realistis +-1.5V
  metrics.voltage = 220.0f + jitterV;

  // Petakan posisi potensiometer (0..4095) menjadi beban arus (0.05A .. 15.00A)
  metrics.current = (rawAdc / 4095.0f) * 15.0f;
  if (metrics.current < 0.05f) {
    metrics.current = 0.0f;
    metrics.powerFactor = 1.00f;
  } else {
    // Simulasikan faktor daya (0.86 - 0.98) tergantung beban
    metrics.powerFactor = 0.88f + (0.10f * (rawAdc / 4095.0f));
    if (metrics.powerFactor > 0.99f) metrics.powerFactor = 0.99f;
  }

  // Rumus Daya Aktif 1-Fasa: P = V * I * cos(phi)
  metrics.power = metrics.voltage * metrics.current * metrics.powerFactor;

  // Akumulasikan kWh (dipercepat 60x pada mode simulasi agar kenaikan kWh terlihat saat pengujian)
  const float SIMULATION_TIME_ACCEL = 60.0f;
  metrics.energyKwh += (metrics.power / 1000.0f) * deltaHours * SIMULATION_TIME_ACCEL;

  metrics.frequency  = 50.0f + (((float)random(-1, 2)) * 0.05f);
  metrics.costRupiah = metrics.energyKwh * TARIF_PLN_PER_KWH;
  metrics.valid      = true;
#endif
}

// ============================================================================
// Tampilan Dashboard Lokal pada OLED SSD1306 (128x64)
// ============================================================================
void updateOledDisplay() {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Baris Header
  display.setCursor(0, 0);
  display.printf("ENERGY METER [%s]", SIMULATE_PZEM ? "SIM" : "PZEM");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (!metrics.valid) {
    display.setCursor(0, 24);
    display.println("SENSOR PZEM OFFLINE!");
    display.println("Cek AC 220V & UART2");
    display.display();
    return;
  }

  // Baris 1: Tegangan, Arus, Power Factor
  display.setCursor(0, 14);
  display.printf("%.1fV  %.2fA  PF:%.2f",
                 metrics.voltage, metrics.current, metrics.powerFactor);

  // Baris 2: Daya Aktif (Watt) berukuran besar
  display.setCursor(0, 26);
  display.setTextSize(2);
  display.printf("%.1f W", metrics.power);

  // Baris 3: Konsumsi Energi (kWh) & Estimasi Biaya PLN (Rp)
  display.setTextSize(1);
  display.setCursor(0, 46);
  display.printf("Energi : %.3f kWh", metrics.energyKwh);
  display.setCursor(0, 56);
  display.printf("Biaya  : Rp %.0f", metrics.costRupiah);

  display.display();
}

// ============================================================================
// Format InfluxDB Line Protocol & Pengiriman via HTTP POST
// ============================================================================
String buildInfluxLineProtocol() {
  // Format InfluxDB Line Protocol:
  // measurement,tag1=val1,tag2=val2 field1=v1,field2=v2
  char line[320];
  snprintf(
    line,
    sizeof(line),
    "power_consumption,location=%s,mode=%s "
    "voltage=%.2f,current=%.3f,power=%.2f,energy_kwh=%.4f,frequency=%.2f,pf=%.2f,cost_idr=%.2f",
    METER_LOCATION,
    SIMULATE_PZEM ? "simulation" : "hardware",
    metrics.voltage,
    metrics.current,
    metrics.power,
    metrics.energyKwh,
    metrics.frequency,
    metrics.powerFactor,
    metrics.costRupiah
  );
  return String(line);
}

void sendToInfluxDB() {
  if (!metrics.valid) return;

  String linePayload = buildInfluxLineProtocol();
  Serial.println("------------------------------------------------------------");
  Serial.printf("[METER] V: %.1f V | I: %.2f A | P: %.1f W | E: %.4f kWh | Freq: %.1f Hz | PF: %.2f\n",
                metrics.voltage, metrics.current, metrics.power,
                metrics.energyKwh, metrics.frequency, metrics.powerFactor);
  Serial.printf("[BIAYA] Estimasi Tagihan PLN: Rp %.2f (Tarif: Rp %.2f/kWh)\n",
                metrics.costRupiah, TARIF_PLN_PER_KWH);
  Serial.printf("[INFLUX LINE] %s\n", linePayload.c_str());

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[INFLUX] Wi-Fi belum terhubung. Pengiriman HTTP dilewati.");
    return;
  }

  // Lewati HTTP request jika masih menggunakan token placeholder di simulator Wokwi
  if (String(INFLUXDB_TOKEN) == "DEV_TOKEN_SIMULASI" ||
      String(INFLUXDB_TOKEN) == "GANTI_DENGAN_API_TOKEN_INFLUXDB") {
    Serial.println("[INFLUX] Info: Isi INFLUXDB_URL & INFLUXDB_TOKEN di secrets.h untuk mengirim ke server InfluxDB aktif.");
    return;
  }

  HTTPClient http;
  String writeUrl = String(INFLUXDB_URL) + "/api/v2/write?org=" +
                    String(INFLUXDB_ORG) + "&bucket=" +
                    String(INFLUXDB_BUCKET) + "&precision=s";

  http.begin(writeUrl);
  http.setTimeout(4000);
  http.addHeader("Authorization", "Token " + String(INFLUXDB_TOKEN));
  http.addHeader("Content-Type", "text/plain; charset=utf-8");

  int httpCode = http.POST(linePayload);
  if (httpCode == 204) {
    // HTTP 204 No Content adalah kode sukses standar dari InfluxDB v2 Write API
    Serial.println("[INFLUX] Sukses menulis data ke InfluxDB v2 (HTTP 204 No Content).");
  } else if (httpCode > 0) {
    Serial.printf("[INFLUX] Gagal menulis ke InfluxDB (HTTP %d): %s\n",
                  httpCode, http.getString().c_str());
  } else {
    Serial.printf("[INFLUX] Koneksi ke server gagal: %s\n",
                  http.errorToString(httpCode).c_str());
  }
  http.end();
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n============================================================");
  Serial.println("  IoT Lab - 05 Smart Energy Meter (PZEM-004T & InfluxDB v2) ");
  Serial.printf("  Mode Sensor : %s\n", SIMULATE_PZEM ? "SIMULASI (Potensiometer GPIO34)" : "HARDWARE (PZEM-004T UART2)");
  Serial.println("============================================================");

  pinMode(PIN_STATUS_LED, OUTPUT);

  #if SIMULATE_PZEM
    pinMode(PIN_POT_SIM, INPUT);
    analogReadResolution(12);
  #else
    Serial2.begin(9600, SERIAL_8N1, PIN_PZEM_RX, PIN_PZEM_TX);
  #endif

  // Inisialisasi Layar OLED SSD1306
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    display.println("IoT Lab - Proyek 05");
    display.println("Smart Energy Meter");
    display.println("Connecting Wi-Fi...");
    display.display();
  } else {
    Serial.println("[OLED] Layar SSD1306 tidak terdeteksi di alamat I2C 0x3C.");
  }

  // Koneksi ke Wi-Fi
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
    digitalWrite(PIN_STATUS_LED, HIGH);
    Serial.println("\n[WIFI] Terhubung!");
    Serial.print("[WIFI] Alamat IP : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Berjalan dalam mode tampilan lokal (Offline).");
  }

  lastEnergyTick = millis();
  readEnergyMetrics();
  updateOledDisplay();
  sendToInfluxDB();
}

void loop() {
  unsigned long now = millis();

  // 1. Pembacaan Sensor & Update Layar OLED Lokal setiap 2 detik
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    readEnergyMetrics();
    updateOledDisplay();
  }

  // 2. Pengiriman Data ke InfluxDB v2 setiap 10 detik
  if (now - lastInfluxMs >= INFLUX_INTERVAL_MS) {
    lastInfluxMs = now;
    sendToInfluxDB();
  }
}
