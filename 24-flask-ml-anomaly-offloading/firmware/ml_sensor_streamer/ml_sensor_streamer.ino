/**
 * ============================================================================
 * Project 24: Server-Side Machine Learning Inference Offloading
 * File: ml_sensor_streamer.ino
 * Target MCUs:
 *   - Arduino Uno / Nano (ATmega328P, 2 KB SRAM) -> Streams JSON over USB UART
 *   - ESP32 DevKit V1 (520 KB SRAM)              -> Streams via Wi-Fi HTTP POST + UART
 * ============================================================================
 *
 * ARCHITECTURAL MOTIVATION:
 *   An 8-bit ATmega328P (2 KB SRAM, 16 MHz, no FPU) cannot host a multi-tree
 *   Random Forest or Isolation Forest ensemble locally. Instead, the MCU acts
 *   as a deterministic signal-conditioning front-end:
 *     1. Samples 3-axis acceleration & die temperature from an MPU6050 IMU
 *        over I2C (0x68) across a 32-sample window (fs = 200 Hz).
 *     2. Extracts a compact 6-dimensional time-domain feature vector in O(1) RAM:
 *          - vib_rms_g      : 3-axis root-mean-square vibration (g)
 *          - vib_p2p_g      : Dynamic peak-to-peak acceleration span (g)
 *          - crest_factor   : Peak-to-RMS ratio (bearing spall indicator)
 *          - temp_c         : MPU6050 die / housing temperature (deg C)
 *          - gas_ppm_ratio  : MQ-135 volatile organic / smoke ratio
 *          - motor_rpm_est  : Simulated / tachometer shaft speed
 *     3. Streams the feature vector to the Python Flask Scikit-Learn server.
 *     4. Executes the closed-loop actuation directive returned by the server:
 *          - NORMAL            : Green LED ON, Motor Relay ON (Closed)
 *          - WARNING_THROTTLE  : Yellow LED ON, intermittent chirp, Relay ON
 *          - CRITICAL_SHUTDOWN : Red LED ON, Emergency Contactor Relay TRIPPED,
 *                                continuous high-decibel alarm
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

// ============================================================================
// Cross-Platform Pin Definitions (ESP32 vs ATmega328P Arduino Uno/Nano)
// ============================================================================
#if defined(ESP32)
  constexpr uint8_t PIN_I2C_SDA       = 21;
  constexpr uint8_t PIN_I2C_SCL       = 22;
  constexpr uint8_t PIN_MQ135_ADC     = 34; // ADC1_CH6
  constexpr uint8_t PIN_RELAY_CUTOFF  = 26; // High-power contactor relay
  constexpr uint8_t PIN_BUZZER        = 25;
  constexpr uint8_t PIN_LED_NORMAL    = 32; // Green
  constexpr uint8_t PIN_LED_WARNING   = 33; // Yellow/Amber
  constexpr uint8_t PIN_LED_CRITICAL  = 27; // Red
  constexpr uint8_t PIN_BTN_FAULT_INJ = 14; // Cycles synthetic fault profiles
  constexpr float   ADC_MAX_COUNTS    = 4095.0f;
#else
  constexpr uint8_t PIN_MQ135_ADC     = A0;
  constexpr uint8_t PIN_RELAY_CUTOFF  = 7;
  constexpr uint8_t PIN_BUZZER        = 8;
  constexpr uint8_t PIN_LED_NORMAL    = 4;
  constexpr uint8_t PIN_LED_WARNING   = 5;
  constexpr uint8_t PIN_LED_CRITICAL  = 6;
  constexpr uint8_t PIN_BTN_FAULT_INJ = 2;
  constexpr float   ADC_MAX_COUNTS    = 1023.0f;
#endif

// ============================================================================
// MPU6050 Register Map & Sampling Configuration
// ============================================================================
constexpr uint8_t  MPU6050_I2C_ADDR   = 0x68;
constexpr uint8_t  MPU_REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t  MPU_REG_ACCEL_XOUT = 0x3B;
constexpr uint16_t WINDOW_SAMPLES     = 32;
constexpr uint16_t SAMPLE_DELAY_MS    = 5;   // ~200 Hz sampling rate
constexpr uint32_t STREAM_INTERVAL_MS = 1500;

enum OperatingRegime : uint8_t {
  REGIME_NOMINAL          = 0,
  REGIME_BEARING_WEAR     = 1,
  REGIME_THERMAL_OVERLOAD = 2,
  REGIME_CAVITATION_SURGE = 3
};

struct FeatureVector {
  float vibRmsG;
  float vibPeakToPeakG;
  float crestFactor;
  float tempCelsius;
  float gasRatio;
  float motorRpm;
};

bool            mpuPresent        = false;
OperatingRegime injectedRegime    = REGIME_NOMINAL;
String          activeCommand     = "NORMAL";
uint32_t        lastStreamMillis  = 0;
uint32_t        lastButtonMillis  = 0;

// ============================================================================
// Direct Register-Level MPU6050 Driver (Zero External Library Dependency)
// ============================================================================
bool initMpu6050() {
  Wire.beginTransmission(MPU6050_I2C_ADDR);
  Wire.write(MPU_REG_PWR_MGMT_1);
  Wire.write(0x00); // Wake up MPU6050 from sleep
  if (Wire.endTransmission(true) != 0) {
    return false;
  }
  return true;
}

bool readMpu6050Sample(float& axG, float& ayG, float& azG, float& tempC) {
  Wire.beginTransmission(MPU6050_I2C_ADDR);
  Wire.write(MPU_REG_ACCEL_XOUT);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  uint8_t bytesRead = Wire.requestFrom(MPU6050_I2C_ADDR, (uint8_t)8, (uint8_t)true);
  if (bytesRead < 8) {
    return false;
  }

  int16_t rawAx   = (Wire.read() << 8) | Wire.read();
  int16_t rawAy   = (Wire.read() << 8) | Wire.read();
  int16_t rawAz   = (Wire.read() << 8) | Wire.read();
  int16_t rawTemp = (Wire.read() << 8) | Wire.read();

  // Default +/-2g sensitivity = 16384 LSB/g
  axG   = rawAx / 16384.0f;
  ayG   = rawAy / 16384.0f;
  azG   = rawAz / 16384.0f;
  tempC = (rawTemp / 340.0f) + 36.53f;
  return true;
}

// ============================================================================
// Windowed Time-Domain Feature Extraction
// ============================================================================
FeatureVector acquireFeatureVector() {
  float sumSqMag = 0.0f;
  float minMag   = 100.0f;
  float maxMag   = 0.0f;
  float tempSum  = 0.0f;

  for (uint16_t i = 0; i < WINDOW_SAMPLES; i++) {
    float ax = 0.02f, ay = -0.01f, az = 1.00f, tC = 41.5f;

    if (mpuPresent && readMpu6050Sample(ax, ay, az, tC)) {
      // Remove 1g static gravity component along Z-axis
      az -= 1.0f;
    } else {
      // Deterministic harmonic synthesizer when running headless
      float phase = (millis() + i * SAMPLE_DELAY_MS) * 0.035f;
      ax = 0.09f * sinf(phase);
      ay = 0.08f * cosf(phase * 1.3f);
      az = 0.06f * sinf(phase * 2.1f);
      tC = 42.0f;
    }

    // Superimpose fault signatures if user pressed the Fault Injection button
    if (injectedRegime == REGIME_BEARING_WEAR) {
      float spike = (i % 6 == 0) ? 1.85f : 0.35f;
      ax += spike;
      tC += 11.5f;
    } else if (injectedRegime == REGIME_THERMAL_OVERLOAD) {
      ax += 0.65f;
      ay += 0.55f;
      tC += 36.0f;
    } else if (injectedRegime == REGIME_CAVITATION_SURGE) {
      float burst = (i % 4 == 0) ? 2.95f : 0.85f;
      ax += burst;
      ay -= burst * 0.6f;
      tC += 28.0f;
    }

    float mag = sqrtf(ax * ax + ay * ay + az * az);
    sumSqMag += (mag * mag);
    if (mag < minMag) minMag = mag;
    if (mag > maxMag) maxMag = mag;
    tempSum += tC;

    delay(SAMPLE_DELAY_MS);
  }

  FeatureVector fv;
  fv.vibRmsG        = sqrtf(sumSqMag / (float)WINDOW_SAMPLES);
  fv.vibPeakToPeakG = maxMag - minMag;
  fv.crestFactor    = (fv.vibRmsG > 0.001f) ? (maxMag / fv.vibRmsG) : 1.0f;
  fv.tempCelsius    = tempSum / (float)WINDOW_SAMPLES;

  // Read MQ-135 Analog Gas Sensor Ratio (normalized Rs/R0 approximation)
  uint16_t rawAdc = analogRead(PIN_MQ135_ADC);
  float baseGas   = 0.85f + (rawAdc / ADC_MAX_COUNTS) * 2.5f;
  if (injectedRegime == REGIME_THERMAL_OVERLOAD) baseGas += 2.4f;
  if (injectedRegime == REGIME_CAVITATION_SURGE) baseGas += 1.6f;
  fv.gasRatio = baseGas;

  // Estimated motor shaft RPM
  fv.motorRpm = (activeCommand == "CRITICAL_SHUTDOWN")
                  ? 0.0f
                  : (activeCommand == "WARNING_THROTTLE" ? 1150.0f : 1785.0f);
  return fv;
}

// ============================================================================
// Closed-Loop Actuator Execution
// ============================================================================
void applyActuatorCommand(const String& cmd) {
  activeCommand = cmd;

  if (cmd == "CRITICAL_SHUTDOWN") {
    // Trip emergency contactor relay (LOW = Cut power to motor)
    digitalWrite(PIN_RELAY_CUTOFF, LOW);
    digitalWrite(PIN_LED_NORMAL, LOW);
    digitalWrite(PIN_LED_WARNING, LOW);
    digitalWrite(PIN_LED_CRITICAL, HIGH);
    tone(PIN_BUZZER, 2400, 450);
  } else if (cmd == "WARNING_THROTTLE") {
    // Keep contactor engaged but signal throttling warning
    digitalWrite(PIN_RELAY_CUTOFF, HIGH);
    digitalWrite(PIN_LED_NORMAL, LOW);
    digitalWrite(PIN_LED_WARNING, HIGH);
    digitalWrite(PIN_LED_CRITICAL, LOW);
    tone(PIN_BUZZER, 1480, 110);
  } else {
    // Nominal operation -> contactor energized, green indicator ON
    digitalWrite(PIN_RELAY_CUTOFF, HIGH);
    digitalWrite(PIN_LED_NORMAL, HIGH);
    digitalWrite(PIN_LED_WARNING, LOW);
    digitalWrite(PIN_LED_CRITICAL, LOW);
    noTone(PIN_BUZZER);
  }
}

// ============================================================================
// Local Edge Heuristic Fallback (Used only when Flask HTTP/Serial is offline)
// ============================================================================
String evaluateFallbackDirective(const FeatureVector& fv) {
  if (fv.vibRmsG > 1.35f || fv.tempCelsius > 72.0f || fv.gasRatio > 2.8f) {
    return "CRITICAL_SHUTDOWN";
  }
  if (fv.vibRmsG > 0.55f || fv.crestFactor > 2.8f || fv.tempCelsius > 54.0f) {
    return "WARNING_THROTTLE";
  }
  return "NORMAL";
}

// ============================================================================
// Setup & Initialization
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_RELAY_CUTOFF, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_NORMAL, OUTPUT);
  pinMode(PIN_LED_WARNING, OUTPUT);
  pinMode(PIN_LED_CRITICAL, OUTPUT);
  pinMode(PIN_BTN_FAULT_INJ, INPUT_PULLUP);

  // Energize motor contactor in nominal startup state
  applyActuatorCommand("NORMAL");

#if defined(ESP32)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
#else
  Wire.begin();
#endif

  mpuPresent = initMpu6050();
  Serial.println(F("\n================================================================"));
  Serial.println(F("Project 24: Server-Side ML Anomaly Offloading Node"));
  Serial.print(F("MPU6050 I2C Status: "));
  Serial.println(mpuPresent ? F("ONLINE (0x68)") : F("SYNTHETIC HARMONIC FALLBACK"));

#if defined(ESP32)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 3500) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WIFI] Connected: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[WIFI] Offline simulation mode (UART + Edge Fallback active)."));
  }
#endif
  Serial.println(F("Press Button (GPIO14 / D2) to cycle Fault Injection Regimes."));
  Serial.println(F("================================================================"));
}

// ============================================================================
// Main Non-Blocking Telemetry & Control Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // 1. Poll Fault Injection Button
  if (digitalRead(PIN_BTN_FAULT_INJ) == LOW && (now - lastButtonMillis) > 400) {
    lastButtonMillis = now;
    injectedRegime = static_cast<OperatingRegime>((injectedRegime + 1) % 4);
    const char* regimeNames[] = {
      "NOMINAL_BASELINE",
      "BEARING_SPALL_WEAR",
      "THERMAL_GAS_OVERLOAD",
      "CAVITATION_CRITICAL_SURGE"
    };
    Serial.print(F("[FAULT_INJECTOR] Switched operating regime -> "));
    Serial.println(regimeNames[injectedRegime]);
  }

  // 2. Check for incoming Serial actuator override commands from Python bridge
  //    Format: `CMD:NORMAL`, `CMD:WARNING_THROTTLE`, or `CMD:CRITICAL_SHUTDOWN`
  if (Serial.available() > 0) {
    String rxLine = Serial.readStringUntil('\n');
    rxLine.trim();
    if (rxLine.startsWith("CMD:")) {
      String cmd = rxLine.substring(4);
      cmd.trim();
      applyActuatorCommand(cmd);
      Serial.print(F("[ACK <- SERIAL] Executed Closed-Loop Command: "));
      Serial.println(cmd);
    }
  }

  // 3. Periodically sample window, stream feature vector, and actuate
  if ((now - lastStreamMillis) >= STREAM_INTERVAL_MS) {
    lastStreamMillis = now;
    FeatureVector fv = acquireFeatureVector();

    // Build compact JSON feature vector
    String jsonPayload = String("{\"node_id\":\"") + NODE_ID +
                         "\",\"vib_rms_g\":" + String(fv.vibRmsG, 3) +
                         ",\"vib_p2p_g\":" + String(fv.vibPeakToPeakG, 3) +
                         ",\"crest_factor\":" + String(fv.crestFactor, 2) +
                         ",\"temp_c\":" + String(fv.tempCelsius, 2) +
                         ",\"gas_ppm_ratio\":" + String(fv.gasRatio, 2) +
                         ",\"motor_rpm\":" + String(fv.motorRpm, 0) + "}";

    // Always emit JSON line over UART so Arduino Uno/Nano USB Serial bridges work
    Serial.println(jsonPayload);

    bool serverReplied = false;
    String serverCommand = "NORMAL";

#if defined(ESP32)
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;
      String url = String("http://") + FLASK_HOST + ":" + String(FLASK_PORT) + INFER_ENDPOINT;
      if (http.begin(client, url)) {
        http.setTimeout(1800);
        http.addHeader("Content-Type", "application/json");
        int code = http.POST(jsonPayload);
        if (code == 200) {
          String resp = http.getString();
          int cmdIdx = resp.indexOf("\"command\":\"");
          if (cmdIdx >= 0) {
            int start = cmdIdx + 11;
            int end   = resp.indexOf('"', start);
            if (end > start) {
              serverCommand = resp.substring(start, end);
              serverReplied = true;
            }
          }
          Serial.printf("[ML <- FLASK] %s\n", resp.c_str());
        }
        http.end();
      }
    }
#endif

    if (!serverReplied) {
      serverCommand = evaluateFallbackDirective(fv);
      Serial.print(F("[CLOSED_LOOP] Actuator Directive: "));
      Serial.println(serverCommand);
    }

    applyActuatorCommand(serverCommand);
  }
}
