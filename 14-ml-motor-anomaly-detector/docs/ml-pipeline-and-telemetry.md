# ML Pipeline, Unsupervised Autoencoder & Telemetry — Project 14: Motor Anomaly Detector

## 1. Rotating Machinery Vibration Physics & 8-D Feature Vector

Supervised classifiers fail in industrial predictive maintenance because **destructive fault training data is rarely available for every motor frame size**, and each pump, fan, or compressor exhibits a unique baseline vibration signature depending on its RPM and mounting stiffness.

To solve this, our firmware extracts an **8-dimensional time-and-frequency vibration vector** from each 128-sample ($200\text{ Hz}$) window:

$$\mathbf{x} = \begin{bmatrix} \text{RMS}_x & \text{RMS}_y & \text{RMS}_z & A_{\text{p2p}} & \text{CF} & \kappa & R_{\text{HF}} & R_{\text{rad/vert}} \end{bmatrix}^T$$

where:
- **$\text{RMS}_x, \text{RMS}_y, \text{RMS}_z$:** Root-mean-square dynamic acceleration ($g$) along radial horizontal, radial transverse, and vertical axes.
- **Crest Factor ($\text{CF}$):** $\text{CF} = \dfrac{\max_i \|\mathbf{a}_i\|_2}{\text{RMS}_{\text{mag}}}$. A healthy sinusoidal vibration has $\text{CF} \approx \sqrt{2} \approx 1.414$; early microscopic bearing spalling introduces sharp repetitive spikes that raise $\text{CF} > 2.8$ long before overall RMS rises.
- **Normalized 4th Moment / Kurtosis ($\kappa$):** $\kappa = \dfrac{\frac{1}{N}\sum_{i} \|\mathbf{a}_i\|_2^4}{\left(\frac{1}{N}\sum_{i} \|\mathbf{a}_i\|_2^2\right)^2}$. Highly sensitive to impulsive bearing race defects.
- **High-Frequency Energy Ratio ($R_{\text{HF}}$):** Ratio of first-difference squared energy $\sum (\|\mathbf{a}_i\| - \|\mathbf{a}_{i-1}\|)^2$ to total energy, separating high-frequency bearing wear from low-frequency ($1\times$ RPM) shaft imbalance.
- **Radial-to-Vertical Asymmetry ($R_{\text{rad/vert}}$):** $\dfrac{\text{RMS}_x + \text{RMS}_y}{2\,\text{RMS}_z}$, separating radial centrifugal imbalance from vertical structural looseness.

---

## 2. Unsupervised Bottleneck Autoencoder (`8 -> 3 -> 8`) + K-Means + On-Device Calibration

### 2.1 On-Device Self-Learning (`CALIBRATE`)
When the operator presses the **CALIBRATE (`GPIO 15`)** button or sends `CALIBRATE` over Serial, the MCU collects $M = 10$ windows on the target motor and updates the baseline mean $\boldsymbol{\mu}_0$ and adaptive scale $\boldsymbol{\sigma}_0$ via Welford's online algorithm:

$$\boldsymbol{\mu}_0^{(m)} = \boldsymbol{\mu}_0^{(m-1)} + \frac{1}{m}\left(\mathbf{x}^{(m)} - \boldsymbol{\mu}_0^{(m-1)}\right), \qquad \boldsymbol{\delta} = \frac{\mathbf{x} - \boldsymbol{\mu}_0}{\boldsymbol{\sigma}_0}$$

### 2.2 Symmetric Autoencoder Reconstruction Error (MSE)
The standardized residual $\boldsymbol{\delta} \in \mathbb{R}^8$ is compressed into a 3-dimensional latent bottleneck $\mathbf{z} \in \mathbb{R}^3$ and reconstructed back to $\hat{\boldsymbol{\delta}} \in \mathbb{R}^8$:

$$\mathbf{z} = 1.25 \tanh\!\left(0.65\,\mathbf{W}_{\text{enc}}\,\boldsymbol{\delta}\right), \qquad \hat{\boldsymbol{\delta}} = \mathbf{W}_{\text{dec}}\,\mathbf{z}, \qquad \text{MSE} = \frac{1}{8}\sum_{i=0}^{7} \left(\delta_i - \hat{\delta}_i\right)^2$$

When $\text{MSE} \ge 4.20$ for two consecutive windows, the firmware automatically trips the **Protective Cutoff Relay (`GPIO 14 = LOW`)**.

---

## 3. Python Autoencoder + K-Means Training & Verification Script (`train_motor_ae.py`)

