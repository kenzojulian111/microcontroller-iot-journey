# Project 19 — Flask USB Serial LED, PWM & Servo Controller

Full-stack hybrid embedded control system bridging a Python **Flask** web application to an **ATmega328P (Arduino Uno/Nano)** or **ESP32** over a deterministic `115200 bps` UART link with NMEA-style XOR frame checksums and an automatic virtual hardware emulator fallback.

---

## 1. System Architecture

```text
+-----------------------------------+       HTTP REST (JSON)       +-----------------------------------+
|  Browser Engineering Dashboard    | <=========================>  |  Flask WSGI Server (app.py)       |
|  - Relay ON/OFF Controls          |   GET  /api/state            |  - Thread-Safe Singleton Bridge   |
|  - 8-Bit PWM Slider (0..255)      |   POST /api/control          |  - threading.RLock() Serialization|
|  - Servo Angle Control (0..180°)  |                              |  - Virtual MCU Fallback (--mock)  |
+-----------------------------------+                              +-----------------+-----------------+
                                                                                     |
                                                                      USB CDC-ACM    | 115200 8N1
                                                                      CMD:PWM,128*0B | JSON ACK
                                                                                     v
                                                                   +-----------------------------------+
                                                                   |  MCU Firmware (.ino)              |
                                                                   |  - Zero-Allocation Frame Parser   |
                                                                   |  - XOR Checksum Validator         |
                                                                   |  - Relay (D4) / PWM (D5) / Servo  |
                                                                   +-----------------------------------+
```

---

## 2. Key Engineering Features

1. **Thread-Safe `SerialHardwareBridge` Singleton:**
   WSGI servers process HTTP requests across multiple worker threads. `SerialHardwareBridge` wraps all serial writes and line reads inside a re-entrant `threading.RLock()`, preventing interleaved bytes on `/dev/ttyACM0`.
2. **NMEA-0183 Style XOR Frame Integrity:**
   Every outbound command (`CMD:PWM,128*0B\n`) appends an 8-bit hexadecimal XOR checksum. The MCU verifies frame integrity before mutating hardware registers.
3. **Zero-Config Mock Hardware Emulator:**
   When launched with `--mock` (or when no physical USB serial adapter is detected), the server seamlessly emulates the MCU state machine and wire protocol so the full web UI and REST API can be tested offline or in CI.

---

## 3. Quick Start

### 3.1 Flash the Firmware
Compile and upload `firmware/serial_led_controller/serial_led_controller.ino` to an Arduino Uno, Nano, or ESP32 using Arduino IDE or `arduino-cli`:

```bash
arduino-cli compile --fqbn arduino:avr:uno firmware/serial_led_controller
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno firmware/serial_led_controller
```

### 3.2 Run the Flask Backend
```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# Connect to physical USB serial device (auto-detects port, or pass --port /dev/ttyACM0)
python3 app.py --port /dev/ttyACM0

# Or run in virtual hardware emulator mode without an Arduino attached:
python3 app.py --mock
```

Open `http://localhost:5000` in your browser.

---

## 4. CLI Verification via `curl`

```bash
# Query current actuator state
curl -s http://localhost:5000/api/state | python3 -m json.tool

# Set Relay ON, PWM to 192 (75.3%), and Servo to 135 degrees
curl -s -X POST http://localhost:5000/api/control \
  -H "Content-Type: application/json" \
  -d '{"relay": 1, "pwm": 192, "servo": 135}' | python3 -m json.tool
```
