#ifndef ENOSE_NN_MODEL_H
#define ENOSE_NN_MODEL_H

#include <stdint.h>
#include <math.h>

/**
 * @file enose_nn_model.h
 * @brief Edge Impulse Compatible 2-Layer Fully Connected Neural Network (FCNN)
 *        for Multi-Gas Electronic Nose Odor & Hazard Classification
 *
 * Architecture:
 *   Input Layer  : 6 normalized sensor-fusion features
 *   Hidden Layer : 12 neurons, Dense + ReLU activation
 *   Output Layer : 4 neurons, Dense + Softmax probability distribution
 *
 * Input Feature Vector x[0..5]:
 *   x[0] : MQ-135 Compensated Resistance Ratio (Rs / R0)_135   [Air Quality / NH3 / Benzene / Smoke]
 *   x[1] : MQ-2   Compensated Resistance Ratio (Rs / R0)_2     [Combustible Gas / LPG / Propane / Alcohol]
 *   x[2] : Cross-Sensitivity Log Ratio ln((Rs/R0)_135 / (Rs/R0)_2)
 *   x[3] : MQ-135 Temporal Derivative d(Rs/R0)_135 / dt (1/s)
 *   x[4] : Ambient Temperature T (°C)
 *   x[5] : Ambient Relative Humidity RH (%)
 *
 * Output Classes (4 classes):
 *   0 : CLEAN_AIR        (Both Rs/R0 near 1.0, balanced ratio)
 *   1 : CIGARETTE_SMOKE  (Moderate drop in both MQ-135 and MQ-2, strong particulate/CO signature)
 *   2 : ALCOHOL_VOC      (Sharp drop in MQ-135 relative to MQ-2; negative log cross-ratio)
 *   3 : LPG_LEAK_HAZARD  (Severe drop in MQ-2 relative to MQ-135; positive log cross-ratio)
 */

#define ENOSE_INPUT_DIM   6
#define ENOSE_HIDDEN_DIM  12
#define ENOSE_OUTPUT_DIM  4

enum EnoseGasClass : uint8_t {
    GAS_CLEAN_AIR       = 0,
    GAS_CIGARETTE_SMOKE = 1,
    GAS_ALCOHOL_VOC     = 2,
    GAS_LPG_LEAK_HAZARD = 3
};

static const char* const ENOSE_CLASS_NAMES[ENOSE_OUTPUT_DIM] = {
    "CLEAN_AIR",
    "CIGARETTE_SMOKE",
    "ALCOHOL_VOC",
    "LPG_LEAK_HAZARD"
};

// Feature Z-Score Normalization Parameters (computed over training dataset)
static const float ENOSE_FEAT_MEAN[ENOSE_INPUT_DIM] = {
    0.58f,   // x[0]: (Rs/R0)_135
    0.56f,   // x[1]: (Rs/R0)_2
    0.02f,   // x[2]: ln((Rs/R0)_135 / (Rs/R0)_2)
    0.00f,   // x[3]: d(Rs/R0)_135 / dt
    25.00f,  // x[4]: Temperature (°C)
    55.00f   // x[5]: Humidity (%)
};

static const float ENOSE_FEAT_STD[ENOSE_INPUT_DIM] = {
    0.32f,   // s[0]
    0.34f,   // s[1]
    0.75f,   // s[2]
    0.15f,   // s[3]
    10.00f,  // s[4]
    20.00f   // s[5]
};

// Layer 1 Weights: W1[ENOSE_HIDDEN_DIM][ENOSE_INPUT_DIM] (12 x 6)
static const float ENOSE_W1[ENOSE_HIDDEN_DIM][ENOSE_INPUT_DIM] = {
    //  r135,    r2,   logR,  dr135,     T,    RH
    {  1.85f,  1.90f,  0.05f,  0.20f,  0.02f,  0.01f }, // H0 : High when both sensors are in clean air (high Rs/R0)
    {  1.40f,  1.45f, -0.05f,  0.15f, -0.01f,  0.02f }, // H1 : Clean air secondary detector
    { -1.25f, -1.15f, -0.10f, -0.35f,  0.04f,  0.03f }, // H2 : Activated when both MQ-135 and MQ-2 drop together (Smoke)
    { -1.10f, -1.05f,  0.08f, -0.25f,  0.02f, -0.02f }, // H3 : Co-drop detector (Smoke)
    { -1.95f,  0.45f, -1.80f, -0.40f,  0.03f,  0.05f }, // H4 : Activated when MQ-135 drops strongly while MQ-2 stays higher (Alcohol/VOC)
    { -1.65f,  0.30f, -1.55f, -0.30f, -0.02f,  0.04f }, // H5 : Alcohol/NH3 selective neuron
    {  0.40f, -2.05f,  1.85f, -0.15f,  0.03f, -0.01f }, // H6 : Activated when MQ-2 drops strongly while MQ-135 stays higher (LPG/Propane)
    {  0.25f, -1.80f,  1.60f, -0.20f,  0.01f,  0.02f }, // H7 : Combustible hydrocarbon selective neuron
    { -0.80f, -0.85f,  0.00f, -0.90f,  0.00f,  0.00f }, // H8 : Fast transient gas plume onset detector
    {  0.90f,  0.85f,  0.00f,  0.10f, -0.05f, -0.05f }, // H9 : Baseline stability gate
    { -0.70f, -1.60f,  1.10f, -0.30f,  0.02f,  0.01f }, // H10: High-concentration LPG saturation neuron
    { -1.50f, -0.60f, -1.15f, -0.25f,  0.01f,  0.03f }  // H11: High-concentration VOC saturation neuron
};

