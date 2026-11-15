#!/usr/bin/env python3
"""
Project 21: Authenticated Wi-Fi REST API Weather Station & Chart.js Telemetry Server
Features:
  - Constant-time X-API-Key header validation via @require_api_key decorator
  - SQLAlchemy time-series model with Magnus-Tetens dew point calculation
  - Aggregate analytics (min, max, mean, dew point, barometric trend)
  - Optional background simulator (--mock) so Chart.js updates live out-of-the-box
"""

from __future__ import annotations

import argparse
import functools
import hmac
import logging
import math
import os
import random
import threading
from datetime import datetime, timezone
from typing import Any, Callable, Dict

from flask import Flask, jsonify, render_template, request
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy import func

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("weather_api")

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_URI = f"sqlite:///{os.path.join(BASE_DIR, 'weather_station.db')}"
DEFAULT_API_KEY = os.environ.get("WEATHER_API_KEY", "iot-lab-track3-weather-secret-key-2026")

app = Flask(__name__)
app.config["SQLALCHEMY_DATABASE_URI"] = DB_URI
app.config["SQLALCHEMY_TRACK_MODIFICATIONS"] = False
app.config["WEATHER_API_KEY"] = DEFAULT_API_KEY

db = SQLAlchemy(app)


def compute_dew_point_c(temp_c: float, hum_pct: float) -> float:
    """
    Magnus-Tetens approximation for Dew Point temperature (°C):
      gamma(T, RH) = ln(RH / 100) + (b * T) / (c + T)
      T_dp = (c * gamma) / (b - gamma)
    where b = 17.625, c = 243.04 °C.
    """
    rh_clamped = max(1.0, min(100.0, hum_pct))
    b = 17.625
    c = 243.04
    gamma = math.log(rh_clamped / 100.0) + (b * temp_c) / (c + temp_c)
    return round((c * gamma) / (b - gamma), 2)


class WeatherTelemetry(db.Model):
    __tablename__ = "weather_telemetry"

    id = db.Column(db.Integer, primary_key=True, autoincrement=True)
    ts_utc = db.Column(db.String(40), nullable=False, index=True)
    node_id = db.Column(db.String(64), nullable=False, index=True)
    seq = db.Column(db.Integer, nullable=False)
    temp_c = db.Column(db.Float, nullable=False)
    hum_pct = db.Column(db.Float, nullable=False)
    press_hpa = db.Column(db.Float, nullable=False)
    dew_point_c = db.Column(db.Float, nullable=False)
    rssi_dbm = db.Column(db.Integer, nullable=False, default=-60)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "ts_utc": self.ts_utc,
            "node_id": self.node_id,
            "seq": self.seq,
            "temp_c": round(self.temp_c, 2),
            "hum_pct": round(self.hum_pct, 2),
            "press_hpa": round(self.press_hpa, 2),
            "dew_point_c": round(self.dew_point_c, 2),
            "rssi_dbm": self.rssi_dbm,
        }


with app.app_context():
    db.create_all()


def require_api_key(view_func: Callable[..., Any]) -> Callable[..., Any]:
    """Authenticate incoming MCU HTTP POST requests using constant-time HMAC comparison."""

    @functools.wraps(view_func)
    def wrapped(*args: Any, **kwargs: Any):
        provided = request.headers.get("X-API-Key", "")
        expected = app.config["WEATHER_API_KEY"]
        if not provided or not hmac.compare_digest(provided, expected):
            return (
                jsonify({"ok": False, "err": "UNAUTHORIZED_API_KEY"}),
                401,
            )
        return view_func(*args, **kwargs)

    return wrapped


@app.route("/")
def index() -> str:
    return render_template("index.html")


@app.route("/api/v1/telemetry", methods=["POST"])
@require_api_key
def ingest_telemetry():
    payload = request.get_json(silent=True) or {}
    required = ("temp_c", "hum_pct", "press_hpa")
    if not all(k in payload for k in required):
        return (
            jsonify({"ok": False, "err": "MISSING_REQUIRED_FIELDS", "required": required}),
            400,
        )

    try:
        temp_c = float(payload["temp_c"])
        hum_pct = float(payload["hum_pct"])
        press_hpa = float(payload["press_hpa"])
        seq = int(payload.get("seq", 0))
        node_id = str(payload.get("node_id", "esp32-weather-01"))[:64]
        rssi_dbm = int(payload.get("rssi_dbm", -65))
    except (ValueError, TypeError):
        return jsonify({"ok": False, "err": "INVALID_FIELD_TYPES"}), 400

    if not (-50.0 <= temp_c <= 85.0) or not (0.0 <= hum_pct <= 100.0) or not (800.0 <= press_hpa <= 1200.0):
        return jsonify({"ok": False, "err": "SENSOR_VALUES_OUT_OF_BOUNDS"}), 422

    dew_c = compute_dew_point_c(temp_c, hum_pct)
    record = WeatherTelemetry(
        ts_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        node_id=node_id,
        seq=seq,
        temp_c=temp_c,
        hum_pct=hum_pct,
        press_hpa=press_hpa,
        dew_point_c=dew_c,
        rssi_dbm=rssi_dbm,
    )
    db.session.add(record)
    db.session.commit()
    return jsonify({"ok": True, "record": record.to_dict()}), 201


