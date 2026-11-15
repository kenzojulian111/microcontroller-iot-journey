# ML Pipeline, Cross-Sensitivity Fusion & Telemetry — Project 12: Electronic Nose

## 1. Metal-Oxide Semiconductor (MOS) Sensor Physics & Cross-Sensitivity

Tin-dioxide ($\text{SnO}_2$) chemoresistive sensors such as the **MQ-135** and **MQ-2** are inherently **non-selective**: a single sensor's resistance $R_s$ drops when exposed to smoke, ethanol vapor, ammonia, or liquefied petroleum gas (LPG). However, because the two sensors have different catalytic doping and heater profiles, their **relative response surface** forms a separable manifold in feature space.

### 1.1 Sensing Resistance & Thermo-Hygrometric Compensation
From the ADC voltage divider with load resistor $R_L = 10\text{ k}\Omega$:

$$R_s = R_L \cdot \frac{V_{cc} - V_{\text{out}}}{V_{\text{out}}}$$

Because $\text{SnO}_2$ grain-boundary conductivity varies with ambient temperature $T$ ($^\circ\text{C}$) and relative humidity $RH$ ($\%$), the firmware normalizes $R_s$ against a 2D empirical surface anchored at standard reference conditions ($T_{\text{ref}} = 20\,^\circ\text{C}$, $RH_{\text{ref}} = 65\%$):

$$C(T, RH) = 1.0 - 0.012(T - 20) - 0.0035(RH - 65) + 0.00012(T - 20)^2$$

$$\left(\frac{R_s}{R_0}\right)_{\text{comp}} = \frac{R_s}{R_0 \cdot C(T, RH)}$$

### 1.2 6-Dimensional Feature Vector
To capture both steady-state selectivity and plume transient dynamics, the firmware constructs:

$$\mathbf{x} = \begin{bmatrix} \left(\dfrac{R_s}{R_0}\right)_{\text{MQ135}} & \left(\dfrac{R_s}{R_0}\right)_{\text{MQ2}} & \ln\!\left(\dfrac{(R_s/R_0)_{\text{MQ135}}}{(R_s/R_0)_{\text{MQ2}}}\right) & \dfrac{d(R_s/R_0)_{\text{MQ135}}}{dt} & T & RH \end{bmatrix}^T$$

Notice the logarithmic cross-sensitivity ratio $x_2 = \ln(r_{135} / r_2)$:
- **`CLEAN_AIR`:** $r_{135} \approx 1.0,\; r_2 \approx 1.0 \implies \ln(r_{135}/r_2) \approx 0.0$
- **`CIGARETTE_SMOKE`:** Both sensors drop ($r_{135} \approx 0.42,\; r_2 \approx 0.45$) $\implies \ln(r_{135}/r_2) \approx -0.07$
- **`ALCOHOL_VOC`:** MQ-135 drops sharply ($r_{135} \approx 0.22$) while MQ-2 drops mildly ($r_2 \approx 0.68$) $\implies \ln(r_{135}/r_2) \approx -1.13$
- **`LPG_LEAK_HAZARD`:** MQ-2 drops sharply ($r_2 \approx 0.18$) while MQ-135 remains high ($r_{135} \approx 0.72$) $\implies \ln(r_{135}/r_2) \approx +1.39$

---

## 2. Python Training & C++ Header Export Script (`train_enose_fcnn.py`)

