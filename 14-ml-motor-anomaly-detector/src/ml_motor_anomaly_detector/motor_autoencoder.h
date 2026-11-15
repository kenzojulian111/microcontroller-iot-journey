#ifndef MOTOR_AUTOENCODER_H
#define MOTOR_AUTOENCODER_H

#include <stdint.h>
#include <math.h>

/**
 * @file motor_autoencoder.h
 * @brief Unsupervised K-Means Clustering + Symmetric Bottleneck Autoencoder (`8 -> 3 -> 8`)
 *        with On-Device Self-Learning / Calibration for Rotating Machinery Predictive Maintenance
 *
 * Vibration Feature Vector x[0..7] (extracted from 128-sample 200 Hz 3-axis accelerometer window):
 *   x[0] : X-axis RMS Acceleration (g)              [Radial horizontal vibration energy]
 *   x[1] : Y-axis RMS Acceleration (g)              [Radial axial/transverse vibration energy]
 *   x[2] : Z-axis Dynamic RMS Acceleration (g)      [Vertical structural vibration energy]
 *   x[3] : Peak-to-Peak 3D Vibration Magnitude (g)  [Shock / impact envelope]
 *   x[4] : Crest Factor (Peak / RMS)                [Early bearing spalling / pitting indicator]
 *   x[5] : Kurtosis Proxy (4th Central Moment)      [Impulsive bearing fault indicator]
 *   x[6] : High-Frequency Energy Ratio (HF / Total) [Bearing wear vs. low-freq shaft imbalance]
 *   x[7] : Radial-to-Vertical Asymmetry Ratio       [(RMS_x + RMS_y) / (2 * RMS_z + eps)]
 *
 * Unsupervised Anomaly & Fault Diagnosis Engine:
 *   1. Symmetric Autoencoder (`8 -> 3 -> 8`):
 *      - Projects centered feature vector delta = (x - mu_baseline) / scale into a 3D latent
 *        bottleneck z = tanh(W_enc * delta + b_enc) and reconstructs delta_hat = W_dec * z + b_dec.
 *      - Computes Mean Squared Reconstruction Error (MSE). When MSE > threshold, an anomaly is flagged.
 *   2. K-Means Clustering (K = 4 centroids in normalized feature space):
 *      - Centroid 0 : NOMINAL_HEALTHY        (Adapted on the fly during On-Device Calibration!)
 *      - Centroid 1 : SHAFT_IMBALANCE_1X     (Elevated radial RMS, low HF ratio, low crest factor)
 *      - Centroid 2 : BEARING_WEAR_SPALLING  (High crest factor, high kurtosis, high HF energy ratio)
 *      - Centroid 3 : LOOSE_MOUNTING_SEVERE  (Severe Z-axis vertical bounce & peak-to-peak shock -> Relay Cutoff)
 */

#define MOTOR_FEAT_DIM     8
#define MOTOR_LATENT_DIM   3
#define MOTOR_NUM_CLUSTERS 4

enum MotorHealthState : uint8_t {
    MOTOR_NOMINAL_HEALTHY       = 0,
    MOTOR_SHAFT_IMBALANCE_1X    = 1,
    MOTOR_BEARING_WEAR_SPALLING = 2,
    MOTOR_LOOSE_MOUNTING_SEVERE = 3
};

static const char* const MOTOR_STATE_NAMES[MOTOR_NUM_CLUSTERS] = {
    "NOMINAL_HEALTHY",
    "SHAFT_IMBALANCE_1X",
    "BEARING_WEAR_SPALLING",
    "LOOSE_MOUNTING_SEVERE"
};

class MotorAnomalyEngine {
public:
    // Baseline reference mean (updated on-device during CALIBRATE mode!)
    float baselineMean[MOTOR_FEAT_DIM] = {
        0.12f, // x[0]: RMS_x (g)
        0.12f, // x[1]: RMS_y (g)
        0.14f, // x[2]: RMS_z (g)
        0.48f, // x[3]: Peak-to-Peak (g)
        1.45f, // x[4]: Crest Factor (sinusoidal nominal ~ 1.414)
        2.10f, // x[5]: Kurtosis proxy
        0.18f, // x[6]: HF Energy Ratio
        0.86f  // x[7]: Radial/Vertical Ratio
    };

