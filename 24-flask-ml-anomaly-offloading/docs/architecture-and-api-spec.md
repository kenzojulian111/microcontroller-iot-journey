# Project 24 — Edge-to-Server ML Offloading Architecture & API Specification

## 1. Architectural Rationale: Why Offload ML from 8-Bit / Low-Power MCUs?

An **ATmega328P (Arduino Uno/Nano)** has **2,048 bytes of SRAM** and no hardware floating-point unit (FPU). Hosting a 140-tree `RandomForestClassifier` plus a 120-tree `IsolationForest` requires $>1.5\text{ MB}$ of node structures and IEEE-754 matrix operations.

Instead of sacrificing model capacity, the system splits the workload:
1. **Edge Microcontroller ($O(1)$ Memory Windowed DSP):**
   Samples the MPU6050 accelerometer over a $N=32$ window at $f_s = 200\text{ Hz}$ and computes sufficient statistics on-the-fly without storing an array:
   $$\text{RMS}_g = \sqrt{\frac{1}{N}\sum_{i=1}^{N} \left(a_{x,i}^2 + a_{y,i}^2 + (a_{z,i}-1)^2\right)}, \qquad \text{CF} = \frac{\max_i \| \mathbf{a}_i \|_2}{\text{RMS}_g}$$
2. **Flask Scikit-Learn Server (Ensemble Inference & Online Retraining):**
   Normalizes the 6-dimensional vector via `StandardScaler`, evaluates out-of-distribution novelty with `IsolationForest`, computes multi-class posterior probabilities with `RandomForestClassifier`, and returns a closed-loop actuator command (`NORMAL`, `WARNING_THROTTLE`, or `CRITICAL_SHUTDOWN`).

---

## 2. Feature Vector Schema (`POST /api/v1/ml/infer`)

### Request Payload
```json
{
  "node_id": "TURBINE-NODE-04",
  "vib_rms_g": 0.84,
  "vib_p2p_g": 2.28,
  "crest_factor": 3.62,
  "temp_c": 56.4,
  "gas_ppm_ratio": 1.28,
  "motor_rpm": 1680
}
```

### Response Payload (`200 OK`)
```json
{
  "status": "ok",
  "node_id": "TURBINE-NODE-04",
  "predicted_class": "BEARING_WEAR",
  "command": "WARNING_THROTTLE",
  "confidence": 0.9643,
  "anomaly_score": 0.6412,
  "is_anomaly": true,
  "probabilities": {
    "NOMINAL": 0.0143,
    "BEARING_WEAR": 0.9643,
    "THERMAL_GAS_OVERLOAD": 0.0143,
    "CAVITATION_SURGE": 0.0071
  },
  "inference_latency_ms": 1.842,
  "timestamp": "14:22:09"
}
```

---

## 3. Closed-Loop Actuation Mapping

| Predicted Class / Anomaly Condition | Returned `command` | Edge Relay (`PIN_RELAY_CUTOFF`) | Visual / Acoustic State |
| :--- | :--- | :--- | :--- |
| `NOMINAL` (Inlier) | `NORMAL` | `HIGH` (Motor Contactor Closed) | Green LED Solid, Buzzer Silent |
| `BEARING_WEAR` or `IsolationForest` Outlier | `WARNING_THROTTLE` | `HIGH` (Throttled RPM Mode) | Yellow LED Solid, 1480 Hz Short Chirp |
| `THERMAL_GAS_OVERLOAD` or `CAVITATION_SURGE` | `CRITICAL_SHUTDOWN` | `LOW` (**Emergency Trip / Cut Power**) | Red LED Solid, 2400 Hz Continuous Alarm |

---

## 4. Online Model Retraining (`POST /api/v1/model/retrain`)

Allows operators to regenerate or update the baseline + fault distribution and persist the updated `.pkl` pipeline without restarting the Flask server:

```bash
curl -X POST http://localhost:5024/api/v1/model/retrain \
  -H "Content-Type: application/json" \
  -d '{"samples_per_class": 500, "seed": 2026}'
```
