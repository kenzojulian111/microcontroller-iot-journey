/**
 * @file dynamic_ml_runtime.h
 * @brief Edge MLOps Dynamic Neural Network Binary Loader, MbedTLS SHA-256
 *        Cryptographic Verifier, LittleFS Atomic File Swapper, and Zero-Reboot
 *        In-RAM Tensor Arena Hot-Reload Engine for ESP32.
 *
 * Binary Model Container Format (`EML1` / TFLite-compatible FlatBuffer Header):
 *   Offset  Size  Field
 *   0x00    4B    magic[4]        = {'E', 'M', 'L', '1'} (0x314C4D45)
 *   0x04    2B    version_major   (uint16_t)
 *   0x06    2B    version_minor   (uint16_t)
 *   0x08    2B    version_patch   (uint16_t)
 *   0x0A    2B    quant_scheme    (0x0008 = Symmetric INT8)
 *   0x0C    4B    input_dim       (uint32_t = 4 features)
 *   0x10    4B    hidden_dim      (uint32_t = 8 neurons)
 *   0x14    4B    output_dim      (uint32_t = 3 classes: NORMAL, DRIFT_WARN, FAULT_CRIT)
 *   0x18    4B    input_scale     (float)
 *   0x1C    32B   w1[8][4]        (int8_t quantized layer-1 weights)
 *   0x3C    32B   b1[8]           (int32_t quantized layer-1 biases)
 *   0x5C    24B   w2[3][8]        (int8_t quantized layer-2 weights)
 *   0x74    12B   b2[3]           (int32_t quantized layer-2 biases)
 *   Total Binary Payload Size: 128 Bytes
 */

#ifndef DYNAMIC_ML_RUNTIME_H
#define DYNAMIC_ML_RUNTIME_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <mbedtls/md.h>
#include <math.h>
#include <stdint.h>

