# 02 - Real-Time Weather & Room Monitor (OLED + Cloud Telemetry)

**Level:** Easy – Intermediate  
**Board:** ESP32 & ESP8266 (Otomatis menyesuaikan melalui preprocessor `#ifdef`)

---

## Deskripsi Proyek
Proyek kedua ini membangun **Stasiun Pemantau Suhu & Kelembapan Ruangan Real-Time** yang menggabungkan tampilan lokal dan pelaporan Cloud:
1. **Akuisisi Sensor Presisi:** Membaca suhu (`°C`), kelembapan relatif (`%`), serta menghitung *Heat Index* (suhu yang dirasakan tubuh manusia) menggunakan sensor **DHT22** (disertai opsi kode untuk sensor **BME280**).
2. **Layar Lokal OLED 0.96" SSD1306 (I2C):** Menampilkan angka suhu & kelembapan berukuran besar, status kenyamanan ruangan (`NYAMAN`, `PANAS/LEMBAP`, `SEJUK/DINGIN`), kekuatan sinyal Wi-Fi (`RSSI`), dan status pengiriman Cloud secara *real-time*.
3. **Sinkronisasi Cloud Non-Blocking (`millis()`):** Mengirimkan data telemetri setiap 15 detik ke **ThingSpeak** dan/atau endpoint HTTP JSON generik (seperti **ThingsBoard** atau server webhook pribadi) tanpa menghentikan pembaruan layar OLED.

---

## Fitur Utama
- **Dual-Architecture Ready:** Satu kode sumber `.ino` dapat langsung di-*compile* untuk **ESP32** (`WiFi.h`, `HTTPClient.h`) maupun **ESP8266** (`ESP8266WiFi.h`, `ESP8266HTTPClient.h`).
- **Tampilan Informatif di Layar OLED 128x64:** Tetap berfungsi sebagai termometer & higrometer digital mandiri meskipun sedang tidak terhubung ke internet (*offline resilience*).
- **Perhitungan Heat Index & Indeks Kenyamanan:** Memberikan informasi praktis apakah kondisi kamar atau ruang kerja sedang ideal, terlalu lembap, atau panas.
- **Siap Simulasi Wokwi:** Dilengkapi file `diagram.json` sehingga seluruh fitur (termasuk layar OLED dan penggeser suhu/kelembapan DHT22) dapat langsung diuji tanpa hardware fisik.

---

## Struktur File Proyek

```text
02-weather-room-monitor/
├── README.md                              # Dokumentasi utama proyek ini
├── src/
│   └── weather_room_monitor/
│       ├── weather_room_monitor.ino       # Sketch utama Arduino (ESP32 & ESP8266)
│       └── secrets.example.h              # Template konfigurasi Wi-Fi & API Key ThingSpeak
├── schematics/
│   ├── diagram.json                       # File rangkaian simulasi Wokwi (ESP32 + DHT22 + OLED)
│   └── wiring-guide.md                    # Tabel pinout ESP32/ESP8266, diagram I2C & troubleshooting
└── docs/
    └── cloud-and-serial-output.md         # Contoh output Serial Monitor, layout OLED & payload Cloud
```

---

## Library yang Wajib Diinstal di Arduino IDE

Buka **Arduino IDE**, masuk ke menu **Sketch > Include Library > Manage Libraries...** (atau ikon *Library Manager* di sisi kiri), lalu cari dan instal 3 library berikut:

1. **`DHT sensor library`** oleh **Adafruit** *(Saat muncul pop-up instalasi dependensi `Adafruit Unified Sensor`, pilih **Install All**)*.
2. **`Adafruit SSD1306`** oleh **Adafruit**.
3. **`Adafruit GFX Library`** oleh **Adafruit**.

*(Opsional: Jika menggunakan sensor BME280, instal juga `Adafruit BME280 Library`.)*

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/weather_room_monitor/`](./src/weather_room_monitor/) lalu salin file [`secrets.example.h`](./src/weather_room_monitor/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/weather_room_monitor/secrets.example.h src/weather_room_monitor/secrets.h
   ```
2. Edit `secrets.h` dan isi:
   - `WIFI_SSID` dan `WIFI_PASSWORD` dengan jaringan Wi-Fi kamu.
   - `THINGSPEAK_API_KEY` dengan *Write API Key* dari channel ThingSpeak kamu (atau kosongkan jika hanya ingin menggunakan layar OLED).
3. Buka file [`weather_room_monitor.ino`](./src/weather_room_monitor/weather_room_monitor.ino) di **Arduino IDE**.
4. Pilih board yang sesuai di menu `Tools > Board`:
   - Untuk **ESP32**: Pilih `DOIT ESP32 DEVKIT V1` (atau varian ESP32 kamu).
   - Untuk **ESP8266**: Pilih `NodeMCU 1.0 (ESP-12E Module)` atau `LOLIN(WEMOS) D1 R2 & mini`.
5. Rangkai komponen sesuai tabel pin di [`schematics/wiring-guide.md`](./schematics/wiring-guide.md).
6. Klik **Upload**, lalu buka **Serial Monitor** pada baud rate **`115200`**.

---

## Cara Menjalankan di Simulator Wokwi

1. Tanpa membuat `secrets.h`, sketch secara otomatis terhubung ke jaringan virtual `Wokwi-GUEST`.
2. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau gunakan ekstensi **Wokwi for VS Code**.
3. Salin kode dari [`weather_room_monitor.ino`](./src/weather_room_monitor/weather_room_monitor.ino) ke editor utama.
4. Di tab **Library Manager** Wokwi, tambahkan `DHT sensor library`, `Adafruit SSD1306`, dan `Adafruit GFX Library`.
5. Salin isi [`schematics/diagram.json`](./schematics/diagram.json) ke tab `diagram.json` di Wokwi, lalu jalankan simulasi. Klik komponen **DHT22** saat simulasi berjalan untuk mengubah nilai suhu dan kelembapan secara interaktif.