```python
#!/usr/bin/env python3
"""
train_motor_ae.py
Demonstrates training the 8 -> 3 -> 8 Bottleneck Autoencoder and 4-Cluster
K-Means diagnostic model for rotating machinery vibration features.
"""

import numpy as np
from sklearn.cluster import KMeans
from sklearn.decomposition import PCA


def generate_vibration_regimes(n: int = 800, seed: int = 42):
    rng = np.random.default_rng(seed)
    # 0: NOMINAL_HEALTHY
    X_nom = np.column_stack([
        rng.normal(0.12, 0.015, n), rng.normal(0.12, 0.015, n), rng.normal(0.14, 0.018, n),
        rng.normal(0.48, 0.05, n), rng.normal(1.45, 0.08, n), rng.normal(2.10, 0.15, n),
        rng.normal(0.18, 0.02, n), rng.normal(0.86, 0.06, n)
    ])
    # 1: SHAFT_IMBALANCE_1X
    X_imb = np.column_stack([
        rng.normal(0.40, 0.03, n), rng.normal(0.39, 0.03, n), rng.normal(0.20, 0.02, n),
        rng.normal(1.20, 0.10, n), rng.normal(1.52, 0.08, n), rng.normal(2.20, 0.15, n),
        rng.normal(0.14, 0.02, n), rng.normal(1.58, 0.10, n)
    ])
    # 2: BEARING_WEAR_SPALLING
    X_brg = np.column_stack([
        rng.normal(0.24, 0.03, n), rng.normal(0.24, 0.03, n), rng.normal(0.28, 0.03, n),
        rng.normal(1.55, 0.12, n), rng.normal(3.10, 0.20, n), rng.normal(5.20, 0.35, n),
        rng.normal(0.65, 0.05, n), rng.normal(0.88, 0.07, n)
    ])
    # 3: LOOSE_MOUNTING_SEVERE
    X_lse = np.column_stack([
        rng.normal(0.34, 0.04, n), rng.normal(0.33, 0.04, n), rng.normal(0.78, 0.06, n),
        rng.normal(2.45, 0.18, n), rng.normal(2.65, 0.18, n), rng.normal(4.40, 0.30, n),
        rng.normal(0.36, 0.04, n), rng.normal(0.43, 0.05, n)
    ])
    return X_nom, X_imb, X_brg, X_lse


def main():
    X_nom, X_imb, X_brg, X_lse = generate_vibration_regimes()
    mu0 = X_nom.mean(axis=0)
    scale0 = np.maximum(np.abs(mu0) * 0.30, 0.04)

    # Fit 3-component linear/tanh autoencoder basis on normal regime
    pca = PCA(n_components=3, random_state=42)
    pca.fit((X_nom - mu0) / scale0)

    X_all = np.vstack([X_nom, X_imb, X_brg, X_lse])
    D_all = (X_all - mu0) / scale0
    kmeans = KMeans(n_clusters=4, random_state=42, n_init=10).fit(D_all)

    for name, X_sub in [
        ("NOMINAL_HEALTHY", X_nom),
        ("SHAFT_IMBALANCE_1X", X_imb),
        ("BEARING_WEAR_SPALLING", X_brg),
        ("LOOSE_MOUNTING_SEVERE", X_lse),
    ]:
        D = (X_sub - mu0) / scale0
        Z = 1.25 * np.tanh(0.65 * (D @ pca.components_.T))
        D_hat = Z @ pca.components_
        mse = np.mean((D - D_hat) ** 2, axis=1)
        print(f"{name:22s} | Mean Autoencoder MSE = {mse.mean():6.3f} +/- {mse.std():5.3f}")


if __name__ == "__main__":
    main()
```

---

## 4. Sample Serial Telemetry & Protective Relay Cutoff Log

```text
====================================================================================
 Project 14: Unsupervised Motor Anomaly Detector (Autoencoder 8->3->8 + K-Means)
 Commands: 'CALIBRATE', 'RESET', 'NORMAL', 'IMBALANCE', 'BEARING', 'LOOSE', 'AUTO'
====================================================================================
[#0001] RMS=[0.12,0.12,0.14]g | Crest=1.42 | Kurt=2.06 | HF=0.18 | AE_MSE= 0.004 | KMeans_d=0.18 | State=0 (NOMINAL_HEALTHY      ) | Relay=CLOSED  | 22us
[CMD] CALIBRATE
[CALIBRATE] >>> Starting On-Device Self-Learning Calibration (10 windows)...
[CALIB  1/10] Learning Baseline -> RMS=[0.117, 0.117, 0.138]g | P2P=0.47g | Crest=1.42 | HF=0.18
...
[CALIB 10/10] Learning Baseline -> RMS=[0.117, 0.117, 0.138]g | P2P=0.47g | Crest=1.42 | HF=0.18
[CALIBRATE] >>> On-Device Calibration COMPLETE! Autoencoder & K-Means anchored.

[SIM] Injected regime: SHAFT_IMBALANCE_1X
[#0012] RMS=[0.40,0.39,0.17]g | Crest=1.43 | Kurt=2.09 | HF=0.14 | AE_MSE= 2.148 | KMeans_d=0.92 | State=1 (SHAFT_IMBALANCE_1X   ) | Relay=CLOSED  | 21us
[SIM] Injected regime: BEARING_WEAR_SPALLING
[#0013] RMS=[0.30,0.29,0.33]g | Crest=3.18 | Kurt=5.42 | HF=0.68 | AE_MSE= 3.682 | KMeans_d=0.84 | State=2 (BEARING_WEAR_SPALLING) | Relay=CLOSED  | 21us
[SIM] Injected regime: LOOSE_MOUNTING_SEVERE
[#0014] RMS=[0.32,0.31,0.82]g | Crest=2.64 | Kurt=4.35 | HF=0.39 | AE_MSE= 6.410 | KMeans_d=0.79 | State=3 (LOOSE_MOUNTING_SEVERE) | Relay=CLOSED  | 22us
[CRITICAL ALARM] >>> MSE Exceeded Safety Limit! Protective Relay TRIPPED (GPIO 14 = LOW)!
[#0015] RMS=[0.32,0.31,0.82]g | Crest=2.64 | Kurt=4.35 | HF=0.39 | AE_MSE= 6.410 | KMeans_d=0.79 | State=3 (LOOSE_MOUNTING_SEVERE) | Relay=TRIPPED | 21us
```
