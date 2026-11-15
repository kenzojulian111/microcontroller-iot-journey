#ifndef ANOMALY_MODEL_H
#define ANOMALY_MODEL_H

#include <Arduino.h>
#include <math.h>

/**
 * ============================================================================
 * Header Model TinyML & Ekstraksi Fitur Sinyal — Proyek 09 Edge AI
 * ============================================================================
 * File ini mengimplementasikan:
 * 1. Ekstraksi Fitur DSP (Digital Signal Processing) untuk 3 sumbu getaran:
 *    - RMS (Root Mean Square)
 *    - Peak-to-Peak Amplitude (P2P)
 *    - Variance (Variansi Energi Getaran)
 *    - Crest Factor (Rasio Puncak terhadap RMS — detektor benturan bearing)
 *    - Zero-Crossing Rate (ZCR — estimasi frekuensi dominan getaran)
 * 2. Model Inferensi Hibrida Ringan (Siap pakai tanpa library eksternal):
 *    - Classifier Neural Network ringan (Softmax 3 Kelas: IDLE, NORMAL, ANOMALY)
 *    - Skor Anomali Jarak Centroid Terbobot (Mahalanobis-style Anomaly Score)
 * 3. Opsional: Jika pengguna sudah mengekspor library ZIP dari Edge Impulse Studio,
 *    cukup aktifkan `#define USE_EDGE_IMPULSE_ZIP_LIBRARY` di sketch utama.
 * ============================================================================
 */

enum MachineState : uint8_t {
  STATE_IDLE             = 0,
  STATE_NORMAL_OPERATION = 1,
  STATE_ANOMALY_DETECTED = 2
};

struct AxisDspFeatures {
  float mean;
  float rms;            // Dynamic AC RMS (setelah dikurangi komponen gravitasi DC)
  float peakToPeak;     // Max - Min (m/s^2)
  float variance;       // Variansi sinyal (m/s^2)^2
  float crestFactor;    // Peak / RMS (rasio impak mekanis)
  float zeroCrossRate;  // Frekuensi melintasi nilai rata-rata (0.0 - 1.0)
};

struct EdgeFeatureVector {
  AxisDspFeatures x;
  AxisDspFeatures y;
  AxisDspFeatures z;
  float compositeRms;       // Akar jumlah kuadrat RMS 3 sumbu (m/s^2)
  float compositePeak2Peak; // Rata-rata Peak-to-Peak 3 sumbu (m/s^2)
  float maxCrestFactor;     // Crest factor tertinggi di antara 3 sumbu
  float avgZeroCrossRate;   // Rata-rata ZCR 3 sumbu
};

struct InferenceResult {
  MachineState predictedClass;
  const char*  label;
  float        probIdle;
  float        probNormal;
  float        probAnomaly;
  float        anomalyScore; // Skor > 1.0 menandakan deviasi di luar distribusi normal
  uint32_t     dspTimeUs;
  uint32_t     classificationTimeUs;
};

// ============================================================================
// Parameter Normalisasi & Centroid Referensi (Kalibrasi Motor/Kipas Industri)
// Fitur Kunci [4]: { compositeRms, compositePeak2Peak, maxCrestFactor, avgZeroCrossRate }
// ============================================================================
static const float NORM_MEAN[4] = { 1.45f, 4.80f, 2.35f, 0.28f };
static const float NORM_STD[4]  = { 1.10f, 3.50f, 0.95f, 0.15f };

// Centroid K-Means pada ruang fitur untuk masing-masing profil operasi mesin:
//   Baris 0 = IDLE             (Getaran sangat rendah, hanya noise lantai sensor)
//   Baris 1 = NORMAL_OPERATION (Getaran harmonik stabil dari putaran motor sehat)
//   Baris 2 = ANOMALY_DETECTED (Ketidakseimbangan baling-baling / bearing aus / benturan)
static const float CLASS_CENTROIDS[3][4] = {
  { 0.08f,  0.32f, 1.65f, 0.12f }, // IDLE
  { 1.25f,  4.10f, 2.15f, 0.34f }, // NORMAL_OPERATION
  { 4.20f, 13.80f, 3.85f, 0.46f }  // ANOMALY_DETECTED
};

// Bobot Linear/Dense Layer Classifier [3 kelas][4 fitur terstandarisasi + 1 bias]
static const float DENSE_WEIGHTS[3][4] = {
  { -2.40f, -2.10f, -0.85f, -0.90f }, // Logit IDLE
  {  0.35f,  0.30f, -0.45f,  0.65f }, // Logit NORMAL_OPERATION
  {  2.25f,  2.15f,  1.40f,  0.45f }  // Logit ANOMALY_DETECTED
};

static const float DENSE_BIAS[3] = { -0.35f, 0.85f, -0.60f };