    float featureScale[MOTOR_FEAT_DIM] = {
        0.10f, 0.10f, 0.12f, 0.35f, 0.45f, 0.80f, 0.12f, 0.30f
    };

    // K-Means Cluster Centroids in Standardized Residual Space delta = (x - baselineMean) / featureScale
    // Centroid 0 is at the origin {0,0,...} and shifts dynamically with online EMA updates.
    float centroids[MOTOR_NUM_CLUSTERS][MOTOR_FEAT_DIM] = {
        // C0: NOMINAL_HEALTHY (centered around calibrated baseline)
        {  0.00f,  0.00f,  0.00f,  0.00f,  0.00f,  0.00f,  0.00f,  0.00f },
        // C1: SHAFT_IMBALANCE_1X (high radial X/Y RMS, elevated radial/vertical ratio, low HF/Crest)
        {  2.80f,  2.75f,  0.60f,  2.10f,  0.15f,  0.10f, -0.30f,  2.40f },
        // C2: BEARING_WEAR_SPALLING (high Crest Factor, high Kurtosis, high HF Energy Ratio)
        {  1.10f,  1.15f,  1.20f,  2.95f,  3.40f,  3.60f,  3.80f,  0.10f },
        // C3: LOOSE_MOUNTING_SEVERE (extreme vertical Z RMS, huge Peak-to-Peak shock, low radial ratio)
        {  2.20f,  2.10f,  5.20f,  5.50f,  2.60f,  2.80f,  1.40f, -1.50f }
    };

    // Symmetric Bottleneck Autoencoder Weights (8 -> 3 -> 8) trained on normal operating manifold
    // Normal correlation modes reside inside the 3D subspace spanned by W_enc; off-manifold faults
    // produce large reconstruction residuals ||delta - delta_hat||^2.
    const float W_enc[MOTOR_LATENT_DIM][MOTOR_FEAT_DIM] = {
        {  0.32f,  0.32f,  0.30f,  0.28f,  0.05f,  0.04f,  0.02f,  0.08f }, // Latent 0: Common-mode amplitude mode
        {  0.35f,  0.35f, -0.38f,  0.05f, -0.04f, -0.04f, -0.05f,  0.42f }, // Latent 1: Radial vs vertical balance mode
        {  0.04f,  0.04f,  0.06f,  0.22f,  0.38f,  0.36f,  0.35f, -0.02f }  // Latent 2: Normal surface micro-texture mode
    };

    const float W_dec[MOTOR_FEAT_DIM][MOTOR_LATENT_DIM] = {
        {  0.32f,  0.35f,  0.04f },
        {  0.32f,  0.35f,  0.04f },
        {  0.30f, -0.38f,  0.06f },
        {  0.28f,  0.05f,  0.22f },
        {  0.05f, -0.04f,  0.38f },
        {  0.04f, -0.04f,  0.36f },
        {  0.02f, -0.05f,  0.35f },
        {  0.08f,  0.42f, -0.02f }
    };

    float mseWarningThreshold  = 1.35f;
    float mseCriticalThreshold = 4.20f;
    uint32_t calibrationCount  = 0;

