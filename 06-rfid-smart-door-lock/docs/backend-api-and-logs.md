# Backend REST API (Python FastAPI + SQLite) & Contoh Log Akses

Dokumen ini menyediakan kode siap pakai untuk menjalankan server **Backend Access Log** menggunakan **Python FastAPI** dan database **SQLite**, spesifikasi payload JSON antara ESP32 dan server, serta contoh tampilan **Serial Monitor** pada **Proyek 06 (`06-rfid-smart-door-lock`)**.

---

## 1. Contoh Output Serial Monitor (`115200` baud)

Berikut contoh log pada **Serial Monitor** ketika kartu Master yang valid ditempelkan, diikuti oleh percobaan tap kartu asing yang tidak terdaftar:

```text
============================================================
  IoT Lab - 06 RFID Smart Door Lock with Access Logs        
  Device ID : door_lab_utama
============================================================
[SOLENOID] Pintu TERKUNCI kembali (Auto-Lock).
[RC522] Modul MFRC522 siap! (Firmware Version: 0x92)
[WIFI] Menghubungkan ke SSID: Wokwi-GUEST ....
[WIFI] Terhubung!
[WIFI] Alamat IP : 10.13.37.2
[NTP]  Menyinkronkan waktu dengan server NTP... Berhasil! Waktu sekarang: 2026-11-15T21:42:10+07:00
[READY] Silakan tempelkan kartu RFID ke reader atau tekan tombol simulasi.

------------------------------------------------------------
[RFID TAP] Sumber    : Tombol Simulasi (Kartu Master Valid)
[RFID TAP] UID Kartu : A1:B2:C3:D4
[RFID TAP] Waktu NTP : 2026-11-15T21:42:18+07:00
[CACHE]    Pemilik   : Budi Santoso (Admin Lab) (TERDAFTAR)
[API TX] POST http://192.168.1.50:8000/api/v1/access-log
[API TX] Payload: {"uid":"A1:B2:C3:D4","device_id":"door_lab_utama","timestamp":"2026-11-15T21:42:18+07:00","local_decision":"GRANTED","user_name":"Budi Santoso (Admin Lab)"}
[API RX] HTTP 200: {"status":"ok","authorized":true,"owner":"Budi Santoso (Admin Lab)"}
[ACCESS]   DITERIMA (SERVER_AUTHORIZED) -> Membuka kunci pintu selama 4 detik.
[SOLENOID] Pintu TERBUKA (Solenoid Energized).
------------------------------------------------------------
[SOLENOID] Pintu TERKUNCI kembali (Auto-Lock).

------------------------------------------------------------
[RFID TAP] Sumber    : Tombol Simulasi (Kartu Asing / Ditolak)
[RFID TAP] UID Kartu : DE:AD:BE:EF
[RFID TAP] Waktu NTP : 2026-11-15T21:42:29+07:00
[CACHE]    Pemilik   : Kartu Tidak Dikenal (TIDAK TERDAFTAR)
[API TX] POST http://192.168.1.50:8000/api/v1/access-log
[API TX] Payload: {"uid":"DE:AD:BE:EF","device_id":"door_lab_utama","timestamp":"2026-11-15T21:42:29+07:00","local_decision":"DENIED","user_name":"Kartu Tidak Dikenal"}
[API RX] HTTP 200: {"status":"ok","authorized":false,"owner":"Unknown"}
[ACCESS]   DITOLAK (SERVER_DENIED) -> Pintu tetap terkunci!
[SOLENOID] Pintu TERKUNCI kembali (Auto-Lock).
------------------------------------------------------------
```

---

## 2. Skema Tabel Database SQLite (`access_logs.db`)

Server backend menyimpan dua tabel utama secara otomatis:
1. **`authorized_cards`**: Menyimpan daftar UID kartu resmi beserta nama pemilik dan perannya (`ADMIN`, `STAFF`, `MEMBER`).
2. **`access_logs`**: Menyimpan seluruh riwayat percobaan akses pintu (baik yang diterima maupun yang ditolak).

