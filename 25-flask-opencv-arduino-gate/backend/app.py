"""
===============================================================================
Project 25: Computer Vision OpenCV + Hardware Servo Gate Trigger
File: backend/app.py
===============================================================================
Description:
    Flask + OpenCV (`cv2`) + PySerial Smart Gate Controller Server.
    Streams live annotated MJPEG frames (`/video_feed`), verifies QR tokens and
    faces, dispatches `GATE:OPEN` / `GATE:CLOSE` / `GATE:LOCK` commands over
    UART (`pyserial`) to the Arduino/ESP32 Servo + HC-SR04 controller, and
    maintains a timestamped access audit log.
===============================================================================
"""

from __future__ import annotations

import os
import time
from collections import deque
from datetime import datetime, timezone
from typing import Any, Deque, Dict, Generator, Optional

from flask import Flask, Response, jsonify, render_template, request

try:
    import serial
except ImportError:
    serial = None  # type: ignore

from vision_pipeline import SmartGateVisionPipeline, VisionDetectionEvent

app = Flask(__name__)
app.config["JSON_SORT_KEYS"] = False


class HardwareGateBridge:
    """
    Manages bidirectional UART communication with the Arduino/ESP32 Servo Gate
    controller (`servo_gate_controller.ino`) with a deterministic software twin
    when no physical `/dev/ttyUSB0` or `/dev/ttyACM0` port is attached.
    """

    def __init__(self) -> None:
        self.serial_port: Optional[str] = os.environ.get("ARDUINO_PORT", "")
        self.baud_rate: int = int(os.environ.get("ARDUINO_BAUD", "115200"))
        self.ser: Optional[Any] = None
        self._connect_serial()

        # Software twin state (synchronized with MCU or simulated when headless)
        self.state: str = "CLOSED"
        self.angle_deg: int = 0
        self.clearance_cm: float = 118.4
        self.vehicle_under_gate: bool = False
        self.last_command: str = "GATE:CLOSE"
        self.last_trigger_ts: float = 0.0

    def _connect_serial(self) -> None:
        if not self.serial_port or serial is None:
            return
        try:
            self.ser = serial.Serial(self.serial_port, self.baud_rate, timeout=0.15)
        except Exception:
            self.ser = None

    def send_command(self, command: str, reason: str = "OPERATOR") -> Dict[str, Any]:
        cmd = command.strip().upper()
        self.last_command = cmd
        self.last_trigger_ts = time.time()

        if self.ser is not None and self.ser.is_open:
            try:
                self.ser.write(f"{cmd}\n".encode("ascii"))
            except Exception:
                pass

        # Update deterministic twin state
        if cmd == "GATE:LOCK":
            self.state = "LOCKED"
            self.angle_deg = 0
        elif cmd == "GATE:UNLOCK":
            self.state = "CLOSED"
            self.angle_deg = 0
        elif cmd == "GATE:OPEN":
            if self.state != "LOCKED":
                self.state = "OPEN"
                self.angle_deg = 90
        elif cmd == "GATE:CLOSE":
            if self.state != "LOCKED":
                if self.vehicle_under_gate or self.clearance_cm < 35.0:
                    self.state = "OBSTRUCTED_HOLD"
                    self.angle_deg = 90
                else:
                    self.state = "CLOSED"
                    self.angle_deg = 0

        return self.get_telemetry(reason=reason)

    def set_simulated_clearance(self, distance_cm: float) -> Dict[str, Any]:
        self.clearance_cm = round(max(5.0, min(200.0, distance_cm)), 1)
        self.vehicle_under_gate = self.clearance_cm < 35.0
        if self.vehicle_under_gate and self.state in ("CLOSING", "CLOSED") and self.state != "LOCKED":
            self.state = "OBSTRUCTED_HOLD"
            self.angle_deg = 90
        elif not self.vehicle_under_gate and self.state == "OBSTRUCTED_HOLD":
            self.state = "OPEN"
            self.angle_deg = 90
        return self.get_telemetry(reason="ULTRASONIC_UPDATE")

    def get_telemetry(self, reason: str = "POLL") -> Dict[str, Any]:
        return {
            "state": self.state,
            "angle_deg": self.angle_deg,
            "clearance_cm": self.clearance_cm,
            "vehicle_under_gate": self.vehicle_under_gate,
            "last_command": self.last_command,
            "serial_connected": bool(self.ser is not None and self.ser.is_open),
            "reason": reason,
        }


vision = SmartGateVisionPipeline()
gate_bridge = HardwareGateBridge()
access_events: Deque[Dict[str, Any]] = deque(maxlen=60)
_last_logged_token: str = ""
_last_logged_time: float = 0.0


def record_vision_event(ev: VisionDetectionEvent, gate_action: str) -> None:
    global _last_logged_token, _last_logged_time
    now = time.time()
    if ev.token_or_subject == _last_logged_token and (now - _last_logged_time) < 3.0:
        return

    _last_logged_token = ev.token_or_subject
    _last_logged_time = now

    access_events.appendleft(
        {
            "timestamp": datetime.now(timezone.utc).strftime("%H:%M:%S"),
            "mode": ev.mode,
            "authorized": ev.authorized,
            "holder": ev.holder_name,
            "vehicle": ev.vehicle_id,
            "token": ev.token_or_subject,
            "confidence": ev.confidence,
            "gate_action": gate_action,
            "clearance_cm": gate_bridge.clearance_cm,
        }
    )


