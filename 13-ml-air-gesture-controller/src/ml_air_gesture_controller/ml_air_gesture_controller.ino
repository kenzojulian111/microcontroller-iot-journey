/**
 * @file ml_air_gesture_controller.ino
 * @brief Project 13: 6-DoF Inertial Air Gesture Controller (MPU6050 at 100 Hz + Neural Classifier)
 *
 * Target Hardware: ESP32 / ESP32-S3
 * Sensor:
 *   - InvenSense MPU6050 6-DoF IMU (I2C 0x68: 3-axis Accelerometer + 3-axis Gyroscope)
 * Actuators:
 *   - PWM RGB LED Channels: Red (GPIO 25), Green (GPIO 26), Blue (GPIO 27)
 *   - Master Appliance Relay (GPIO 14)
 *   - Demo Gesture Trigger Button (GPIO 15 — cycles synthetic 100 Hz IMU gesture waveforms
 *     for hands-free Wokwi verification, in addition to live physical MPU6050 motion capture!)
 *
 * Real-Time Pipeline:
 *   1. Continuous 100 Hz polling of (ax, ay, az, gx, gy, gz)
 *   2. Motion onset gate triggers an 80-sample (800 ms) capture window when dynamic
 *      acceleration exceeds 0.35 g or angular velocity exceeds 65 deg/s
 *   3. Extracts 12 temporal segment descriptors (early/late window means, rotational energy
 *      integrals, circular phase coupling, and bimodal jerk impulse symmetry)
 *   4. Executes neural inference via `classifyGesture()` in `gesture_tflm_model.h`
 *   5. Maps recognized gestures to smart-home hardware controls:
 *      - SWIPE_UP   -> Increase PWM Brightness (+25%)
 *      - SWIPE_DOWN -> Decrease PWM Brightness (-25%)
 *      - CIRCLE_CW  -> Cycle RGB Color Preset (Warm White -> Cyan -> Magenta -> Emerald)
 *      - DOUBLE_TAP -> Toggle Master Appliance Relay ON/OFF
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "gesture_tflm_model.h"

// ============================================================================
// Hardware Pin Definitions & Sampling Constants
// ============================================================================
#define I2C_SDA_PIN           21
#define I2C_SCL_PIN           22

#define RGB_RED_PIN           25
#define RGB_GREEN_PIN         26
#define RGB_BLUE_PIN          27
#define RELAY_PIN             14
#define DEMO_BTN_PIN          15

#define SAMPLE_RATE_HZ        100
#define SAMPLE_INTERVAL_MS    (1000 / SAMPLE_RATE_HZ)
#define GESTURE_WINDOW_SAMPLES 80   // 80 samples @ 100 Hz = 800 ms gesture window

#define MOTION_ACCEL_THRESH_G 0.35f // Dynamic acceleration trigger threshold (g)
#define MOTION_GYRO_THRESH_DPS 65.0f // Angular velocity trigger threshold (deg/s)
#define CONFIDENCE_THRESHOLD  0.60f

#define RAD_TO_DEG_F          57.2957795f
#define GRAVITY_MSS           9.80665f

// ============================================================================
// Data Structures & Global State
// ============================================================================
struct ImuFrame {
    float ax_g;
    float ay_g;
    float az_dyn_g; // Gravity-compensated vertical acceleration (g)
    float gx_dps;
    float gy_dps;
    float gz_dps;
};

Adafruit_MPU6050 mpu;
bool hasMpu6050 = false;

ImuFrame captureWindow[GESTURE_WINDOW_SAMPLES];
bool isCapturingGesture = false;
uint8_t capturedSamples = 0;
uint32_t lastSampleTickMs = 0;

// Smart-home actuator state
uint8_t brightnessPct = 50; // 0..100%
uint8_t rgbColorMode  = 0;  // 0: Warm White, 1: Cyan, 2: Magenta, 3: Emerald Green
bool masterRelayState = true;
uint8_t nextSyntheticGesture = 0;

static const uint8_t RGB_PRESETS[4][3] = {
    { 255, 220, 160 }, // Mode 0: Warm White
    {   0, 230, 255 }, // Mode 1: Cyber Cyan
    { 255,  30, 190 }, // Mode 2: Neon Magenta
    {  20, 255,  90 }  // Mode 3: Emerald Green
};

static const char* const RGB_MODE_NAMES[4] = {
    "WARM_WHITE",
    "CYBER_CYAN",
    "NEON_MAGENTA",
    "EMERALD_GREEN"
};

// ============================================================================
// Actuator Update Functions
// ============================================================================
void updateOutputs() {
    digitalWrite(RELAY_PIN, masterRelayState ? HIGH : LOW);

    if (!masterRelayState || brightnessPct == 0) {
        analogWrite(RGB_RED_PIN,   0);
        analogWrite(RGB_GREEN_PIN, 0);
        analogWrite(RGB_BLUE_PIN,  0);
        return;
    }

    float scale = static_cast<float>(brightnessPct) / 100.0f;
    analogWrite(RGB_RED_PIN,   static_cast<int>(RGB_PRESETS[rgbColorMode][0] * scale));
    analogWrite(RGB_GREEN_PIN, static_cast<int>(RGB_PRESETS[rgbColorMode][1] * scale));
    analogWrite(RGB_BLUE_PIN,  static_cast<int>(RGB_PRESETS[rgbColorMode][2] * scale));
}

void executeGestureCommand(uint8_t gestureClass) {
    switch (gestureClass) {
        case GESTURE_SWIPE_UP:
            brightnessPct = (brightnessPct <= 75) ? (brightnessPct + 25) : 100;
            break;
        case GESTURE_SWIPE_DOWN:
            brightnessPct = (brightnessPct >= 25) ? (brightnessPct - 25) : 0;
            break;
        case GESTURE_CIRCLE_CW:
            rgbColorMode = (rgbColorMode + 1) % 4;
            break;
        case GESTURE_DOUBLE_TAP:
            masterRelayState = !masterRelayState;
            break;
        default:
            break;
    }
    updateOutputs();
}

// ============================================================================
// Synthetic 6-DoF Waveform Generator (for Wokwi Button / Serial Injection)
// ============================================================================
void populateSyntheticGestureWindow(uint8_t targetClass) {
    for (uint8_t i = 0; i < GESTURE_WINDOW_SAMPLES; i++) {
        float t = static_cast<float>(i) / static_cast<float>(GESTURE_WINDOW_SAMPLES); // 0.0 .. 1.0
        ImuFrame &f = captureWindow[i];
        f.ax_g = 0.0f;
        f.ay_g = 0.0f;
        f.az_dyn_g = 0.0f;
        f.gx_dps = 0.0f;
        f.gy_dps = 0.0f;
        f.gz_dps = 0.0f;

        if (targetClass == GESTURE_SWIPE_UP) {
            // Upward acceleration in first half, deceleration in second half, strong +gy pitch peak
            f.az_dyn_g = (t < 0.5f) ? (1.15f * sinf(t * 2.0f * PI)) : (-0.55f * sinf((t - 0.5f) * 2.0f * PI));
            f.gy_dps   = 185.0f * sinf(t * PI);
        } else if (targetClass == GESTURE_SWIPE_DOWN) {
            // Downward acceleration in first half, strong -gy pitch peak
            f.az_dyn_g = (t < 0.5f) ? (-1.15f * sinf(t * 2.0f * PI)) : (0.55f * sinf((t - 0.5f) * 2.0f * PI));
            f.gy_dps   = -185.0f * sinf(t * PI);
        } else if (targetClass == GESTURE_CIRCLE_CW) {
            // Continuous 1-Hz orbital X/Y acceleration and phase-locked roll/yaw rates
            float theta = 2.0f * PI * t;
            f.ax_g   =  0.85f * cosf(theta);
            f.ay_g   =  0.85f * sinf(theta);
            f.gx_dps = -95.0f * sinf(theta);
            f.gy_dps =  95.0f * cosf(theta);
            f.gz_dps =  82.0f;
        } else if (targetClass == GESTURE_DOUBLE_TAP) {
            // Two sharp vertical Z-axis impact spikes at sample 18 (180 ms) and sample 52 (520 ms)
            if (i == 18 || i == 52) {
                f.az_dyn_g = 2.40f;
            } else if (i == 19 || i == 53) {
                f.az_dyn_g = -1.65f;
            } else {
                f.az_dyn_g = 0.02f * sinf(t * 6.0f * PI);
            }
        }
    }
}

// ============================================================================
// Temporal Segment Descriptor Feature Extractor
// ============================================================================
void extractGestureFeatures(const ImuFrame window[GESTURE_WINDOW_SAMPLES], float feat[GESTURE_FEATURE_DIM]) {
    const uint8_t halfN = GESTURE_WINDOW_SAMPLES / 2;

    float sumAzEarly = 0.0f;
    float sumAzLate  = 0.0f;
    float gyPeak     = 0.0f;
    float sumGx      = 0.0f;
    float sumGz      = 0.0f;
    float sumCirc    = 0.0f;

    float energyGx   = 0.0f;
    float energyGy   = 0.0f;
    float energyGz   = 0.0f;

    uint8_t jerkSpikes = 0;
    float azMin =  100.0f;
    float azMax = -100.0f;

    float energyEarly = 0.0f;
    float energyLate  = 0.0f;
    float sumAccSq    = 0.0f;

    for (uint8_t i = 0; i < GESTURE_WINDOW_SAMPLES; i++) {
        const ImuFrame &s = window[i];

        if (i < halfN) {
            sumAzEarly  += s.az_dyn_g;
            energyEarly += s.az_dyn_g * s.az_dyn_g;
        } else {
            sumAzLate   += s.az_dyn_g;
            energyLate  += s.az_dyn_g * s.az_dyn_g;
        }

        if (fabsf(s.gy_dps) > fabsf(gyPeak)) {
            gyPeak = s.gy_dps;
        }

        sumGx += fabsf(s.gx_dps);
        sumGz += fabsf(s.gz_dps);

        // Circular phase coupling between lateral acceleration and angular rate
        sumCirc += (s.ax_g * (s.gy_dps / 100.0f)) - (s.ay_g * (s.gx_dps / 100.0f));

        energyGx += s.gx_dps * s.gx_dps;
        energyGy += s.gy_dps * s.gy_dps;
        energyGz += s.gz_dps * s.gz_dps;

        if (i > 0) {
            float dAz = fabsf(s.az_dyn_g - window[i - 1].az_dyn_g);
            if (dAz > 1.80f) {
                jerkSpikes++;
            }
        }

        if (s.az_dyn_g < azMin) azMin = s.az_dyn_g;
        if (s.az_dyn_g > azMax) azMax = s.az_dyn_g;

        sumAccSq += (s.ax_g * s.ax_g) + (s.ay_g * s.ay_g) + (s.az_dyn_g * s.az_dyn_g);
    }

    float totalGyroEnergy = energyGx + energyGy + energyGz + 1e-3f;
    float maxHalfE = fmaxf(energyEarly, energyLate) + 1e-4f;
    float minHalfE = fminf(energyEarly, energyLate);

    feat[0]  = sumAzEarly / static_cast<float>(halfN);
    feat[1]  = sumAzLate  / static_cast<float>(halfN);
    feat[2]  = gyPeak;
    feat[3]  = sumGx / static_cast<float>(GESTURE_WINDOW_SAMPLES);
    feat[4]  = sumGz / static_cast<float>(GESTURE_WINDOW_SAMPLES);
    feat[5]  = sumCirc / static_cast<float>(GESTURE_WINDOW_SAMPLES);
    feat[6]  = energyGy / totalGyroEnergy;
    feat[7]  = (energyGx + energyGz) / totalGyroEnergy;
    feat[8]  = static_cast<float>(jerkSpikes);
    feat[9]  = azMax - azMin;
    feat[10] = minHalfE / maxHalfE; // Near 1.0 when both halves have symmetric tap energy
    feat[11] = sqrtf(sumAccSq / static_cast<float>(GESTURE_WINDOW_SAMPLES));
}

/**
 * @brief Run feature extraction + neural inference on the current 80-sample window
 */
