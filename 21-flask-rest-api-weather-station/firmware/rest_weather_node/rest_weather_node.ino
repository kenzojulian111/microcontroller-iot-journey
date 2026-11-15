/*
 * Project 21: Authenticated Wi-Fi REST API Weather Station Node
 * Target Architectures: ESP32 & ESP8266
 * Sensor: Bosch BME280 (I2C: Temp, Relative Humidity, Barometric Pressure)
 * Features:
 *   - HTTP POST /api/v1/telemetry with X-API-Key authentication header
 *   - Fixed-allocation circular ring buffer (32 samples) for offline store-and-forward
 *   - Non-blocking Wi-Fi reconnection state machine
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
#endif

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

Adafruit_BME280 bme;
bool bmeAvailable = false;

struct WeatherSample {
  uint32_t seq;
  float    tempC;
  float    humPct;
  float    pressHpa;
  int32_t  rssiDbm;
  uint32_t uptimeMs;
};

// Offline Store-and-Forward Ring Buffer
constexpr size_t RING_CAPACITY = 32;
WeatherSample ringBuffer[RING_CAPACITY];
size_t ringHead  = 0;
size_t ringTail  = 0;
size_t ringCount = 0;

uint32_t sampleSeq      = 0;
uint32_t lastSampleTime = 0;
constexpr uint32_t SAMPLE_PERIOD_MS = 5000UL;

void enqueueSample(const WeatherSample& s) {
  ringBuffer[ringHead] = s;
  ringHead = (ringHead + 1) % RING_CAPACITY;
  if (ringCount < RING_CAPACITY) {
    ringCount++;
  } else {
    // Overwrite oldest sample on full buffer
    ringTail = (ringTail + 1) % RING_CAPACITY;
  }
}

bool peekSample(WeatherSample& out) {
  if (ringCount == 0) return false;
  out = ringBuffer[ringTail];
  return true;
}

void popSample() {
  if (ringCount == 0) return;
  ringTail = (ringTail + 1) % RING_CAPACITY;
  ringCount--;
}

WeatherSample acquireWeatherSample() {
  WeatherSample s;
  s.seq = ++sampleSeq;
  if (bmeAvailable) {
    s.tempC    = bme.readTemperature();
    s.humPct   = bme.readHumidity();
    s.pressHpa = bme.readPressure() / 100.0f;
  } else {
    // Deterministic fallback synthetic weather profile for Wokwi / bench test
    float phase = static_cast<float>(s.seq) * 0.12f;
    s.tempC    = 24.5f + 2.2f * sinf(phase);
    s.humPct   = 60.0f - 5.0f * sinf(phase);
    s.pressHpa = 1013.25f + 1.8f * cosf(phase * 0.5f);
  }
  s.rssiDbm  = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  s.uptimeMs = millis();
  return s;
}

bool transmitSampleHttp(const WeatherSample& s) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, API_ENDPOINT_URL)) {
    return false;
  }

  http.setTimeout(3500);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY_SECRET);

  char payload[256];
  snprintf(
    payload,
    sizeof(payload),
    "{\"node_id\":\"%s\",\"seq\":%lu,\"temp_c\":%.2f,\"hum_pct\":%.2f,\"press_hpa\":%.2f,\"rssi_dbm\":%ld,\"uptime_ms\":%lu}",
    NODE_ID,
    static_cast<unsigned long>(s.seq),
    s.tempC,
    s.humPct,
    s.pressHpa,
    static_cast<long>(s.rssiDbm),
    static_cast<unsigned long>(s.uptimeMs)
  );

  int httpCode = http.POST(reinterpret_cast<uint8_t*>(payload), strlen(payload));
  http.end();

  Serial.printf("[HTTP] POST seq=%lu -> status=%d (queued=%u)\n",
                static_cast<unsigned long>(s.seq), httpCode, static_cast<unsigned>(ringCount));
  return (httpCode == 200 || httpCode == 201);
}

void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;
  static uint32_t lastAttempt = 0;
  if (millis() - lastAttempt > 10000UL) {
    lastAttempt = millis();
    Serial.printf("[WiFi] Connecting to SSID: %s ...\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  bmeAvailable = bme.begin(0x76) || bme.begin(0x77);
  if (bmeAvailable) {
    Serial.println(F("[BME280] Initialized I2C environmental sensor."));
  } else {
    Serial.println(F("[BME280] Not detected on 0x76/0x77 — using synthetic fallback model."));
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastSampleTime = millis();
}

void loop() {
  ensureWifiConnected();

  uint32_t now = millis();
  if (now - lastSampleTime >= SAMPLE_PERIOD_MS) {
    lastSampleTime += SAMPLE_PERIOD_MS;
    WeatherSample sample = acquireWeatherSample();
    enqueueSample(sample);
  }

  // Drain up to 4 queued samples per loop iteration when Wi-Fi is online
  uint8_t burst = 0;
  WeatherSample pending;
  while (burst < 4 && peekSample(pending)) {
    if (transmitSampleHttp(pending)) {
      popSample();
      burst++;
    } else {
      break; // Keep in ring buffer for next retry cycle
    }
  }
}
