# Project 24: Server-Side Machine Learning Inference Offloading

**Track III — Phase 7: Full-Stack Hybrid MCU + Python Flask Scikit-Learn Pipeline**

Demonstrates how memory-constrained microcontrollers (**Arduino Uno/Nano** with 2 KB SRAM or **ESP32**) perform real-time $O(1)$-memory time-domain feature extraction on **MPU6050 3-axis vibration + temperature** and **MQ-135 gas concentration**, offload the 6-feature vector to a **Python Flask + Scikit-Learn (`IsolationForest` + `RandomForestClassifier`)** server, and execute closed-loop emergency relay cutoffs in milliseconds.

---

## Key Capabilities

1. **Cross-Platform Edge DSP Firmware (`firmware/ml_sensor_streamer/ml_sensor_streamer.ino`):**
   - Compiles cleanly for both **Arduino Uno/Nano** (streams JSON lines over 115200 bps UART) and **ESP32** (streams via Wi-Fi HTTP POST + UART).
   - Computes 3-axis vibration RMS ($g$), peak-to-peak span ($g$), crest factor (bearing spall indicator), housing temperature ($^\circ\text{C}$), and MQ-135 VOC/smoke ratio across a 32-sample window.
   - Includes a hardware **Fault Regime Injection Button (`GPIO14` / `D2`)** that cycles through `NOMINAL`, `BEARING_WEAR`, `THERMAL_OVERLOAD`, and `CAVITATION_SURGE` profiles.
   - Executes closed-loop actuation directives (`NORMAL`, `WARNING_THROTTLE`, `CRITICAL_SHUTDOWN`) by tripping the emergency motor contactor relay and driving status LEDs + piezo alarm.
2. **Hybrid Unsupervised + Supervised ML Backend (`backend/ml_engine.py` & `backend/app.py`):**
   - Auto-trains and persists (`industrial_rf_pipeline.pkl`) a `StandardScaler` + `IsolationForest` + `RandomForestClassifier` ensemble on startup.
   - Supports online retraining via `POST /api/v1/model/retrain` and real-time telemetry inference via `POST /api/v1/ml/infer`.
3. **Interactive ML Operator Console (`backend/templates/index.html`):**
   - Visualizes class posterior probabilities, Gini feature importances, anomaly scores, and closed-loop relay trip history.

---

## Directory Structure

```text
24-flask-ml-anomaly-offloading/
├── README.md
├── firmware/
│   └── ml_sensor_streamer/
│       ├── ml_sensor_streamer.ino
│       └── secrets.example.h
├── backend/
│   ├── app.py
│   ├── ml_engine.py
│   ├── requirements.txt
│   ├── templates/
│   │   └── index.html
│   └── static/
│       └── style.css
├── schematics/
│   ├── diagram.json
│   └── wiring-guide.md
└── docs/
    └── architecture-and-api-spec.md
```

---

## Quick Start

### 1. Start the Flask Scikit-Learn Inference Server

```bash
cd 24-flask-ml-anomaly-offloading/backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 app.py
```

The server trains `industrial_rf_pipeline.pkl` on first launch and listens on `http://0.0.0.0:5024`.

### 2. Test Real-Time Inference via CLI or Web UI

Open `http://localhost:5024` in your browser, or send a feature vector via `curl`:

```bash
curl -X POST http://localhost:5024/api/v1/ml/infer \
  -H "Content-Type: application/json" \
  -d '{"node_id":"TURBINE-NODE-04","vib_rms_g":1.85,"vib_p2p_g":4.40,"crest_factor":4.10,"temp_c":74.2,"gas_ppm_ratio":2.60,"motor_rpm":1210}'
```

### 3. Run the Firmware in Wokwi or on Hardware

Open `schematics/diagram.json` in Wokwi. Press the **Blue Pushbutton (`GPIO14`)** to inject bearing wear, thermal overload, or cavitation faults and observe the emergency contactor relay (`GPIO26`) trip on `CRITICAL_SHUTDOWN`.
