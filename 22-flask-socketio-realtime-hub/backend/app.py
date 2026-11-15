#!/usr/bin/env python3
"""
Project 22: Full-Duplex Bi-Directional WebSockets — Flask-SocketIO Realtime Hub
Manages device & browser rooms, synchronizes 2-channel relay state in real time
without HTTP polling, instruments round-trip WebSocket latency (ping_latency),
and provides a REST/WebSocket hardware button simulator for bench testing.
"""

from __future__ import annotations

import argparse
import logging
import threading
import time
from datetime import datetime, timezone
from typing import Any, Dict

from flask import Flask, jsonify, render_template, request
from flask_socketio import SocketIO, emit, join_room

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("socketio_hub")

app = Flask(__name__)
app.config["SECRET_KEY"] = "iot-lab-track3-socketio-secret-2026"

# Using threading async_mode + simple-websocket for zero-config native WebSockets
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

state_lock = threading.Lock()
hub_state: Dict[str, Any] = {
    "device_id": "esp32-rt-node-01",
    "seq": 0,
    "relay1": 0,
    "relay2": 0,
    "source": "HUB_INIT",
    "uptime_ms": 0,
    "updated_at": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
    "connected_clients": 0,
}

boot_monotonic = time.monotonic()


def snapshot_state() -> Dict[str, Any]:
    with state_lock:
        hub_state["uptime_ms"] = int((time.monotonic() - boot_monotonic) * 1000)
        return dict(hub_state)


@app.route("/")
def index() -> str:
    return render_template("index.html")


@app.route("/api/state", methods=["GET"])
def api_get_state():
    return jsonify({"ok": True, "state": snapshot_state()}), 200


@app.route("/api/simulate/hw_button", methods=["POST"])
def api_simulate_hw_button():
    """
    Allows CI / bench testing of a physical tactile ISR button press on the MCU,
    immediately broadcasting `device_state_update` to all connected WebSocket clients.
    """
    payload = request.get_json(silent=True) or {}
    channel = int(payload.get("channel", 1))
    if channel not in (1, 2):
        return jsonify({"ok": False, "err": "INVALID_CHANNEL"}), 400

    with state_lock:
        key = f"relay{channel}"
        hub_state[key] = 0 if hub_state[key] else 1
        hub_state["seq"] += 1
        hub_state["source"] = f"HW_BUTTON_{channel}"
        hub_state["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        current = dict(hub_state)

    socketio.emit("device_state_update", current)
    return jsonify({"ok": True, "state": current}), 200


@socketio.on("connect")
def handle_connect():
    with state_lock:
        hub_state["connected_clients"] += 1
    join_room("browsers")
    emit("device_state_update", snapshot_state())


@socketio.on("disconnect")
def handle_disconnect():
    with state_lock:
        hub_state["connected_clients"] = max(0, hub_state["connected_clients"] - 1)


@socketio.on("join_device_room")
def handle_join_device(data: Dict[str, Any]):
    device_id = str((data or {}).get("device_id", "esp32-rt-node-01"))
    join_room("devices")
    join_room(device_id)
    emit("device_state_update", snapshot_state(), to="browsers")


@socketio.on("device_state_update")
def handle_device_state_update(data: Dict[str, Any]):
    """Invoked by the ESP32 node when a physical ISR button is pressed or relay changes."""
    with state_lock:
        if "relay1" in data:
            hub_state["relay1"] = 1 if int(data["relay1"]) else 0
        if "relay2" in data:
            hub_state["relay2"] = 1 if int(data["relay2"]) else 0
        hub_state["seq"] = int(data.get("seq", hub_state["seq"] + 1))
        hub_state["device_id"] = str(data.get("device_id", hub_state["device_id"]))
        hub_state["source"] = str(data.get("source", "MCU_EVENT"))
        hub_state["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        current = dict(hub_state)

    emit("device_state_update", current, broadcast=True)


@socketio.on("web_toggle_relay")
def handle_web_toggle_relay(data: Dict[str, Any]):
    """Invoked by browser UI switches; updates state and pushes command to MCU & all browsers."""
    channel = int((data or {}).get("channel", 1))
    if channel not in (1, 2):
        emit("error_event", {"err": "INVALID_CHANNEL"})
        return

    with state_lock:
        key = f"relay{channel}"
        if "state" in (data or {}):
            hub_state[key] = 1 if int(data["state"]) else 0
        else:
            hub_state[key] = 0 if hub_state[key] else 1
        hub_state["seq"] += 1
        hub_state["source"] = f"WEB_UI_CH{channel}"
        hub_state["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        new_val = hub_state[key]
        current = dict(hub_state)

    # Push actuation event down to ESP32 firmware and broadcast state to all browser tabs
    emit("mcu_set_relay", {"channel": channel, "state": new_val}, broadcast=True)
    emit("device_state_update", current, broadcast=True)


@socketio.on("ping_latency")
def handle_ping_latency(data: Dict[str, Any]):
    """Echo client high-resolution timestamp immediately for RTT instrumentation."""
    client_t0 = (data or {}).get("client_t0", 0)
    emit(
        "pong_latency",
        {
            "client_t0": client_t0,
            "server_ts": time.time() * 1000.0,
            "seq": hub_state["seq"],
        },
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Flask-SocketIO Full-Duplex Realtime Hub")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Bind address")
    parser.add_argument("--port-http", type=int, default=5003, help="HTTP/WS listen port")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    socketio.run(app, host=args.host, port=args.port_http, debug=False, allow_unsafe_werkzeug=True)
