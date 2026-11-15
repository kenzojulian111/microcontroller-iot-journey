# 06 - RFID / Biometric Smart Door Lock with Access Logs

**Level:** Intermediate  
**Board:** ESP32

## Deskripsi Proyek
Sistem kunci pintu pintar menggunakan kartu RFID atau sensor sidik jari yang memvalidasi UID/ID pengguna ke server serta mencatat riwayat akses (*access logs*) beserta waktu masuknya.

## Tech Stack & Komponen
- **Hardware:** ESP32, Modul RFID RC522 (SPI) atau Fingerprint AS608 (UART), Solenoid Door Lock 12V + Relay, Buzzer.
- **Software & Library:** `MFRC522` / `Adafruit_Fingerprint`, `HTTPClient`, `ArduinoJson`, REST API (FastAPI / Node.js) atau Firebase.

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`)
- `schematics/` — Diagram pengkabelan (*wiring*)
- `docs/` — Screenshot log akses & skema API