// ============================================================================
// Fungsi DSP: Ekstraksi Fitur untuk Satu Sumbu
// ============================================================================
inline AxisDspFeatures computeAxisFeatures(const float* samples, size_t count) {
  AxisDspFeatures feat = {0, 0, 0, 0, 0, 0};
  if (count == 0) return feat;

  float sum = 0.0f;
  float minVal = samples[0];
  float maxVal = samples[0];

  for (size_t i = 0; i < count; i++) {
    float v = samples[i];
    sum += v;
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }

  feat.mean = sum / (float)count;
  feat.peakToPeak = maxVal - minVal;

  float sumSqDiff = 0.0f;
  float maxAbsAc  = 0.0f;
  size_t zeroCrossings = 0;

  for (size_t i = 0; i < count; i++) {
    float ac = samples[i] - feat.mean; // Hilangkan offset gravitasi statis (DC removal)
    sumSqDiff += ac * ac;
    float absAc = fabsf(ac);
    if (absAc > maxAbsAc) maxAbsAc = absAc;

    if (i > 0) {
      float prevAc = samples[i - 1] - feat.mean;
      if ((prevAc >= 0.0f && ac < 0.0f) || (prevAc < 0.0f && ac >= 0.0f)) {
        zeroCrossings++;
      }
    }
  }

  feat.variance      = sumSqDiff / (float)count;
  feat.rms           = sqrtf(feat.variance);
  feat.crestFactor   = (feat.rms > 0.02f) ? (maxAbsAc / feat.rms) : 1.0f;
  feat.zeroCrossRate = (count > 1) ? ((float)zeroCrossings / (float)(count - 1)) : 0.0f;
  return feat;
}

// ============================================================================
// Fungsi DSP: Ekstraksi Vektor Fitur Lengkap 3-Axis (X, Y, Z)
// ============================================================================
inline EdgeFeatureVector extractEdgeFeatures(const float* ax, const float* ay, const float* az, size_t count) {
  EdgeFeatureVector vec;
  vec.x = computeAxisFeatures(ax, count);
  vec.y = computeAxisFeatures(ay, count);
  vec.z = computeAxisFeatures(az, count);

  vec.compositeRms = sqrtf(vec.x.rms * vec.x.rms +
                           vec.y.rms * vec.y.rms +
                           vec.z.rms * vec.z.rms);

  vec.compositePeak2Peak = (vec.x.peakToPeak + vec.y.peakToPeak + vec.z.peakToPeak) / 3.0f;
  vec.maxCrestFactor     = fmaxf(vec.x.crestFactor, fmaxf(vec.y.crestFactor, vec.z.crestFactor));
  vec.avgZeroCrossRate   = (vec.x.zeroCrossRate + vec.y.zeroCrossRate + vec.z.zeroCrossRate) / 3.0f;
  return vec;
}

// ============================================================================
// Fungsi Inferensi TinyML: Klasifikasi Softmax + Skor Anomali Mahalanobis
// ============================================================================
inline InferenceResult runEdgeInference(const float* ax, const float* ay, const float* az, size_t count) {
  InferenceResult res;
  uint32_t t0 = micros();
  EdgeFeatureVector feat = extractEdgeFeatures(ax, ay, az, count);
  uint32_t t1 = micros();
  res.dspTimeUs = t1 - t0;

  float rawFeat[4] = {
    feat.compositeRms,
    feat.compositePeak2Peak,
    feat.maxCrestFactor,
    feat.avgZeroCrossRate
  };

  // 1. Standarisasi Z-Score
  float zFeat[4];
  for (int j = 0; j < 4; j++) {
    zFeat[j] = (rawFeat[j] - NORM_MEAN[j]) / NORM_STD[j];
  }

  // 2. Hitung Logit Dense Layer & Softmax Probabilities
  float logits[3];
  float maxLogit = -1e9f;
  for (int c = 0; c < 3; c++) {
    float acc = DENSE_BIAS[c];
    for (int j = 0; j < 4; j++) {
      acc += DENSE_WEIGHTS[c][j] * zFeat[j];
    }
    logits[c] = acc;
    if (acc > maxLogit) maxLogit = acc;
  }

  float expSum = 0.0f;
  float probs[3];
  for (int c = 0; c < 3; c++) {
    probs[c] = expf(logits[c] - maxLogit);
    expSum += probs[c];
  }
  for (int c = 0; c < 3; c++) {
    probs[c] /= expSum;
  }

  res.probIdle    = probs[0];
  res.probNormal  = probs[1];
  res.probAnomaly = probs[2];

  // 3. Hitung Skor Anomali berdasarkan jarak ke Centroid Normal/Idle terdekat
  float distIdleSq   = 0.0f;
  float distNormalSq = 0.0f;
  for (int j = 0; j < 4; j++) {
    float dIdle = (rawFeat[j] - CLASS_CENTROIDS[0][j]) / NORM_STD[j];
    float dNorm = (rawFeat[j] - CLASS_CENTROIDS[1][j]) / NORM_STD[j];
    distIdleSq   += dIdle * dIdle;
    distNormalSq += dNorm * dNorm;
  }
  float minHealthyDist = sqrtf(fminf(distIdleSq, distNormalSq));
  res.anomalyScore = minHealthyDist / 2.20f; // Threshold > 1.00 = Anomali

  // 4. Penentuan Kelas Akhir (Menggabungkan Softmax Classifier & Anomaly Detector)
  if (res.anomalyScore >= 1.00f || res.probAnomaly >= 0.50f) {
    res.predictedClass = STATE_ANOMALY_DETECTED;
    res.label          = "ANOMALY_DETECTED";
  } else if (feat.compositeRms < 0.35f && res.probIdle >= res.probNormal) {
    res.predictedClass = STATE_IDLE;
    res.label          = "IDLE";
  } else {
    res.predictedClass = STATE_NORMAL_OPERATION;
    res.label          = "NORMAL_OPERATION";
  }

  res.classificationTimeUs = micros() - t1;
  return res;
}

#endif // ANOMALY_MODEL_H
