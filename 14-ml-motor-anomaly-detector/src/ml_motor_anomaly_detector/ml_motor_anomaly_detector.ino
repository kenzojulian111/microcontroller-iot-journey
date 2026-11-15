/**
 * @file ml_motor_anomaly_detector.ino
 * @brief Project 14: Unsupervised Motor Predictive Maintenance & Anomaly Detector
 *        (8 -> 3 -> 8 Symmetric Autoencoder + K-Means Clustering + On-Device Calibration)
 *
 * Target Hardware: ESP32
 * Sensors & Inputs:
 *   - 3-Axis MEMS Accelerometer (MPU6050 / ADXL345 on I2C 0x68/0x53) sampled at 200 Hz
 *   - Fault / Vibration Load Simulator Potentiometer (GPIO 34) for interactive Wokwi testing
 *   - On-Device Self-Learning Calibration Button (GPIO 15) / Serial Command `CALIBRATE`
 * Actuators:
 *   - Protective Motor Cutoff Relay (GPIO 14 — trips OFF automatically on severe anomaly)
 *   - Status LEDs: Green Nominal (GPIO 19), Yellow Warning (GPIO 18), Red Critical Trip (GPIO 23)
 *
 * Predictive Maintenance Capabilities:
 *   - Extracts an 8-dimensional time/frequency vibration signature every 128 samples (640 ms)
 *   - Computes Autoencoder Reconstruction Error (MSE) + K-Means Centroid assignment
 *   - Interactive On-Device Calibration (`CALIBRATE`) learns the baseline vibration signature of
 *     any fan, pump, or compressor over 10 windows without retraining in Python!
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "motor_autoencoder.h"

// ============================================================================
// Hardware Pin Definitions & Sampling Constants
// ============================================================================
#define I2C_SDA_PIN            21
#define I2C_SCL_PIN            22

#define FAULT_SIM_POT_PIN      34   // Wokwi dial to sweep through Nominal -> Imbalance -> Bearing -> Loose Bolt
#define CALIB_BTN_PIN          15   // Pushbutton to trigger On-Device Self-Learning Calibration

#define RELAY_CUTOFF_PIN       14   // Protective motor power relay (HIGH = Motor Running, LOW = Tripped)
#define LED_NOMINAL_PIN        19
#define LED_WARNING_PIN        18
#define LED_CRITICAL_PIN       23

#define VIB_WINDOW_SAMPLES     128
#define VIB_SAMPLE_RATE_HZ     200
#define CALIBRATION_WINDOWS    10   // Number of 128-sample windows collected during CALIBRATE
#define TRIP_CONSECUTIVE_LIMIT 2    // Consecutive severe windows before latching relay cutoff

#define GRAVITY_MSS            9.80665f

// ============================================================================
// Global State & Instances
// ============================================================================
Adafruit_MPU6050 mpu;
MotorAnomalyEngine anomalyEngine;

bool hasImu = false;
bool isCalibrating = false;
uint8_t calibWindowsRemaining = 0;

bool relayTripped = false;
uint8_t consecutiveSevereCount = 0;
uint32_t frameCounter = 0;

// Forced fault mode via Serial ('NORMAL', 'IMBALANCE', 'BEARING', 'LOOSE', 'AUTO')
int8_t serialFaultOverride = -1; // -1 = use potentiometer + physical IMU

// ============================================================================
// High-Rate 3-Axis Vibration Capture & Feature Extraction
// ============================================================================

/**
 * @brief Acquire 128 samples of 3-axis vibration at 200 Hz and compute the 8-element feature vector:
 *        [RMS_x, RMS_y, RMS_z, PeakToPeak, CrestFactor, KurtosisProxy, HFRatio, RadialVerticalRatio]
 */
