/*
 * Project 20: Flask Serial Sensor Telemetry Logger
 * Target Architectures: ATmega328P (Arduino Uno/Nano) & ESP32
 * Sensors: DHT22 (Temperature & Relative Humidity) + LDR Photoresistor Divider (ADC)
 * Output Format: JSON Lines (NDJSON) at 115200 8N1 every 2000 ms
 *
 * Example Output Frame:
 *   {"seq":42,"temp_c":26.40,"hum_pct":61.20,"lux_raw":512,"v_adc":2.50,"uptime_ms":84002}
 */

#include <Arduino.h>
#include <DHT.h>

#define DHTTYPE DHT22

#if defined(ESP32)
  constexpr uint8_t PIN_DHT    = 4;
  constexpr uint8_t PIN_LDR    = 34; // ADC1_CH6 (input-only)
  constexpr float   ADC_MAX    = 4095.0f;
  constexpr float   V_REF      = 3.3f;
#else
  constexpr uint8_t PIN_DHT    = 2;
  constexpr uint8_t PIN_LDR    = A0;
  constexpr float   ADC_MAX    = 1023.0f;
  constexpr float   V_REF      = 5.0f;
#endif

DHT dht(PIN_DHT, DHTTYPE);

constexpr uint32_t SAMPLE_INTERVAL_MS = 2000UL;
uint32_t lastSampleMs = 0;
uint32_t seqNumber    = 0;

// Oversample ADC 16x to attenuate high-frequency switching noise
uint16_t readOversampledAdc(uint8_t pin) {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < 16; i++) {
    acc += analogRead(pin);
  }
  return static_cast<uint16_t>(acc >> 4);
}

void emitTelemetryFrame() {
  float tempC  = dht.readTemperature();
  float humPct = dht.readHumidity();
  uint16_t rawAdc = readOversampledAdc(PIN_LDR);

  // Normalize raw ADC to 10-bit scale (0..1023) for cross-platform consistency
#if defined(ESP32)
  uint16_t luxRaw10Bit = static_cast<uint16_t>((static_cast<uint32_t>(rawAdc) * 1023UL) / 4095UL);
#else
  uint16_t luxRaw10Bit = rawAdc;
#endif

  float vAdc = (static_cast<float>(rawAdc) / ADC_MAX) * V_REF;

  bool dhtValid = !isnan(tempC) && !isnan(humPct);
  if (!dhtValid) {
    // Fallback diagnostic values if DHT22 is disconnected in bench test
    tempC  = 25.0f + 1.5f * sinf(static_cast<float>(seqNumber) * 0.1f);
    humPct = 55.0f + 3.0f * cosf(static_cast<float>(seqNumber) * 0.1f);
  }

  seqNumber++;

  Serial.print(F("{\"seq\":"));
  Serial.print(seqNumber);
  Serial.print(F(",\"temp_c\":"));
  Serial.print(tempC, 2);
  Serial.print(F(",\"hum_pct\":"));
  Serial.print(humPct, 2);
  Serial.print(F(",\"lux_raw\":"));
  Serial.print(luxRaw10Bit);
  Serial.print(F(",\"v_adc\":"));
  Serial.print(vAdc, 3);
  Serial.print(F(",\"sensor_ok\":"));
  Serial.print(dhtValid ? F("true") : F("false"));
  Serial.print(F(",\"uptime_ms\":"));
  Serial.print(millis());
  Serial.println(F("}"));
}

void setup() {
  Serial.begin(115200);
#if defined(ESP32)
  analogReadResolution(12);
#endif
  dht.begin();
  delay(1000);
  lastSampleMs = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs += SAMPLE_INTERVAL_MS;
    emitTelemetryFrame();
  }
}
