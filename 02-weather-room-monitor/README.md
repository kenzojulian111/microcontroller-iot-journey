# 02 - Real-Time Weather & Room Monitor

**Level:** Easy  
**Board:** ESP8266 atau ESP32

## Deskripsi Proyek
Membaca data suhu dan kelembapan udara menggunakan sensor DHT22 atau BME280, menampilkannya secara lokal ke layar OLED SSD1306 melalui protokol I2C, sekaligus mengirimkannya ke *dashboard* cloud.

## Tech Stack & Komponen
- **Hardware:** ESP8266 / ESP32, Sensor DHT22 atau BME280, Layar OLED 0.96" SSD1306 (I2C).
- **Software & Library:** `Wire.h`, `Adafruit_SSD1306`, `Adafruit_Sensor`, `DHT` / `Adafruit_BME280`, HTTP Client (ThingSpeak / Blynk / ThingsBoard).

## Struktur Folder
- `src/` — Kode program Arduino (`.ino`)
- `schematics/` — Diagram pengkabelan (*wiring*)
- `docs/` — Screenshot OLED & dashboard cloud
