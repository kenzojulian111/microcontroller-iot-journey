/**
 * @file ml_electronic_nose_gas.ino
 * @brief Project 12: Multi-Gas Electronic Nose (E-Nose) with Edge Neural Network & MQTT Telemetry
 *
 * Target Hardware: ESP32
 * Sensor Array:
 *   - MQ-135 (GPIO 34 / ADC1_CH6): Air Quality, NH3, Benzene, Alcohol, Smoke
 *   - MQ-2   (GPIO 35 / ADC1_CH7): Combustible Gas, LPG, Propane, Hydrogen, Smoke
 *   - DHT22  (GPIO 4): Ambient Temperature (°C) & Relative Humidity (%) for T/RH drift compensation
 * Actuators:
 *   - Piezo Alarm Buzzer (GPIO 18)
 *   - Hazard Status LEDs: Green Clean LED (GPIO 19), Red Hazard LED (GPIO 23)
 *
 * Signal Processing & TinyML Pipeline:
 *   1. Voltage divider conversion: Rs = R_L * (V_cc - V_out) / V_out
 *   2. Empirical T/RH baseline compensation factor C(T, RH)
 *   3. Normalized resistance ratios (Rs / R0)_135, (Rs / R0)_2, and cross-sensitivity log ratio
 *   4. 2-Layer Fully Connected Neural Network (6 -> 12 ReLU -> 4 Softmax) in `enose_nn_model.h`
 *   5. Real-time JSON telemetry and hazard alerts over MQTT (`PubSubClient.h`)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "secrets.example.h"
#include "enose_nn_model.h"

// ============================================================================
// Hardware Pin Definitions & Sensor Constants
// ============================================================================
#define MQ135_ADC_PIN        34
#define MQ2_ADC_PIN          35
#define DHT_PIN              4
#define DHT_TYPE             DHT22

#define BUZZER_PIN           18
#define LED_CLEAN_PIN        19
#define LED_HAZARD_PIN       23
#define CALIB_BTN_PIN        15

#define ADC_MAX_COUNTS       4095.0f
#define ADC_REF_VOLTAGE      3.3f
#define MQ_LOAD_RESISTOR_K   10.0f   // 10 kOhm load resistor R_L on breakout board

// Default calibrated clean-air baseline resistances R0 (kOhm) at 20°C, 65% RH
#define DEFAULT_R0_MQ135_K   28.5f
#define DEFAULT_R0_MQ2_K     18.0f

#define INFERENCE_INTERVAL_MS 1000UL
#define MQTT_PUBLISH_EVERY_N  2      // Publish regular telemetry every 2 seconds (alerts publish immediately)

// ============================================================================
// Global Instances & State Variables
// ============================================================================
DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

float r0Mq135 = DEFAULT_R0_MQ135_K;
float r0Mq2   = DEFAULT_R0_MQ2_K;

float prevRatio135 = 1.0f;
uint32_t lastSampleMs = 0;
uint32_t sampleCounter = 0;
uint8_t prevPredictedClass = GAS_CLEAN_AIR;

// ============================================================================
// Gas Sensor Physics & Compensation Functions
// ============================================================================

/**
 * @brief Convert 12-bit ESP32 ADC reading into sensing element resistance Rs (kOhm)
 *        Rs = R_L * (V_cc - V_adc) / V_adc
 */
float computeSensorResistanceKOhm(int adcCounts) {
    float clampedCounts = constrain(static_cast<float>(adcCounts), 15.0f, ADC_MAX_COUNTS - 15.0f);
    float vOut = (clampedCounts / ADC_MAX_COUNTS) * ADC_REF_VOLTAGE;
    // Higher gas concentration increases sensor conductivity -> increases V_out -> lowers Rs
    float rsKOhm = MQ_LOAD_RESISTOR_K * ((ADC_REF_VOLTAGE - vOut) / vOut);
    return rsKOhm;
}

/**
 * @brief Compute temperature & humidity drift compensation factor C(T, RH)
 *        for SnO2 metal-oxide semiconductor (MOS) gas sensors.
 *        Reference baseline: T_ref = 20°C, RH_ref = 65%
 */
float computeThermoHygroCorrection(float tempC, float rhPct) {
    // Empirical polynomial fit from MQ-135 / MQ-2 datasheet curves:
    // C(T, RH) = 1.0 - 0.012*(T - 20) - 0.0035*(RH - 65) + 0.00012*(T - 20)^2
    float dT  = tempC - 20.0f;
    float dRH = rhPct - 65.0f;
    float factor = 1.0f - (0.012f * dT) - (0.0035f * dRH) + (0.00012f * dT * dT);
    return constrain(factor, 0.55f, 1.50f);
}

