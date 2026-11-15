# Project 22 — Flask-SocketIO Full-Duplex Realtime Hub

Low-latency, full-duplex **WebSocket (`Flask-SocketIO`)** control and telemetry hub synchronizing physical **ESP32 / ESP8266** hardware interrupt (`ISR`) buttons and dual relays with multiple concurrent web browsers in `< 50 ms` with **zero HTTP polling**.

---

## 1. Architecture Overview

```text
+-----------------------------------+    Persistent RFC-6455 WS     +-----------------------------------+
|  ESP32 / ESP8266 Realtime Node    | <===========================> |  Flask-SocketIO Hub (app.py)      |
|  - IRAM_ATTR Falling-Edge ISRs    |  42["device_state_update",..] |  - Thread-Safe State Mutex        |
|  - 35 ms Debounce Blanking        |  42["mcu_set_relay",..]       |  - Multi-Client Broadcast Rooms   |
|  - Dual Relay Outputs (GPIO26/27) |                               |  - RTT Ping/Pong Instrumentation  |
+-----------------------------------+                               +-----------------+-----------------+
                                                                                      |
                                                                       Socket.IO v4   | < 50 ms Push
                                                                                      v
                                                                    +-----------------------------------+
                                                                    |  Multi-Client Browser UI          |
                                                                    |  - Instant Relay State Sync       |
                                                                    |  - Live WebSocket Frame Inspector |
                                                                    |  - Sub-ms RTT Latency Badge       |
                                                                    +-----------------------------------+
```

---

## 2. Quick Start

### 2.1 Configure & Upload Firmware
1. Copy `firmware/socketio_realtime_node/secrets.example.h` to `firmware/socketio_realtime_node/secrets.h` and configure `WIFI_SSID`, `WIFI_PASSWORD`, and `WS_HUB_HOST`.
2. Compile and upload to ESP32:
```bash
arduino-cli lib install "WebSockets"
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/socketio_realtime_node
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/socketio_realtime_node
```

### 2.2 Launch the Flask-SocketIO Hub
```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

python3 app.py --port-http 5003
```

Open `http://localhost:5003` across two or more browser tabs. Toggling a relay or clicking **Simulate MCU ISR Button** in one tab updates every connected client instantaneously over WebSockets.

---

## 3. CLI Verification

```bash
# Query current synchronized hub state
curl -s http://localhost:5003/api/state | python3 -m json.tool

# Simulate an ESP32 hardware interrupt button press on Channel 1 (broadcasts over WebSockets)
curl -s -X POST http://localhost:5003/api/simulate/hw_button \
  -H "Content-Type: application/json" \
  -d '{"channel": 1}' | python3 -m json.tool
```