void runInferenceAndActuate() {
    float features[GESTURE_FEATURE_DIM];
    float probs[GESTURE_NUM_CLASSES];
    float confidence = 0.0f;

    uint32_t t0 = micros();
    extractGestureFeatures(captureWindow, features);
    uint8_t predClass = classifyGesture(features, probs, &confidence);
    uint32_t totalUs = micros() - t0;

    if (confidence >= CONFIDENCE_THRESHOLD) {
        executeGestureCommand(predClass);
    }

    Serial.printf(
        "[GESTURE] Pred=%u (%-10s) | Conf=%5.1f%% [%.2f, %.2f, %.2f, %.2f] | "
        "Action: %-20s | State: Relay=%s, Bright=%3u%%, Mode=%s | %luus\n",
        predClass,
        GESTURE_CLASS_NAMES[predClass],
        confidence * 100.0f,
        probs[0], probs[1], probs[2], probs[3],
        GESTURE_ACTION_DESCRIPTIONS[predClass],
        masterRelayState ? "ON " : "OFF",
        brightnessPct,
        RGB_MODE_NAMES[rgbColorMode],
        totalUs
    );
}

// ============================================================================
// Arduino Setup & 100 Hz Loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(RGB_RED_PIN,   OUTPUT);
    pinMode(RGB_GREEN_PIN, OUTPUT);
    pinMode(RGB_BLUE_PIN,  OUTPUT);
    pinMode(RELAY_PIN,     OUTPUT);
    pinMode(DEMO_BTN_PIN,  INPUT_PULLUP);

    updateOutputs();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (mpu.begin(0x68, &Wire)) {
        hasMpu6050 = true;
        mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
        Serial.println(F("[INIT] InvenSense MPU6050 initialized (+/-4g, +/-500 dps, 44 Hz DLPF)."));
    } else {
        Serial.println(F("[WARN] MPU6050 not detected -> Synthetic gesture injection still available."));
    }

    Serial.println(F("================================================================================"));
    Serial.println(F(" Project 13: 6-DoF Air Gesture Controller (100 Hz IMU + TFLM Neural Engine)"));
    Serial.println(F(" Move MPU6050, press GPIO 15 button, or send 'UP', 'DOWN', 'CIRCLE', 'TAP'."));
    Serial.println(F("================================================================================"));
}