/**
 * @brief Perform clean-air baseline R0 calibration for both MQ sensors
 */
void calibrateCleanAirBaseline(float tempC, float rhPct) {
    Serial.println(F("[CALIB] Sampling clean air baseline R0 across 16 readings..."));
    float sumRs135 = 0.0f;
    float sumRs2   = 0.0f;
    float corr = computeThermoHygroCorrection(tempC, rhPct);

    for (uint8_t i = 0; i < 16; i++) {
        sumRs135 += computeSensorResistanceKOhm(analogRead(MQ135_ADC_PIN)) / corr;
        sumRs2   += computeSensorResistanceKOhm(analogRead(MQ2_ADC_PIN))   / corr;
        delay(25);
    }

    r0Mq135 = sumRs135 / 16.0f;
    r0Mq2   = sumRs2   / 16.0f;
    prevRatio135 = 1.0f;

    Serial.printf("[CALIB] Completed -> R0_MQ135 = %.2f kOhm | R0_MQ2 = %.2f kOhm\n", r0Mq135, r0Mq2);
}

// ============================================================================
// Wi-Fi & MQTT Telemetry Functions
// ============================================================================
void connectWiFiNonBlocking() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("[WIFI] Connecting to SSID '%s'...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 3500UL) {
        delay(250);
        Serial.print('.');
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(F(" Offline mode (will retry in background)."));
    }
}

void ensureMqttConnected() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqttClient.connected()) {
        mqttClient.loop();
        return;
    }

    String clientId = String(MQTT_CLIENT_ID) + "-" + String(random(1000, 9999));
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
        Serial.printf("[MQTT] Connected to broker %s:%d as %s\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT, clientId.c_str());
    }
}

void publishEnoseTelemetry(
    const float features[ENOSE_INPUT_DIM],
    const float probs[ENOSE_OUTPUT_DIM],
    uint8_t predClass,
    uint32_t inferenceUs,
    bool isUrgentAlert
) {
    char payload[384];
    snprintf(
        payload,
        sizeof(payload),
        "{\"sample\":%lu,\"class_id\":%u,\"class_label\":\"%s\",\"confidence\":%.3f,"
        "\"rs_r0_mq135\":%.3f,\"rs_r0_mq2\":%.3f,\"log_ratio\":%.3f,\"dratio_dt\":%.3f,"
        "\"temp_c\":%.1f,\"humidity_pct\":%.1f,"
        "\"probs\":[%.3f,%.3f,%.3f,%.3f],\"inference_us\":%lu}",
        sampleCounter,
        predClass,
        ENOSE_CLASS_NAMES[predClass],
        probs[predClass],
        features[0],
        features[1],
        features[2],
        features[3],
        features[4],
        features[5],
        probs[0], probs[1], probs[2], probs[3],
        inferenceUs
    );

    if (mqttClient.connected()) {
        mqttClient.publish(MQTT_TOPIC_TELEMETRY, payload);
        if (isUrgentAlert) {
            mqttClient.publish(MQTT_TOPIC_ALERT, payload, true);
        }
    }
}

// ============================================================================
// Setup & Main Loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_CLEAN_PIN, OUTPUT);
    pinMode(LED_HAZARD_PIN, OUTPUT);
    pinMode(CALIB_BTN_PIN, INPUT_PULLUP);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_CLEAN_PIN, HIGH);
    digitalWrite(LED_HAZARD_PIN, LOW);

    analogReadResolution(12);
    dht.begin();

    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setBufferSize(512);
    connectWiFiNonBlocking();

    Serial.println(F("================================================================================"));
    Serial.println(F(" Project 12: TinyML Electronic Nose (MQ-135 + MQ-2 + DHT22 + 2-Layer FCNN)"));
    Serial.println(F(" Send 'CALIB' over Serial or press GPIO 15 button to zero clean-air R0 baseline."));
    Serial.println(F("================================================================================"));
}

