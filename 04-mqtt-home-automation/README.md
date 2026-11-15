# 04 - MQTT Home Automation Hub

**Level:** Intermediate  
**Board:** ESP32 & ESP8266 (Otomatis menyesuaikan melalui preprocessor `#ifdef`)

---

## Deskripsi Proyek
Proyek keempat ini membangun **Node Otomasi Rumah Pintar berbasis MQTT (*Publish-Subscribe*)** yang menggabungkan pembacaan sensor lingkungan (**DHT22**), deteksi gerakan manusia (**PIR HC-SR501**), dan kontrol aktuator lampu (**Modul Relay** + tombol fisik lokal).

Berbeda dengan HTTP polling yang berat, arsitektur **MQTT** memungkinkan komunikasi dua arah yang sangat ringan, *real-time*, dan mudah diintegrasikan dengan platform *Smart Home* populer seperti **Home Assistant**, **Node-RED**, maupun aplikasi MQTT di smartphone.

---

## Fitur Utama
- **Dual-Architecture Ready:** Kode yang sama dapat langsung di-*compile* untuk **ESP32** maupun **ESP8266 (NodeMCU / Wemos D1 Mini)**.
- **Non-Blocking Auto-Reconnect:** Tetap responsif membaca tombol fisik dinding dan sensor gerak PIR meskipun koneksi Wi-Fi atau MQTT Broker sedang terputus sementara.
- **Last Will and Testament (LWT):** Menggunakan fitur LWT pada topik `home/node01/status` (`"online"` / `"offline"`, *retained*) sehingga dashboard selalu mengetahui apakah node sedang aktif atau mati listrik.
- **Multi-Format Command Parser:** Topik kontrol `home/node01/relay/set` mendukung perintah teks sederhana (`"ON"`, `"OFF"`, `"TOGGLE"`) maupun payload JSON (`{"state":"ON"}`).
- **Two-Way State Synchronization:** Menekan tombol fisik (*Manual Override*) secara langsung mempublikasikan perubahan status ke `home/node01/relay/state` (*retained*) agar tampilan saklar di Home Assistant / Node-RED tetap sinkron.

---

## Struktur File Proyek

```text
04-mqtt-home-automation/
├── README.md                              # Dokumentasi utama proyek ini
├── src/
│   └── mqtt_home_automation/
│       ├── mqtt_home_automation.ino       # Sketch utama Arduino (ESP32 & ESP8266)
│       └── secrets.example.h              # Template konfigurasi Wi-Fi & MQTT Broker
├── schematics/
│   ├── diagram.json                       # File rangkaian simulasi Wokwi
│   └── wiring-guide.md                    # Tabel pinout ESP32/ESP8266 & diagram topologi MQTT
└── docs/
    └── mqtt-topics-and-nodered.md         # Spesifikasi topik, Home Assistant YAML & Node-RED flow
```

---

## Persiapan Library di Arduino IDE

Instal library berikut melalui **Library Manager** (`Sketch > Include Library > Manage Libraries...`):
1. **PubSubClient** oleh *Nick O'Leary*
2. **DHT sensor library** oleh *Adafruit* (beserta **Adafruit Unified Sensor**)

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/mqtt_home_automation/`](./src/mqtt_home_automation/) lalu salin file [`secrets.example.h`](./src/mqtt_home_automation/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/mqtt_home_automation/secrets.example.h src/mqtt_home_automation/secrets.h
   ```
2. Edit `secrets.h` dan isi `WIFI_SSID`, `WIFI_PASSWORD`, serta alamat `MQTT_BROKER` (gunakan IP broker Mosquitto lokal Anda atau biarkan `broker.emqx.io` untuk pengujian cepat).
3. Buka file [`mqtt_home_automation.ino`](./src/mqtt_home_automation/mqtt_home_automation.ino) di **Arduino IDE**.
4. Pilih board yang sesuai di menu `Tools > Board`:
   - **ESP32:** `DOIT ESP32 DEVKIT V1`
   - **ESP8266:** `NodeMCU 1.0 (ESP-12E Module)` atau `LOLIN(WEMOS) D1 R2 & mini`
5. Rangkai komponen sesuai panduan di [`schematics/wiring-guide.md`](./schematics/wiring-guide.md).
6. Klik **Upload**, lalu buka **Serial Monitor** pada baud rate **`115200`**.
7. Uji pengiriman perintah dan pemantauan topik menggunakan panduan di [`docs/mqtt-topics-and-nodered.md`](./docs/mqtt-topics-and-nodered.md).

---

## Cara Menjalankan di Simulator Wokwi

1. Tanpa perlu membuat `secrets.h`, sketch secara otomatis terhubung ke jaringan virtual `Wokwi-GUEST` dan broker publik `broker.emqx.io:1883`.
2. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau gunakan ekstensi **Wokwi for VS Code**.
3. Salin isi [`mqtt_home_automation.ino`](./src/mqtt_home_automation/mqtt_home_automation.ino) ke editor kode utama, dan tambahkan library `PubSubClient` serta `DHT sensor library` di tab **Library Manager** Wokwi.
4. Salin isi [`schematics/diagram.json`](./schematics/diagram.json) ke tab `diagram.json` lalu jalankan simulasi.
5. Gunakan aplikasi **MQTT Explorer** atau perintah `mosquitto_pub -h broker.emqx.io -t "home/node01/relay/set" -m "ON"` dari terminal komputer Anda untuk menyalakan lampu di dalam simulator secara langsung!
