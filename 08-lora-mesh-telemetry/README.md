# 08 - LoRa Mesh Environmental Telemetry (Long-Range IoT)

**Level:** Advance  
**Board:** ESP32 (Sensor Nodes & Gateway)

## Deskripsi Proyek
Mengirim data telemetri lingkungan (misalnya deteksi dini banjir atau kualitas udara) dari area tanpa jangkauan Wi-Fi/seluler menuju *gateway* utama sejauh beberapa kilometer menggunakan modul radio LoRa.

## Tech Stack & Komponen
- **Hardware:** 2x atau lebih ESP32, Modul LoRa SX1278 (433MHz) / RFM95 (915MHz), Sensor Lingkungan.
- **Software & Library:** `LoRa` by Sandeep Mistry / `RadioHead`, paket routing Mesh / Gateway-to-Cloud forwarding.

## Struktur Folder
- `src/` — Kode program Arduino untuk Node & Gateway (`.ino`)
- `schematics/` — Diagram pengkabelan SPI LoRa & arsitektur jaringan
- `docs/` — Pengujian jarak (*range test*), RSSI/SNR, & tampilan data
