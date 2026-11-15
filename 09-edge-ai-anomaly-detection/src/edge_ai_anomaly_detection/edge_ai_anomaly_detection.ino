/**
 * ============================================================================
 * Proyek 09 : Edge AI Anomaly Detection (TinyML Predictive Maintenance)
 * Repo      : IoT_lab
 * Board     : ESP32 + Sensor Akselerometer/Giroskop I2C MPU6050
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Akuisisi sinyal getaran 3-axis (`ax`, `ay`, `az` dalam satuan m/s^2) dari
 *    sensor MPU6050 dengan frekuensi sampling presisi 100 Hz (interval 10 ms)
 *    ke dalam Sliding Window Buffer berukuran 64 sampel (640 ms).
 * 2. Ekstraksi Fitur DSP Real-Time di Edge (RMS, Peak-to-Peak, Variance,
 *    Crest Factor, dan Zero-Crossing Rate) menggunakan `anomaly_model.h`.
 * 3. Mendukung 2 Mode Operasi yang dapat diganti kapan saja melalui tombol fisik
 *    pada GPIO14 atau perintah Serial Monitor (`m`, `1`, `2`, `s`):
 *    - MODE 1: Edge AI Real-Time Inference (`IDLE`, `NORMAL_OPERATION`, `ANOMALY_DETECTED`)
 *    - MODE 2: CSV Data Forwarder (`ax,ay,az`) untuk pelatihan model di Edge Impulse Studio
 * 4. Dilengkapi Generator Getaran Sintetis (opsional via perintah Serial `s`)
 *    untuk menguji respons deteksi anomali secara mudah di simulator Wokwi.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include "anomaly_model.h"

// ============================================================================
// Konfigurasi Pin Hardware (ESP32)
// ============================================================================
const int PIN_I2C_SDA     = 21;
const int PIN_I2C_SCL     = 22;
const int PIN_MODE_BUTTON = 14; // Tombol ganti mode / simulasi profil getaran
const int PIN_LED_NORMAL  = 26; // LED Hijau: Mesin Normal / Idle
const int PIN_LED_ANOMALY = 27; // LED Merah: Anomali Terdeteksi!
const int PIN_BUZZER      = 25; // Buzzer Alarm Peringatan Dini

// ============================================================================
// Konfigurasi Register I2C MPU6050 & Sampling Sinyal
// ============================================================================
const uint8_t MPU6050_I2C_ADDR     = 0x68;
const uint8_t REG_PWR_MGMT_1       = 0x6B;
const uint8_t REG_ACCEL_CONFIG     = 0x1C;
const uint8_t REG_ACCEL_XOUT_H     = 0x3B;
const float   ACCEL_SCALE_2G       = 16384.0f; // LSB/g untuk rentang +-2g
const float   GRAVITY_MS2          = 9.80665f;

const size_t  WINDOW_SAMPLES       = 64;       // 64 sampel @ 100 Hz = jendela 640 ms
const size_t  STRIDE_SAMPLES       = 32;       // Overlap 50% (Inferensi tiap 320 ms)
const unsigned long SAMPLE_INTERVAL_US = 10000UL; // 10,000 us = 10 ms (100 Hz)

// ============================================================================
// Buffer Sinyal & Variabel State
// ============================================================================
float bufAx[WINDOW_SAMPLES];
float bufAy[WINDOW_SAMPLES];
float bufAz[WINDOW_SAMPLES];
size_t sampleCount = 0;

enum OperatingMode : uint8_t {
  MODE_EDGE_INFERENCE = 0, // Mode Default: Klasifikasi TinyML Real-Time
  MODE_DATA_FORWARDER = 1  // Mode CSV: Streaming "ax,ay,az" untuk Edge Impulse CLI
};

OperatingMode currentMode = MODE_EDGE_INFERENCE;
bool mpuConnected         = false;

// Mode injeksi getaran sintetis untuk mempermudah pengujian di Wokwi:
//   0 = Baca murni dari sensor MPU6050
//   1 = Simulasi Mesin Normal (Harmonik 18 Hz halus)
//   2 = Simulasi Mesin Anomali (Benturan bearing & ketidakseimbangan poros)
uint8_t syntheticFaultProfile = 0;

unsigned long lastSampleMicros = 0;
unsigned long lastDebounceTime = 0;
bool lastButtonReading         = HIGH;

// ============================================================================
// Driver I2C Langsung untuk Sensor MPU6050 (Tanpa Ketergantungan Library)
// ============================================================================
bool initMpu6050() {
  Wire.beginTransmission(MPU6050_I2C_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(0x00); // Bangunkan MPU6050 dari mode sleep
  if (Wire.endTransmission(true) != 0) {
    return false;
  }

  // Konfigurasi rentang penuh akselerometer ke +-2g
  Wire.beginTransmission(MPU6050_I2C_ADDR);
  Wire.write(REG_ACCEL_CONFIG);
  Wire.write(0x00);
  Wire.endTransmission(true);
  return true;
}

void readAccelerometerMs2(float& ax, float& ay, float& az) {
  if (mpuConnected) {
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    Wire.endTransmission(false);
    uint8_t bytesReceived = Wire.requestFrom(MPU6050_I2C_ADDR, (uint8_t)6, (uint8_t)true);

    if (bytesReceived == 6) {
      int16_t rawX = (int16_t)((Wire.read() << 8) | Wire.read());
      int16_t rawY = (int16_t)((Wire.read() << 8) | Wire.read());
      int16_t rawZ = (int16_t)((Wire.read() << 8) | Wire.read());

      ax = ((float)rawX / ACCEL_SCALE_2G) * GRAVITY_MS2;
      ay = ((float)rawY / ACCEL_SCALE_2G) * GRAVITY_MS2;
      az = ((float)rawZ / ACCEL_SCALE_2G) * GRAVITY_MS2;
    }
  } else {
    ax = 0.0f;
    ay = 0.0f;
    az = GRAVITY_MS2;
  }

  // Tambahkan komponen getaran sintetis jika diaktifkan untuk demo pengujian
  float tSec = (float)millis() / 1000.0f;
  if (syntheticFaultProfile == 1) {
    // Getaran sinusoidal halus motor sehat (RMS ~ 1.2 m/s^2)
    ax += 1.15f * sinf(2.0f * PI * 18.0f * tSec);
    ay += 0.95f * cosf(2.0f * PI * 18.0f * tSec);
    az += 0.60f * sinf(2.0f * PI * 36.0f * tSec);
  } else if (syntheticFaultProfile == 2) {
    // Getaran anomali berat + lonjakan impak mekanis (bearing spalling / unbalance)
    float impact = ((random(0, 100) < 18) ? 7.5f : 0.0f);
    ax += 3.80f * sinf(2.0f * PI * 24.0f * tSec) + impact;
    ay += 3.40f * cosf(2.0f * PI * 24.0f * tSec) - (impact * 0.7f);
    az += 2.90f * sinf(2.0f * PI * 48.0f * tSec) + ((float)random(-150, 150) / 100.0f);
  }
}

// ============================================================================
// Kontrol Indikator LED & Alarm Buzzer berdasarkan Hasil Inferensi
// ============================================================================
void updateHardwareIndicators(MachineState state) {
  if (state == STATE_ANOMALY_DETECTED) {
    digitalWrite(PIN_LED_NORMAL, LOW);
    digitalWrite(PIN_LED_ANOMALY, HIGH);
    tone(PIN_BUZZER, 2400, 180); // Bunyikan alarm 2.4 kHz
  } else {
    digitalWrite(PIN_LED_NORMAL, HIGH);
    digitalWrite(PIN_LED_ANOMALY, LOW);
    noTone(PIN_BUZZER);
  }
}

// ============================================================================
// Geser Sliding Window Buffer (50% Overlap Stride)
// ============================================================================
void shiftSlidingWindow() {
  size_t remaining = WINDOW_SAMPLES - STRIDE_SAMPLES;
  for (size_t i = 0; i < remaining; i++) {
    bufAx[i] = bufAx[i + STRIDE_SAMPLES];
    bufAy[i] = bufAy[i + STRIDE_SAMPLES];
    bufAz[i] = bufAz[i + STRIDE_SAMPLES];
  }
  sampleCount = remaining;
}

// ============================================================================
// Penanganan Input Perintah Serial & Tombol Fisik
// ============================================================================
void printMenuBanner() {
  Serial.println("\n======================================================================");
  Serial.println("  IoT Lab - 09 Edge AI Anomaly Detection (TinyML Vibration Monitor)   ");
  Serial.printf("  Mode Aktif : %s\n",
                (currentMode == MODE_EDGE_INFERENCE) ? "EDGE AI INFERENCE (Real-Time)" : "CSV DATA FORWARDER (Edge Impulse)");
  Serial.println("  Perintah Serial Monitor:");
  Serial.println("    'm' -> Ganti Mode (Edge AI Inference <-> CSV Data Forwarder)");
  Serial.println("    '0' -> Mode Sensor MPU6050 Murni (IDLE / Sesuai Slider Wokwi)");
  Serial.println("    '1' -> Simulasikan Getaran Motor NORMAL_OPERATION");
  Serial.println("    '2' -> Simulasikan Getaran Rusak ANOMALY_DETECTED (Bearing/Unbalance)");
  Serial.println("======================================================================\n");
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  if (cmd == 'm' || cmd == 'M') {
    currentMode = (currentMode == MODE_EDGE_INFERENCE) ? MODE_DATA_FORWARDER : MODE_EDGE_INFERENCE;
    printMenuBanner();
    if (currentMode == MODE_DATA_FORWARDER) {
      Serial.println("ax,ay,az");
    }
  } else if (cmd == '0') {
    syntheticFaultProfile = 0;
    Serial.println("[SIM] Profil Getaran: 0 (Baca murni sensor MPU6050 / IDLE)");
  } else if (cmd == '1') {
    syntheticFaultProfile = 1;
    Serial.println("[SIM] Profil Getaran: 1 (Simulasi Putaran Motor Sehat / NORMAL_OPERATION)");
  } else if (cmd == '2') {
    syntheticFaultProfile = 2;
    Serial.println("[SIM] Profil Getaran: 2 (Simulasi Kerusakan Bearing / ANOMALY_DETECTED)");
  }
}

void checkModeButton() {
  int reading = digitalRead(PIN_MODE_BUTTON);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > 50UL) {
    static bool stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        // Siklus profil getaran (0 -> 1 -> 2 -> 0) saat tombol ditekan
        syntheticFaultProfile = (syntheticFaultProfile + 1) % 3;
        Serial.printf("\n[BUTTON] Tombol ditekan! Beralih ke Profil Getaran Uji #%u (%s)\n",
                      syntheticFaultProfile,
                      (syntheticFaultProfile == 0) ? "SENSOR_MURNI/IDLE" :
                      (syntheticFaultProfile == 1) ? "MOTOR_NORMAL" : "MOTOR_ANOMALY");
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
  delay(200);

  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_NORMAL, OUTPUT);
  pinMode(PIN_LED_ANOMALY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_NORMAL, HIGH);
  digitalWrite(PIN_LED_ANOMALY, LOW);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000); // Fast I2C 400 kHz untuk pembacaan sampel berkecepatan tinggi

  mpuConnected = initMpu6050();
  printMenuBanner();

  if (mpuConnected) {
    Serial.println("[MPU6050] Sensor Akselerometer I2C (0x68) berhasil diinisialisasi!");
  } else {
    Serial.println("[MPU6050] Peringatan: Sensor tidak terdeteksi di 0x68, menggunakan baseline sintetis.");
  }

  lastSampleMicros = micros();
}

void loop() {
  handleSerialCommands();
  checkModeButton();

  // Jaga interval sampling tepat 100 Hz (10,000 mikrodetik)
  unsigned long nowMicros = micros();
  if (nowMicros - lastSampleMicros < SAMPLE_INTERVAL_US) {
    return;
  }
  lastSampleMicros = nowMicros;

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  readAccelerometerMs2(ax, ay, az);

  // Mode 2: CSV Data Forwarder untuk Edge Impulse Studio CLI
  if (currentMode == MODE_DATA_FORWARDER) {
    Serial.printf("%.3f,%.3f,%.3f\n", ax, ay, az);
    return;
  }

  // Mode 1: Kumpulkan sampel ke dalam Sliding Window Buffer & jalankan Edge AI
  bufAx[sampleCount] = ax;
  bufAy[sampleCount] = ay;
  bufAz[sampleCount] = az;
  sampleCount++;

  if (sampleCount >= WINDOW_SAMPLES) {
    EdgeFeatureVector feat = extractEdgeFeatures(bufAx, bufAy, bufAz, WINDOW_SAMPLES);
    InferenceResult res    = runEdgeInference(bufAx, bufAy, bufAz, WINDOW_SAMPLES);

    updateHardwareIndicators(res.predictedClass);

    Serial.printf("[EDGE-AI] Status: %-17s | AnomalyScore: %.2f | Conf(I/N/A): %.0f%%/%.0f%%/%.0f%% | "
                  "RMS: %.2f m/s2 | P2P: %.2f | Crest: %.2f | DSP+NN: %lu+%lu us\n",
                  res.label,
                  res.anomalyScore,
                  res.probIdle * 100.0f,
                  res.probNormal * 100.0f,
                  res.probAnomaly * 100.0f,
                  feat.compositeRms,
                  feat.compositePeak2Peak,
                  feat.maxCrestFactor,
                  (unsigned long)res.dspTimeUs,
                  (unsigned long)res.classificationTimeUs);

    // Geser window 50% (32 sampel) agar inferensi berikutnya berjalan mulus (Sliding Window)
    shiftSlidingWindow();
  }
}
