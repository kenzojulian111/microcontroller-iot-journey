# 03 - Smart Plant Watering System (Automated & Notified)

**Level:** Easy  
**Board:** ESP8266 atau ESP32

## Deskripsi Proyek
Mengukur kelembapan tanah menggunakan *capacitive soil moisture sensor*, menyalakan pompa air mini secara otomatis saat tanah kering, serta mengirimkan notifikasi status penyiraman ke Telegram atau Discord.

## Tech Stack & Komponen
- **Hardware:** ESP8266 / ESP32, Capacitive Soil Moisture Sensor, Modul Relay 5V, Mini Submersible Water Pump.
- **Software & Library:** `WiFiClientSecure.h`, `HTTPClient.h`, Telegram Bot API (`UniversalTelegramBot`) atau Discord Webhook via HTTP POST.

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`)
- `schematics/` — Diagram pengkabelan (*wiring*)
- `docs/` — Screenshot notifikasi Telegram/Discord & pengujian sensor
