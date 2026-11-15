#!/usr/bin/env python3
"""
Project 20: Flask Serial Telemetry Logger, SQLite Persistence (WAL) & CSV Exporter
Runs a background daemon thread (SerialTelemetryIngestor) that continuously reads
NDJSON frames from UART (or generates a realistic synthetic sensor stream in mock mode)
and persists records to an indexed SQLite3 database in Write-Ahead Logging (WAL) mode.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import logging
import math
import os
import random
import sqlite3
import threading
import time
from datetime import datetime, timezone
from typing import Any, Dict, Generator, List, Optional

from flask import Flask, Response, jsonify, render_template, request

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None  # type: ignore

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("sensor_logger")

DEFAULT_DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "telemetry.db")


class TelemetryDatabase:
    """Thread-safe SQLite3 persistence layer configured with WAL journal mode."""

    def __init__(self, db_path: str = DEFAULT_DB_PATH) -> None:
        self.db_path = db_path
        self._write_lock = threading.Lock()
        self._init_schema()

    def _get_conn(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path, timeout=10.0, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA journal_mode=WAL;")
        conn.execute("PRAGMA synchronous=NORMAL;")
        return conn

    def _init_schema(self) -> None:
        with self._write_lock:
            conn = self._get_conn()
            try:
                conn.executescript(
                    """
                    CREATE TABLE IF NOT EXISTS sensor_readings (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        ts_utc TEXT NOT NULL,
                        seq INTEGER NOT NULL,
                        temp_c REAL NOT NULL,
                        hum_pct REAL NOT NULL,
                        lux_raw INTEGER NOT NULL,
                        v_adc REAL NOT NULL,
                        source TEXT NOT NULL
                    );
                    CREATE INDEX IF NOT EXISTS idx_sensor_readings_ts
                        ON sensor_readings(id DESC, ts_utc DESC);
                    """
                )
                conn.commit()
            finally:
                conn.close()

    def insert_reading(self, record: Dict[str, Any]) -> int:
        with self._write_lock:
            conn = self._get_conn()
            try:
                cur = conn.execute(
                    """
                    INSERT INTO sensor_readings (ts_utc, seq, temp_c, hum_pct, lux_raw, v_adc, source)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        record["ts_utc"],
                        int(record["seq"]),
                        float(record["temp_c"]),
                        float(record["hum_pct"]),
                        int(record["lux_raw"]),
                        float(record.get("v_adc", round((int(record["lux_raw"]) / 1023.0) * 5.0, 3))),
                        str(record.get("source", "MOCK")),
                    ),
                )
                conn.commit()
                return int(cur.lastrowid or 0)
            finally:
                conn.close()

    def get_latest_and_stats(self) -> Dict[str, Any]:
        conn = self._get_conn()
        try:
            latest_row = conn.execute(
                "SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1"
            ).fetchone()
            agg_row = conn.execute(
                """
                SELECT
                    COUNT(*) AS total_count,
                    ROUND(MIN(temp_c), 2) AS temp_min,
                    ROUND(MAX(temp_c), 2) AS temp_max,
                    ROUND(AVG(temp_c), 2) AS temp_avg,
                    ROUND(MIN(hum_pct), 2) AS hum_min,
                    ROUND(MAX(hum_pct), 2) AS hum_max,
                    ROUND(AVG(hum_pct), 2) AS hum_avg,
                    MIN(lux_raw) AS lux_min,
                    MAX(lux_raw) AS lux_max,
                    ROUND(AVG(lux_raw), 1) AS lux_avg
                FROM sensor_readings
                """
            ).fetchone()
            return {
                "latest": dict(latest_row) if latest_row else None,
                "stats": dict(agg_row) if agg_row else {},
            }
        finally:
            conn.close()

    def get_history(self, limit: int = 100) -> List[Dict[str, Any]]:
        limit = max(1, min(limit, 5000))
        conn = self._get_conn()
        try:
            rows = conn.execute(
                "SELECT * FROM sensor_readings ORDER BY id DESC LIMIT ?", (limit,)
            ).fetchall()
            return [dict(r) for r in rows]
        finally:
            conn.close()

    def stream_all_rows(self) -> Generator[Dict[str, Any], None, None]:
        conn = self._get_conn()
        try:
            cur = conn.execute("SELECT * FROM sensor_readings ORDER BY id ASC")
            for row in cur:
                yield dict(row)
        finally:
            conn.close()