void captureAndExtractVibrationFeatures(float features[MOTOR_FEAT_DIM]) {
    int potRaw = analogRead(FAULT_SIM_POT_PIN);
    float dialNorm = static_cast<float>(potRaw) / 4095.0f;

    // Determine active synthetic fault regime if overridden or rotated in Wokwi
    uint8_t simRegime = MOTOR_NOMINAL_HEALTHY;
    if (serialFaultOverride >= 0) {
        simRegime = static_cast<uint8_t>(serialFaultOverride);
    } else if (dialNorm < 0.28f) {
        simRegime = MOTOR_NOMINAL_HEALTHY;
    } else if (dialNorm < 0.55f) {
        simRegime = MOTOR_SHAFT_IMBALANCE_1X;
    } else if (dialNorm < 0.80f) {
        simRegime = MOTOR_BEARING_WEAR_SPALLING;
    } else {
        simRegime = MOTOR_LOOSE_MOUNTING_SEVERE;
    }

    float sumX2 = 0.0f, sumY2 = 0.0f, sumZ2 = 0.0f;
    float sumMag2 = 0.0f, sumMag4 = 0.0f;
    float minMag = 100.0f, maxMag = 0.0f;
    float sumHfDiff2 = 0.0f;
    float prevMag = 0.0f;

    for (uint16_t i = 0; i < VIB_WINDOW_SAMPLES; i++) {
        float t = static_cast<float>(i) / static_cast<float>(VIB_SAMPLE_RATE_HZ);
        float ax = 0.0f, ay = 0.0f, az = 0.0f;

        if (hasImu) {
            sensors_event_t a, g, temp;
            mpu.getEvent(&a, &g, &temp);
            ax = a.acceleration.x / GRAVITY_MSS;
            ay = a.acceleration.y / GRAVITY_MSS;
            az = (a.acceleration.z / GRAVITY_MSS) - 1.0f;
        }

        // Superimpose physically accurate rotating-machinery rotational harmonics (30 Hz = 1800 RPM synchronous speed)
        float omega1X = 2.0f * PI * 30.0f * t;
        float omegaBF = 2.0f * PI * 85.0f * t; // Ball-pass outer race (BPFO) bearing frequency

        if (simRegime == MOTOR_NOMINAL_HEALTHY) {
            ax += 0.165f * sinf(omega1X);
            ay += 0.165f * cosf(omega1X);
            az += 0.195f * sinf(omega1X + 0.4f);
        } else if (simRegime == MOTOR_SHAFT_IMBALANCE_1X) {
            // Unbalanced rotor mass produces dominant 1X radial centrifugal force (ax, ay >> az)
            ax += 0.56f * sinf(omega1X);
            ay += 0.55f * cosf(omega1X);
            az += 0.24f * sinf(omega1X);
        } else if (simRegime == MOTOR_BEARING_WEAR_SPALLING) {
            // Bearing raceway spalling produces high-frequency repetitive micro-impacts (high Crest & Kurtosis)
            ax += 0.24f * sinf(omega1X) + 0.18f * sinf(omegaBF);
            ay += 0.24f * cosf(omega1X) + 0.18f * cosf(omegaBF);
            az += 0.26f * sinf(omega1X);
            if (i % 16 == 0) {
                ax += 0.95f;
                ay += 0.85f;
                az += 1.05f;
            }
        } else if (simRegime == MOTOR_LOOSE_MOUNTING_SEVERE) {
            // Structural looseness causes violent sub-harmonic/2X vertical Z-axis slamming
            ax += 0.45f * sinf(omega1X);
            ay += 0.44f * cosf(omega1X);
            az += 0.98f * sinf(2.0f * omega1X) + ((i % 12 == 0) ? 1.45f : -0.35f);
        }

        sumX2 += ax * ax;
        sumY2 += ay * ay;
        sumZ2 += az * az;

        float mag = sqrtf(ax * ax + ay * ay + az * az);
        float mag2 = mag * mag;
        sumMag2 += mag2;
        sumMag4 += mag2 * mag2;

        if (mag < minMag) minMag = mag;
        if (mag > maxMag) maxMag = mag;

        if (i > 0) {
            float diff = mag - prevMag;
            sumHfDiff2 += diff * diff;
        }
        prevMag = mag;
    }

    const float N = static_cast<float>(VIB_WINDOW_SAMPLES);
    float rmsX   = sqrtf(sumX2 / N);
    float rmsY   = sqrtf(sumY2 / N);
    float rmsZ   = sqrtf(sumZ2 / N);
    float rmsMag = sqrtf(sumMag2 / N) + 1e-5f;

    float peakToPeak  = maxMag - minMag;
    float crestFactor = maxMag / rmsMag;
    float kurtosis    = (sumMag4 / N) / ((sumMag2 / N) * (sumMag2 / N) + 1e-6f);
    float hfRatio     = constrain(sumHfDiff2 / (2.5f * sumMag2 + 1e-5f), 0.02f, 1.00f);
    float radialVert  = (rmsX + rmsY) / (2.0f * rmsZ + 1e-4f);

    features[0] = rmsX;
    features[1] = rmsY;
    features[2] = rmsZ;
    features[3] = peakToPeak;
    features[4] = crestFactor;
    features[5] = kurtosis;
    features[6] = hfRatio;
    features[7] = radialVert;
}

