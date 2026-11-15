# Project 25: Computer Vision OpenCV + Hardware Servo Gate Trigger

**Track III — Phase 8: Full-Stack Hybrid MCU + Python Flask OpenCV Pipeline**

An end-to-end automated vehicle barrier gate pairing a **Python Flask + OpenCV (`cv2`)** computer vision pipeline (QR code access token verification + Haar Cascade face detection + headless synthetic camera fallback) with an **Arduino Uno / ESP32 Motorized Servo Gate Controller** featuring an **HC-SR04 ultrasonic anti-crush safety interlock**.

---

## Key Capabilities

1. **Non-Blocking Servo & Ultrasonic Anti-Crush Firmware (`firmware/servo_gate_controller/servo_gate_controller.ino`):**
   - Compatible with both **Arduino Uno/Nano** and **ESP32**.
   - Smoothly ramps the barrier boom (`SG90` / `MG996R` servo) between $0^\circ$ (`CLOSED`) and $90^\circ$ (`OPEN`) at $18\text{ ms/deg}$ without blocking `loop()`.
   - Continuously polls an **HC-SR04 ultrasonic sensor** beneath the barrier: refuses to close if a vehicle is present ($<35\text{ cm}$) and immediately reverses the boom back to $90^\circ$ if an obstacle enters the lane during closure (`EMERGENCY_REVERSE_OBSTACLE`).
   - Controls Red/Green traffic signal LEDs and an acoustic warning buzzer via `GATE:OPEN`, `GATE:CLOSE`, `GATE:LOCK`, and `GATE:UNLOCK` serial commands.
2. **OpenCV Computer Vision Backend (`backend/vision_pipeline.py` & `backend/app.py`):**
   - Decodes QR code vehicle passes via `cv2.QRCodeDetector()` and detects operator faces via `cv2.CascadeClassifier`.
   - Includes a **Headless Synthetic Frame Generator Fallback** so `/video_feed` streams annotated 640x360 MJPEG video and executes real OpenCV detection even on headless Linux servers without a physical webcam.
   - Automatically triggers the Arduino servo gate over `pyserial` upon credential verification.

---

## Directory Structure

```text
25-flask-opencv-arduino-gate/
├── README.md
├── firmware/
│   └── servo_gate_controller/
│       └── servo_gate_controller.ino
├── backend/
│   ├── app.py
│   ├── vision_pipeline.py
│   ├── requirements.txt
│   ├── templates/
│   │   └── index.html
│   └── static/
│       └── style.css
├── schematics/
│   ├── diagram.json
│   └── wiring-guide.md
└── docs/
    └── architecture-and-api-spec.md
```

---

## Quick Start

### 1. Launch the Flask + OpenCV Vision Server

```bash
cd 25-flask-opencv-arduino-gate/backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 app.py
```

Open `http://localhost:5025` to view the live annotated MJPEG stream (`/video_feed`), inject synthetic QR passes or faces, and test the HC-SR04 ultrasonic anti-crush interlock.

### 2. Test Vision Simulation & Gate Actuation via CLI

```bash
curl -X POST http://localhost:5025/api/v1/vision/simulate \
  -H "Content-Type: application/json" \
  -d '{"scene": "VALID_QR", "clearance_cm": 110.0}'
```

### 3. Run the Servo & Ultrasonic Gate Firmware in Wokwi

Open `schematics/diagram.json` in Wokwi. Click the **HC-SR04 Ultrasonic Sensor** to drag the distance slider below `35 cm` while the gate is closing, or send `GATE:OPEN`, `GATE:CLOSE`, and `GATE:LOCK` in the Serial Monitor.