namespace EdgeMLOps {

static constexpr uint8_t  MODEL_MAGIC[4] = {'E', 'M', 'L', '1'};
static constexpr size_t   INPUT_DIM      = 4;
static constexpr size_t   HIDDEN_DIM     = 8;
static constexpr size_t   OUTPUT_DIM     = 3;

static const char* const CLASS_LABELS[OUTPUT_DIM] = {
    "NORMAL",
    "DRIFT_WARN",
    "FAULT_CRIT"
};

#pragma pack(push, 1)
struct DynamicModelBinary {
    uint8_t  magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t quant_scheme;
    uint32_t input_dim;
    uint32_t hidden_dim;
    uint32_t output_dim;
    float    input_scale;
    int8_t   w1[HIDDEN_DIM][INPUT_DIM];
    int32_t  b1[HIDDEN_DIM];
    int8_t   w2[OUTPUT_DIM][HIDDEN_DIM];
    int32_t  b2[OUTPUT_DIM];
};
#pragma pack(pop)

static_assert(sizeof(DynamicModelBinary) == 128, "DynamicModelBinary must be exactly 128 bytes");

struct InferenceOutput {
    uint8_t  predicted_class;
    float    probabilities[OUTPUT_DIM];
    uint32_t inference_latency_us;
    char     active_version[16];
};

/**
 * @brief Computes lowercase 64-character hexadecimal SHA-256 digest of a memory buffer
 *        using ESP32 hardware-accelerated MbedTLS SHA-256 engine.
 */
class SHA256Verifier {
public:
    static void computeBufferHex(const uint8_t* data, size_t len, char out_hex[65]) {
        uint8_t digest[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_setup(&ctx, info, 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, data, len);
        mbedtls_md_finish(&ctx, digest);
        mbedtls_md_free(&ctx);

        for (size_t i = 0; i < 32; ++i) {
            snprintf(&out_hex[i * 2], 3, "%02x", digest[i]);
        }
        out_hex[64] = '\0';
    }

    static bool computeFileHex(const char* path, char out_hex[65]) {
        File f = LittleFS.open(path, "r");
        if (!f) return false;

        uint8_t digest[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_setup(&ctx, info, 0);
        mbedtls_md_starts(&ctx);

        uint8_t buf[128];
        while (f.available()) {
            size_t r = f.read(buf, sizeof(buf));
            mbedtls_md_update(&ctx, buf, r);
        }
        f.close();

        mbedtls_md_finish(&ctx, digest);
        mbedtls_md_free(&ctx);

        for (size_t i = 0; i < 32; ++i) {
            snprintf(&out_hex[i * 2], 3, "%02x", digest[i]);
        }
        out_hex[64] = '\0';
        return true;
    }
};

/**
 * @brief Factory generator for Baseline Model v1.0.0 and Drift-Recalibrated Model v2.1.0,
 *        plus in-RAM Tensor Arena hot-reload execution engine.
 */
class DynamicMLRuntime {
public:
    DynamicMLRuntime() = default;

    /**
     * @brief Populates a v1.0.0 Factory Baseline model or a v2.1.0 Drift-Recalibrated
     *        model binary image.
     *
     * Key behavioral difference on high-temperature drift vector [T=58C, Vib=0.45g, I=2.1A, dT=0.8]:
     *   - v1.0.0 (uncalibrated for ambient thermal drift) misclassifies 58C normal operation as FAULT_CRIT.
     *   - v2.1.0 (retrained on thermal drift data) accurately classifies it as DRIFT_WARN or NORMAL
     *     while reserving FAULT_CRIT for genuine high-vibration bearing failure!
     */
    static DynamicModelBinary buildPresetModel(uint8_t preset_id) {
        DynamicModelBinary m{};
        memcpy(m.magic, MODEL_MAGIC, 4);
        m.quant_scheme = 0x0008;
        m.input_dim    = INPUT_DIM;
        m.hidden_dim   = HIDDEN_DIM;
        m.output_dim   = OUTPUT_DIM;
        m.input_scale  = 1.5f;

        if (preset_id == 1) {
            // Model v1.0.0: Baseline Factory Weights (Over-sensitive to temperature alone)
            m.version_major = 1; m.version_minor = 0; m.version_patch = 0;
            const int8_t w1[HIDDEN_DIM][INPUT_DIM] = {
                {-20, -25, -15, -10},
                { 10,  12,   8,   6},
                { 38,  15,  10,  18}, // Strong positive weight on feature[0] (Temperature)
                { 34,  18,  12,  14},
                {-15, -20, -10, -12},
                { 12,  22,  10,   8},
                { 28,  24,  14,  16},
                {-10, -14,  -8,  -6}
            };
            const int32_t b1[HIDDEN_DIM] = {450, 50, -350, -320, 380, -100, -280, 250};
            const int8_t w2[OUTPUT_DIM][HIDDEN_DIM] = {
                { 32, -10, -28, -26,  28, -12, -22,  20}, // Class 0: NORMAL
                { -8,  28,   8,   6, -10,  26,   8,  -8}, // Class 1: DRIFT_WARN
                {-28,  -6,  38,  36, -24,  10,  34, -18}  // Class 2: FAULT_CRIT
            };
            const int32_t b2[OUTPUT_DIM] = {10, 0, -10};
            memcpy(m.w1, w1, sizeof(w1));
            memcpy(m.b1, b1, sizeof(b1));
            memcpy(m.w2, w2, sizeof(w2));
            memcpy(m.b2, b2, sizeof(b2));
        } else {
            // Model v2.1.0: OTA Drift-Recalibrated Weights (Separates Thermal Drift from Vibration Fault)
            m.version_major = 2; m.version_minor = 1; m.version_patch = 0;
            const int8_t w1[HIDDEN_DIM][INPUT_DIM] = {
                {-22, -28, -16, -12},
                { 34, -14,   8,  20}, // Isolates high-temp + low-vibration into DRIFT_WARN
                {  6,  42,  24,  10}, // Requires high vibration (feature[1]) for FAULT_CRIT
                {  8,  40,  22,  12},
                {-18, -22, -12, -10},
                { 30, -12,   6,  18},
                {  4,  38,  20,   8},
                {-12, -16, -10,  -8}
            };
            const int32_t b1[HIDDEN_DIM] = {480, -240, -680, -640, 400, -210, -620, 280};
            const int8_t w2[OUTPUT_DIM][HIDDEN_DIM] = {
                { 34, -18, -28, -26,  30, -16, -24,  22}, // Class 0: NORMAL
                {-14,  38, -12, -10, -12,  36, -10, -10}, // Class 1: DRIFT_WARN
                {-28, -16,  42,  40, -24, -14,  38, -20}  // Class 2: FAULT_CRIT
            };
            const int32_t b2[OUTPUT_DIM] = {20, 15, -45};
            memcpy(m.w1, w1, sizeof(w1));
            memcpy(m.b1, b1, sizeof(b1));
            memcpy(m.w2, w2, sizeof(w2));
            memcpy(m.b2, b2, sizeof(b2));
        }
        return m;
    }

    /**
     * @brief Validates magic bytes, dimensions, and quantization header of a model image.
     */
    static bool validateModelHeader(const DynamicModelBinary& m) {
        if (memcmp(m.magic, MODEL_MAGIC, 4) != 0) return false;
        if (m.quant_scheme != 0x0008) return false;
        if (m.input_dim != INPUT_DIM || m.hidden_dim != HIDDEN_DIM || m.output_dim != OUTPUT_DIM) {
            return false;
        }
        return true;
    }

    /**
     * @brief Atomically verifies a staging file against its expected SHA-256 hex digest,
     *        backs up the previous active model, promotes staging -> active, and hot-reloads
     *        the in-RAM tensor arena without restarting the MCU.
     */
    bool hotSwapFromStaging(const char* staging_path,
                            const char* active_path,
                            const char* backup_path,
                            const char* expected_sha256_hex) {
        char computed_hex[65] = {0};
        if (!SHA256Verifier::computeFileHex(staging_path, computed_hex)) {
            Serial.println(F("[MLOps ERROR] Cannot open staging model for SHA-256 verification."));
            return false;
        }

        if (strcasecmp(computed_hex, expected_sha256_hex) != 0) {
            Serial.printf("[MLOps SECURITY] SHA-256 MISMATCH!\n  Expected: %s\n  Computed: %s\n",
                          expected_sha256_hex, computed_hex);
            LittleFS.remove(staging_path);
            return false;
        }

        // Verify binary header before swapping
        File sf = LittleFS.open(staging_path, "r");
        if (!sf || sf.size() != sizeof(DynamicModelBinary)) {
            if (sf) sf.close();
            LittleFS.remove(staging_path);
            return false;
        }
        DynamicModelBinary candidate{};
        sf.read(reinterpret_cast<uint8_t*>(&candidate), sizeof(DynamicModelBinary));
        sf.close();

        if (!validateModelHeader(candidate)) {
            Serial.println(F("[MLOps ERROR] Invalid model magic header or tensor dimensions."));
            LittleFS.remove(staging_path);
            return false;
        }

        // Atomic LittleFS swap: active -> backup, staging -> active
        if (LittleFS.exists(active_path)) {
            LittleFS.remove(backup_path);
            LittleFS.rename(active_path, backup_path);
        }
        if (!LittleFS.rename(staging_path, active_path)) {
            Serial.println(F("[MLOps ERROR] Failed to rename staging to active; rolling back."));
            if (LittleFS.exists(backup_path)) {
                LittleFS.rename(backup_path, active_path);
            }
            return false;
        }

        // Hot-reload weights into in-RAM active tensor arena
        active_model_ = candidate;
        strncpy(active_sha256_, computed_hex, sizeof(active_sha256_));
        model_loaded_ = true;
        hot_swap_count_++;
        return true;
    }

    /**
     * @brief Loads `/models/active_model.bin` from LittleFS into RAM on boot (or provisions
     *        factory v1.0.0 if the filesystem is fresh).
     */
    bool loadOrProvisionActiveModel(const char* active_path) {
        LittleFS.mkdir("/models");
        if (LittleFS.exists(active_path)) {
            File f = LittleFS.open(active_path, "r");
            if (f && f.size() == sizeof(DynamicModelBinary)) {
                DynamicModelBinary loaded{};
                f.read(reinterpret_cast<uint8_t*>(&loaded), sizeof(DynamicModelBinary));
                f.close();
                if (validateModelHeader(loaded)) {
                    active_model_ = loaded;
                    SHA256Verifier::computeFileHex(active_path, active_sha256_);
                    model_loaded_ = true;
                    return true;
                }
            }
            if (f) f.close();
        }

        // Provision factory v1.0.0 baseline into LittleFS
        DynamicModelBinary v1 = buildPresetModel(1);
        File wf = LittleFS.open(active_path, "w");
        if (wf) {
            wf.write(reinterpret_cast<const uint8_t*>(&v1), sizeof(DynamicModelBinary));
            wf.close();
        }
        active_model_ = v1;
        SHA256Verifier::computeBufferHex(
            reinterpret_cast<const uint8_t*>(&v1), sizeof(DynamicModelBinary), active_sha256_
        );
        model_loaded_ = true;
        return true;
    }

    /**
     * @brief Executes quantized INT8 2-layer neural network inference using the currently
     *        loaded in-RAM model weights.
     */
    InferenceOutput runInference(const float features[INPUT_DIM]) const {
        uint32_t t0 = micros();
        InferenceOutput out{};
        snprintf(out.active_version, sizeof(out.active_version), "v%u.%u.%u",
                 active_model_.version_major,
                 active_model_.version_minor,
                 active_model_.version_patch);

        // Quantize float features to int8_t [-128, 127]
        int8_t x_q[INPUT_DIM];
        for (size_t i = 0; i < INPUT_DIM; ++i) {
            float scaled = features[i] * active_model_.input_scale;
            if (scaled > 127.0f) scaled = 127.0f;
            if (scaled < -128.0f) scaled = -128.0f;
            x_q[i] = static_cast<int8_t>(lrintf(scaled));
        }

        // Layer 1: INT8 Dense + ReLU -> [8]
        int8_t h_q[HIDDEN_DIM];
        for (size_t h = 0; h < HIDDEN_DIM; ++h) {
            int32_t acc = active_model_.b1[h];
            for (size_t i = 0; i < INPUT_DIM; ++i) {
                acc += static_cast<int32_t>(x_q[i]) * static_cast<int32_t>(active_model_.w1[h][i]);
            }
            int32_t rq = acc >> 5;
            if (rq < 0) rq = 0;
            if (rq > 127) rq = 127;
            h_q[h] = static_cast<int8_t>(rq);
        }

        // Layer 2: INT8 Dense -> Softmax [3]
        float logits[OUTPUT_DIM];
        float max_l = -1e9f;
        for (size_t c = 0; c < OUTPUT_DIM; ++c) {
            int32_t acc = active_model_.b2[c];
            for (size_t h = 0; h < HIDDEN_DIM; ++h) {
                acc += static_cast<int32_t>(h_q[h]) * static_cast<int32_t>(active_model_.w2[c][h]);
            }
            logits[c] = static_cast<float>(acc) * 0.015f;
            if (logits[c] > max_l) max_l = logits[c];
        }

        float sum_exp = 0.0f;
        uint8_t best_c = 0;
        for (size_t c = 0; c < OUTPUT_DIM; ++c) {
            out.probabilities[c] = expf(logits[c] - max_l);
            sum_exp += out.probabilities[c];
        }
        for (size_t c = 0; c < OUTPUT_DIM; ++c) {
            out.probabilities[c] /= sum_exp;
            if (out.probabilities[c] > out.probabilities[best_c]) {
                best_c = static_cast<uint8_t>(c);
            }
        }

        out.predicted_class      = best_c;
        out.inference_latency_us = micros() - t0;
        return out;
    }

    const DynamicModelBinary& getActiveModel() const { return active_model_; }
    const char* getActiveSHA256() const { return active_sha256_; }
    uint32_t getHotSwapCount() const { return hot_swap_count_; }

private:
    DynamicModelBinary active_model_{};
    char               active_sha256_[65] = {0};
    bool               model_loaded_      = false;
    uint32_t           hot_swap_count_    = 0;
};

} // namespace EdgeMLOps

#endif // DYNAMIC_ML_RUNTIME_H
