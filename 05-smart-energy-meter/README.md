# 05 - Smart Energy Meter (Monitoring Daya Listrik & Biaya PLN)

**Level:** Intermediate  
**Board:** ESP32 (HardwareSerial UART2 + I2C OLED SSD1306)

---

## Deskripsi Proyek
Proyek kelima ini membangun **Smart Energy Meter (Alat Pemantau Konsumsi Listrik Pintar)** menggunakan mikrokontroler **ESP32** dan modul sensor daya **Peacefair PZEM-004T v3.0**. Alat ini membaca parameter kelistrikan AC 1-Fasa secara lengkap, menghitung estimasi biaya tagihan listrik dalam **Rupiah (PLN)**, menampilkannya pada layar **OLED SSD1306**, serta mengirimkan rekaman datanya secara berkala ke *Time-Series Database* **InfluxDB v2** untuk divisualisasikan pada **Grafana Dashboard**.

Agar aman dan mudah dipelajari tanpa harus langsung berhadapan dengan tegangan tinggi **AC 220V**, proyek ini juga dilengkapi **Mode Simulasi Realistis (`SIMULATE_PZEM`)** yang memungkinkan Anda mengatur naik-turunnya beban arus listrik menggunakan potensiometer di simulator **Wokwi** maupun breadboard.

---

## Fitur Utama
- **6-in-1 Power Telemetry:** Membaca **Tegangan (V)**, **Arus (A)**, **Daya Aktif (W)**, **Energi Terpakai (kWh)**, **Frekuensi (Hz)**, dan **Power Factor ($\cos \varphi$)** melalui komunikasi Modbus-RTU pada `HardwareSerial` UART2 (`GPIO 16` & `GPIO 17`).
- **Kalkulator Tagihan Listrik PLN:** Otomatis menghitung estimasi biaya pemakaian listrik dalam **Rupiah (Rp)** berdasarkan tarif PLN per kWh yang dapat disesuaikan (default Golongan R-1 Nonsubsidi `Rp 1.444,70 / kWh`).
- **Dual Mode (Hardware PZEM-004T & Simulasi Wokwi):** Cukup ubah makro `#define SIMULATE_PZEM` antara `true` (simulasi potensiometer `GPIO 34`) atau `false` (sensor fisik PZEM-004T v3.0).
- **Layar OLED Lokal (SSD1306 I2C):** Menampilkan Watt, Volt, Ampere, Power Factor, kWh, dan estimasi Rupiah secara *real-time* meskipun internet sedang mati.
- **Native InfluxDB v2 Line Protocol:** Mengirimkan data langsung ke endpoint HTTP API `/api/v2/write` InfluxDB v2 tanpa ketergantungan library eksternal yang berat.

---

## Struktur File Proyek

```text
05-smart-energy-meter/
├── README.md                              # Dokumentasi utama proyek ini
├── src/
│   └── smart_energy_meter/
│       ├── smart_energy_meter.ino         # Sketch utama ESP32 (PZEM-004T / Simulasi + InfluxDB)
│       └── secrets.example.h              # Template konfigurasi Wi-Fi & InfluxDB v2
├── schematics/
│   ├── diagram.json                       # File rangkaian simulasi Wokwi (ESP32 + OLED + Potensiometer)
│   └── wiring-guide.md                    # Panduan pengkabelan AC 220V & UART PZEM-004T yang aman
└── docs/
    └── influxdb-and-grafana-setup.md      # Docker Compose InfluxDB+Grafana & contoh query Flux
```

---

## Persiapan Library di Arduino IDE

Instal library berikut melalui **Library Manager** (`Sketch > Include Library > Manage Libraries...`):
1. **Adafruit SSD1306** oleh *Adafruit*
2. **Adafruit GFX Library** oleh *Adafruit*
3. **PZEM004Tv30** oleh *Jakub Mandula* *(diperlukan saat menggunakan modul fisik PZEM-004T dengan `SIMULATE_PZEM = false`)*

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/smart_energy_meter/`](./src/smart_energy_meter/) lalu salin file [`secrets.example.h`](./src/smart_energy_meter/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/smart_energy_meter/secrets.example.h src/smart_energy_meter/secrets.h
   ```
2. Edit `secrets.h` dan masukkan kredensial Wi-Fi serta `INFLUXDB_URL`, `INFLUXDB_TOKEN`, `INFLUXDB_ORG`, dan `INFLUXDB_BUCKET` sesuai panduan di [`docs/influxdb-and-grafana-setup.md`](./docs/influxdb-and-grafana-setup.md).
3. Buka [`smart_energy_meter.ino`](./src/smart_energy_meter/smart_energy_meter.ino) di **Arduino IDE**:
   - Jika menggunakan modul fisik **PZEM-004T v3.0**, ubah `#define SIMULATE_PZEM false` pada baris konfigurasi.
   - Jika ingin menguji tanpa beban AC 220V, biarkan `#define SIMULATE_PZEM true`.
4. Pilih board **ESP32** (`DOIT ESP32 DEVKIT V1`), hubungkan rangkaian sesuai [`schematics/wiring-guide.md`](./schematics/wiring-guide.md), lalu klik **Upload**.
5. Buka **Serial Monitor** pada baud rate **`115200`** untuk melihat pembacaan daya dan status pengiriman ke InfluxDB.

---

## Cara Menjalankan di Simulator Wokwi

1. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau gunakan ekstensi **Wokwi for VS Code**.
2. Salin isi [`smart_energy_meter.ino`](./src/smart_energy_meter/smart_energy_meter.ino) ke tab sketch utama, dan tambahkan library `Adafruit SSD1306` serta `Adafruit GFX Library` di tab **Library Manager** Wokwi.
3. Salin isi [`schematics/diagram.json`](./schematics/diagram.json) ke tab `diagram.json` lalu jalankan simulasi.
4. Putar knob **Potensiometer** di layar simulasi Wokwi untuk melihat perubahan arus (`A`), daya aktif (`Watt`), akumulasi energi (`kWh`), dan tagihan listrik (`Rp`) secara langsung di layar OLED maupun Serial Monitor!
