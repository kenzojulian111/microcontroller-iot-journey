# Project 20 — Flask Serial Sensor Logger, SQLite Historian & CSV Exporter

Continuous environmental data acquisition pipeline streaming **NDJSON** sensor telemetry (`DHT22` + `16x` oversampled `LDR` ADC) over UART into a background Python daemon thread backed by **SQLite3 in Write-Ahead Logging (WAL)** mode and streaming **RFC-4180 CSV** exports.

---

## 1. Architecture Overview

```text
+-----------------------------------+       2.0 s NDJSON Stream    +-----------------------------------+
|  ATmega328P / ESP32 Sensor Node   | ===========================> |  SerialTelemetryIngestor Thread   |
|  - DHT22 Temp & Humidity          |  {"seq":42,"temp_c":26.4,..} |  - Background Daemon Thread       |
|  - 16x Oversampled LDR ADC        |                              |  - Auto-Fallback Synthetic Stream |
+-----------------------------------+                              +-----------------+-----------------+
                                                                                     |
                                                                      SQLite3 INSERT | (WAL Journal Mode)
                                                                                     v
+-----------------------------------+       HTTP REST + CSV        +-----------------------------------+
|  Real-Time Web Historian UI       | <==========================> |  Flask Application + SQLite3 DB   |
|  - Min / Max / Mean KPI Cards     |   GET /api/telemetry/latest  |  - Indexed Time-Series Queries    |
|  - Live Rolling Table             |   GET /api/telemetry/history |  - O(1) Memory Generator CSV      |
|  - 1-Click RFC-4180 CSV Download  |   GET /api/export/csv        |                                   |
+-----------------------------------+                              +-----------------------------------+
```

---

## 2. Quick Start

### 2.1 Flash the Firmware
```bash
arduino-cli lib install "DHT sensor library" "Adafruit Unified Sensor"
arduino-cli compile --fqbn arduino:avr:uno firmware/serial_sensor_logger
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno firmware/serial_sensor_logger
```

### 2.2 Start the Backend Server
```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# Connect to physical Arduino/ESP32 (or auto-fallback to synthetic telemetry)
python3 app.py --port /dev/ttyACM0

# Or force synthetic sensor telemetry mode explicitly:
python3 app.py --mock
```

Navigate to `http://localhost:5001` in your browser.

---

## 3. CLI Verification via `curl`

```bash
# Fetch latest sensor sample & min/max/avg statistics
curl -s http://localhost:5001/api/telemetry/latest | python3 -m json.tool

# Fetch last 5 historical records
curl -s "http://localhost:5001/api/telemetry/history?limit=5" | python3 -m json.tool

# Download RFC-4180 CSV export
curl -s http://localhost:5001/api/export/csv | head -n 10
```
