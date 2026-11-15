"""
===============================================================================
Project 24: Server-Side Machine Learning Inference Offloading
File: backend/ml_engine.py
===============================================================================
Description:
    Hybrid Unsupervised + Supervised Scikit-Learn Machine Learning Pipeline
    designed for real-time edge microcontroller telemetry offloading.

Pipeline Architecture:
    1. `StandardScaler`: Zero-mean, unit-variance normalization across all 6
       sensor features (`vib_rms_g`, `vib_p2p_g`, `crest_factor`, `temp_c`,
       `gas_ppm_ratio`, `motor_rpm`).
    2. `IsolationForest`: Unsupervised anomaly detector trained on nominal
       rotating-machinery baselines to flag novel out-of-distribution states.
    3. `RandomForestClassifier`: Multi-class fault classifier distinguishing:
         - Class 0: `NOMINAL`
         - Class 1: `BEARING_WEAR`
         - Class 2: `THERMAL_GAS_OVERLOAD`
         - Class 3: `CAVITATION_SURGE`
===============================================================================
"""

from __future__ import annotations

import os
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Tuple

import joblib
import numpy as np
from sklearn.ensemble import IsolationForest, RandomForestClassifier
from sklearn.metrics import accuracy_score, f1_score
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler

FEATURE_NAMES: List[str] = [
    "vib_rms_g",
    "vib_p2p_g",
    "crest_factor",
    "temp_c",
    "gas_ppm_ratio",
    "motor_rpm",
]

CLASS_LABELS: Dict[int, str] = {
    0: "NOMINAL",
    1: "BEARING_WEAR",
    2: "THERMAL_GAS_OVERLOAD",
    3: "CAVITATION_SURGE",
}

COMMAND_MAP: Dict[str, str] = {
    "NOMINAL": "NORMAL",
    "BEARING_WEAR": "WARNING_THROTTLE",
    "THERMAL_GAS_OVERLOAD": "CRITICAL_SHUTDOWN",
    "CAVITATION_SURGE": "CRITICAL_SHUTDOWN",
}


@dataclass
class InferenceResult:
    predicted_class: str
    command: str
    confidence: float
    anomaly_score: float
    is_anomaly: bool
    class_probabilities: Dict[str, float]
    inference_latency_ms: float


