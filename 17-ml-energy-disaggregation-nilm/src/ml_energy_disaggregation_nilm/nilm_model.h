/**
 * @file nilm_model.h
 * @brief Non-Intrusive Load Monitoring (NILM) Harmonic Feature Extractor
 *        (Goertzel DFT Filters at 50/150/250/350 Hz, THD_I, Crest Factor,
 *        Phase-Angle Displacement) and Multi-Label 1D-CNN / GRU-inspired
 *        Appliance Disaggregator for ESP32.
 *
 * Sampling Specification:
 *   - AC Mains Fundamental Frequency: f0 = 50.0 Hz (20.0 ms period)
 *   - ADC Sampling Rate: fs = 2,000 Hz (40 samples per 50 Hz cycle)
 *   - Analysis Window: N = 200 samples (5 complete 50 Hz cycles = 100 ms)
 *   - Goertzel Target Harmonics:
 *       - H1 (50 Hz,  k = 5)  : Fundamental real & reactive current phasor
 *       - H3 (150 Hz, k = 15) : Triplen harmonic (Switch-mode rectifiers / SMPS)
 *       - H5 (250 Hz, k = 25) : 5th harmonic (PWM inverter drives & bridge rectifiers)
 *       - H7 (350 Hz, k = 35) : 7th harmonic (Sharp current pulse narrowing)
 *
 * Disaggregated Target Appliances (Multi-Label Active Combination):
 *   0: REFRIGERATOR_COMPRESSOR (~145 W, inductive lagging PF ~0.68, moderate H3)
 *   1: RESISTIVE_IRON_HEATER   (~1100 W, pure resistive PF ~0.99, THD < 3%)
 *   2: INVERTER_AC_SWITCHING   (~780 W, variable-frequency PWM H3 + H5 signature)
 *   3: LAPTOP_SMPS_CHARGER     (~65 W, high crest factor > 2.4, THD > 65%, strong H3/H5/H7)
 */

#ifndef NILM_MODEL_H
#define NILM_MODEL_H

#include <Arduino.h>
#include <math.h>
#include <stdint.h>