class SerialTelemetryIngestor:
    """
    Background daemon thread that reads newline-delimited JSON sensor frames from
    pyserial or synthesizes realistic environmental waveforms in mock mode.
    """

    def __init__(
        self,
        db: TelemetryDatabase,
        port: Optional[str] = None,
        baudrate: int = 115200,
        force_mock: bool = False,
        sample_interval: float = 2.0,
    ) -> None:
        self.db = db
        self.port_name = port or os.environ.get("SERIAL_PORT", "")
        self.baudrate = baudrate
        self.mock_mode = force_mock or os.environ.get("SERIAL_MOCK", "0") == "1"
        self.sample_interval = sample_interval
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._serial: Optional[Any] = None
        self._seq = 0

    def _auto_detect_port(self) -> Optional[str]:
        if serial is None:
            return None
        for p in serial.tools.list_ports.comports():
            desc = (p.description or "").lower()
            if any(k in desc for k in ("arduino", "ch340", "cp210", "ftdi", "usb", "acm")):
                return p.device
        return None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        if not self.mock_mode:
            target = self.port_name or self._auto_detect_port()
            if target and serial is not None:
                try:
                    self._serial = serial.Serial(target, self.baudrate, timeout=3.0)
                    self.port_name = target
                    logger.info("Opened telemetry serial port %s @ %d bps", target, self.baudrate)
                except Exception as exc:
                    logger.warning("Serial open failed (%s); using synthetic mock stream.", exc)
                    self.mock_mode = True
            else:
                logger.info("No physical UART found; running synthetic telemetry generator.")
                self.mock_mode = True

        self._thread = threading.Thread(
            target=self._run_loop, name="SerialTelemetryIngestor", daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)

    def _run_loop(self) -> None:
        while not self._stop_event.is_set():
            if self.mock_mode or self._serial is None:
                self._ingest_mock_sample()
                self._stop_event.wait(self.sample_interval)
            else:
                try:
                    raw = self._serial.readline().decode("ascii", errors="ignore").strip()
                    if not raw or not raw.startswith("{"):
                        continue
                    data = json.loads(raw)
                    record = {
                        "ts_utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                        "seq": int(data.get("seq", 0)),
                        "temp_c": round(float(data["temp_c"]), 2),
                        "hum_pct": round(float(data["hum_pct"]), 2),
                        "lux_raw": int(data["lux_raw"]),
                        "v_adc": round(float(data.get("v_adc", (int(data["lux_raw"]) / 1023.0) * 5.0)), 3),
                        "source": f"UART:{self.port_name}",
                    }
                    self.db.insert_reading(record)
                except Exception as exc:
                    logger.warning("UART read exception (%s); falling back to mock stream.", exc)
                    self.mock_mode = True

    def _ingest_mock_sample(self) -> None:
        self._seq += 1
        phase = self._seq * 0.15
        temp_c = round(25.8 + 2.1 * math.sin(phase) + random.uniform(-0.15, 0.15), 2)
        hum_pct = round(58.5 - 4.2 * math.sin(phase) + random.uniform(-0.4, 0.4), 2)
        lux_raw = int(max(0, min(1023, 540 + 280 * math.cos(phase * 0.5) + random.randint(-12, 12))))
        v_adc = round((lux_raw / 1023.0) * 5.0, 3)

        record = {
            "ts_utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "seq": self._seq,
            "temp_c": temp_c,
            "hum_pct": hum_pct,
            "lux_raw": lux_raw,
            "v_adc": v_adc,
            "source": "MOCK_SYNTHETIC",
        }
        self.db.insert_reading(record)


app = Flask(__name__)
db = TelemetryDatabase()
ingestor: Optional[SerialTelemetryIngestor] = None


def ensure_ingestor(force_mock: bool = False, port: Optional[str] = None, baud: int = 115200) -> SerialTelemetryIngestor:
    global ingestor
    if ingestor is None:
        ingestor = SerialTelemetryIngestor(db=db, port=port, baudrate=baud, force_mock=force_mock)
        ingestor.start()
    return ingestor


@app.route("/")
def index() -> str:
    ensure_ingestor()
    return render_template("index.html")


@app.route("/api/telemetry/latest", methods=["GET"])
def api_telemetry_latest():
    ing = ensure_ingestor()
    payload = db.get_latest_and_stats()
    payload["mode"] = "MOCK" if ing.mock_mode else "HARDWARE"
    payload["port"] = "virtual://dht22-ldr-sim" if ing.mock_mode else ing.port_name
    return jsonify(payload), 200


@app.route("/api/telemetry/history", methods=["GET"])
def api_telemetry_history():
    ensure_ingestor()
    try:
        limit = int(request.args.get("limit", "100"))
    except ValueError:
        limit = 100
    rows = db.get_history(limit=limit)
    return jsonify({"count": len(rows), "records": rows}), 200


@app.route("/api/export/csv", methods=["GET"])
def api_export_csv() -> Response:
    """Stream RFC-4180 compliant CSV download directly from SQLite cursor."""
    ensure_ingestor()

    def generate_csv() -> Generator[str, None, None]:
        buf = io.StringIO()
        writer = csv.writer(buf, lineterminator="\r\n")
        writer.writerow(["id", "ts_utc", "seq", "temp_c", "hum_pct", "lux_raw", "v_adc", "source"])
        yield buf.getvalue()
        buf.seek(0)
        buf.truncate(0)

        for row in db.stream_all_rows():
            writer.writerow([
                row["id"],
                row["ts_utc"],
                row["seq"],
                f"{row['temp_c']:.2f}",
                f"{row['hum_pct']:.2f}",
                row["lux_raw"],
                f"{row['v_adc']:.3f}",
                row["source"],
            ])
            yield buf.getvalue()
            buf.seek(0)
            buf.truncate(0)

    filename = f"telemetry_export_{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}.csv"
    return Response(
        generate_csv(),
        mimetype="text/csv",
        headers={"Content-Disposition": f'attachment; filename="{filename}"'},
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Flask Serial Sensor Telemetry Logger")
    parser.add_argument("--port", type=str, default=None, help="Serial device path")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate")
    parser.add_argument("--mock", action="store_true", help="Force synthetic telemetry mode")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Bind address")
    parser.add_argument("--port-http", type=int, default=5001, help="HTTP listen port")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    ensure_ingestor(force_mock=args.mock, port=args.port, baud=args.baud)
    app.run(host=args.host, port=args.port_http, debug=False, threaded=True)