class IndustrialAnomalyEngine:
    """
    Manages synthetic baseline dataset generation, model persistence (`.pkl`),
    online retraining, and sub-millisecond vector inference.
    """

    def __init__(self, artifact_path: str) -> None:
        self.artifact_path = artifact_path
        self.scaler: StandardScaler = StandardScaler()
        self.iso_forest: IsolationForest = IsolationForest(
            n_estimators=120, contamination=0.12, random_state=42
        )
        self.rf_classifier: RandomForestClassifier = RandomForestClassifier(
            n_estimators=140, max_depth=10, random_state=42
        )
        self.training_metrics: Dict[str, Any] = {}
        self._load_or_train_initial()

    def _generate_synthetic_corpus(
        self, samples_per_class: int = 350, seed: int = 42
    ) -> Tuple[np.ndarray, np.ndarray]:
        rng = np.random.default_rng(seed)

        # Class 0: NOMINAL (Low vibration RMS, moderate temperature, clean air)
        c0 = np.column_stack(
            [
                rng.normal(0.14, 0.04, samples_per_class).clip(0.03, 0.35),  # vib_rms_g
                rng.normal(0.38, 0.09, samples_per_class).clip(0.10, 0.80),  # vib_p2p_g
                rng.normal(1.85, 0.25, samples_per_class).clip(1.20, 2.55),  # crest_factor
                rng.normal(42.0, 2.8, samples_per_class).clip(32.0, 51.0),   # temp_c
                rng.normal(1.05, 0.18, samples_per_class).clip(0.60, 1.65),  # gas_ppm_ratio
                rng.normal(1785, 25, samples_per_class).clip(1700, 1850),    # motor_rpm
            ]
        )

        # Class 1: BEARING_WEAR (Elevated crest factor & RMS due to raceway spalling)
        c1 = np.column_stack(
            [
                rng.normal(0.78, 0.16, samples_per_class).clip(0.42, 1.30),
                rng.normal(2.15, 0.35, samples_per_class).clip(1.20, 3.20),
                rng.normal(3.45, 0.40, samples_per_class).clip(2.50, 4.80),
                rng.normal(55.0, 3.5, samples_per_class).clip(46.0, 66.0),
                rng.normal(1.25, 0.22, samples_per_class).clip(0.80, 1.95),
                rng.normal(1690, 45, samples_per_class).clip(1500, 1780),
            ]
        )

        # Class 2: THERMAL_GAS_OVERLOAD (High housing temp + elevated MQ-135 VOC/smoke ratio)
        c2 = np.column_stack(
            [
                rng.normal(0.62, 0.15, samples_per_class).clip(0.30, 1.15),
                rng.normal(1.65, 0.30, samples_per_class).clip(0.90, 2.60),
                rng.normal(2.35, 0.30, samples_per_class).clip(1.60, 3.20),
                rng.normal(79.5, 5.0, samples_per_class).clip(68.0, 98.0),
                rng.normal(3.35, 0.45, samples_per_class).clip(2.30, 4.90),
                rng.normal(1480, 70, samples_per_class).clip(1100, 1680),
            ]
        )

        # Class 3: CAVITATION_SURGE (Severe broadband vibration + high peak-to-peak)
        c3 = np.column_stack(
            [
                rng.normal(1.72, 0.28, samples_per_class).clip(1.15, 2.80),
                rng.normal(4.20, 0.55, samples_per_class).clip(2.80, 6.00),
                rng.normal(3.90, 0.45, samples_per_class).clip(2.80, 5.50),
                rng.normal(71.0, 4.5, samples_per_class).clip(60.0, 88.0),
                rng.normal(2.45, 0.38, samples_per_class).clip(1.60, 3.60),
                rng.normal(1250, 95, samples_per_class).clip(800, 1550),
            ]
        )

        X = np.vstack([c0, c1, c2, c3])
        y = np.concatenate(
            [
                np.zeros(samples_per_class, dtype=int),
                np.ones(samples_per_class, dtype=int),
                np.full(samples_per_class, 2, dtype=int),
                np.full(samples_per_class, 3, dtype=int),
            ]
        )
        return X, y

    def _load_or_train_initial(self) -> None:
        if os.path.exists(self.artifact_path):
            try:
                bundle = joblib.load(self.artifact_path)
                self.scaler = bundle["scaler"]
                self.iso_forest = bundle["iso_forest"]
                self.rf_classifier = bundle["rf_classifier"]
                self.training_metrics = bundle["metrics"]
                return
            except Exception:
                pass
        self.train_pipeline(samples_per_class=350, seed=42)

    def train_pipeline(
        self, samples_per_class: int = 350, seed: int = 42
    ) -> Dict[str, Any]:
        t0 = time.perf_counter()
        X, y = self._generate_synthetic_corpus(
            samples_per_class=samples_per_class, seed=seed
        )

        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.22, stratify=y, random_state=seed
        )

        X_train_scaled = self.scaler.fit_transform(X_train)
        X_test_scaled = self.scaler.transform(X_test)

        # Fit IsolationForest primarily on nominal + light wear distribution
        nominal_mask = y_train == 0
        self.iso_forest.fit(X_train_scaled[nominal_mask])

        # Fit supervised RandomForest fault classifier across all 4 regimes
        self.rf_classifier.fit(X_train_scaled, y_train)

        y_pred = self.rf_classifier.predict(X_test_scaled)
        acc = float(accuracy_score(y_test, y_pred))
        macro_f1 = float(f1_score(y_test, y_pred, average="macro"))
        elapsed_ms = (time.perf_counter() - t0) * 1000.0

        importances = {
            name: round(float(imp), 4)
            for name, imp in zip(FEATURE_NAMES, self.rf_classifier.feature_importances_)
        }

        self.training_metrics = {
            "total_samples": int(len(X)),
            "accuracy": round(acc, 4),
            "macro_f1": round(macro_f1, 4),
            "train_time_ms": round(elapsed_ms, 2),
            "feature_importances": importances,
            "trained_at_epoch": int(time.time()),
        }

        joblib.dump(
            {
                "scaler": self.scaler,
                "iso_forest": self.iso_forest,
                "rf_classifier": self.rf_classifier,
                "metrics": self.training_metrics,
            },
            self.artifact_path,
        )
        return self.training_metrics

    def evaluate_feature_vector(self, features: Dict[str, float]) -> InferenceResult:
        t0 = time.perf_counter()
        row = np.array(
            [[float(features.get(col, 0.0)) for col in FEATURE_NAMES]],
            dtype=np.float64,
        )
        row_scaled = self.scaler.transform(row)

        # Unsupervised IsolationForest anomaly score (negative = more anomalous)
        raw_decision = float(self.iso_forest.decision_function(row_scaled)[0])
        iso_pred = int(self.iso_forest.predict(row_scaled)[0])  # -1 = anomaly, 1 = normal
        anomaly_score = round(float(np.clip(0.5 - raw_decision, 0.0, 1.0)), 4)

        # Supervised RandomForest probabilities
        probs = self.rf_classifier.predict_proba(row_scaled)[0]
        pred_idx = int(np.argmax(probs))
        pred_label = CLASS_LABELS[pred_idx]
        confidence = round(float(probs[pred_idx]), 4)

        prob_map = {
            CLASS_LABELS[i]: round(float(probs[i]), 4) for i in range(len(probs))
        }

        is_anomaly = (iso_pred == -1) or (pred_idx != 0)
        command = COMMAND_MAP[pred_label]

        # Escalate to WARNING_THROTTLE if IsolationForest flags an unseen anomaly
        # even when RandomForest predicts NOMINAL
        if command == "NORMAL" and iso_pred == -1 and anomaly_score > 0.62:
            command = "WARNING_THROTTLE"

        latency_ms = round((time.perf_counter() - t0) * 1000.0, 3)

        return InferenceResult(
            predicted_class=pred_label,
            command=command,
            confidence=confidence,
            anomaly_score=anomaly_score,
            is_anomaly=is_anomaly,
            class_probabilities=prob_map,
            inference_latency_ms=latency_ms,
        )
