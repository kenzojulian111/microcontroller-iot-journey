# Project 15: Edge Offline Voice Command Node (Audio DSP + MFCC + INT8 1D-CNN)

![Track II — TinyML & Edge AI](https://img.shields.io/badge/Track_II-Advanced_Edge_AI-00897B?style=for-the-badge)
![DSP](https://img.shields.io/badge/Audio_DSP-Radix2_FFT_%2B_13_MFCC-1E88E5?style=for-the-badge)
![Inference](https://img.shields.io/badge/Model-INT8_1D_CNN_KWS-F4511E?style=for-the-badge)

## 1. Overview

This project implements a **100% offline, privacy-preserving Keyword Spotting (KWS) voice command node** on the **ESP32 / ESP32-S3** using an **INMP441 I2S Digital MEMS Microphone**. Unlike cloud-dependent voice assistants that stream raw audio over Wi-Fi, this node performs real-time **Mel-Frequency Cepstral Coefficient (MFCC)** extraction and **quantized `INT8` 1D-Convolutional Neural Network (1D-CNN)** inference directly in MCU SRAM with sub-2 ms total latency.

### Recognized Vocabulary (5 Classes)
| Class ID | Keyword | Acoustic Profile | Embedded Action |
| :---: | :--- | :--- | :--- |
| `0` | `_SILENCE_` | Background noise floor ($<-36\text{ dBFS}$) | Idle / Maintain state |
| `1` | `_UNKNOWN_` | Out-of-vocabulary speech & ambient transients | Reject activation |
| `2` | **`NYALAKAN`** | Indonesian *"Turn ON"* (Formants $720\text{ Hz} + 1180\text{ Hz}$) | Energize Relay (`GPIO 4` HIGH) & Status LED |
| `3` | **`MATIKAN`** | Indonesian *"Turn OFF"* (Formants $480\text{ Hz} + 1950\text{ Hz}$) | De-energize Relay (`GPIO 4` LOW) & Status LED |
| `4` | **`STATUS`** | Diagnostic query (Sibilant `/s/` energy $>3.2\text{ kHz}$) | Emit JSON telemetry & heap diagnostics over UART |

---

## 2. Directory Structure

```text
15-edge-offline-voice-command/
├── README.md
├── src/
│   └── edge_offline_voice_command/
│       ├── edge_offline_voice_command.ino   # I2S DMA capture, OLED visualizer, relay state machine
│       └── kws_mfcc_model.h                 # Radix-2 FFT, 20-Mel filterbank, DCT-II & INT8 1D-CNN
├── schematics/
│   ├── diagram.json                         # Wokwi circuit with 3 utterance injection buttons + OLED + Relay
│   └── wiring-guide.md                      # INMP441 I2S pinout for ESP32 and ESP32-S3
└── docs/
    └── architecture-and-training.md         # Mathematical derivations & TensorFlow INT8 export script
```

---

## 3. Key Technical Highlights

1. **Zero-Dependency Audio DSP Engine (`kws_mfcc_model.h`):**
   - **Pre-Emphasis FIR Filter:** $y[n] = x[n] - 0.97x[n-1]$ to equalize high-frequency sibilants.
   - **In-Place Radix-2 Cooley-Tukey FFT:** 256-point complex FFT with bit-reversal permutation and precomputed Hamming window.
   - **20-Band Mel Filterbank & DCT-II:** Integrates periodogram power across $300\text{ Hz} - 8000\text{ Hz}$ on the Mel scale and projects onto 13 orthogonal cepstral coefficients.
2. **Quantized `INT8` 1D-CNN + Posterior EMA Smoother:**
   - Evaluates a `[16 x 13]` temporal-cepstral rolling spectrogram using `INT8` weights and `INT32` accumulators in **$\approx 125\text{ }\mu\text{s}$**.
   - Exponential moving average ($\alpha = 0.45$) and a $1200\text{ ms}$ lockout window prevent false triggers.
3. **Dual-Mode Hardware / Wokwi Simulation:**
   - Automatically reads live 16 kHz PCM audio via ESP-IDF `driver/i2s.h` DMA when an INMP441 microphone is wired, or synthesizes realistic multi-formant utterances when triggered via pushbuttons (`GPIO 12`, `GPIO 14`, `GPIO 27`) or Serial commands (`NYALAKAN`, `MATIKAN`, `STATUS`).

---

## 4. Quick Start & Simulation Guide

### Running in Wokwi Simulator
1. Open `schematics/diagram.json` in Wokwi (VS Code extension or browser).
2. Compile and start the simulation. The SSD1306 OLED displays the active predicted keyword, confidence percentage, RMS level ($\text{dBFS}$), spectral centroid ($\text{Hz}$), DSP/NN execution time ($\mu\text{s}$), and a live 13-band MFCC bar graph.
3. Click the **Green (`Voice: NYALAKAN`)**, **Red (`Voice: MATIKAN`)**, or **Blue (`Voice: STATUS`)** pushbuttons to inject synthetic 16 kHz formant waveforms into the DSP pipeline and observe the relay and telemetry respond in real time.

### Sample Serial Telemetry Output
```json
{"frame":25,"kw":"_SILENCE_","conf":0.996,"rms_db":-51.8,"centroid_hz":3920,"dsp_us":942,"nn_us":118,"relay":false}
[KWS ACTION] Voice Command 'NYALAKAN' (94.2%) -> Relay ON
{"frame":50,"kw":"NYALAKAN","conf":0.942,"rms_db":-7.4,"centroid_hz":918,"dsp_us":965,"nn_us":121,"relay":true}
```