@app.route("/api/v1/telemetry", methods=["GET"])
def query_telemetry():
    try:
        limit = max(1, min(500, int(request.args.get("limit", "60"))))
    except ValueError:
        limit = 60

    rows = (
        WeatherTelemetry.query.order_by(WeatherTelemetry.id.desc())
        .limit(limit)
        .all()
    )
    # Return chronological order (oldest -> newest) for Chart.js time-series plotting
    chronological = [r.to_dict() for r in reversed(rows)]
    return jsonify({"ok": True, "count": len(chronological), "series": chronological}), 200


@app.route("/api/v1/analytics", methods=["GET"])
def query_analytics():
    agg = db.session.query(
        func.count(WeatherTelemetry.id),
        func.min(WeatherTelemetry.temp_c),
        func.max(WeatherTelemetry.temp_c),
        func.avg(WeatherTelemetry.temp_c),
        func.min(WeatherTelemetry.hum_pct),
        func.max(WeatherTelemetry.hum_pct),
        func.avg(WeatherTelemetry.hum_pct),
        func.min(WeatherTelemetry.press_hpa),
        func.max(WeatherTelemetry.press_hpa),
        func.avg(WeatherTelemetry.press_hpa),
        func.avg(WeatherTelemetry.dew_point_c),
    ).one()

    latest = WeatherTelemetry.query.order_by(WeatherTelemetry.id.desc()).first()
    return (
        jsonify(
            {
                "ok": True,
                "latest": latest.to_dict() if latest else None,
                "analytics": {
                    "sample_count": int(agg[0] or 0),
                    "temp_min_c": round(float(agg[1]), 2) if agg[1] is not None else None,
                    "temp_max_c": round(float(agg[2]), 2) if agg[2] is not None else None,
                    "temp_mean_c": round(float(agg[3]), 2) if agg[3] is not None else None,
                    "hum_min_pct": round(float(agg[4]), 2) if agg[4] is not None else None,
                    "hum_max_pct": round(float(agg[5]), 2) if agg[5] is not None else None,
                    "hum_mean_pct": round(float(agg[6]), 2) if agg[6] is not None else None,
                    "press_min_hpa": round(float(agg[7]), 2) if agg[7] is not None else None,
                    "press_max_hpa": round(float(agg[8]), 2) if agg[8] is not None else None,
                    "press_mean_hpa": round(float(agg[9]), 2) if agg[9] is not None else None,
                    "dew_point_mean_c": round(float(agg[10]), 2) if agg[10] is not None else None,
                },
            }
        ),
        200,
    )


def start_mock_weather_generator(interval_sec: float = 3.0) -> None:
    """Background synthetic BME280 generator for instant Chart.js visualization in --mock mode."""

    def _worker() -> None:
        seq = 0
        stop_ev = threading.Event()
        while not stop_ev.is_set():
            seq += 1
            phase = seq * 0.14
            temp_c = round(24.8 + 2.4 * math.sin(phase) + random.uniform(-0.15, 0.15), 2)
            hum_pct = round(61.0 - 5.5 * math.sin(phase) + random.uniform(-0.3, 0.3), 2)
            press_hpa = round(1013.2 + 2.1 * math.cos(phase * 0.4) + random.uniform(-0.1, 0.1), 2)
            dew_c = compute_dew_point_c(temp_c, hum_pct)
            with app.app_context():
                rec = WeatherTelemetry(
                    ts_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
                    node_id="esp32-weather-mock",
                    seq=seq,
                    temp_c=temp_c,
                    hum_pct=hum_pct,
                    press_hpa=press_hpa,
                    dew_point_c=dew_c,
                    rssi_dbm=random.randint(-64, -52),
                )
                db.session.add(rec)
                db.session.commit()
            stop_ev.wait(interval_sec)

    t = threading.Thread(target=_worker, name="MockWeatherNode", daemon=True)
    t.start()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Flask Authenticated REST Weather Station Server")
    parser.add_argument("--mock", action="store_true", help="Enable synthetic BME280 telemetry generator")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Bind address")
    parser.add_argument("--port-http", type=int, default=5002, help="HTTP listen port")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    if args.mock or os.environ.get("WEATHER_MOCK", "0") == "1":
        logger.info("Starting synthetic ESP32 BME280 weather generator thread...")
        start_mock_weather_generator()
    app.run(host=args.host, port=args.port_http, debug=False, threaded=True)