void loop() {
    // 1. Check Serial Commands for interactive gesture testing
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        if (cmd == "UP") {
            populateSyntheticGestureWindow(GESTURE_SWIPE_UP);
            runInferenceAndActuate();
        } else if (cmd == "DOWN") {
            populateSyntheticGestureWindow(GESTURE_SWIPE_DOWN);
            runInferenceAndActuate();
        } else if (cmd == "CIRCLE") {
            populateSyntheticGestureWindow(GESTURE_CIRCLE_CW);
            runInferenceAndActuate();
        } else if (cmd == "TAP") {
            populateSyntheticGestureWindow(GESTURE_DOUBLE_TAP);
            runInferenceAndActuate();
        }
    }

    // 2. Check Demo Pushbutton (cycles through all 4 gestures in Wokwi)
    static uint32_t lastBtnPressMs = 0;
    if (digitalRead(DEMO_BTN_PIN) == LOW && (millis() - lastBtnPressMs > 450UL)) {
        lastBtnPressMs = millis();
        populateSyntheticGestureWindow(nextSyntheticGesture);
        runInferenceAndActuate();
        nextSyntheticGesture = (nextSyntheticGesture + 1) % GESTURE_NUM_CLASSES;
        return;
    }

    // 3. 100 Hz Physical MPU6050 Sampling & Motion-Triggered Window Capture
    uint32_t now = millis();
    if (!hasMpu6050 || (now - lastSampleTickMs < SAMPLE_INTERVAL_MS)) {
        return;
    }
    lastSampleTickMs = now;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    ImuFrame current;
    current.ax_g     = a.acceleration.x / GRAVITY_MSS;
    current.ay_g     = a.acceleration.y / GRAVITY_MSS;
    current.az_dyn_g = (a.acceleration.z / GRAVITY_MSS) - 1.0f; // Remove 1g static gravity
    current.gx_dps   = g.gyro.x * RAD_TO_DEG_F;
    current.gy_dps   = g.gyro.y * RAD_TO_DEG_F;
    current.gz_dps   = g.gyro.z * RAD_TO_DEG_F;

    float dynAccelMag = sqrtf(
        current.ax_g * current.ax_g +
        current.ay_g * current.ay_g +
        current.az_dyn_g * current.az_dyn_g
    );
    float gyroMag = sqrtf(
        current.gx_dps * current.gx_dps +
        current.gy_dps * current.gy_dps +
        current.gz_dps * current.gz_dps
    );

    if (!isCapturingGesture) {
        if (dynAccelMag > MOTION_ACCEL_THRESH_G || gyroMag > MOTION_GYRO_THRESH_DPS) {
            isCapturingGesture = true;
            capturedSamples = 0;
        }
    }

    if (isCapturingGesture) {
        captureWindow[capturedSamples++] = current;
        if (capturedSamples >= GESTURE_WINDOW_SAMPLES) {
            isCapturingGesture = false;
            capturedSamples = 0;
            runInferenceAndActuate();
        }
    }
}