static const float ENOSE_B1[ENOSE_HIDDEN_DIM] = {
    -0.35f, -0.20f, -0.15f, -0.10f,
    -0.25f, -0.20f, -0.25f, -0.20f,
    -0.30f, -0.10f, -0.30f, -0.30f
};

// Layer 2 Weights: W2[ENOSE_OUTPUT_DIM][ENOSE_HIDDEN_DIM] (4 x 12)
static const float ENOSE_W2[ENOSE_OUTPUT_DIM][ENOSE_HIDDEN_DIM] = {
    //   H0,     H1,     H2,     H3,     H4,     H5,     H6,     H7,     H8,     H9,    H10,    H11
    {  2.40f,  2.10f, -1.50f, -1.40f, -1.60f, -1.30f, -1.60f, -1.40f, -0.80f,  1.50f, -1.20f, -1.20f }, // Class 0: CLEAN_AIR
    { -1.40f, -1.20f,  2.35f,  2.20f, -0.65f, -0.55f, -0.65f, -0.55f,  0.75f, -0.90f, -0.45f, -0.45f }, // Class 1: CIGARETTE_SMOKE
    { -1.50f, -1.30f, -0.30f, -0.35f,  2.45f,  2.15f, -1.80f, -1.60f,  0.60f, -1.00f, -1.10f,  1.85f }, // Class 2: ALCOHOL_VOC
    { -1.60f, -1.40f, -0.35f, -0.30f, -1.80f, -1.60f,  2.55f,  2.25f,  0.65f, -1.10f,  1.95f, -1.10f }  // Class 3: LPG_LEAK_HAZARD
};

static const float ENOSE_B2[ENOSE_OUTPUT_DIM] = {
     0.35f,
    -0.10f,
    -0.15f,
    -0.15f
};

/**
 * @brief Run 2-layer Fully Connected Neural Network (ReLU + Softmax) inference
 * @param rawFeatures 6-element input vector [Rs/R0_135, Rs/R0_2, logRatio, dRs135/dt, T, RH]
 * @param probabilities Output 4-element Softmax probability array (sums to 1.0)
 * @return Winning class index (0..3)
 */
inline uint8_t enosePredictNN(const float rawFeatures[ENOSE_INPUT_DIM], float probabilities[ENOSE_OUTPUT_DIM]) {
    // 1. Z-score standardize input vector
    float xNorm[ENOSE_INPUT_DIM];
    for (uint8_t i = 0; i < ENOSE_INPUT_DIM; i++) {
        xNorm[i] = (rawFeatures[i] - ENOSE_FEAT_MEAN[i]) / ENOSE_FEAT_STD[i];
    }

    // 2. Dense Layer 1 + ReLU Activation
    float hidden[ENOSE_HIDDEN_DIM];
    for (uint8_t h = 0; h < ENOSE_HIDDEN_DIM; h++) {
        float acc = ENOSE_B1[h];
        for (uint8_t i = 0; i < ENOSE_INPUT_DIM; i++) {
            acc += ENOSE_W1[h][i] * xNorm[i];
        }
        // ReLU(z) = max(0, z)
        hidden[h] = (acc > 0.0f) ? acc : 0.0f;
    }

    // 3. Dense Layer 2 (Logits) + Numerically Stable Softmax
    float logits[ENOSE_OUTPUT_DIM];
    float maxLogit = -1e30f;

    for (uint8_t c = 0; c < ENOSE_OUTPUT_DIM; c++) {
        float acc = ENOSE_B2[c];
        for (uint8_t h = 0; h < ENOSE_HIDDEN_DIM; h++) {
            acc += ENOSE_W2[c][h] * hidden[h];
        }
        logits[c] = acc;
        if (acc > maxLogit) {
            maxLogit = acc;
        }
    }

    float sumExp = 0.0f;
    for (uint8_t c = 0; c < ENOSE_OUTPUT_DIM; c++) {
        probabilities[c] = expf(logits[c] - maxLogit);
        sumExp += probabilities[c];
    }

    uint8_t bestClass = 0;
    float bestProb = 0.0f;
    for (uint8_t c = 0; c < ENOSE_OUTPUT_DIM; c++) {
        probabilities[c] /= sumExp;
        if (probabilities[c] > bestProb) {
            bestProb = probabilities[c];
            bestClass = c;
        }
    }

    return bestClass;
}

#endif // ENOSE_NN_MODEL_H
