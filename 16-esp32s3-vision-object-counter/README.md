# Project 16: ESP32-S3 Embedded Vision Object Counter (FOMO + Centroid Tracker)

![Track II — TinyML & Edge AI](https://img.shields.io/badge/Track_II-Embedded_Computer_Vision-00897B?style=for-the-badge)
![Detector](https://img.shields.io/badge/Detector-FOMO_96x96_to_12x12_Grid-1E88E5?style=for-the-badge)
![Target](https://img.shields.io/badge/Hardware-ESP32--S3_Sense_%7C_ESP32--CAM-F4511E?style=for-the-badge)

## 1. Overview

This project implements a real-time **Embedded Computer Vision People & Object Counter** on the **ESP32-S3 Sense / ESP32-CAM** using the **FOMO (*Faster Objects, More Objects*)** spatial centroid architecture (`fomo_vision_model.h`).

Instead of running heavyweight bounding-box detectors that exhaust microcontroller SRAM, FOMO maps a `96x96` grayscale camera frame through an $8 \times 8$ strided depthwise-separable convolutional backbone into a **`12x12` spatial activation heatmap**, followed by **$3 \times 3$ Grid Non-Maximum Suppression (NMS)**, **sub-cell barycentric centroid interpolation**, and a **bidirectional virtual tripwire tracker** (`X = 48.0 px`).

Furthermore, the firmware includes a **deterministic `96x96` Moving-Blob Optical Frame Synthesizer** so the complete vision pipeline compiles and runs interactively on a standard ESP32 in **Wokwi Simulator** without requiring physical camera hardware.

---

## 2. Directory Structure

```text
16-esp32s3-vision-object-counter/
├── README.md
├── src/
│   └── esp32s3_vision_object_counter/
│       ├── esp32s3_vision_object_counter.ino  # OV2640 / Synthetic capture, OLED UI, HTTP heatmap server
│       ├── fomo_vision_model.h                # FOMO 12x12 inference, 3x3 NMS & Euclidean line-crossing tracker
│       └── secrets.example.h                  # Wi-Fi credentials & OV2640 hardware enable flag
├── schematics/
│   ├── diagram.json                           # Wokwi circuit with ENTRY/EXIT/RESET buttons + SSD1306 OLED
│   └── wiring-guide.md                        # OV2640 DVP pinout for XIAO ESP32S3 Sense & ESP32-CAM
└── docs/
    └── architecture-and-training.md           # Barycentric centroid math, tracker kinematics & Keras model
```

---

## 3. Key Technical Highlights

1. **Sub-2 ms FOMO `12x12` Spatial Grid Inference (`fomo_vision_model.h`):**
   - Processes `96x96` grayscale frames ($9,216\text{ Bytes}$ in internal DRAM) into a `12x12` probability grid.
   - Applies $3 \times 3$ local-peak NMS and computes sub-pixel object coordinates $(\hat{x}, \hat{y}) \in [0, 96)^2$ via probability-weighted barycentric interpolation.
2. **Bidirectional Multi-Object Velocity Tracker:**
   - Associates detections across consecutive frames using Euclidean distance gating ($d_{\max} = 28\text{ px}$).
   - Tracks horizontal velocity $v_x$ across the virtual tripwire ($X = 48.0\text{ px}$) to increment `entries` ($L \rightarrow R$), `exits` ($R \rightarrow L$), and net `current_occupancy`.
3. **Split-Screen SSD1306 OLED & Live HTTP Dashboard:**
   - The left half of the OLED renders the live `12x12` FOMO activation grid and vertical tripwire; the right half displays `IN`, `OUT`, `OCC`, active tracks, and CNN execution latency in microseconds.
   - Hosts `GET /api/metrics` (JSON telemetry + 2D heatmap array) and `GET /` (live auto-refreshing HTML heatmap table) on Port 80.

---

## 4. Quick Start & Simulation Guide

### Running in Wokwi Simulator
1. Open `schematics/diagram.json` in Wokwi and start the simulation.
2. Observe autonomous pedestrian blobs traversing the `96x96` optical field on the OLED's `12x12` heatmap window and crossing the center tripwire.
3. Press **Green (`Spawn ENTRY`)** on `GPIO 12` to inject a left-to-right pedestrian, **Orange (`Spawn EXIT`)** on `GPIO 14` to inject a right-to-left pedestrian, or **Red (`Reset Occupancy`)** on `GPIO 27`.

### Sample Serial Telemetry Output
```json
{"frame":10,"det":1,"tracks":1,"entries":1,"exits":0,"occupancy":1,"fomo_us":1184,"track_us":32}
{"frame":20,"det":1,"tracks":1,"entries":1,"exits":1,"occupancy":0,"fomo_us":1179,"track_us":29}
```
