# Project 21 — Authenticated Wi-Fi REST API Weather Station & Chart.js Telemetry

End-to-end wireless meteorological station featuring an **ESP32 / ESP8266** sensor node with an offline circular store-and-forward ring buffer, constant-time **`X-API-Key`** REST authentication, **SQLAlchemy** time-series persistence, **Magnus-Tetens dew point** analytics, and a dual-canvas **Chart.js** dashboard.

---

## 1. Architecture Overview

```text
+-----------------------------------+    HTTP POST /api/v1/telemetry   +-----------------------------------+
|  ESP32 / ESP8266 Weather Node     | ===============================> |  Flask REST API Server (app.py)   |
|  - BME280 (I2C Temp/Hum/Press)    |    Header: X-API-Key: <secret>   |  - @require_api_key (HMAC check)  |
|  - 32-Slot Offline Ring Buffer    |                                  |  - Magnus-Tetens Dew Point Math   |
|  - Auto Store-and-Forward Drain   |                                  |  - SQLAlchemy SQLite Time-Series  |
+-----------------------------------+                                  +-----------------+-----------------+
                                                                                         |
                                                                          GET /api/v1/telemetry & /analytics
                                                                                         v
                                                                       +-----------------------------------+
                                                                       |  Chart.js Telemetry Dashboard     |
                                                                       |  - Multi-Axis Temp/Dew/RH Chart   |
                                                                       |  - Barometric Pressure Area Chart |
                                                                       +-----------------------------------+
```

---

## 2. Quick Start

### 2.1 Configure & Flash Firmware
1. Copy `firmware/rest_weather_node/secrets.example.h` to `firmware/rest_weather_node/secrets.h` and set `WIFI_SSID`, `WIFI_PASSWORD`, and `API_ENDPOINT_URL`.
2. Compile and upload to ESP32:
```bash
arduino-cli lib install "Adafruit BME280 Library" "Adafruit Unified Sensor"
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/rest_weather_node
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/rest_weather_node
```

### 2.2 Run the Flask REST Server
```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# Start server (optionally pass --mock to spawn a synthetic ESP32 weather generator thread)
python3 app.py --mock
```

Open `http://localhost:5002` in your browser.

---

## 3. CLI Verification via `curl`

```bash
# 1. Test unauthorized request (returns 401 UNAUTHORIZED_API_KEY)
curl -i -X POST http://localhost:5002/api/v1/telemetry \
  -H "Content-Type: application/json" \
  -d '{"temp_c": 25.4, "hum_pct": 60.0, "press_hpa": 1013.25}'

# 2. Test authenticated ingestion with valid X-API-Key (returns 201 Created with dew_point_c)
curl -s -X POST http://localhost:5002/api/v1/telemetry \
  -H "Content-Type: application/json" \
  -H "X-API-Key: iot-lab-track3-weather-secret-key-2026" \
  -d '{"node_id": "esp32-weather-01", "seq": 1, "temp_c": 26.2, "hum_pct": 64.5, "press_hpa": 1012.8, "rssi_dbm": -55}' \
  | python3 -m json.tool

# 3. Query aggregate psychrometric analytics
curl -s http://localhost:5002/api/v1/analytics | python3 -m json.tool
```