// ============================================================================
// Calibration, Relay Protection & Command Handling
// ============================================================================
void startOnDeviceCalibration() {
    isCalibrating = true;
    calibWindowsRemaining = CALIBRATION_WINDOWS;
    relayTripped = false;
    consecutiveSevereCount = 0;
    digitalWrite(RELAY_CUTOFF_PIN, HIGH);
    anomalyEngine.beginCalibration();

    Serial.println(F("\n[CALIBRATE] >>> Starting On-Device Self-Learning Calibration (10 windows)..."));
    Serial.println(F("[CALIBRATE] >>> Learning normal 8-D vibration manifold & K-Means Centroid 0..."));
}

void resetRelayTrip() {
    relayTripped = false;
    consecutiveSevereCount = 0;
    digitalWrite(RELAY_CUTOFF_PIN, HIGH);
    Serial.println(F("[PROTECTION] Motor Cutoff Relay RESET -> Motor power restored (GPIO 14 = HIGH)."));
}

void handleSerialInput() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "CALIBRATE" || cmd == "CALIB") {
        startOnDeviceCalibration();
    } else if (cmd == "RESET") {
        resetRelayTrip();
    } else if (cmd == "NORMAL") {
        serialFaultOverride = MOTOR_NOMINAL_HEALTHY;
        Serial.println(F("[SIM] Injected regime: NOMINAL_HEALTHY"));
    } else if (cmd == "IMBALANCE") {
        serialFaultOverride = MOTOR_SHAFT_IMBALANCE_1X;
        Serial.println(F("[SIM] Injected regime: SHAFT_IMBALANCE_1X"));
    } else if (cmd == "BEARING") {
        serialFaultOverride = MOTOR_BEARING_WEAR_SPALLING;
        Serial.println(F("[SIM] Injected regime: BEARING_WEAR_SPALLING"));
    } else if (cmd == "LOOSE") {
        serialFaultOverride = MOTOR_LOOSE_MOUNTING_SEVERE;
        Serial.println(F("[SIM] Injected regime: LOOSE_MOUNTING_SEVERE"));
    } else if (cmd == "AUTO") {
        serialFaultOverride = -1;
        Serial.println(F("[SIM] Returned to Wokwi Potentiometer + Physical IMU control."));
    }
}

// ============================================================================
// Setup & Main Loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(CALIB_BTN_PIN,    INPUT_PULLUP);
    pinMode(RELAY_CUTOFF_PIN, OUTPUT);
    pinMode(LED_NOMINAL_PIN,  OUTPUT);
    pinMode(LED_WARNING_PIN,  OUTPUT);
    pinMode(LED_CRITICAL_PIN, OUTPUT);

    digitalWrite(RELAY_CUTOFF_PIN, HIGH); // Motor energized initially
    digitalWrite(LED_NOMINAL_PIN,  HIGH);
    digitalWrite(LED_WARNING_PIN,  LOW);
    digitalWrite(LED_CRITICAL_PIN, LOW);

    analogReadResolution(12);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    if (mpu.begin(0x68, &Wire)) {
        hasImu = true;
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setFilterBandwidth(MPU6050_BAND_94_HZ);
        Serial.println(F("[INIT] 3-Axis Accelerometer initialized (+/-8g, 94 Hz bandwidth)."));
    } else {
        Serial.println(F("[INIT] Using Wokwi Rotating Machinery Harmonic Synthesizer + Fault Dial (GPIO 34)."));
    }

    Serial.println(F("===================================================================================="));
    Serial.println(F(" Project 14: Unsupervised Motor Anomaly Detector (Autoencoder 8->3->8 + K-Means)"));
    Serial.println(F(" Commands: 'CALIBRATE', 'RESET', 'NORMAL', 'IMBALANCE', 'BEARING', 'LOOSE', 'AUTO'"));
    Serial.println(F("===================================================================================="));
}

