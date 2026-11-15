# Project 17: ML Energy Disaggregation — Non-Intrusive Load Monitoring (NILM)

![Track II — TinyML & Edge AI](https://img.shields.io/badge/Track_II-Smart_Grid_Edge_AI-00897B?style=for-the-badge)
![DSP](https://img.shields.io/badge/Harmonic_DSP-2000Hz_Goertzel_DFT-1E88E5?style=for-the-badge)
![Classifier](https://img.shields.io/badge/Disaggregator-Multi--Label_NILM-F4511E?style=for-the-badge)

## 1. Overview

**Non-Intrusive Load Monitoring (NILM)** disaggregates a building's total electrical consumption—measured at a **single main AC wire** via one **SCT-013 split-core current transformer**—into individual appliance states and wattages without installing smart plugs on every outlet.

By sampling the $50\text{ Hz}$ AC current waveform at **$2,000\text{ Hz}$ ($40\text{ samples/cycle}$)** over a 5-cycle ($200\text{-sample}$) window, this firmware executes **second-order Goertzel DFT filters** at the **fundamental ($50\text{ Hz}$)**, **3rd ($150\text{ Hz}$)**, **5th ($250\text{ Hz}$)**, and **7th ($350\text{ Hz}$)** harmonics, alongside reactive current ($I_q$), Total Harmonic Distortion ($\text{THD}_I$), and Crest Factor. A multi-label neural + Maximum-A-Posteriori (MAP) harmonic subspace disaggregator (`nilm_model.h`) identifies any simultaneous combination of 4 residential appliances in under **$350\text{ }\mu\text{s}$**.

### Target Disaggregated Appliances
| ID | Appliance Class | Power | PF | $\text{THD}_I$ | Distinguishing Harmonic Signature |
| :---: | :--- | :---: | :---: | :---: | :--- |
| `0` | **`REFRIGERATOR_COMPRESSOR`** | $145\text{ W}$ | $0.68$ | $\approx 9\%$ | Lagging inductive quadrature current ($I_q = 0.46\text{ A}$) |
| `1` | **`RESISTIVE_IRON_HEATER`** | $1100\text{ W}$ | $0.99$ | $<1\%$ | High in-phase fundamental ($I_1 = 4.78\text{ A}$), zero harmonic distortion |
| `2` | **`INVERTER_AC_SWITCHING`** | $780\text{ W}$ | $0.92$ | $\approx 23\%$ | Strong $150\text{ Hz}$ ($I_3 = 0.68\text{ A}$) and $250\text{ Hz}$ ($I_5 = 0.42\text{ A}$) PWM harmonics |
| `3` | **`LAPTOP_SMPS_CHARGER`** | $65\text{ W}$ | $0.62$ | $>85\%$ | Narrow diode-bridge capacitor pulse ($I_3, I_5, I_7$ + Crest Factor $>2.3$) |

---

## 2. Directory Structure

```text
17-ml-energy-disaggregation-nilm/
├── README.md
├── src/
│   └── ml_energy_disaggregation_nilm/
│       ├── ml_energy_disaggregation_nilm.ino  # 2kHz ADC sampler, 4 load switches, OLED UI & JSON telemetry
│       └── nilm_model.h                       # Goertzel 50/150/250/350Hz DFT & 16-state NILM disaggregator
├── schematics/
│   ├── diagram.json                           # Wokwi circuit with 4 appliance toggle buttons + SCT-013 + OLED
│   └── wiring-guide.md                        # SCT-013 33-Ohm burden resistor & 1.65V mid-rail bias schematic
└── docs/
    └── architecture-and-training.md           # Goertzel IIR derivations & harmonic subspace math
```

---

## 3. Quick Start & Simulation Guide

### Running in Wokwi Simulator
1. Open `schematics/diagram.json` in Wokwi and start the simulation.
2. By default, the firmware cycles through 6 multi-appliance load combinations every 6 seconds so you can watch the Goertzel harmonics and OLED disaggregation table update automatically.
3. Press any of the 4 pushbuttons (`GPIO 12` = Fridge, `GPIO 14` = Iron, `GPIO 27` = Inverter AC, `GPIO 26` = Laptop SMPS) to toggle individual appliances ON/OFF on the virtual AC bus and verify that the disaggregator isolates the exact active subset!

### Sample Serial Telemetry Output
```json
{"win":5,"p_total_w":210.0,"i_rms_a":1.09,"pf":0.84,"thd_pct":31.4,"h1_a":1.04,"h3_a":0.28,"h5_a":0.18,"h7_a":0.12,"loads":{"fridge":true,"iron":false,"inverter_ac":false,"smps":true},"dsp_us":245,"nn_us":84}
[LOAD SWITCH] Toggled RESISTIVE_IRON_HEATER -> ON (Active Mask: 0x0B)
{"win":10,"p_total_w":1310.0,"i_rms_a":5.76,"pf":0.99,"thd_pct":5.8,"h1_a":5.74,"h3_a":0.30,"h5_a":0.18,"h7_a":0.12,"loads":{"fridge":true,"iron":true,"inverter_ac":false,"smps":true},"dsp_us":248,"nn_us":86}
```
