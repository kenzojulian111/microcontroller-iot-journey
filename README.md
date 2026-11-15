<<<<<<< HEAD
# Embedded IoT Lab (`IoT_lab`)

Repositori portofolio dan laboratorium pembelajaran **Internet of Things (IoT)** berbasis mikrokontroler (**ESP32** & **ESP8266**) menggunakan **Arduino IDE**. Proyek disusun secara bertahap dari tingkat pemula (*Easy*), menengah (*Intermediate*), hingga mahir (*Advance*).

---

## Daftar Proyek & Roadmap

### Level 1: Easy (Dasar Sensor, Aktuator & Wi-Fi)
Fokus pada pembacaan data sensor, kontrol aktuator via jaringan lokal, dan pengiriman data sederhana.

| No | Proyek | Board Rekomendasi | Tech Stack & Protokol | Status |
| :--- | :--- | :--- | :--- | :---: |
| **01** | [Web-Controlled Smart Switch](./01-web-smart-switch/) | ESP8266 / ESP32 | C++ (Arduino), HTML/CSS, `WebServer` / `ESP8266WebServer` | Completed |
| **02** | [Real-Time Weather & Room Monitor](./02-weather-room-monitor/) | ESP8266 / ESP32 | I2C, DHT22/BME280, OLED SSD1306, Blynk / ThingSpeak | Planned |
| **03** | [Smart Plant Watering System](./03-smart-plant-watering/) | ESP8266 / ESP32 | ADC (Soil Moisture), Relay, HTTP POST, Telegram Bot / Discord Webhook | Planned |

### Level 2: Intermediate (Protokol IoT, Database & Integrasi)
Fokus pada protokol komunikasi standar industri (MQTT), penyimpanan data historis, dan *dashboard* kustom.

| No | Proyek | Board Rekomendasi | Tech Stack & Protokol | Status |
| :--- | :--- | :--- | :--- | :---: |
| **04** | [MQTT Home Automation Hub](./04-mqtt-home-automation/) | ESP32 + ESP8266 | MQTT (Mosquitto), `PubSubClient`, Node-RED / Home Assistant | Planned |
| **05** | [Smart Energy Meter](./05-smart-energy-meter/) | ESP32 | UART (PZEM-004T) / ADC (ACS712), InfluxDB, Grafana | Planned |
| **06** | [RFID / Biometric Smart Door Lock](./06-rfid-smart-door-lock/) | ESP32 | SPI (RC522) / UART (Fingerprint), REST API, PostgreSQL / Firebase | Planned |

### Level 3: Advance (Edge Computing, OTA, Keamanan & Mesh)
Fokus pada keandalan sistem skala produksi, pemrosesan di perangkat (*edge*), dan komunikasi jarak jauh tanpa Wi-Fi.

| No | Proyek | Board Rekomendasi | Tech Stack & Protokol | Status |
| :--- | :--- | :--- | :--- | :---: |
| **07** | [OTA Firmware Update & Fleet Manager](./07-ota-firmware-manager/) | ESP32 / ESP8266 | `ArduinoOTA` / `HTTPUpdate`, HTTPS, GitHub Releases | Planned |
| **08** | [LoRa Mesh Environmental Telemetry](./08-lora-mesh-telemetry/) | ESP32 (Node & Gateway) | SPI, LoRa SX1278 / RFM95, Mesh Routing, Gateway Forwarding | Planned |
| **09** | [Edge AI Anomaly Detection (TinyML)](./09-edge-ai-anomaly-detection/) | ESP32 | I2C (MPU6050), Edge Impulse, TensorFlow Lite for Microcontrollers | Planned |
| **10** | [Secure End-to-End Encrypted Telemetry](./10-secure-tls-telemetry/) | ESP32 | MQTTS (TLS/SSL), Sertifikat X.509, AWS IoT Core / HiveMQ Cloud | Planned |

---

## Struktur Direktori

Setiap proyek memiliki struktur standar agar dokumentasi, kode sumber, dan skematik mudah dijelajahi:

```text
IoT_lab/
├── README.md                          # Daftar isi semua proyek & arsitektur umum
├── 01-web-smart-switch/
│   ├── README.md                      # Penjelasan proyek, komponen, & cara kerja
│   ├── src/                           # Sketch Arduino IDE (.ino) & header pendukung
│   ├── schematics/                    # Diagram wiring (Wokwi / Fritzing / EasyEDA)
│   └── docs/                          # Screenshot dashboard, web UI, atau serial monitor
├── 02-weather-room-monitor/
│   └── ...
└── 10-secure-tls-telemetry/
    └── ...
```

---

## Persiapan Environment (Arduino IDE)

1. **Instal Arduino IDE** (versi 2.x direkomendasikan).
2. **Tambahkan Additional Boards Manager URLs** di `File > Preferences`:
   - **ESP32:** `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - **ESP8266:** `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. **Instal Board Package** melalui `Tools > Board > Boards Manager`:
   - Cari dan instal `esp32` by Espressif Systems.
   - Cari dan instal `esp8266` by ESP8266 Community.
4. **Manajemen Kredensial Aman:**
   - Simpan SSID Wi-Fi, password, dan API token di file `secrets.h` (sudah masuk di `.gitignore` agar tidak ter-*commit* ke publik) dan sediakan `secrets.example.h` sebagai template.
=======
# microcontroller-iot-journey
>>>>>>> ec69ddecb49b843b7790f0e9c4af4a2814ccc5aa
