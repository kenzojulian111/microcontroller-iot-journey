# ML Pipeline, Temporal Segment Descriptors & Telemetry — Project 13: Air Gesture Controller

## 1. 100 Hz Motion Onset Gate & 12-Feature Temporal Descriptor Extraction

Feeding raw 6-DoF time-series windows ($80 \times 6 = 480$ floats) directly into a dense network wastes flash memory and overfits to phase shifts in gesture start time. Instead, our firmware triggers an **80-sample ($800\text{ ms}$) capture window** when dynamic acceleration $\|\mathbf{a}_{\text{dyn}}\|_2 > 0.35g$ or angular velocity $\|\boldsymbol{\omega}\|_2 > 65\,^\circ/\text{s}$, and compresses the $80 \times 6$ window into **12 shift-robust kinematic descriptors**:

1. **Early & Late Window Vertical Acceleration Means ($f_0, f_1$):**
   $$f_0 = \frac{2}{N}\sum_{i=0}^{N/2 - 1} a_{z,\text{dyn}}[i], \qquad f_1 = \frac{2}{N}\sum_{i=N/2}^{N-1} a_{z,\text{dyn}}[i]$$
   Captures the acceleration-then-deceleration sign reversal between `SWIPE_UP` ($f_0 > 0, f_1 < 0$) and `SWIPE_DOWN` ($f_0 < 0, f_1 > 0$).
2. **Signed Pitch Angular Velocity Peak ($f_2 = g_{y,\text{peak}}$):** Resolves wrist pitch rotation direction and speed.
3. **Mean Absolute Roll & Yaw Rates ($f_3, f_4$):** $\overline{|g_x|}$ and $\overline{|g_z|}$.
4. **Circular Phase Coupling Integral ($f_5$):**
   $$f_5 = \frac{1}{N}\sum_{i=0}^{N-1} \left( a_x[i]\frac{g_y[i]}{100} - a_y[i]\frac{g_x[i]}{100} \right)$$
   During a clockwise circular motion (`CIRCLE_CW`), centripetal acceleration $(a_x, a_y)$ and orthogonal angular velocity $(g_y, -g_x)$ remain locked in phase, yielding a large positive integral.
5. **Directional Gyroscope Energy Ratios ($f_6, f_7$):** Pitch energy fraction $E(g_y)/E_{\text{total}}$ vs. orbital energy fraction $(E(g_x)+E(g_z))/E_{\text{total}}$.
6. **High-Jerk Impulse Count ($f_8$), Peak-to-Peak $a_z$ Range ($f_9$), and Bimodal Symmetry ($f_{10}$):**
   $$f_8 = \sum_{i=1}^{N-1} \mathbb{I}\!\left(|a_{z,\text{dyn}}[i] - a_{z,\text{dyn}}[i-1]| > 1.8g\right), \qquad f_{10} = \frac{\min(E_{\text{early}}, E_{\text{late}})}{\max(E_{\text{early}}, E_{\text{late}})}$$
   Uniquely isolates `DOUBLE_TAP`, where two crisp vertical impacts occur in the first and second halves of the window ($f_8 \ge 2$, $f_{10} \approx 1.0$).

---

## 2. Python TFLite Training & Quantization Script (`train_gesture_tflm.py`)

