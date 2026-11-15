# Project 18: Edge MLOps — Dynamic OTA Model Hot-Swapping Pipeline

![Track II — TinyML & Edge AI](https://img.shields.io/badge/Track_II-Edge_MLOps_%26_Security-00897B?style=for-the-badge)
![Storage](https://img.shields.io/badge/Storage-LittleFS_Atomic_Dual--Slot-1E88E5?style=for-the-badge)
![Security](https://img.shields.io/badge/Integrity-MbedTLS_SHA--256_Verified-F4511E?style=for-the-badge)

## 1. Overview

Production TinyML deployments inevitably encounter **concept drift** and **seasonal sensor shift** (e.g., an industrial motor operating in a $55^\circ\text{C}$ summer enclosure triggers false-positive fault alarms on a model trained at $25^\circ\text{C}$). Reflashing an entire microcontroller firmware image across thousands of field nodes just to update a few kilobytes of neural network weights wastes bandwidth and forces disruptive system reboots.

This project implements a **Zero-Reboot Edge MLOps Model Hot-Swapping Pipeline** on the **ESP32**:
1. **Decoupled LittleFS Model Storage:** Neural network weights live in `/models/active_model.bin` on the `LittleFS` flash filesystem rather than hard-coded C arrays.
2. **Staging & MbedTLS SHA-256 Cryptographic Verification:** Incoming OTA model binaries are written to `/models/staging_model.bin` and verified against the manifest's 256-bit `SHA-256` digest (`mbedtls/md.h`) and `EML1` magic header before activation.
3. **Atomic LittleFS Swap & In-RAM Arena Hot-Reload:** Upon verification, `/models/active_model.bin` is rotated to `/models/backup_model.bin`, the staging file is promoted atomically, and the in-RAM inference weights are hot-reloaded in **$<20\text{ ms}$ with zero reboot**.

---

## 2. Directory Structure

```text
18-ota-tflite-model-updater/
├── README.md
├── src/
│   └── ota_tflite_model_updater/
│       ├── ota_tflite_model_updater.ino    # LittleFS manager, OTA triggers, OLED & drift telemetry
│       ├── dynamic_ml_runtime.h            # EML1 binary loader, MbedTLS SHA-256 & INT8 NN arena
│       └── secrets.example.h               # Wi-Fi credentials & Model Registry manifest URL
├── schematics/
│   ├── diagram.json                        # Wokwi circuit with 3 MLOps buttons, Drift Pot & OLED
│   └── wiring-guide.md                     # Pinout & recommended LittleFS flash partition table
└── docs/
    └── architecture-and-training.md        # Sequence diagram, binary spec & Python Registry server
```

---

## 3. Interactive Concept-Drift & Security Demonstration (Wokwi)

1. **Boot State (`Model v1.0.0`):** Start the Wokwi simulation. With the **Thermal Drift Simulator potentiometer (`GPIO 34`)** set above $50^\circ\text{C}$ while vibration remains normal ($0.45\text{ g}$), the uncalibrated factory model `v1.0.0` falsely predicts **`FAULT_CRIT` (99%)**.
2. **Trigger Authentic OTA Hot-Swap (`GPIO 12` — Green Button):** Press **`OTA Hot-Swap v2.1.0`**. The ESP32 stages the drift-recalibrated `v2.1.0` binary to `/models/staging_model.bin`, verifies its SHA-256 hash (`mbedtls/md.h`), swaps it into `/models/active_model.bin`, and reloads the RAM arena without rebooting. Immediately, the prediction corrects to **`DRIFT_WARN` / `NORMAL`**.
3. **Simulate Tampered / Corrupted OTA Payload (`GPIO 14` — Red Button):** Press **`Inject Corrupt SHA256`**. The firmware flips 1 bit in the staged weight matrix (`w1[0][0] ^= 0x7F`). MbedTLS SHA-256 detects the digest mismatch, deletes `/models/staging_model.bin`, logs a security alert, and preserves the active model untouched.
4. **Instant Rollback (`GPIO 27` — Yellow Button):** Press **`Rollback to v1.0.0`** to atomically restore the baseline model.

### Sample Serial Output
```text
{"step":10,"model":"v1.0.0","sha256_prefix":"24e8d493c987","swaps":0,"temp_c":52.4,"pred":"FAULT_CRIT","conf":0.988,"nn_us":41,"status":"READY (BOOT v1.0.0)"}
--------------------------------------------------------------------
[MLOps OTA] Staged 128 bytes to /models/staging_model.bin (Target v2.1.0)
[MLOps OTA] Manifest Expected SHA-256: 8f31c6a09b2d4e157802a14c6e90b3d17f42a8e5c019d37b6a28f4e190c3d5b2
[MLOps SUCCESS] Atomic Hot-Swap to v2.1.0 completed in 14 ms (Zero Reboot!)
--------------------------------------------------------------------
{"step":20,"model":"v2.1.0","sha256_prefix":"8f31c6a09b2d","swaps":1,"temp_c":52.4,"pred":"DRIFT_WARN","conf":0.964,"nn_us":39,"status":"HOT-SWAP OK (14ms)"}
```
