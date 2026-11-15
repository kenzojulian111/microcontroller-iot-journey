# Project 23: Smart Access Control & SQLAlchemy Attendance System

**Track III — Phase 7: Full-Stack Hybrid MCU + Python Flask ORM**

A full-stack physical access control and time-attendance system pairing an **ESP32 + MFRC522 SPI RFID Reader + SSD1306 I2C OLED + Door Strike Relay** with a **Python Flask + SQLAlchemy ORM** backend.

---

## Key Capabilities

1. **Edge RFID Terminal (`firmware/rfid_attendance_node/rfid_attendance_node.ino`):**
   - Reads ISO/IEC 14443A 13.56 MHz tags via SPI (`MFRC522`) or simulated Wokwi pushbuttons (`GPIO14` Valid / `GPIO12` Rogue) and UART injection (`UID:04:A3:9B:12`).
   - Transmits `POST /api/v1/attendance/punch` with UID, terminal ID, and Wi-Fi RSSI.
   - Parses the Flask JSON decision, displays the employee's name, role, action (`CHECK_IN` / `CHECK_OUT` / `DENIED`), and shift hours on a **128x64 SSD1306 OLED**, energizes the solenoid door lock relay for 3 seconds, and plays distinct acoustic chimes on the piezo buzzer.
   - Includes a deterministic offline edge verifier so Wokwi simulations work seamlessly even without a local bridge.
2. **Relational SQLAlchemy Backend (`backend/app.py`):**
   - Models `Employee` and `AttendanceLog` tables with automatic stateful `CHECK_IN` $\leftrightarrow$ `CHECK_OUT` toggling and shift duration computation.
   - Provides REST endpoints for badge enrollment (`POST /api/v1/employees`), instant credential revocation (`PATCH /api/v1/employees/<id>/toggle`), and CSV audit report export (`GET /api/v1/attendance/export.csv`).
3. **Interactive Operator Console (`backend/templates/index.html`):**
   - Live KPI cards, one-click hardware terminal simulation with a virtual 128x64 OLED preview, badge enrollment form, and auto-refreshing attendance audit ledger.

---

## Directory Structure

```text
23-flask-rfid-attendance-system/
├── README.md
├── firmware/
│   └── rfid_attendance_node/
│       ├── rfid_attendance_node.ino
│       └── secrets.example.h
├── backend/
│   ├── app.py
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

### 1. Launch the Flask + SQLAlchemy Backend

```bash
cd 23-flask-rfid-attendance-system/backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 app.py
```

The server initializes `attendance.db` with seeded engineering personnel and listens on `http://0.0.0.0:5023`.

### 2. Test the API via CLI or Dashboard

Open `http://localhost:5023` in your browser, or test the punch endpoint directly:

```bash
curl -X POST http://localhost:5023/api/v1/attendance/punch \
  -H "Content-Type: application/json" \
  -d '{"uid": "04:A3:9B:12", "terminal_id": "GATE-01", "rssi": -48}'
```

### 3. Run the Firmware in Wokwi or on ESP32

1. Copy `firmware/rfid_attendance_node/secrets.example.h` to `secrets.h` and set `FLASK_HOST` to your computer's LAN IP.
2. Open `schematics/diagram.json` in Wokwi or flash `rfid_attendance_node.ino` to an ESP32.
3. Press the **Green Pushbutton (`GPIO14`)** to simulate authorized employee card taps (`CHECK_IN` / `CHECK_OUT`) or the **Red Pushbutton (`GPIO12`)** to trigger an unauthorized `DE:AD:BE:EF` security alert.