    /**
     * @brief Reset and begin on-device self-learning calibration for a specific motor/fan
     */
    void beginCalibration() {
        calibrationCount = 0;
        for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
            baselineMean[i] = 0.0f;
            centroids[MOTOR_NOMINAL_HEALTHY][i] = 0.0f;
        }
    }

    /**
     * @brief Incrementally update the healthy baseline and K-Means Centroid 0 using Welford's online mean
     */
    void accumulateCalibrationSample(const float features[MOTOR_FEAT_DIM]) {
        calibrationCount++;
        float eta = 1.0f / static_cast<float>(calibrationCount);
        for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
            baselineMean[i] += eta * (features[i] - baselineMean[i]);
            // Adapt feature scale to 25% of nominal magnitude (with safe floor)
            float adaptiveScale = fabsf(baselineMean[i]) * 0.30f;
            featureScale[i] = (adaptiveScale > 0.04f) ? adaptiveScale : 0.04f;
        }
    }

    /**
     * @brief Run Autoencoder (`8 -> 3 -> 8`) reconstruction + K-Means nearest centroid classification
     * @param features Raw 8-element vibration feature vector
     * @param mseOut Output Mean Squared Reconstruction Error
     * @param nearestClusterDistOut Output Euclidean distance to winning K-Means centroid
     * @param reconstructedOut Optional output array for reconstructed features
     * @return Diagnosed MotorHealthState (0..3)
     */
    uint8_t evaluate(
        const float features[MOTOR_FEAT_DIM],
        float *mseOut,
        float *nearestClusterDistOut,
        float reconstructedOut[MOTOR_FEAT_DIM] = nullptr
    ) {
        // 1. Standardize residual relative to learned healthy baseline
        float delta[MOTOR_FEAT_DIM];
        for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
            delta[i] = (features[i] - baselineMean[i]) / featureScale[i];
        }

        // 2. Encoder Bottleneck Projection (8 -> 3) with hyperbolic tangent saturation
        //    Normal operating variations stay within the linear region of tanh(z) (|z| < 0.8),
        //    whereas anomalous vibration spikes saturate the bottleneck and yield high MSE.
        float latent[MOTOR_LATENT_DIM];
        for (uint8_t k = 0; k < MOTOR_LATENT_DIM; k++) {
            float acc = 0.0f;
            for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
                acc += W_enc[k][i] * delta[i];
            }
            latent[k] = 1.25f * tanhf(acc * 0.65f);
        }

        // 3. Decoder Reconstruction (3 -> 8) & Mean Squared Error (MSE)
        float sumSqErr = 0.0f;
        for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
            float dec = 0.0f;
            for (uint8_t k = 0; k < MOTOR_LATENT_DIM; k++) {
                dec += W_dec[i][k] * latent[k];
            }
            if (reconstructedOut != nullptr) {
                reconstructedOut[i] = baselineMean[i] + (dec * featureScale[i]);
            }
            float err = delta[i] - dec;
            sumSqErr += err * err;
        }
        float mse = sumSqErr / static_cast<float>(MOTOR_FEAT_DIM);
        if (mseOut != nullptr) {
            *mseOut = mse;
        }

        // 4. Unsupervised K-Means Nearest Centroid Assignment
        uint8_t bestCluster = MOTOR_NOMINAL_HEALTHY;
        float bestDistSq = 1e30f;

        for (uint8_t c = 0; c < MOTOR_NUM_CLUSTERS; c++) {
            float distSq = 0.0f;
            for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
                float d = delta[i] - centroids[c][i];
                distSq += d * d;
            }
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestCluster = c;
            }
        }

        if (nearestClusterDistOut != nullptr) {
            *nearestClusterDistOut = sqrtf(bestDistSq);
        }

        // Gentle online EMA tracking of Centroid 0 when firmly inside nominal regime
        if (bestCluster == MOTOR_NOMINAL_HEALTHY && mse < (mseWarningThreshold * 0.5f)) {
            for (uint8_t i = 0; i < MOTOR_FEAT_DIM; i++) {
                centroids[0][i] = 0.98f * centroids[0][i] + 0.02f * delta[i];
            }
        }

        // If Autoencoder MSE exceeds warning threshold even when K-Means is near C0, escalate
        if (bestCluster == MOTOR_NOMINAL_HEALTHY && mse >= mseWarningThreshold) {
            bestCluster = (mse >= mseCriticalThreshold) ? MOTOR_LOOSE_MOUNTING_SEVERE : MOTOR_SHAFT_IMBALANCE_1X;
        }

        return bestCluster;
    }
};

#endif // MOTOR_AUTOENCODER_H
