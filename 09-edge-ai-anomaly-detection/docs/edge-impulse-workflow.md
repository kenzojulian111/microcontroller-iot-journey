# Edge Impulse Studio Training Workflow & UART Inference Telemetry — 09 Edge AI Anomaly Detection

This project ships with a pre-trained, zero-dependency DSP feature extractor and Dual-Head TinyML model inside [`anomaly_model.h`](../src/edge_ai_anomaly_detection/anomaly_model.h) that compiles and runs out-of-the-box. If you wish to train a custom **TinyML** model tailored to the specific acoustic/vibration signature of your own machinery using **Edge Impulse Studio**, follow the end-to-end engineering workflow below.

---

## 1. End-to-End Custom TinyML Training Workflow in Edge Impulse

### Step 1 — Enable the 100 Hz CSV Data Forwarder Mode on the ESP32
1. Flash [`edge_ai_anomaly_detection.ino`](../src/edge_ai_anomaly_detection/edge_ai_anomaly_detection.ino) onto your ESP32.
2. Open the **Serial Monitor (`115200` baud)**, type **`m`**, and press **Enter** to switch from Inference Mode to **CSV Data Forwarder Mode**.
3. The ESP32 immediately begins streaming tri-axial acceleration rows (`ax,ay,az` in $\text{m/s}^2$) at a deterministic **100 Hz** sample rate:
   ```text
   ax,ay,az
   0.120,-0.085,9.812
   0.145,-0.092,9.798
   ...
   ```
4. Close the Arduino IDE Serial Monitor (to release the serial port lock) and launch the Edge Impulse CLI forwarder on your workstation:
   ```bash
   edge-impulse-data-forwarder --frequency 100
   ```
5. Assign the sensor axis labels `accX, accY, accZ` when prompted by the CLI.

### Step 2 — Acquire Labeled Vibration Datasets Across 3 Operating Regimes
In **Edge Impulse Studio > Data acquisition**, record **10-second windows** (minimum 2–3 minutes of total data across training and test splits) for each operating class:
1. **`IDLE`**: Machine de-energized / stationary ambient baseline.
2. **`NORMAL_OPERATION`**: Motor, fan, or pump operating under nominal speed and balanced load.
3. **`ANOMALY_DETECTED`**: Induce a controlled mechanical fault (e.g., attach an eccentric mass such as a coin or binder clip to one fan blade to create rotor unbalance, loosen a mounting bolt, or introduce rhythmic mechanical tapping on the bearing housing).

### Step 3 — Impulse Architecture Design (DSP + Neural Classifier + K-Means Anomaly)
Navigate to **Impulse design > Create impulse** and configure the blocks:
1. **Time-Series Input Block**:
   - *Window size*: `640 ms` (64 samples)
   - *Window increase (stride)*: `320 ms` (50% overlap)
   - *Frequency*: `100 Hz`
2. **Processing Block (DSP)**: Select **Spectral Analysis** (automatically extracts per-axis RMS, Skewness, Kurtosis, and FFT spectral power bins after high-pass DC removal).
3. **Learning Block 1 (Supervised)**: Select **Classification (Keras)** with a compact fully connected topology (`16 -> 8 -> 3 Softmax`) to classify `IDLE`, `NORMAL_OPERATION`, and `ANOMALY_DETECTED`.
4. **Learning Block 2 (Unsupervised)**: Select **Anomaly Detection (K-means / Gaussian Mixture)** on the RMS and spectral power axes to catch novel fault modes outside the training distribution.

### Step 4 — C++ Library Export & Deployment
1. Open the **Deployment** tab, select **Arduino library**, choose either **Quantized (`int8`)** or **Unoptimized (`float32`)**, and click **Build**.
2. Import the downloaded `.zip` archive into Arduino IDE via **`Sketch > Include Library > Add .ZIP Library...`**.

---

## 2. UART Serial Monitor Output (`115200` baud)

The following UART console log demonstrates real-time Edge AI inference across all three machine states (`IDLE`, `NORMAL_OPERATION`, and `ANOMALY_DETECTED`) triggered via the `GPIO 14` push button or UART commands:

```text
======================================================================
  IoT Lab - 09 Edge AI Anomaly Detection (TinyML Vibration Monitor)   
  Mode Aktif : EDGE AI INFERENCE (Real-Time)
  Perintah Serial Monitor:
    'm' -> Ganti Mode (Edge AI Inference <-> CSV Data Forwarder)
    '0' -> Mode Sensor MPU6050 Murni (IDLE / Sesuai Slider Wokwi)
    '1' -> Simulasikan Getaran Motor NORMAL_OPERATION
    '2' -> Simulasikan Getaran Rusak ANOMALY_DETECTED (Bearing/Unbalance)
======================================================================

[MPU6050] Sensor Akselerometer I2C (0x68) berhasil diinisialisasi!
[EDGE-AI] Status: IDLE              | AnomalyScore: 0.14 | Conf(I/N/A): 94%/6%/0%  | RMS: 0.02 m/s2 | P2P: 0.05 | Crest: 1.42 | DSP+NN: 165+38 us
[EDGE-AI] Status: IDLE              | AnomalyScore: 0.14 | Conf(I/N/A): 94%/6%/0%  | RMS: 0.02 m/s2 | P2P: 0.06 | Crest: 1.45 | DSP+NN: 162+36 us

[BUTTON] Tombol ditekan! Beralih ke Profil Getaran Uji #1 (MOTOR_NORMAL)
[EDGE-AI] Status: NORMAL_OPERATION  | AnomalyScore: 0.21 | Conf(I/N/A): 4%/93%/3%  | RMS: 1.13 m/s2 | P2P: 2.18 | Crest: 1.58 | DSP+NN: 168+39 us
[EDGE-AI] Status: NORMAL_OPERATION  | AnomalyScore: 0.19 | Conf(I/N/A): 3%/94%/3%  | RMS: 1.14 m/s2 | P2P: 2.20 | Crest: 1.56 | DSP+NN: 166+37 us

[BUTTON] Tombol ditekan! Beralih ke Profil Getaran Uji #2 (MOTOR_ANOMALY)
[EDGE-AI] Status: ANOMALY_DETECTED  | AnomalyScore: 2.48 | Conf(I/N/A): 0%/1%/99%  | RMS: 4.52 m/s2 | P2P: 11.84 | Crest: 3.12 | DSP+NN: 171+41 us
[EDGE-AI] Status: ANOMALY_DETECTED  | AnomalyScore: 2.61 | Conf(I/N/A): 0%/1%/99%  | RMS: 4.68 m/s2 | P2P: 12.30 | Crest: 3.25 | DSP+NN: 169+38 us
```

Observe the deterministic execution profile on the ESP32 (`DSP+NN` column): computing DC removal and 17 time-domain features across 192 floating-point values takes only **~168 microseconds (`0.168 ms`)**, while evaluating the Dual-Head Softmax + Mahalanobis model takes **~38 microseconds (`0.038 ms`)**—leaving over **99.9% of CPU capacity** available for wireless telemetry and control tasks.
