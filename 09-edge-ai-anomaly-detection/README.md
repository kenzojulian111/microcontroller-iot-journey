# 09 - Edge AI Anomaly Detection (TinyML)

**Level:** Advance  
**Board:** ESP32

## Deskripsi Proyek
Menjalankan model *machine learning* ringan (*TinyML*) langsung di dalam mikrokontroler ESP32 untuk mendeteksi anomali getaran pada mesin/kipas secara *real-time* di *edge* tanpa bergantung pada koneksi internet.

## Tech Stack & Komponen
- **Hardware:** ESP32, Sensor Accelerometer & Gyroscope MPU6050 (I2C).
- **Software & Library:** `Adafruit_MPU6050`, Edge Impulse SDK / TensorFlow Lite for Microcontrollers.

## Struktur Folder
- `src/` — Kode program akuisisi data & inferensi model Arduino (`.ino`)
- `schematics/` — Diagram pengkabelan sensor MPU6050
- `docs/` — Akurasi model, spektrum FFT, & hasil inferensi di Serial Monitor
