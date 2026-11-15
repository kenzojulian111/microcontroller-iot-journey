"""
===============================================================================
Project 24: Server-Side Machine Learning Inference Offloading
File: backend/app.py
===============================================================================
Description:
    Flask REST API & Telemetry Offloading Server for memory-constrained MCUs
    (Arduino Uno/Nano via Serial Bridge & ESP32 via HTTP POST).
===============================================================================
"""

from __future__ import annotations

import os
from collections import deque
from datetime import datetime, timezone
from typing import Any, Deque, Dict

from flask import Flask, Response, jsonify, render_template, request

from ml_engine import FEATURE_NAMES, IndustrialAnomalyEngine

BASE_DIR = os.path.abspath(os.path.dirname(__file__))
MODEL_PATH = os.path.join(BASE_DIR, "industrial_rf_pipeline.pkl")

app = Flask(__name__)
app.config["JSON_SORT_KEYS"] = False

ml_engine = IndustrialAnomalyEngine(artifact_path=MODEL_PATH)
telemetry_history: Deque[Dict[str, Any]] = deque(maxlen=80)

# Seed a few initial telemetry frames so the dashboard renders immediately
_SEED_VECTORS = [
    {
        "node_id": "TURBINE-NODE-04",
        "vib_rms_g": 0.13,
        "vib_p2p_g": 0.36,
        "crest_factor": 1.82,
        "temp_c": 41.8,
        "gas_ppm_ratio": 1.02,
        "motor_rpm": 1785,
    },
    {
        "node_id": "TURBINE-NODE-04",
        "vib_rms_g": 0.15,
        "vib_p2p_g": 0.41,
        "crest_factor": 1.91,
        "temp_c": 42.3,
        "gas_ppm_ratio": 1.06,
        "motor_rpm": 1782,
    },
]
for vec in _SEED_VECTORS:
    res = ml_engine.evaluate_feature_vector(vec)
    telemetry_history.appendleft(
        {
            "timestamp": datetime.now(timezone.utc).strftime("%H:%M:%S"),
            "node_id": vec["node_id"],
            "features": vec,
            "predicted_class": res.predicted_class,
            "command": res.command,
            "confidence": res.confidence,
            "anomaly_score": res.anomaly_score,
            "is_anomaly": res.is_anomaly,
            "probabilities": res.class_probabilities,
            "latency_ms": res.inference_latency_ms,
        }
    )


@app.route("/", methods=["GET"])
def index() -> str:
    return render_template("index.html")


@app.route("/api/v1/ml/infer", methods=["POST"])
def infer_telemetry() -> tuple[Response, int]:
    """
    Ingests a 6-feature sensor vector from an ESP32 or Arduino Serial Bridge,
    runs IsolationForest + RandomForestClassifier inference, and returns a
    closed-loop actuation directive (`NORMAL`, `WARNING_THROTTLE`, `CRITICAL_SHUTDOWN`).
    """
    payload = request.get_json(silent=True) or {}
    node_id = str(payload.get("node_id", "TURBINE-NODE-04"))

    features: Dict[str, float] = {
        "vib_rms_g": float(payload.get("vib_rms_g", 0.14)),
        "vib_p2p_g": float(payload.get("vib_p2p_g", 0.38)),
        "crest_factor": float(payload.get("crest_factor", 1.85)),
        "temp_c": float(payload.get("temp_c", 42.0)),
        "gas_ppm_ratio": float(payload.get("gas_ppm_ratio", 1.05)),
        "motor_rpm": float(payload.get("motor_rpm", 1785.0)),
    }

    result = ml_engine.evaluate_feature_vector(features)
    now_str = datetime.now(timezone.utc).strftime("%H:%M:%S")

    record = {
        "timestamp": now_str,
        "node_id": node_id,
        "features": features,
        "predicted_class": result.predicted_class,
        "command": result.command,
        "confidence": result.confidence,
        "anomaly_score": result.anomaly_score,
        "is_anomaly": result.is_anomaly,
        "probabilities": result.class_probabilities,
        "latency_ms": result.inference_latency_ms,
    }
    telemetry_history.appendleft(record)

    return (
        jsonify(
            {
                "status": "ok",
                "node_id": node_id,
                "predicted_class": result.predicted_class,
                "command": result.command,
                "confidence": result.confidence,
                "anomaly_score": result.anomaly_score,
                "is_anomaly": result.is_anomaly,
                "probabilities": result.class_probabilities,
                "inference_latency_ms": result.inference_latency_ms,
                "timestamp": now_str,
            }
        ),
        200,
    )


@app.route("/api/v1/ml/status", methods=["GET"])
def get_ml_status() -> Response:
    history_list = list(telemetry_history)
    anomalies_count = sum(1 for item in history_list if item["is_anomaly"])
    shutdown_count = sum(
        1 for item in history_list if item["command"] == "CRITICAL_SHUTDOWN"
    )
    latest = history_list[0] if history_list else None

    return jsonify(
        {
            "model_metrics": ml_engine.training_metrics,
            "feature_names": FEATURE_NAMES,
            "summary": {
                "buffered_frames": len(history_list),
                "anomalies_detected": anomalies_count,
                "critical_shutdowns": shutdown_count,
                "latest_command": latest["command"] if latest else "NORMAL",
                "latest_class": latest["predicted_class"] if latest else "NOMINAL",
            },
            "latest": latest,
            "history": history_list[:35],
        }
    )


@app.route("/api/v1/model/retrain", methods=["POST"])
def retrain_model() -> tuple[Response, int]:
    """Triggers online retraining of the Scikit-Learn pipeline with a fresh random seed."""
    payload = request.get_json(silent=True) or {}
    samples_per_class = min(max(int(payload.get("samples_per_class", 400)), 100), 2000)
    seed = int(payload.get("seed", int(datetime.now(timezone.utc).timestamp()) % 10000))

    metrics = ml_engine.train_pipeline(samples_per_class=samples_per_class, seed=seed)
    return jsonify({"status": "retrained", "metrics": metrics}), 200


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5024))
    app.run(host="0.0.0.0", port=port, debug=False)