void loop() {
    // Check Serial or Hardware Button for baseline R0 recalibration
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        if (cmd == "CALIB") {
            float t = dht.readTemperature();
            float h = dht.readHumidity();
            calibrateCleanAirBaseline(isnan(t) ? 24.0f : t, isnan(h) ? 55.0f : h);
        }
    }

    uint32_t now = millis();
    if (now - lastSampleMs < INFERENCE_INTERVAL_MS) {
        return;
    }
    float dtSec = (now - lastSampleMs) / 1000.0f;
    lastSampleMs = now;
    sampleCounter++;

    // 1. Read Ambient Temperature & Relative Humidity
    float tempC = dht.readTemperature();
    float rhPct = dht.readHumidity();
    if (isnan(tempC) || isnan(rhPct)) {
        tempC = 24.5f;
        rhPct = 55.0f;
    }

    if (digitalRead(CALIB_BTN_PIN) == LOW) {
        calibrateCleanAirBaseline(tempC, rhPct);
    }

    // 2. Read MQ-135 and MQ-2 Analog Channels and Compute T/RH-Compensated Rs/R0
    int adc135 = analogRead(MQ135_ADC_PIN);
    int adc2   = analogRead(MQ2_ADC_PIN);

    float rs135 = computeSensorResistanceKOhm(adc135);
    float rs2   = computeSensorResistanceKOhm(adc2);
    float corr  = computeThermoHygroCorrection(tempC, rhPct);

    float ratio135 = constrain((rs135 / corr) / r0Mq135, 0.05f, 1.50f);
    float ratio2   = constrain((rs2   / corr) / r0Mq2,   0.05f, 1.50f);

    // 3. Compute Cross-Sensitivity Log Ratio & Temporal Derivative
    float logCrossRatio = logf(ratio135 / ratio2);
    float dRatio135_dt  = (sampleCounter == 1) ? 0.0f : ((ratio135 - prevRatio135) / dtSec);
    prevRatio135 = ratio135;

    // 4. Construct 6-Element Feature Vector & Run FCNN Inference
    float features[ENOSE_INPUT_DIM] = {
        ratio135,
        ratio2,
        logCrossRatio,
        dRatio135_dt,
        tempC,
        rhPct
    };

    float probabilities[ENOSE_OUTPUT_DIM] = {0};
    uint32_t t0 = micros();
    uint8_t predClass = enosePredictNN(features, probabilities);
    uint32_t inferenceUs = micros() - t0;

    // 5. Actuate Local Hazard Indicators (LEDs + Buzzer)
    if (predClass == GAS_CLEAN_AIR) {
        digitalWrite(LED_CLEAN_PIN, HIGH);
        digitalWrite(LED_HAZARD_PIN, LOW);
        noTone(BUZZER_PIN);
    } else if (predClass == GAS_LPG_LEAK_HAZARD) {
        digitalWrite(LED_CLEAN_PIN, LOW);
        digitalWrite(LED_HAZARD_PIN, HIGH);
        tone(BUZZER_PIN, 2600, 450); // High-priority LPG leak alarm
    } else {
        digitalWrite(LED_CLEAN_PIN, LOW);
        digitalWrite(LED_HAZARD_PIN, (sampleCounter % 2 == 0) ? HIGH : LOW);
        if (predClass == GAS_CIGARETTE_SMOKE) {
            tone(BUZZER_PIN, 1200, 150);
        } else {
            noTone(BUZZER_PIN);
        }
    }

    // 6. Publish Telemetry & Urgent Alerts via MQTT
    ensureMqttConnected();
    bool isUrgentAlert = (predClass != GAS_CLEAN_AIR && predClass != prevPredictedClass);
    if (isUrgentAlert || (sampleCounter % MQTT_PUBLISH_EVERY_N == 0)) {
        publishEnoseTelemetry(features, probabilities, predClass, inferenceUs, isUrgentAlert);
    }
    prevPredictedClass = predClass;

    // 7. Output Structured Serial Log
    Serial.printf(
        "[#%04lu] MQ135(Rs/R0)=%.2f | MQ2(Rs/R0)=%.2f | ln(R135/R2)=%+5.2f | dR/dt=%+5.2f | T=%.1fC RH=%.0f%% | "
        "Class=%u (%s, %.1f%%) [%.2f, %.2f, %.2f, %.2f] | %luus\n",
        sampleCounter,
        features[0],
        features[1],
        features[2],
        features[3],
        features[4],
        features[5],
        predClass,
        ENOSE_CLASS_NAMES[predClass],
        probabilities[predClass] * 100.0f,
        probabilities[0], probabilities[1], probabilities[2], probabilities[3],
        inferenceUs
    );
}
