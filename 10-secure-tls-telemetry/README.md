# 10 - Secure End-to-End Encrypted Telemetry (TLS/SSL)

**Level:** Advance  
**Board:** ESP32

## Deskripsi Proyek
Membangun komunikasi perangkat IoT ke *cloud broker* dengan enkripsi TLS/SSL dan autentikasi sertifikat X.509 (*mTLS*) untuk mencegah penyadapan (*man-in-the-middle*) serta pemalsuan identitas perangkat.

## Tech Stack & Komponen
- **Hardware:** ESP32.
- **Software & Library:** `WiFiClientSecure`, `PubSubClient` / `MQTT`, NTP Time Sync (untuk validasi masa berlaku sertifikat), AWS IoT Core atau HiveMQ Cloud.

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`) & contoh template sertifikat (`secrets.example.h`)
- `schematics/` — Arsitektur keamanan TLS handshake
- `docs/` — Screenshot koneksi terenkripsi di AWS IoT Core / HiveMQ Cloud