# Seed initial event log
access_events.appendleft(
    {
        "timestamp": datetime.now(timezone.utc).strftime("%H:%M:%S"),
        "mode": "QR_VERIFIED",
        "authorized": True,
        "holder": "Kenzo Julian",
        "vehicle": "EV-01 CyberSedan",
        "token": "IOTLAB-GATE-PASS-KENZO-2026",
        "confidence": 0.99,
        "gate_action": "GATE:OPEN (90 deg)",
        "clearance_cm": 118.4,
    }
)


@app.route("/", methods=["GET"])
def index() -> str:
    return render_template("index.html")


def mjpeg_frame_generator() -> Generator[bytes, None, None]:
    while True:
        jpeg_bytes, ev = vision.process_next_frame(
            gate_state=gate_bridge.state,
            gate_angle=gate_bridge.angle_deg,
            clearance_cm=gate_bridge.clearance_cm,
        )
        if ev.mode != "IDLE":
            if ev.authorized and gate_bridge.state != "LOCKED":
                gate_bridge.send_command("GATE:OPEN", reason=ev.mode)
                record_vision_event(ev, "GATE:OPEN (90 deg)")
            else:
                record_vision_event(ev, "REJECTED (0 deg)")

        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n" + jpeg_bytes + b"\r\n"
        )
        time.sleep(0.12)


@app.route("/video_feed", methods=["GET"])
def video_feed() -> Response:
    """Streams live annotated OpenCV MJPEG frames (`multipart/x-mixed-replace`)."""
    return Response(
        mjpeg_frame_generator(),
        mimetype="multipart/x-mixed-replace; boundary=frame",
    )


@app.route("/api/v1/gate/status", methods=["GET"])
def get_gate_status() -> Response:
    # Run one frame step if no MJPEG client is actively pulling frames so API tests
    # immediately trigger detections as well
    _, ev = vision.process_next_frame(
        gate_state=gate_bridge.state,
        gate_angle=gate_bridge.angle_deg,
        clearance_cm=gate_bridge.clearance_cm,
    )
    if ev.mode != "IDLE":
        if ev.authorized and gate_bridge.state != "LOCKED":
            gate_bridge.send_command("GATE:OPEN", reason=ev.mode)
            record_vision_event(ev, "GATE:OPEN (90 deg)")
        else:
            record_vision_event(ev, "REJECTED (0 deg)")

    return jsonify(
        {
            "gate": gate_bridge.get_telemetry(),
            "vision": {
                "synthetic_camera": vision.use_synthetic_camera,
                "active_scene": vision.synthetic_scene,
                "last_detection": ev.mode,
                "fps": ev.fps,
            },
            "events": list(access_events)[:30],
        }
    )


@app.route("/api/v1/gate/command", methods=["POST"])
def post_gate_command() -> tuple[Response, int]:
    """Dispatches `GATE:OPEN`, `GATE:CLOSE`, `GATE:LOCK`, or `GATE:UNLOCK` to the MCU."""
    payload = request.get_json(silent=True) or {}
    cmd = str(payload.get("command", "")).strip().upper()
    if cmd not in ("GATE:OPEN", "GATE:CLOSE", "GATE:LOCK", "GATE:UNLOCK"):
        return (
            jsonify(
                {
                    "status": "error",
                    "message": "Invalid command. Allowed: GATE:OPEN, GATE:CLOSE, GATE:LOCK, GATE:UNLOCK",
                }
            ),
            400,
        )

    telemetry = gate_bridge.send_command(cmd, reason="MANUAL_API_OVERRIDE")
    return jsonify({"status": "ok", "gate": telemetry}), 200


@app.route("/api/v1/vision/simulate", methods=["POST"])
def simulate_vision_target() -> tuple[Response, int]:
    """
    Injects a synthetic QR code or face target into the OpenCV frame pipeline
    and optionally updates the simulated HC-SR04 ultrasonic clearance.
    """
    payload = request.get_json(silent=True) or {}
    scene = str(payload.get("scene", "STANDBY")).strip().upper()
    clearance = payload.get("clearance_cm", None)

    if clearance is not None:
        gate_bridge.set_simulated_clearance(float(clearance))

    if scene in ("STANDBY", "VALID_QR", "ROGUE_QR", "FACE_OP"):
        vision.trigger_synthetic_scene(scene, duration_sec=6.0)

    _, ev = vision.process_next_frame(
        gate_state=gate_bridge.state,
        gate_angle=gate_bridge.angle_deg,
        clearance_cm=gate_bridge.clearance_cm,
    )
    if ev.mode != "IDLE":
        if ev.authorized and gate_bridge.state != "LOCKED":
            gate_bridge.send_command("GATE:OPEN", reason=ev.mode)
            record_vision_event(ev, "GATE:OPEN (90 deg)")
        else:
            record_vision_event(ev, "REJECTED (0 deg)")

    return (
        jsonify(
            {
                "status": "ok",
                "scene": vision.synthetic_scene,
                "detection": {
                    "mode": ev.mode,
                    "authorized": ev.authorized,
                    "holder": ev.holder_name,
                    "vehicle": ev.vehicle_id,
                    "confidence": ev.confidence,
                },
                "gate": gate_bridge.get_telemetry(),
            }
        ),
        200,
    )


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5025))
    app.run(host="0.0.0.0", port=port, debug=False, threaded=True)