```sql
CREATE TABLE IF NOT EXISTS authorized_cards (
    uid TEXT PRIMARY KEY,
    owner_name TEXT NOT NULL,
    role TEXT DEFAULT 'MEMBER',
    active INTEGER DEFAULT 1
);

CREATE TABLE IF NOT EXISTS access_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uid TEXT NOT NULL,
    device_id TEXT NOT NULL,
    timestamp TEXT NOT NULL,
    user_name TEXT,
    decision TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

---

## 3. Kode Server Backend Python FastAPI (`server.py`)

Simpan skrip berikut sebagai `server.py` di komputer/server Anda untuk menerima dan memvalidasi log akses dari ESP32:

```python
import sqlite3
from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel

API_SECRET_KEY = "iotlab-doorlock-secret-key-2026"
DB_PATH = "access_logs.db"

app = FastAPI(title="IoT Lab - Smart Door Lock API", version="1.0.0")


class AccessTapPayload(BaseModel):
    uid: str
    device_id: str
    timestamp: str
    local_decision: str
    user_name: str = "Unknown"


def init_db():
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute("""
            CREATE TABLE IF NOT EXISTS authorized_cards (
                uid TEXT PRIMARY KEY,
                owner_name TEXT NOT NULL,
                role TEXT DEFAULT 'MEMBER',
                active INTEGER DEFAULT 1
            )
        """)
        cur.execute("""
            CREATE TABLE IF NOT EXISTS access_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                uid TEXT NOT NULL,
                device_id TEXT NOT NULL,
                timestamp TEXT NOT NULL,
                user_name TEXT,
                decision TEXT NOT NULL,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        """)
        # Seed data awal sesuai Offline Cache di ESP32
        seed_cards = [
            ("A1:B2:C3:D4", "Budi Santoso (Admin Lab)", "ADMIN", 1),
            ("04:5A:8E:92", "Siti Aminah (Asisten)", "STAFF", 1),
            ("73:1F:44:0C", "Rizky Pratama (Teknisi)", "MEMBER", 1),
        ]
        cur.executemany(
            "INSERT OR IGNORE INTO authorized_cards (uid, owner_name, role, active) VALUES (?, ?, ?, ?)",
            seed_cards,
        )
        conn.commit()


@app.on_event("startup")
def on_startup():
    init_db()


@app.post("/api/v1/access-log")
def verify_and_log_access(
    payload: AccessTapPayload,
    x_api_key: str = Header(default=""),
):
    if x_api_key != API_SECRET_KEY:
        raise HTTPException(status_code=401, detail="Invalid X-API-Key")

    uid_clean = payload.uid.upper().strip()
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute(
            "SELECT owner_name, role, active FROM authorized_cards WHERE UPPER(uid) = ?",
            (uid_clean,),
        )
        row = cur.fetchone()

        if row and row[2] == 1:
            authorized = True
            owner_name = row[0]
            decision = "GRANTED"
        else:
            authorized = False
            owner_name = "Unknown"
            decision = "DENIED"

        cur.execute(
            """
            INSERT INTO access_logs (uid, device_id, timestamp, user_name, decision)
            VALUES (?, ?, ?, ?, ?)
            """,
            (uid_clean, payload.device_id, payload.timestamp, owner_name, decision),
        )
        conn.commit()

    return {
        "status": "ok",
        "authorized": authorized,
        "owner": owner_name,
        "decision": decision,
    }


@app.get("/api/v1/logs")
def list_recent_logs(limit: int = 20):
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        cur = conn.cursor()
        cur.execute(
            "SELECT * FROM access_logs ORDER BY id DESC LIMIT ?",
            (limit,),
        )
        return [dict(r) for r in cur.fetchall()]
```

### Cara Menjalankan Server FastAPI:
```bash
pip install fastapi uvicorn
uvicorn server:app --host 0.0.0.0 --port 8000
```

Setelah server aktif, Anda dapat melihat dokumentasi interaktif Swagger UI di `http://localhost:8000/docs` dan memeriksa riwayat log akses terbaru di `http://localhost:8000/api/v1/logs`.