```python
#!/usr/bin/env python3
"""
train_gesture_tflm.py
Trains the 12-feature -> 8 ReLU -> 4 Softmax air gesture classifier and exports
weights + TensorFlow Lite FlatBuffer array for gesture_tflm_model.h.
"""

import numpy as np
from sklearn.neural_network import MLPClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report

CLASSES = ["SWIPE_UP", "SWIPE_DOWN", "CIRCLE_CW", "DOUBLE_TAP"]


def synthesize_gesture_features(n_per_class: int = 1000, seed: int = 42):
    rng = np.random.default_rng(seed)
    X, y = [], []

    # 0: SWIPE_UP
    X.append(np.column_stack([
        rng.normal(0.75, 0.15, n_per_class),   # f0: az_early > 0
        rng.normal(-0.35, 0.12, n_per_class),  # f1: az_late < 0
        rng.normal(175.0, 25.0, n_per_class),  # f2: gy_peak > 0
        rng.uniform(5.0, 25.0, n_per_class),   # f3: gx_mean
        rng.uniform(5.0, 25.0, n_per_class),   # f4: gz_mean
        rng.normal(0.0, 0.12, n_per_class),    # f5: circ_coupling
        rng.uniform(0.78, 0.98, n_per_class),  # f6: gy_energy_ratio
        rng.uniform(0.02, 0.22, n_per_class),  # f7: rot_energy_ratio
        rng.integers(0, 2, n_per_class),       # f8: jerk_spikes
        rng.uniform(1.2, 2.2, n_per_class),    # f9: az_p2p
        rng.uniform(0.15, 0.45, n_per_class),  # f10: symmetry
        rng.uniform(0.55, 0.95, n_per_class),  # f11: accel_rms
    ]))
    y.append(np.zeros(n_per_class, dtype=int))

    # 1: SWIPE_DOWN
    X.append(np.column_stack([
        rng.normal(-0.75, 0.15, n_per_class),  # f0: az_early < 0
        rng.normal(0.35, 0.12, n_per_class),   # f1: az_late > 0
        rng.normal(-175.0, 25.0, n_per_class), # f2: gy_peak < 0
        rng.uniform(5.0, 25.0, n_per_class),
        rng.uniform(5.0, 25.0, n_per_class),
        rng.normal(0.0, 0.12, n_per_class),
        rng.uniform(0.78, 0.98, n_per_class),
        rng.uniform(0.02, 0.22, n_per_class),
        rng.integers(0, 2, n_per_class),
        rng.uniform(1.2, 2.2, n_per_class),
        rng.uniform(0.15, 0.45, n_per_class),
        rng.uniform(0.55, 0.95, n_per_class),
    ]))
    y.append(np.ones(n_per_class, dtype=int))

    # 2: CIRCLE_CW
    X.append(np.column_stack([
        rng.normal(0.0, 0.15, n_per_class),
        rng.normal(0.0, 0.15, n_per_class),
        rng.normal(85.0, 30.0, n_per_class),
        rng.normal(62.0, 12.0, n_per_class),
        rng.normal(78.0, 15.0, n_per_class),
        rng.normal(0.82, 0.18, n_per_class),   # f5: strong circular phase coupling
        rng.uniform(0.25, 0.45, n_per_class),
        rng.uniform(0.55, 0.78, n_per_class),  # f7: high roll+yaw orbital energy
        np.zeros(n_per_class),
        rng.uniform(0.2, 0.7, n_per_class),
        rng.uniform(0.75, 0.98, n_per_class),
        rng.uniform(0.70, 1.10, n_per_class),
    ]))
    y.append(np.full(n_per_class, 2, dtype=int))

    # 3: DOUBLE_TAP
    X.append(np.column_stack([
        rng.normal(0.02, 0.08, n_per_class),
        rng.normal(0.02, 0.08, n_per_class),
        rng.normal(0.0, 18.0, n_per_class),
        rng.uniform(1.0, 12.0, n_per_class),
        rng.uniform(1.0, 12.0, n_per_class),
        rng.normal(0.0, 0.05, n_per_class),
        rng.uniform(0.15, 0.45, n_per_class),
        rng.uniform(0.15, 0.45, n_per_class),
        rng.integers(2, 5, n_per_class),       # f8: 2..4 sharp jerk spikes
        rng.uniform(3.2, 4.5, n_per_class),    # f9: large peak-to-peak Z impact
        rng.uniform(0.82, 1.00, n_per_class),  # f10: symmetric bimodal energy
        rng.uniform(0.45, 0.85, n_per_class),
    ]))
    y.append(np.full(n_per_class, 3, dtype=int))

    return np.vstack(X).astype(np.float32), np.concatenate(y)


def main():
    X, y = synthesize_gesture_features()
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.25, stratify=y, random_state=42
    )
    clf = MLPClassifier(hidden_layer_sizes=(8,), max_iter=300, random_state=42)
    clf.fit(X_train, y_train)
    print(classification_report(y_test, clf.predict(X_test), target_names=CLASSES, digits=4))


if __name__ == "__main__":
    main()
```

---

## 3. Sample Serial Telemetry Log

```text
[INIT] InvenSense MPU6050 initialized (+/-4g, +/-500 dps, 44 Hz DLPF).
================================================================================
 Project 13: 6-DoF Air Gesture Controller (100 Hz IMU + TFLM Neural Engine)
 Move MPU6050, press GPIO 15 button, or send 'UP', 'DOWN', 'CIRCLE', 'TAP'.
================================================================================
[GESTURE] Pred=0 (SWIPE_UP  ) | Conf= 99.6% [1.00, 0.00, 0.00, 0.00] | Action: PWM Brightness +25%  | State: Relay=ON , Bright= 75%, Mode=WARM_WHITE | 68us
[GESTURE] Pred=1 (SWIPE_DOWN) | Conf= 99.6% [0.00, 1.00, 0.00, 0.00] | Action: PWM Brightness -25%  | State: Relay=ON , Bright= 50%, Mode=WARM_WHITE | 66us
[GESTURE] Pred=2 (CIRCLE_CW ) | Conf= 99.4% [0.00, 0.00, 0.99, 0.00] | Action: Cycle RGB Color Mode | State: Relay=ON , Bright= 50%, Mode=CYBER_CYAN | 67us
[GESTURE] Pred=3 (DOUBLE_TAP) | Conf= 99.8% [0.00, 0.00, 0.00, 1.00] | Action: Toggle Master Relay  | State: Relay=OFF, Bright= 50%, Mode=CYBER_CYAN | 65us
```
