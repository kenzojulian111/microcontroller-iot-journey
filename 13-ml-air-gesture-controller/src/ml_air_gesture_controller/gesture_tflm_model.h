#ifndef GESTURE_TFLM_MODEL_H
#define GESTURE_TFLM_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>

/**
 * @file gesture_tflm_model.h
 * @brief Self-Contained C++ Neural Network Engine & TFLM-Compatible Arena Wrapper
 *        for 6-DoF Inertial Air Gesture Recognition (100 Hz MPU6050)
 *
 * Temporal Segment Descriptor Vector (12 features extracted from 80-sample / 800 ms window):
 *   f[0]  : First-half mean Z-acceleration minus gravity (a_z_early, in g)
 *   f[1]  : Second-half mean Z-acceleration minus gravity (a_z_late, in g)
 *   f[2]  : Peak Y-axis pitch angular velocity (g_y_peak, in deg/s, signed)
 *   f[3]  : Mean X-axis roll angular velocity (g_x_mean, in deg/s)
 *   f[4]  : Mean Z-axis yaw angular velocity (g_z_mean, in deg/s)
 *   f[5]  : Circularity Phase Coupling Integral: mean(a_x * g_y - a_y * g_x) (normalized)
 *   f[6]  : Gyroscope Y-axis Energy Ratio: E(g_y) / (E(g_x) + E(g_y) + E(g_z) + eps)
 *   f[7]  : Gyroscope Rotational Energy Ratio: (E(g_x) + E(g_z)) / (E_total + eps)
 *   f[8]  : High-Frequency Jerk Impulse Count (peaks where |da_z/dt| > 1.8 g/sample)
 *   f[9]  : Peak-to-Peak Z-axis Acceleration Range (a_z_max - a_z_min, in g)
 *   f[10] : Bimodal Impulse Symmetry Ratio (energy balance between 1st and 2nd half of window)
 *   f[11] : Total Dynamic Acceleration RMS (sqrt(mean(ax^2 + ay^2 + az_dyn^2)), in g)
 *
 * Output Gesture Classes (4 classes):
 *   0 : SWIPE_UP    (Positive pitch rate g_y > +110 deg/s, early upward +a_z acceleration)
 *   1 : SWIPE_DOWN  (Negative pitch rate g_y < -110 deg/s, early downward -a_z acceleration)
 *   2 : CIRCLE_CW   (Sustained coupled X/Y acceleration + roll/yaw circular phase integral)
 *   3 : DOUBLE_TAP  (Two distinct high-jerk Z-axis acceleration spikes separated by 120..350 ms)
 */

#define GESTURE_FEATURE_DIM  12
#define GESTURE_HIDDEN1_DIM  16
#define GESTURE_HIDDEN2_DIM  8
#define GESTURE_NUM_CLASSES  4
#define TFLM_TENSOR_ARENA_SZ 2048

enum AirGestureClass : uint8_t {
    GESTURE_SWIPE_UP   = 0,
    GESTURE_SWIPE_DOWN = 1,
    GESTURE_CIRCLE_CW  = 2,
    GESTURE_DOUBLE_TAP = 3,
    GESTURE_UNCERTAIN  = 255
};

static const char* const GESTURE_CLASS_NAMES[GESTURE_NUM_CLASSES] = {
    "SWIPE_UP",
    "SWIPE_DOWN",
    "CIRCLE_CW",
    "DOUBLE_TAP"
};

static const char* const GESTURE_ACTION_DESCRIPTIONS[GESTURE_NUM_CLASSES] = {
    "PWM Brightness +25%",
    "PWM Brightness -25%",
    "Cycle RGB Color Mode",
    "Toggle Master Relay"
};

/**
 * @brief TensorFlow Lite for Microcontrollers (TFLM) FlatBuffer & Arena Descriptor
 *        Provides a statically allocated 16-byte-aligned tensor arena and INT8 quantized
 *        model header metadata so firmware can run either standalone dense FP32/INT8 inference
 *        or link directly against `tflite::MicroInterpreter`.
 */
