# 04 - MQTT Home Automation Hub

**Level:** Intermediate  
**Board:** ESP32 & ESP8266 (Multi-Node)

## Deskripsi Proyek
Menghubungkan beberapa *node* sensor dan saklar lampu pintar menggunakan arsitektur *Publish-Subscribe* (MQTT) agar komunikasi antar perangkat lebih ringan, cepat, dan terpusat.

## Tech Stack & Komponen
- **Hardware:** ESP32 / ESP8266, Sensor Suhu/Gerak (PIR), Modul Relay.
- **Software & Library:** `PubSubClient`, Mosquitto MQTT Broker, Node-RED atau Home Assistant.

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`)
- `schematics/` — Diagram pengkabelan (*wiring*) & topologi topik MQTT
- `docs/` — Screenshot dashboard Node-RED / Home Assistant
