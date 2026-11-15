# 05 - Smart Energy Meter (Monitoring Daya Listrik)

**Level:** Intermediate  
**Board:** ESP32

## Deskripsi Proyek
Mengukur tegangan (V), arus (A), daya aktif (W), faktor daya (PF), dan konsumsi energi (kWh) peralatan rumah tangga secara *real-time* lalu mencatatnya ke dalam *time-series database* untuk divisualisasikan.

## Tech Stack & Komponen
- **Hardware:** ESP32, Sensor PZEM-004T v3.0 (UART Modbus-RTU) atau ACS712.
- **Software & Library:** `PZEM004Tv30`, InfluxDB Client, Grafana Dashboard.

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`)
- `schematics/` — Diagram pengkabelan (*wiring*) AC/DC yang aman
- `docs/` — Screenshot grafik pemakaian listrik di Grafana