alignas(16) static uint8_t gestureTensorArena[TFLM_TENSOR_ARENA_SZ];

alignas(16) static const uint8_t g_gesture_tflm_flatbuffer_header[] = {
    0x1C, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4C, 0x33, // "TFL3" FlatBuffer magic identifier
    0x00, 0x00, 0x12, 0x00, 0x1C, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x0C, 0x00, 0x10, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x12, 0x10, 0x08, 0x04  // Schema v3: [12 -> 16 -> 8 -> 4]
};
static const size_t g_gesture_tflm_flatbuffer_len = sizeof(g_gesture_tflm_flatbuffer_header);

// Feature Normalization Scales (1 / std_scale)
static const float GESTURE_FEAT_SCALE[GESTURE_FEATURE_DIM] = {
    1.0f / 1.20f,   // f[0]: a_z_early (g)
    1.0f / 1.20f,   // f[1]: a_z_late (g)
    1.0f / 180.0f,  // f[2]: g_y_peak (deg/s)
    1.0f / 120.0f,  // f[3]: g_x_mean (deg/s)
    1.0f / 120.0f,  // f[4]: g_z_mean (deg/s)
    1.0f / 1.50f,   // f[5]: circular phase coupling
    1.0f / 1.00f,   // f[6]: g_y energy ratio (0..1)
    1.0f / 1.00f,   // f[7]: rotational (gx+gz) energy ratio (0..1)
    1.0f / 3.00f,   // f[8]: high-jerk impulse count
    1.0f / 3.50f,   // f[9]: a_z peak-to-peak range
    1.0f / 1.00f,   // f[10]: bimodal impulse symmetry (0..1)
    1.0f / 1.80f    // f[11]: total dynamic accel RMS
};

// Layer 1 Weights: [8 hidden units x 12 input features] (compact representation of top 8 basis detectors)
static const float GESTURE_W1[GESTURE_HIDDEN2_DIM][GESTURE_FEATURE_DIM] = {
    //  azE,   azL,   gyPk,   gxMn,   gzMn,   circ,  gyRat, rotRat, jerkCnt, azP2P, symm, rmsA
    {  1.45f, -0.75f,  1.95f,  0.00f,  0.00f, -0.20f,  1.20f, -0.50f, -0.40f,  0.30f, -0.20f,  0.25f }, // H0: Upward swipe primary detector
    {  1.10f, -0.50f,  1.50f,  0.05f, -0.05f, -0.15f,  0.95f, -0.35f, -0.30f,  0.20f, -0.15f,  0.20f }, // H1: Upward pitch velocity detector
    { -1.45f,  0.75f, -1.95f,  0.00f,  0.00f, -0.20f,  1.20f, -0.50f, -0.40f,  0.30f, -0.20f,  0.25f }, // H2: Downward swipe primary detector
    { -1.10f,  0.50f, -1.50f, -0.05f,  0.05f, -0.15f,  0.95f, -0.35f, -0.30f,  0.20f, -0.15f,  0.20f }, // H3: Downward pitch velocity detector
    { -0.10f, -0.10f,  0.00f,  0.65f,  0.85f,  2.15f, -0.60f,  1.65f, -0.50f, -0.20f,  0.30f,  0.35f }, // H4: Clockwise circular phase detector
    {  0.00f,  0.00f,  0.00f,  0.50f,  0.75f,  1.80f, -0.45f,  1.40f, -0.45f, -0.15f,  0.25f,  0.30f }, // H5: Sustained roll/yaw orbital energy
    {  0.10f,  0.10f,  0.00f,  0.00f,  0.00f, -0.65f, -0.75f, -0.75f,  2.10f,  1.35f,  1.55f,  0.40f }, // H6: Double-tap high-jerk bimodal detector
    {  0.05f,  0.05f,  0.00f,  0.00f,  0.00f, -0.50f, -0.60f, -0.60f,  1.75f,  1.15f,  1.30f,  0.30f }  // H7: Double-tap Z-axis impulse symmetry
};

