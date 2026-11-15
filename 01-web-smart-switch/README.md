# 01 - Web-Controlled Smart Switch (Local Web Server)

**Level:** Easy  
**Board:** ESP32 & ESP8266 (Otomatis menyesuaikan melalui preprocessor `#ifdef`)

---

## Deskripsi Proyek
Proyek pertama ini membangun **Saklar Pintar 2-Channel (Smart Switch)** yang dapat dikontrol melalui:
1. **Dashboard Web Modern (Responsive Dark UI)** yang di-*host* langsung dari memori Flash (`PROGMEM`) mikrokontroler tanpa perlu koneksi internet eksternal.
2. **Tombol Fisik (Push Button)** dengan *software debounce* sehingga saklar tetap bisa dinyalakan/dimatikan secara manual layaknya saklar dinding sungguhan, dan statusnya otomatis tersinkronisasi ke halaman web secara *real-time*.
3. **REST API JSON Lokal** (`/api/status`, `/api/toggle`, `/api/all`) untuk integrasi dengan skrip atau sistem lain.

---

## Fitur Utama
- **Dual-Architecture Ready:** Kode yang sama dapat langsung di-*compile* untuk **ESP32** (`WebServer.h`) maupun **ESP8266** (`ESP8266WebServer.h`).
- **Tanpa Reload Halaman (AJAX / Fetch API):** Menekan tombol di web langsung memicu perubahan relay tanpa memuat ulang seluruh halaman HTML.
- **Live Device Telemetry:** Menampilkan alamat IP perangkat, kekuatan sinyal Wi-Fi (`RSSI` dalam dBm), waktu nyala (`Uptime`), dan sisa memori RAM (`Free Heap`).
- **Hemat RAM:** Seluruh halaman HTML, CSS, dan JavaScript disimpan di memori Flash menggunakan `PROGMEM`.

---

## Struktur File Proyek

```text
01-web-smart-switch/
├── README.md                          # Dokumentasi utama proyek ini
├── src/
│   └── web_smart_switch/
│       ├── web_smart_switch.ino       # Sketch utama Arduino (ESP32 & ESP8266)
│       ├── webpage.h                  # Kode HTML/CSS/JS Dashboard (PROGMEM)
│       └── secrets.example.h          # Template konfigurasi SSID & Password Wi-Fi
├── schematics/
│   ├── diagram.json                   # File rangkaian simulasi Wokwi
│   └── wiring-guide.md                # Tabel pinout ESP32/ESP8266 & diagram wiring
└── docs/
    └── api-and-serial-output.md       # Dokumentasi REST API & contoh log Serial Monitor
```

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/web_smart_switch/`](./src/web_smart_switch/) lalu salin file [`secrets.example.h`](./src/web_smart_switch/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/web_smart_switch/secrets.example.h src/web_smart_switch/secrets.h
   ```
2. Edit `secrets.h` dan masukkan nama Wi-Fi (`WIFI_SSID`) serta kata sandi (`WIFI_PASSWORD`) rumah/hotspot kamu.
3. Buka file [`web_smart_switch.ino`](./src/web_smart_switch/web_smart_switch.ino) di **Arduino IDE**.
4. Pilih board yang sesuai di menu `Tools > Board`:
   - Untuk **ESP32**: Pilih `DOIT ESP32 DEVKIT V1` (atau varian ESP32 kamu).
   - Untuk **ESP8266**: Pilih `NodeMCU 1.0 (ESP-12E Module)` atau `LOLIN(WEMOS) D1 R2 & mini`.
5. Hubungkan rangkaian sesuai panduan di [`schematics/wiring-guide.md`](./schematics/wiring-guide.md).
6. Klik **Upload**, lalu buka **Serial Monitor** pada baud rate **`115200`**.
7. Salin alamat IP yang muncul di Serial Monitor (contoh: `http://192.168.1.42`) ke browser HP atau laptop yang terhubung ke jaringan Wi-Fi yang sama.

---

## Cara Menjalankan di Simulator Wokwi

1. Tanpa membuat `secrets.h`, sketch secara otomatis menggunakan jaringan virtual `Wokwi-GUEST`.
2. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau gunakan ekstensi **Wokwi for VS Code**.
3. Salin isi [`web_smart_switch.ino`](./src/web_smart_switch/web_smart_switch.ino) dan buat tab baru bernama `webpage.h` dengan isi dari [`webpage.h`](./src/web_smart_switch/webpage.h).
4. Salin isi [`schematics/diagram.json`](./schematics/diagram.json) ke tab `diagram.json` di Wokwi, lalu jalankan simulasi.
