# 07 - OTA (Over-The-Air) Firmware Update & Fleet Manager

**Level:** Advance  
**Board:** ESP32 / ESP8266

## Deskripsi Proyek
Sistem pembaruan *firmware* nirkabel (*Over-The-Air*) baik melalui jaringan lokal (`ArduinoOTA`) maupun penarikan otomatis biner `.bin` terbaru dari server cloud / GitHub Releases melalui HTTPS.

## Tech Stack & Komponen
- **Hardware:** ESP32 / ESP8266.
- **Software & Library:** `ArduinoOTA`, `HTTPUpdate` / `ESPhttpUpdate`, `WiFiClientSecure`, Semantic Versioning.

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`)
- `schematics/` — Diagram alur proses OTA update
- `docs/` — Log proses update firmware & manajemen versi