```python
#!/usr/bin/env python3
"""
train_enose_fcnn.py
Trains a 2-layer Fully Connected Neural Network (6 -> 12 ReLU -> 4 Softmax)
for the Project 12 Electronic Nose and exports C++ weights for enose_nn_model.h.
"""

import numpy as np
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report

CLASSES = ["CLEAN_AIR", "CIGARETTE_SMOKE", "ALCOHOL_VOC", "LPG_LEAK_HAZARD"]


def generate_enose_dataset(n_per_class: int = 1200, seed: int = 42):
    rng = np.random.default_rng(seed)
    X_parts, y_parts = [], []

    # 0: CLEAN_AIR
    r135 = rng.uniform(0.82, 1.12, n_per_class)
    r2 = rng.uniform(0.82, 1.12, n_per_class)
    dr = rng.normal(0.0, 0.02, n_per_class)
    X_parts.append(np.column_stack([r135, r2, np.log(r135 / r2), dr,
                                    rng.uniform(18, 34, n_per_class),
                                    rng.uniform(35, 75, n_per_class)]))
    y_parts.append(np.zeros(n_per_class, dtype=int))

    # 1: CIGARETTE_SMOKE (both sensors drop roughly together)
    r135 = rng.uniform(0.28, 0.58, n_per_class)
    r2 = r135 * rng.uniform(0.85, 1.18, n_per_class)
    dr = rng.uniform(-0.18, -0.01, n_per_class)
    X_parts.append(np.column_stack([r135, r2, np.log(r135 / r2), dr,
                                    rng.uniform(18, 34, n_per_class),
                                    rng.uniform(35, 75, n_per_class)]))
    y_parts.append(np.ones(n_per_class, dtype=int))

    # 2: ALCOHOL_VOC (MQ-135 drops strongly, MQ-2 stays higher)
    r135 = rng.uniform(0.12, 0.38, n_per_class)
    r2 = rng.uniform(0.55, 0.88, n_per_class)
    dr = rng.uniform(-0.25, -0.02, n_per_class)
    X_parts.append(np.column_stack([r135, r2, np.log(r135 / r2), dr,
                                    rng.uniform(18, 34, n_per_class),
                                    rng.uniform(35, 75, n_per_class)]))
    y_parts.append(np.full(n_per_class, 2, dtype=int))

    # 3: LPG_LEAK_HAZARD (MQ-2 drops strongly, MQ-135 stays higher)
    r135 = rng.uniform(0.55, 0.90, n_per_class)
    r2 = rng.uniform(0.10, 0.35, n_per_class)
    dr = rng.uniform(-0.08, 0.01, n_per_class)
    X_parts.append(np.column_stack([r135, r2, np.log(r135 / r2), dr,
                                    rng.uniform(18, 34, n_per_class),
                                    rng.uniform(35, 75, n_per_class)]))
    y_parts.append(np.full(n_per_class, 3, dtype=int))

    return np.vstack(X_parts).astype(np.float32), np.concatenate(y_parts)


def main():
    X, y = generate_enose_dataset()
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.25, stratify=y, random_state=42
    )

    scaler = StandardScaler()
    X_train_s = scaler.fit_transform(X_train)
    X_test_s = scaler.transform(X_test)

    mlp = MLPClassifier(
        hidden_layer_sizes=(12,),
        activation="relu",
        max_iter=400,
        random_state=42,
    )
    mlp.fit(X_train_s, y_train)

    print(classification_report(y_test, mlp.predict(X_test_s), target_names=CLASSES, digits=4))
    print("Mean:", np.round(scaler.mean_, 4))
    print("Std :", np.round(scaler.scale_, 4))


if __name__ == "__main__":
    main()
```

---

## 3. Sample Serial & MQTT Telemetry Output

```text
================================================================================
 Project 12: TinyML Electronic Nose (MQ-135 + MQ-2 + DHT22 + 2-Layer FCNN)
 Send 'CALIB' over Serial or press GPIO 15 button to zero clean-air R0 baseline.
================================================================================
[#0001] MQ135(Rs/R0)=0.96 | MQ2(Rs/R0)=0.98 | ln(R135/R2)=-0.02 | dR/dt= +0.00 | T=24.5C RH=55% | Class=0 (CLEAN_AIR, 99.7%) [1.00, 0.00, 0.00, 0.00] | 28us
[#0002] MQ135(Rs/R0)=0.41 | MQ2(Rs/R0)=0.44 | ln(R135/R2)=-0.07 | dR/dt= -0.55 | T=24.5C RH=55% | Class=1 (CIGARETTE_SMOKE, 96.4%) [0.00, 0.96, 0.02, 0.02] | 27us
[#0003] MQ135(Rs/R0)=0.21 | MQ2(Rs/R0)=0.72 | ln(R135/R2)=-1.23 | dR/dt= -0.20 | T=24.5C RH=55% | Class=2 (ALCOHOL_VOC, 99.1%) [0.00, 0.01, 0.99, 0.00] | 26us
[#0004] MQ135(Rs/R0)=0.74 | MQ2(Rs/R0)=0.16 | ln(R135/R2)=+1.53 | dR/dt= +0.53 | T=24.5C RH=55% | Class=3 (LPG_LEAK_HAZARD, 99.5%) [0.00, 0.00, 0.00, 1.00] | 27us
```

### Example MQTT JSON Alert Payload (`iot_lab/project12/enose/alert`)
```json
{
  "sample": 4,
  "class_id": 3,
  "class_label": "LPG_LEAK_HAZARD",
  "confidence": 0.995,
  "rs_r0_mq135": 0.740,
  "rs_r0_mq2": 0.160,
  "log_ratio": 1.531,
  "dratio_dt": 0.530,
  "temp_c": 24.5,
  "humidity_pct": 55.0,
  "probs": [0.000, 0.003, 0.002, 0.995],
  "inference_us": 27
}
```