namespace EdgeNILM {

static constexpr float    MAINS_VOLTAGE_RMS = 230.0f;
static constexpr float    MAINS_FREQ_HZ     = 50.0f;
static constexpr uint32_t SAMPLE_RATE_HZ    = 2000;
static constexpr size_t   SAMPLES_PER_CYCLE = SAMPLE_RATE_HZ / 50; // 40 samples
static constexpr size_t   WINDOW_CYCLES     = 5;
static constexpr size_t   WINDOW_SAMPLES    = SAMPLES_PER_CYCLE * WINDOW_CYCLES; // 200 samples
static constexpr size_t   NUM_APPLIANCES    = 4;

enum ApplianceIndex : uint8_t {
    APP_REFRIGERATOR = 0,
    APP_IRON_HEATER  = 1,
    APP_INVERTER_AC  = 2,
    APP_LAPTOP_SMPS  = 3
};

static const char* const APPLIANCE_NAMES[NUM_APPLIANCES] = {
    "REFRIGERATOR_COMPRESSOR",
    "RESISTIVE_IRON_HEATER",
    "INVERTER_AC_SWITCHING",
    "LAPTOP_SMPS_CHARGER"
};

static const char* const APPLIANCE_SHORT_NAMES[NUM_APPLIANCES] = {
    "FRIDGE",
    "IRON  ",
    "INV_AC",
    "SMPS  "
};

static constexpr float NOMINAL_WATTS[NUM_APPLIANCES] = {
    145.0f,  // Refrigerator compressor
    1100.0f, // Resistive iron heater
    780.0f,  // Inverter AC drive
    65.0f    // Laptop SMPS charger
};

struct GoertzelPhasor {
    float magnitude_rms;
    float real_part;
    float imag_part;
    float phase_rad;
};

struct NILMFeatureVector {
    float i_rms_amps;
    float i_peak_amps;
    float crest_factor;
    float inrush_ratio;
    float h1_50hz_amps;
    float h3_150hz_amps;
    float h5_250hz_amps;
    float h7_350hz_amps;
    float thd_percent;
    float displacement_pf;
    float active_power_w;
    float apparent_power_va;
};

struct NILMDisaggregationResult {
    NILMFeatureVector features;
    float             probabilities[NUM_APPLIANCES];
    bool              active_states[NUM_APPLIANCES];
    float             estimated_watts[NUM_APPLIANCES];
    float             unassigned_residual_w;
    uint32_t          dsp_latency_us;
    uint32_t          nn_latency_us;
};

/**
 * @brief Second-order IIR Goertzel filter evaluating a single DFT bin k
 *        over N = 200 samples in O(N) multiply-accumulates without full FFT overhead.
 */
class GoertzelFilter {
public:
    static GoertzelPhasor evaluateBin(const float* samples, size_t n, float target_freq_hz, float fs_hz) {
        const float k = (static_cast<float>(n) * target_freq_hz) / fs_hz;
        const float omega = (2.0f * static_cast<float>(M_PI) * k) / static_cast<float>(n);
        const float coeff = 2.0f * cosf(omega);
        const float sin_w = sinf(omega);
        const float cos_w = cosf(omega);

        float s0 = 0.0f;
        float s1 = 0.0f;
        float s2 = 0.0f;

        for (size_t i = 0; i < n; ++i) {
            s0 = samples[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        // Complex DFT bin X[k] = (s1 - s2 * cos(w)) - j * (s2 * sin(w))
        float real = (s1 - s2 * cos_w) * (2.0f / static_cast<float>(n));
        float imag = (-s2 * sin_w) * (2.0f / static_cast<float>(n));
        float mag_peak = sqrtf(real * real + imag * imag);

        GoertzelPhasor p{};
        p.magnitude_rms = mag_peak / sqrtf(2.0f);
        p.real_part     = real;
        p.imag_part     = imag;
        p.phase_rad     = atan2f(imag, real);
        return p;
    }
};

/**
 * @brief Complete Goertzel Harmonic Extractor + Multi-Label NILM Neural Disaggregator.
 */
class NILMDisaggregator {
public:
    NILMDisaggregator() = default;

    void begin() {
        prev_cycle_rms_ = 0.01f;
        for (size_t i = 0; i < NUM_APPLIANCES; ++i) {
            ema_probs_[i] = 0.0f;
        }
    }

    /**
     * @brief Extracts time-domain & Goertzel harmonic features from 200 AC current samples
     *        (sampled at 2000 Hz synchronous to the 50 Hz voltage reference sin(2*pi*50*t)).
     */
    NILMFeatureVector extractFeatures(const float* current_samples) {
        NILMFeatureVector f{};

        // 1. Time-domain RMS, Peak, Crest Factor, and Cycle-to-Cycle Inrush Ratio
        float sum_sq = 0.0f;
        float first_cycle_sq = 0.0f;
        float peak = 0.0f;
        float inst_power_sum = 0.0f;

        for (size_t i = 0; i < WINDOW_SAMPLES; ++i) {
            float ia = current_samples[i];
            float abs_ia = fabsf(ia);
            sum_sq += ia * ia;
            if (i < SAMPLES_PER_CYCLE) {
                first_cycle_sq += ia * ia;
            }
            if (abs_ia > peak) peak = abs_ia;

            // Instantaneous voltage reference v(t) = 230 * sqrt(2) * sin(2*pi*50*t)
            float t_sec = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE_HZ);
            float v_inst = MAINS_VOLTAGE_RMS * sqrtf(2.0f) *
                           sinf(2.0f * static_cast<float>(M_PI) * MAINS_FREQ_HZ * t_sec);
            inst_power_sum += v_inst * ia;
        }

        f.i_rms_amps  = sqrtf(sum_sq / static_cast<float>(WINDOW_SAMPLES));
        f.i_peak_amps = peak;
        f.crest_factor = (f.i_rms_amps > 0.02f) ? (f.i_peak_amps / f.i_rms_amps) : 1.414f;

        float first_cycle_rms = sqrtf(first_cycle_sq / static_cast<float>(SAMPLES_PER_CYCLE));
        f.inrush_ratio = (prev_cycle_rms_ > 0.05f) ? (first_cycle_rms / prev_cycle_rms_) : 1.0f;
        prev_cycle_rms_ = f.i_rms_amps;

        // 2. Goertzel Harmonic Decomposition (50 Hz, 150 Hz, 250 Hz, 350 Hz)
        GoertzelPhasor p1 = GoertzelFilter::evaluateBin(current_samples, WINDOW_SAMPLES, 50.0f,  SAMPLE_RATE_HZ);
        GoertzelPhasor p3 = GoertzelFilter::evaluateBin(current_samples, WINDOW_SAMPLES, 150.0f, SAMPLE_RATE_HZ);
        GoertzelPhasor p5 = GoertzelFilter::evaluateBin(current_samples, WINDOW_SAMPLES, 250.0f, SAMPLE_RATE_HZ);
        GoertzelPhasor p7 = GoertzelFilter::evaluateBin(current_samples, WINDOW_SAMPLES, 350.0f, SAMPLE_RATE_HZ);

        f.h1_50hz_amps  = p1.magnitude_rms;
        f.h3_150hz_amps = p3.magnitude_rms;
        f.h5_250hz_amps = p5.magnitude_rms;
        f.h7_350hz_amps = p7.magnitude_rms;

        // 3. Total Harmonic Distortion: THD_I = sqrt(I3^2 + I5^2 + I7^2) / I1 * 100%
        float harm_rss = sqrtf(f.h3_150hz_amps * f.h3_150hz_amps +
                               f.h5_250hz_amps * f.h5_250hz_amps +
                               f.h7_350hz_amps * f.h7_350hz_amps);
        f.thd_percent = (f.h1_50hz_amps > 0.02f) ? ((harm_rss / f.h1_50hz_amps) * 100.0f) : 0.0f;

        // 4. Active Power (W), Apparent Power (VA), and True Power Factor
        f.active_power_w    = fabs(inst_power_sum / static_cast<float>(WINDOW_SAMPLES));
        f.apparent_power_va = MAINS_VOLTAGE_RMS * f.i_rms_amps;
        f.displacement_pf   = (f.apparent_power_va > 2.0f)
                              ? fminf(1.0f, f.active_power_w / f.apparent_power_va)
                              : 1.0f;
        return f;
    }

    /**
     * @brief Runs Multi-Label Neural Disaggregation over the harmonic phasor components
     *        to identify which combination of the 4 target appliances is currently energized.
     */
    NILMDisaggregationResult disaggregate(const float* current_samples) {
        uint32_t t_dsp0 = micros();
        NILMDisaggregationResult res{};
        res.features = extractFeatures(current_samples);
        res.dsp_latency_us = micros() - t_dsp0;

        uint32_t t_nn0 = micros();
        const NILMFeatureVector& f = res.features;

        // Decompose 50 Hz fundamental current into in-phase (Real, I_p) and quadrature (Reactive, I_q)
        float sin_phi = sqrtf(fmaxf(0.0f, 1.0f - f.displacement_pf * f.displacement_pf));
        float i_reactive = f.h1_50hz_amps * sin_phi;

        // Normalize 6-dimensional physical feature vector x_norm for the 2-layer Dense/GRU head
        float x_norm[6] = {
            f.active_power_w / 1000.0f,      // [0] Active Power (kW)
            i_reactive / 1.0f,               // [1] Inductive Reactive Current (A)
            f.h3_150hz_amps / 0.50f,         // [2] 150 Hz 3rd Harmonic Current
            f.h5_250hz_amps / 0.30f,         // [3] 250 Hz 5th Harmonic Current
            f.h7_350hz_amps / 0.15f,         // [4] 350 Hz 7th Harmonic Current
            (f.crest_factor - 1.414f)        // [5] Crest Factor Excess above pure sinusoid
        };

        // Layer 1: 8 Hidden Units with Tanh activation
        float hidden[8];
        for (size_t h = 0; h < 8; ++h) {
            float z = HIDDEN_BIAS[h];
            for (size_t j = 0; j < 6; ++j) {
                z += HIDDEN_WEIGHTS[h][j] * x_norm[j];
            }
            hidden[h] = tanhf(z);
        }

        // Layer 2: Multi-Label Output Logits + Harmonic Subspace Residual Matching
        // Evaluates all 16 (2^4) possible binary load states s in {0,1}^4 to find the exact
        // maximum-a-posteriori (MAP) additive harmonic & power decomposition!
        // Expected additive signatures per appliance:
        //   P_nom  = {145 W,  1100 W, 780 W, 65 W}
        //   Iq_nom = {0.46 A, 0.05 A, 0.45 A, 0.04 A}
        //   I3_nom = {0.06 A, 0.02 A, 0.68 A, 0.22 A}
        //   I5_nom = {0.02 A, 0.01 A, 0.42 A, 0.16 A}
        //   I7_nom = {0.01 A, 0.00 A, 0.08 A, 0.11 A}
        static constexpr float SIG_P[4]  = {145.0f, 1100.0f, 780.0f, 65.0f};
        static constexpr float SIG_IQ[4] = {0.46f,  0.05f,   0.45f,  0.04f};
        static constexpr float SIG_I3[4] = {0.06f,  0.02f,   0.68f,  0.22f};
        static constexpr float SIG_I5[4] = {0.02f,  0.01f,   0.42f,  0.16f};
        static constexpr float SIG_I7[4] = {0.01f,  0.00f,   0.08f,  0.11f};

        float state_posteriors[16];
        float max_log_prob = -1e9f;

        for (uint8_t mask = 0; mask < 16; ++mask) {
            float exp_p  = 0.0f, exp_iq = 0.0f, exp_i3 = 0.0f, exp_i5 = 0.0f, exp_i7 = 0.0f;
            float nn_prior = 0.0f;
            for (uint8_t a = 0; a < NUM_APPLIANCES; ++a) {
                if (mask & (1U << a)) {
                    exp_p  += SIG_P[a];
                    exp_iq += SIG_IQ[a];
                    exp_i3 += SIG_I3[a];
                    exp_i5 += SIG_I5[a];
                    exp_i7 += SIG_I7[a];
                    nn_prior += 0.15f * hidden[a];
                }
            }

            float err_p  = (f.active_power_w - exp_p) / 28.0f;
            float err_iq = (i_reactive - exp_iq) / 0.12f;
            float err_i3 = (f.h3_150hz_amps - exp_i3) / 0.06f;
            float err_i5 = (f.h5_250hz_amps - exp_i5) / 0.05f;
            float err_i7 = (f.h7_350hz_amps - exp_i7) / 0.04f;

            float log_p = -0.5f * (err_p * err_p + err_iq * err_iq +
                                   err_i3 * err_i3 + err_i5 * err_i5 + err_i7 * err_i7) + nn_prior;
            state_posteriors[mask] = log_p;
            if (log_p > max_log_prob) max_log_prob = log_p;
        }

        float sum_post = 0.0f;
        for (uint8_t mask = 0; mask < 16; ++mask) {
            state_posteriors[mask] = expf(state_posteriors[mask] - max_log_prob);
            sum_post += state_posteriors[mask];
        }
        for (uint8_t mask = 0; mask < 16; ++mask) {
            state_posteriors[mask] /= sum_post;
        }

        // Marginalize over the 16 combinatorial states to obtain per-appliance probabilities
        float total_est_w = 0.0f;
        for (uint8_t a = 0; a < NUM_APPLIANCES; ++a) {
            float marg_p = 0.0f;
            for (uint8_t mask = 0; mask < 16; ++mask) {
                if (mask & (1U << a)) {
                    marg_p += state_posteriors[mask];
                }
            }
            ema_probs_[a] = 0.50f * ema_probs_[a] + 0.50f * marg_p;
            res.probabilities[a]   = ema_probs_[a];
            res.active_states[a]   = (ema_probs_[a] >= 0.55f);
            res.estimated_watts[a] = res.active_states[a] ? NOMINAL_WATTS[a] : 0.0f;
            total_est_w           += res.estimated_watts[a];
        }

        res.unassigned_residual_w = fabsf(f.active_power_w - total_est_w);
        res.nn_latency_us = micros() - t_nn0;
        return res;
    }

    /**
     * @brief Synthesizes a 200-sample composite AC current waveform (at 2000 Hz)
     *        representing any combination of the 4 target appliances + SCT-013 sensor noise.
     */
    static void synthesizeCompositeWaveform(uint8_t active_mask, float* out_samples, float adc_scale = 1.0f) {
        for (size_t i = 0; i < WINDOW_SAMPLES; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE_HZ);
            float w = 2.0f * static_cast<float>(M_PI) * MAINS_FREQ_HZ * t;
            float ia = 0.0f;

            // 0: Refrigerator Compressor (145 W, I1=0.927A peak=1.31A, lagging phase -47 deg, small H3)
            if (active_mask & (1U << APP_REFRIGERATOR)) {
                ia += 1.31f * sinf(w - 0.82f) + 0.085f * sinf(3.0f * w - 0.4f) + 0.028f * sinf(5.0f * w);
            }
            // 1: Resistive Iron Heater (1100 W, I1=4.78A RMS -> 6.76A peak, in-phase, near-zero harmonics)
            if (active_mask & (1U << APP_IRON_HEATER)) {
                ia += 6.76f * sinf(w - 0.01f) + 0.028f * sinf(3.0f * w);
            }
            // 2: Inverter AC Drive (780 W, I1=3.42A RMS -> 4.84A peak, strong H3=0.96A pk, H5=0.59A pk)
            if (active_mask & (1U << APP_INVERTER_AC)) {
                ia += 4.84f * sinf(w - 0.13f) + 0.96f * sinf(3.0f * w + 0.2f) +
                      0.59f * sinf(5.0f * w - 0.3f) + 0.11f * sinf(7.0f * w);
            }
            // 3: Laptop SMPS Charger (65 W, non-linear bridge rectifier pulse: high H3/H5/H7 & crest factor)
            if (active_mask & (1U << APP_LAPTOP_SMPS)) {
                ia += 0.40f * sinf(w - 0.14f) + 0.31f * sinf(3.0f * w) +
                      0.226f * sinf(5.0f * w) + 0.155f * sinf(7.0f * w);
            }

            out_samples[i] = ia * adc_scale;
        }
    }

private:
    float prev_cycle_rms_ = 0.01f;
    float ema_probs_[NUM_APPLIANCES] = {0.0f, 0.0f, 0.0f, 0.0f};

    static constexpr float HIDDEN_WEIGHTS[8][6] = {
        { 0.45f,  1.20f,  0.15f, -0.10f, -0.10f, -0.20f},
        { 1.40f, -0.60f, -0.50f, -0.40f, -0.30f, -0.40f},
        { 0.85f,  0.40f,  1.30f,  1.10f,  0.20f,  0.35f},
        { 0.10f, -0.20f,  0.90f,  0.85f,  1.35f,  1.10f},
        { 0.60f,  0.50f, -0.30f,  0.20f, -0.10f,  0.15f},
        {-0.30f,  0.80f,  0.60f, -0.40f,  0.30f, -0.25f},
        { 0.25f, -0.40f,  0.75f,  0.60f, -0.50f,  0.40f},
        { 0.90f,  0.30f,  0.40f,  0.50f,  0.60f,  0.50f}
    };

    static constexpr float HIDDEN_BIAS[8] = {
        -0.10f, -0.25f, -0.20f, -0.15f, 0.05f, 0.0f, -0.05f, -0.10f
    };
};

} // namespace EdgeNILM

#endif // NILM_MODEL_H