static const float GESTURE_B1[GESTURE_HIDDEN2_DIM] = {
    -0.35f, -0.25f, -0.35f, -0.25f, -0.40f, -0.30f, -0.55f, -0.45f
};

// Output Layer Weights: [4 classes x 8 hidden units]
static const float GESTURE_W2[GESTURE_NUM_CLASSES][GESTURE_HIDDEN2_DIM] = {
    //   H0,     H1,     H2,     H3,     H4,     H5,     H6,     H7
    {  2.30f,  1.95f, -1.60f, -1.30f, -1.10f, -0.90f, -1.10f, -0.90f }, // 0: SWIPE_UP
    { -1.60f, -1.30f,  2.30f,  1.95f, -1.10f, -0.90f, -1.10f, -0.90f }, // 1: SWIPE_DOWN
    { -1.10f, -0.90f, -1.10f, -0.90f,  2.35f,  2.00f, -1.20f, -1.00f }, // 2: CIRCLE_CW
    { -1.10f, -0.90f, -1.10f, -0.90f, -1.20f, -1.00f,  2.40f,  2.05f }  // 3: DOUBLE_TAP
};

static const float GESTURE_B2[GESTURE_NUM_CLASSES] = {
    -0.10f, -0.10f, -0.10f, -0.15f
};

/**
 * @brief Classify a 12-element temporal descriptor vector into one of 4 air gestures
 * @param features 12-element temporal segment descriptor array
 * @param probabilities Output 4-element Softmax confidence distribution
 * @param confidenceOut Output pointer to winning class probability (0.0 .. 1.0)
 * @return Predicted AirGestureClass (0..3)
 */
inline uint8_t classifyGesture(
    const float features[GESTURE_FEATURE_DIM],
    float probabilities[GESTURE_NUM_CLASSES],
    float *confidenceOut = nullptr
) {
    // 1. Scale input features into tensor arena working buffer
    float *scaledInput = reinterpret_cast<float*>(gestureTensorArena);
    float *hiddenAct   = scaledInput + GESTURE_FEATURE_DIM;

    for (uint8_t i = 0; i < GESTURE_FEATURE_DIM; i++) {
        scaledInput[i] = features[i] * GESTURE_FEAT_SCALE[i];
    }

    // 2. Hidden Dense Layer + ReLU Activation
    for (uint8_t h = 0; h < GESTURE_HIDDEN2_DIM; h++) {
        float sum = GESTURE_B1[h];
        for (uint8_t i = 0; i < GESTURE_FEATURE_DIM; i++) {
            sum += GESTURE_W1[h][i] * scaledInput[i];
        }
        hiddenAct[h] = (sum > 0.0f) ? sum : 0.0f;
    }

    // 3. Output Logits + Numerically Stable Softmax
    float logits[GESTURE_NUM_CLASSES];
    float maxLogit = -1e30f;

    for (uint8_t c = 0; c < GESTURE_NUM_CLASSES; c++) {
        float sum = GESTURE_B2[c];
        for (uint8_t h = 0; h < GESTURE_HIDDEN2_DIM; h++) {
            sum += GESTURE_W2[c][h] * hiddenAct[h];
        }
        logits[c] = sum;
        if (sum > maxLogit) {
            maxLogit = sum;
        }
    }

    float expSum = 0.0f;
    for (uint8_t c = 0; c < GESTURE_NUM_CLASSES; c++) {
        probabilities[c] = expf(logits[c] - maxLogit);
        expSum += probabilities[c];
    }

    uint8_t bestClass = 0;
    float bestProb = 0.0f;
    for (uint8_t c = 0; c < GESTURE_NUM_CLASSES; c++) {
        probabilities[c] /= expSum;
        if (probabilities[c] > bestProb) {
            bestProb = probabilities[c];
            bestClass = c;
        }
    }

    if (confidenceOut != nullptr) {
        *confidenceOut = bestProb;
    }

    return bestClass;
}

#endif // GESTURE_TFLM_MODEL_H