void loop() {
    handleSerialInput();

    if (digitalRead(CALIB_BTN_PIN) == LOW && !isCalibrating) {
        startOnDeviceCalibration();
        delay(300);
    }

    frameCounter++;
    float features[MOTOR_FEAT_DIM];
    captureAndExtractVibrationFeatures(features);

    // 1. Handle On-Device Calibration Mode
    if (isCalibrating) {
        anomalyEngine.accumulateCalibrationSample(features);
        calibWindowsRemaining--;

        digitalWrite(LED_NOMINAL_PIN,  (calibWindowsRemaining % 2 == 0) ? HIGH : LOW);
        digitalWrite(LED_WARNING_PIN,  HIGH);
        digitalWrite(LED_CRITICAL_PIN, LOW);

        Serial.printf(
            "[CALIB %2u/%2u] Learning Baseline -> RMS=[%.3f, %.3f, %.3f]g | P2P=%.2fg | Crest=%.2f | HF=%.2f\n",
            CALIBRATION_WINDOWS - calibWindowsRemaining,
            CALIBRATION_WINDOWS,
            anomalyEngine.baselineMean[0],
            anomalyEngine.baselineMean[1],
            anomalyEngine.baselineMean[2],
            anomalyEngine.baselineMean[3],
            anomalyEngine.baselineMean[4],
            anomalyEngine.baselineMean[6]
        );

        if (calibWindowsRemaining == 0) {
            isCalibrating = false;
            Serial.println(F("[CALIBRATE] >>> On-Device Calibration COMPLETE! Autoencoder & K-Means anchored.\n"));
        }
        delay(450);
        return;
    }

    // 2. Run Unsupervised Autoencoder + K-Means Inference
    float mseError = 0.0f;
    float clusterDist = 0.0f;
    uint32_t t0 = micros();
    uint8_t healthState = anomalyEngine.evaluate(features, &mseError, &clusterDist);
    uint32_t inferenceUs = micros() - t0;

    // 3. Evaluate Protective Relay Cutoff & Status LEDs
    if (healthState == MOTOR_LOOSE_MOUNTING_SEVERE || mseError >= anomalyEngine.mseCriticalThreshold) {
        consecutiveSevereCount++;
        if (consecutiveSevereCount >= TRIP_CONSECUTIVE_LIMIT && !relayTripped) {
            relayTripped = true;
            digitalWrite(RELAY_CUTOFF_PIN, LOW); // Cut power to protect motor bearings/shaft
            Serial.println(F("[CRITICAL ALARM] >>> MSE Exceeded Safety Limit! Protective Relay TRIPPED (GPIO 14 = LOW)!"));
        }
    } else {
        if (consecutiveSevereCount > 0) consecutiveSevereCount--;
    }

    if (relayTripped) {
        digitalWrite(LED_NOMINAL_PIN,  LOW);
        digitalWrite(LED_WARNING_PIN,  LOW);
        digitalWrite(LED_CRITICAL_PIN, HIGH);
    } else if (healthState == MOTOR_NOMINAL_HEALTHY) {
        digitalWrite(LED_NOMINAL_PIN,  HIGH);
        digitalWrite(LED_WARNING_PIN,  LOW);
        digitalWrite(LED_CRITICAL_PIN, LOW);
    } else if (healthState == MOTOR_SHAFT_IMBALANCE_1X || healthState == MOTOR_BEARING_WEAR_SPALLING) {
        digitalWrite(LED_NOMINAL_PIN,  LOW);
        digitalWrite(LED_WARNING_PIN,  HIGH);
        digitalWrite(LED_CRITICAL_PIN, LOW);
    } else {
        digitalWrite(LED_NOMINAL_PIN,  LOW);
        digitalWrite(LED_WARNING_PIN,  HIGH);
        digitalWrite(LED_CRITICAL_PIN, HIGH);
    }

    // 4. Emit Diagnostic Telemetry Line
    Serial.printf(
        "[#%04lu] RMS=[%.2f,%.2f,%.2f]g | Crest=%.2f | Kurt=%.2f | HF=%.2f | "
        "AE_MSE=%6.3f | KMeans_d=%.2f | State=%u (%-21s) | Relay=%s | %luus\n",
        frameCounter,
        features[0], features[1], features[2],
        features[4],
        features[5],
        features[6],
        mseError,
        clusterDist,
        healthState,
        MOTOR_STATE_NAMES[healthState],
        relayTripped ? "TRIPPED" : "CLOSED ",
        inferenceUs
    );

    delay(650);
}
