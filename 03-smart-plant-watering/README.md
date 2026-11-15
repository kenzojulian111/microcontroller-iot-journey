# 03 - Smart Plant Watering System (Automated & Notified)

**Level:** Intermediate  
**Board:** ESP32 & ESP8266 (Otomatis menyesuaikan melalui preprocessor `#ifdef`)

---

## Deskripsi Proyek
Proyek ketiga ini membangun **Sistem Penyiram Tanaman Otomatis Pintar** yang menjaga kelembapan media tanam secara mandiri dan mengirimkan laporan langsung ke ponsel pengguna:
1. **Akuisisi Sensor Kelembapan Tanah dengan Moving Average Filter:** Membaca **Capacitive Soil Moisture Sensor** melalui pin ADC, meredam fluktuasi sinyal analog dengan teknik *multi-sampling* dan rata-rata bergerak (*moving average* 8 sampel), lalu memetakannya ke skala `0% – 100%`.
2. **Kontrol Pompa Berbasis Histeresis:** Mencegah relay bergetar (*chattering*) di sekitar satu titik ambang dengan memisahkan ambang batas menyala (`< 35%`) dan ambang batas mati (`>= 65%`).
3. **Proteksi Keamanan Ganda (*Anti-Overwatering & Cooldown*):** Membatasi durasi maksimum pompa menyala (`15 detik` per siklus) dan menerapkan waktu tunggu resapan air (*cooldown* `30 detik`) agar tanah tidak banjir jika air membutuhkan waktu untuk meresap ke area sensor.
4. **Notifikasi Real-Time via HTTPS:** Mengirimkan pemberitahuan otomatis ke **Telegram Bot API** dan/atau **Discord Webhook** setiap kali pompa mulai menyiram dan selesai menyiram.

---

## Fitur Utama
- **Dual-Architecture Ready:** Kode yang sama dapat langsung di-*compile* untuk **ESP32** (`GPIO 34` ADC 12-bit) maupun **ESP8266** (`A0` ADC 10-bit).
- **Pembacaan Stabil & Terkalibrasi:** Dilengkapi konstanta `ADC_DRY_VALUE` dan `ADC_WET_VALUE` yang mudah disesuaikan dengan hasil kalibrasi sensor di udara dan di dalam air.
- **Tetap Bekerja Saat Offline:** Apabila Wi-Fi rumah sedang mati, logika otomatisasi pompa dan proteksi *anti-overwatering* tetap bekerja penuh menjaga tanaman.
- **Tanpa Library Eksternal Tambahan:** Seluruh komunikasi HTTPS JSON ke Telegram dan Discord menggunakan library bawaan core board (`WiFiClientSecure` + `HTTPClient`).

---

## Struktur File Proyek

```text
03-smart-plant-watering/
├── README.md                                  # Dokumentasi utama proyek ini
├── src/
│   └── smart_plant_watering/
│       ├── smart_plant_watering.ino           # Sketch utama Arduino (ESP32 & ESP8266)
│       └── secrets.example.h                  # Template Wi-Fi, Token Bot Telegram & Discord
├── schematics/
│   ├── diagram.json                           # File rangkaian simulasi Wokwi
│   └── wiring-guide.md                        # Tabel pinout, panduan kalibrasi ADC & dioda flyback
└── docs/
    └── notification-and-calibration.md        # Panduan @BotFather Telegram, Discord & log Serial
```

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/smart_plant_watering/`](./src/smart_plant_watering/) lalu salin file [`secrets.example.h`](./src/smart_plant_watering/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/smart_plant_watering/secrets.example.h src/smart_plant_watering/secrets.h
   ```
2. Edit `secrets.h` dan masukkan:
   - `WIFI_SSID` dan `WIFI_PASSWORD` jaringan Wi-Fi kamu.
   - `TELEGRAM_BOT_TOKEN` dan `TELEGRAM_CHAT_ID` (ikuti panduan di [`docs/notification-and-calibration.md`](./docs/notification-and-calibration.md)), atau `DISCORD_WEBHOOK_URL`.
3. Buka file [`smart_plant_watering.ino`](./src/smart_plant_watering/smart_plant_watering.ino) di **Arduino IDE**.
4. Pilih board yang sesuai di menu `Tools > Board`:
   - Untuk **ESP32**: Pilih `DOIT ESP32 DEVKIT V1` (atau varian ESP32 kamu).
   - Untuk **ESP8266**: Pilih `NodeMCU 1.0 (ESP-12E Module)` atau `LOLIN(WEMOS) D1 R2 & mini`.
5. Hubungkan sensor kelembapan tanah kapasitif, modul relay, dan pompa air sesuai panduan di [`schematics/wiring-guide.md`](./schematics/wiring-guide.md).
6. Klik **Upload**, lalu buka **Serial Monitor** pada baud rate **`115200`**.

---

## Cara Menjalankan di Simulator Wokwi

1. Tanpa membuat `secrets.h`, sketch secara otomatis menggunakan jaringan virtual `Wokwi-GUEST`.
2. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau gunakan ekstensi **Wokwi for VS Code**.
3. Salin isi [`smart_plant_watering.ino`](./src/smart_plant_watering/smart_plant_watering.ino) ke editor utama.
4. Salin isi [`schematics/diagram.json`](./schematics/diagram.json) ke tab `diagram.json` di Wokwi, lalu jalankan simulasi.
5. Putar **Potensiometer** (simulator tegangan analog sensor kelembapan tanah):
   - Putar ke arah nilai ADC tinggi (`> 2600`) untuk mensimulasikan **tanah kering (< 35%)** dan melihat relay serta indikator pompa menyala.
   - Putar ke arah nilai ADC rendah (`< 2000`) untuk mensimulasikan **tanah basah (>= 65%)** dan melihat pompa berhenti serta masuk ke mode *cooldown*.
