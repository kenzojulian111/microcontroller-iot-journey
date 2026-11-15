#!/usr/bin/env python3
"""
Project 19: Flask USB Serial Bridge — Web-to-Hardware Control Server
Provides a thread-safe SerialHardwareBridge singleton with XOR frame checksums
and automatic fallback to an in-memory virtual hardware emulator when no physical
UART device is attached or --mock is specified.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import threading
import time
from typing import Any, Dict, Optional

from flask import Flask, jsonify, render_template, request

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None  # type: ignore

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("serial_bridge")


def compute_xor_checksum(payload: str) -> str:
    """Compute 8-bit hexadecimal XOR checksum across ASCII characters."""
    crc = 0
    for ch in payload.encode("ascii", errors="ignore"):
        crc ^= ch
    return f"{crc:02X}"


class SerialHardwareBridge:
    """
    Thread-safe Singleton managing synchronous command/response transactions over UART.
    Uses a re-entrant mutex so concurrent WSGI worker threads cannot interleave bytes
    on the underlying serial descriptor.
    """

    _instance: Optional["SerialHardwareBridge"] = None
    _instance_lock = threading.Lock()

    def __new__(cls, *args: Any, **kwargs: Any) -> "SerialHardwareBridge":
        with cls._instance_lock:
            if cls._instance is None:
                cls._instance = super().__new__(cls)
                cls._instance._initialized = False
            return cls._instance

    def __init__(
        self,
        port: Optional[str] = None,
        baudrate: int = 115200,
        timeout: float = 1.0,
        force_mock: bool = False,
    ) -> None:
        if getattr(self, "_initialized", False):
            return

        self._io_lock = threading.RLock()
        self.port_name = port or os.environ.get("SERIAL_PORT", "")
        self.baudrate = baudrate
        self.timeout = timeout
        self.mock_mode = force_mock or os.environ.get("SERIAL_MOCK", "0") == "1"
        self._serial: Optional[ Any ] = None
        self._boot_monotonic = time.monotonic()

        # Virtual Hardware Emulator State (used in Mock mode or cached state)
        self._state: Dict[str, Any] = {
            "ok": True,
            "seq": 0,
            "relay": 0,
            "pwm": 0,
            "servo": 90,
            "uptime_ms": 0,
            "mode": "MOCK",
            "port": "virtual://atmega328p-sim",
            "last_frame_tx": "",
            "last_frame_rx": "",
            "last_latency_ms": 0.0,
        }

        if not self.mock_mode:
            self._connect_hardware()
        else:
            logger.info("Initialized SerialHardwareBridge in MOCK Hardware Emulator mode.")

        self._initialized = True

    def _auto_detect_port(self) -> Optional[str]:
        if serial is None:
            return None
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            desc = (p.description or "").lower()
            if any(k in desc for k in ("arduino", "ch340", "cp210", "ftdi", "usb", "acm", "uart")):
                return p.device
        return ports[0].device if ports else None

    def _connect_hardware(self) -> None:
        if serial is None:
            logger.warning("pyserial not installed; falling back to virtual mock hardware.")
            self.mock_mode = True
            return

        target_port = self.port_name or self._auto_detect_port()
        if not target_port:
            logger.warning("No physical USB UART discovered; falling back to virtual mock hardware.")
            self.mock_mode = True
            return

        try:
            self._serial = serial.Serial(
                port=target_port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                write_timeout=self.timeout,
            )
            self.port_name = target_port
            # Wait for ATmega328P auto-reset on DTR assertion
            time.sleep(1.8)
            self._serial.reset_input_buffer()
            self._state["mode"] = "HARDWARE"
            self._state["port"] = target_port
            logger.info("Connected to hardware controller on %s @ %d bps", target_port, self.baudrate)
            self.send_command("STATUS")
        except Exception as exc:
            logger.warning(
                "Could not open serial port '%s' (%s). Falling back to virtual mock mode.",
                target_port,
                exc,
            )
            self._serial = None
            self.mock_mode = True
            self._state["mode"] = "MOCK"
            self._state["port"] = "virtual://atmega328p-sim"

    def send_command(self, cmd_body: str, use_checksum: bool = True) -> Dict[str, Any]:
        """
        Format, transmit, and await JSON response for a command body (e.g. 'RELAY,1' or 'PWM,128').
        Thread-safe via internal RLock.
        """
        with self._io_lock:
            t0 = time.perf_counter()
            raw_cmd = f"CMD:{cmd_body}"
            if use_checksum:
                crc = compute_xor_checksum(raw_cmd)
                wire_frame = f"{raw_cmd}*{crc}\n"
            else:
                wire_frame = f"{raw_cmd}\n"

            self._state["last_frame_tx"] = wire_frame.strip()

            if self.mock_mode or self._serial is None:
                response = self._emulate_command(cmd_body)
            else:
                response = self._transact_serial(wire_frame)

            latency_ms = round((time.perf_counter() - t0) * 1000.0, 2)
            self._state.update(response)
            self._state["last_latency_ms"] = latency_ms
            self._state["mode"] = "MOCK" if self.mock_mode else "HARDWARE"
            self._state["port"] = (
                "virtual://atmega328p-sim" if self.mock_mode else self.port_name
            )
            return dict(self._state)

    def _transact_serial(self, wire_frame: str) -> Dict[str, Any]:
        assert self._serial is not None
        try:
            self._serial.reset_input_buffer()
            self._serial.write(wire_frame.encode("ascii"))
            self._serial.flush()
            raw_line = self._serial.readline().decode("ascii", errors="replace").strip()
            self._state["last_frame_rx"] = raw_line
            if not raw_line:
                return {"ok": False, "err": "SERIAL_TIMEOUT"}
            parsed = json.loads(raw_line)
            return parsed
        except Exception as exc:
            logger.error("Serial transaction error: %s — switching to mock fallback.", exc)
            self.mock_mode = True
            return self._emulate_command(wire_frame.split("*")[0].replace("CMD:", "").strip())

    def _emulate_command(self, cmd_body: str) -> Dict[str, Any]:
        """Deterministic software model of the MCU parser for offline/CI verification."""
        uptime_ms = int((time.monotonic() - self._boot_monotonic) * 1000)
        if cmd_body == "STATUS":
            self._state["seq"] += 1
            res = {
                "ok": True,
                "seq": self._state["seq"],
                "relay": self._state["relay"],
                "pwm": self._state["pwm"],
                "servo": self._state["servo"],
                "uptime_ms": uptime_ms,
            }
            self._state["last_frame_rx"] = json.dumps(res, separators=(",", ":"))
            return res

        if "," not in cmd_body:
            return {"ok": False, "err": "MISSING_ARGUMENT", "uptime_ms": uptime_ms}

        target, val_str = cmd_body.split(",", 1)
        try:
            val = int(val_str)
        except ValueError:
            return {"ok": False, "err": "NON_NUMERIC_VALUE", "uptime_ms": uptime_ms}

        if target == "RELAY":
            if val not in (0, 1):
                return {"ok": False, "err": "RELAY_OUT_OF_RANGE", "uptime_ms": uptime_ms}
            self._state["relay"] = val
        elif target == "PWM":
            if not (0 <= val <= 255):
                return {"ok": False, "err": "PWM_OUT_OF_RANGE", "uptime_ms": uptime_ms}
            self._state["pwm"] = val
        elif target == "SERVO":
            if not (0 <= val <= 180):
                return {"ok": False, "err": "SERVO_OUT_OF_RANGE", "uptime_ms": uptime_ms}
            self._state["servo"] = val
        else:
            return {"ok": False, "err": "UNKNOWN_COMMAND", "uptime_ms": uptime_ms}

        self._state["seq"] += 1
        res = {
            "ok": True,
            "seq": self._state["seq"],
            "relay": self._state["relay"],
            "pwm": self._state["pwm"],
            "servo": self._state["servo"],
            "uptime_ms": uptime_ms,
        }
        self._state["last_frame_rx"] = json.dumps(res, separators=(",", ":"))
        return res

    def get_state(self) -> Dict[str, Any]:
        return self.send_command("STATUS")


app = Flask(__name__)
bridge: Optional[SerialHardwareBridge] = None


def get_bridge() -> SerialHardwareBridge:
    global bridge
    if bridge is None:
        bridge = SerialHardwareBridge()
    return bridge


@app.route("/")
def index() -> str:
    return render_template("index.html")


@app.route("/api/state", methods=["GET"])
def api_get_state():
    state = get_bridge().get_state()
    return jsonify(state), 200


@app.route("/api/control", methods=["POST"])
def api_post_control():
    payload = request.get_json(silent=True) or {}
    b = get_bridge()
    last_state: Dict[str, Any] = {}

    if not any(k in payload for k in ("relay", "pwm", "servo")):
        return (
            jsonify({"ok": False, "err": "NO_CONTROL_KEYS_PROVIDED"}),
            400,
        )

    if "relay" in payload:
        try:
            relay_val = int(payload["relay"])
        except (ValueError, TypeError):
            return jsonify({"ok": False, "err": "INVALID_RELAY_TYPE"}), 400
        if relay_val not in (0, 1):
            return jsonify({"ok": False, "err": "RELAY_OUT_OF_RANGE"}), 400
        last_state = b.send_command(f"RELAY,{relay_val}")
        if not last_state.get("ok", False):
            return jsonify(last_state), 400

    if "pwm" in payload:
        try:
            pwm_val = int(payload["pwm"])
        except (ValueError, TypeError):
            return jsonify({"ok": False, "err": "INVALID_PWM_TYPE"}), 400
        if not (0 <= pwm_val <= 255):
            return jsonify({"ok": False, "err": "PWM_OUT_OF_RANGE"}), 400
        last_state = b.send_command(f"PWM,{pwm_val}")
        if not last_state.get("ok", False):
            return jsonify(last_state), 400

    if "servo" in payload:
        try:
            servo_val = int(payload["servo"])
        except (ValueError, TypeError):
            return jsonify({"ok": False, "err": "INVALID_SERVO_TYPE"}), 400
        if not (0 <= servo_val <= 180):
            return jsonify({"ok": False, "err": "SERVO_OUT_OF_RANGE"}), 400
        last_state = b.send_command(f"SERVO,{servo_val}")
        if not last_state.get("ok", False):
            return jsonify(last_state), 400

    return jsonify(last_state), 200


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Flask USB Serial Hardware Controller")
    parser.add_argument("--port", type=str, default=None, help="Serial device path (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate (default: 115200)")
    parser.add_argument("--mock", action="store_true", help="Force virtual hardware emulator mode")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Bind address")
    parser.add_argument("--port-http", type=int, default=5000, help="HTTP listen port")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    bridge = SerialHardwareBridge(
        port=args.port,
        baudrate=args.baud,
        force_mock=args.mock,
    )
    app.run(host=args.host, port=args.port_http, debug=False, threaded=True)
