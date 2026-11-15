# Project 12: Multi-Gas Electronic Nose (E-Nose) with Edge Neural Network

> **Track II — Phase 4: Classical TinyML, Sensor Fusion & Predictive Maintenance on MCU**  
> **Target MCU:** ESP32 | **Model:** 2-Layer Fully Connected Neural Network (`6 -> 12 ReLU -> 4 Softmax`) | **Sensors:** MQ-135 + MQ-2 + DHT22 + MQTT Telemetry

---

## 1. Overview & Architecture

Low-cost metal-oxide ($\text{SnO}_2$) gas sensors suffer from severe **cross-sensitivity** and **temperature/humidity baseline drift**—a single threshold on an MQ-2 or MQ-135 sensor will falsely trigger an LPG leak alarm when someone uses alcohol hand sanitizer nearby or when ambient humidity spikes.

This project implements a **6-feature sensor-fusion Electronic Nose (E-Nose)** powered by an on-device **2-layer Fully Connected Neural Network (FCNN)** in `enose_nn_model.h`. By combining temperature/humidity-compensated resistance ratios $(R_s/R_0)_{\text{MQ135}}$ and $(R_s/R_0)_{\text{MQ2}}$ with their logarithmic selectivity ratio $\ln(r_{135}/r_2)$ and transient plume derivative, the ESP32 discriminates four distinct chemical regimes in **27 microseconds** and publishes real-time JSON alerts over MQTT:

| Class ID | Label | Sensor Signature & Selectivity | Local & Cloud Action |
| :---: | :--- | :--- | :--- |
| `0` | `CLEAN_AIR` | $(R_s/R_0)_{\text{135}} \approx 1.0$, $(R_s/R_0)_{\text{2}} \approx 1.0$, $\ln(r_{135}/r_2) \approx 0$ | Green LED ON, Buzzer OFF |
| `1` | `CIGARETTE_SMOKE` | Simultaneous drop in both MQ-135 and MQ-2 ($\ln(r_{135}/r_2) \approx 0$, $r < 0.55$) | Red LED Blink + $1.2\text{ kHz}$ Chirp + MQTT Alert |
| `2` | `ALCOHOL_VOC` | Strong MQ-135 drop, mild MQ-2 response ($\ln(r_{135}/r_2) < -0.65$) | Red LED Blink + Silent VOC MQTT Alert |
| `3` | `LPG_LEAK_HAZARD` | Strong MQ-2 drop, mild MQ-135 response ($\ln(r_{135}/r_2) > +0.65$) | Solid Red LED + $2.6\text{ kHz}$ Alarm + Retained MQTT Hazard |

```mermaid
flowchart LR
    A["MQ-135 (GPIO 34)\nMQ-2 (GPIO 35)\nDHT22 (GPIO 4)"] --> B["T/RH Drift Compensation\n& Rs/R0 Normalization"]
    B --> C["Cross-Sensitivity Features\n[r135, r2, ln(r135/r2), dr/dt, T, RH]"]
    C --> D["2-Layer FCNN\n6 -> 12 ReLU -> 4 Softmax"]
    D --> E["Local Buzzer/LEDs &\nMQTT JSON Alert Publisher"]
```

---

## 2. Directory Structure

```text
12-ml-electronic-nose-gas/
├── README.md
├── src/
│   └── ml_electronic_nose_gas/
│       ├── ml_electronic_nose_gas.ino
│       ├── enose_nn_model.h
│       └── secrets.example.h
├── schematics/
│   ├── diagram.json
│   └── wiring-guide.md
└── docs/
    └── ml-pipeline-and-telemetry.md
```

---

## 3. Quick Start & Interactive Wokwi Simulation

1. Open `schematics/diagram.json` in Wokwi or flash `src/ml_electronic_nose_gas/ml_electronic_nose_gas.ino` onto an ESP32.
2. Adjust the two analog gas sensor sliders (`MQ-135` on `GPIO 34` and `MQ-2` on `GPIO 35`):
   - **Both sliders low ($\sim 25\text{–}35\%$):** Classifies `CLEAN_AIR` (`99.7%` confidence).
   - **Both sliders raised ($\sim 65\%$):** Classifies `CIGARETTE_SMOKE`.
   - **Raise `MQ-135` high ($\sim 80\%$) while keeping `MQ-2` low ($\sim 35\%$):** Classifies `ALCOHOL_VOC`.
   - **Raise `MQ-2` high ($\sim 85\%$) while keeping `MQ-135` low ($\sim 35\%$):** Classifies `LPG_LEAK_HAZARD` and triggers the $2.6\text{ kHz}$ alarm.
3. Press the **Calibrate R0 (`GPIO 15`)** button or send `CALIB` in the Serial Monitor to re-zero the clean-air baseline resistance $R_0$ at any ambient temperature/humidity.

---

## 4. Documentation Links
- [Hardware Wiring Guide](schematics/wiring-guide.md)
- [ML Pipeline, Cross-Sensitivity Fusion & Python Training Script](docs/ml-pipeline-and-telemetry.md)
