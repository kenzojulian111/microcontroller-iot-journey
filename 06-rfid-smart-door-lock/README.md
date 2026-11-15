# 06 - RFID / Biometric Smart Door Lock with Access Logs

**Level:** Intermediate  
**Board:** ESP32 (SPI RFID MFRC522 + NTP Time Sync + HTTP REST API + Offline Cache)

---

## Deskripsi Proyek
Proyek keenam ini membangun **Sistem Kunci Pintu Pintar (Smart Door Lock)** berbasis kartu/tag **RFID 13.56 MHz (MFRC522)** yang terintegrasi dengan pencatatan riwayat akses (*Access Logs*) waktu nyata ke server **REST API** serta memiliki mekanisme **Offline Cache Fallback**.

Setiap kali kartu ditempelkan ke *reader*, **ESP32** membaca **UID** kartu, mengambil waktu akurat berformat **ISO-8601** dari server **NTP**, lalu memvalidasi dan mencatat log akses ke server backend. Apabila jaringan Wi-Fi atau server sedang mengalami gangguan, ESP32 secara otomatis beralih menggunakan **Daftar Kartu Lokal Fallback (Offline Cache)** di memori internal sehingga pemilik ruangan tetap dapat membuka pintu tanpa terkunci di luar.

---

## Fitur Utama
- **Pembacaan UID RFID SPI (MFRC522):** Membaca kartu dan *keyfob* MIFARE 13.56 MHz dengan penghentian sesi kriptografi otomatis (`PICC_HaltA` & `PCD_StopCrypto1`) agar kartu tidak terbaca ganda.
- **Timestamp NTP ISO-8601 Akurat:** Menyinkronkan jam internal ESP32 dengan `pool.ntp.org` (zona waktu WIB `UTC+7`) untuk menghasilkan timestamp standar `YYYY-MM-DDTHH:MM:SS+07:00`.
- **Hybrid Cloud + Offline Cache Validation:** Mengirim HTTP POST JSON (`uid`, `device_id`, `timestamp`, `user_name`) ke endpoint REST API dengan autentikasi header `X-API-Key`, dan otomatis fallback ke daftar kartu lokal jika Wi-Fi terputus.
- **Non-Blocking Auto-Lock Solenoid:** Mengaktifkan Relay Solenoid Door Lock selama 4 detik lalu mengunci kembali secara otomatis menggunakan timer `millis()`, lengkap dengan indikator **LED Hijau**, **LED Merah**, dan nada **Buzzer**.
- **Wokwi Simulation Ready:** Dilengkapi 2 tombol simulasi tap kartu (**Kartu Master Valid** di `GPIO 32` dan **Kartu Asing Ditolak** di `GPIO 33`) sehingga seluruh alur buka-kunci, alarm, dan log API dapat langsung diuji di simulator Wokwi.

---

## Struktur File Proyek

```text
06-rfid-smart-door-lock/
├── README.md                              # Dokumentasi utama proyek ini
├── src/
│   └── rfid_smart_door_lock/
│       ├── rfid_smart_door_lock.ino       # Sketch utama ESP32 (RC522 + NTP + REST API + Cache)
│       └── secrets.example.h              # Template konfigurasi Wi-Fi & Endpoint REST API
├── schematics/
│   ├── diagram.json                       # File rangkaian simulasi Wokwi
│   └── wiring-guide.md                    # Pinout SPI RC522 & rangkaian Solenoid 12V + Dioda 1N4007
└── docs/
    └── backend-api-and-logs.md            # Server Python FastAPI + SQLite siap pakai & log Serial
```

---

## Persiapan Library di Arduino IDE

Instal library berikut melalui **Library Manager** (`Sketch > Include Library > Manage Libraries...`):
1. **MFRC522** oleh *GithubCommunity* (Miguel Balboa)

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/rfid_smart_door_lock/`](./src/rfid_smart_door_lock/) lalu salin file [`secrets.example.h`](./src/rfid_smart_door_lock/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/rfid_smart_door_lock/secrets.example.h src/rfid_smart_door_lock/secrets.h
   ```
2. Edit `secrets.h` dan isi `WIFI_SSID`, `WIFI_PASSWORD`, `API_ENDPOINT_URL`, serta `API_SECRET_KEY`.
3. *(Opsional)* Jalankan server backend Python FastAPI di komputer Anda mengikuti panduan di [`docs/backend-api-and-logs.md`](./docs/backend-api-and-logs.md).
4. Buka [`rfid_smart_door_lock.ino`](./src/rfid_smart_door_lock/rfid_smart_door_lock.ino) di **Arduino IDE**, pilih board **ESP32** (`DOIT ESP32 DEVKIT V1`), dan hubungkan modul RC522 serta relay solenoid sesuai [`schematics/wiring-guide.md`](./schematics/wiring-guide.md).
5. Klik **Upload** dan buka **Serial Monitor** pada baud rate **`115200`**.
6. Tempelkan kartu RFID Anda ke modul RC522, catat UID yang tampil di Serial Monitor, lalu daftarkan UID tersebut ke dalam array `LOCAL_CARD_CACHE[]` atau tabel SQLite di server backend.

---

## Cara Menjalankan di Simulator Wokwi

1. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau gunakan ekstensi **Wokwi for VS Code**.
2. Salin isi [`rfid_smart_door_lock.ino`](./src/rfid_smart_door_lock/rfid_smart_door_lock.ino) ke editor sketch utama, dan tambahkan library `MFRC522` di tab **Library Manager** Wokwi.
3. Salin isi [`schematics/diagram.json`](./schematics/diagram.json) ke tab `diagram.json` lalu jalankan simulasi.
4. Setelah waktu NTP berhasil disinkronkan di Serial Monitor:
   - Tekan **Tombol Hijau (`GPIO 32`)** untuk menyimulasikan tap **Kartu Master Valid (`A1:B2:C3:D4`)** — Relay Solenoid & LED Hijau akan menyala selama 4 detik disertai nada *beep* konfirmasi.
   - Tekan **Tombol Merah (`GPIO 33`)** untuk menyimulasikan tap **Kartu Asing (`DE:AD:BE:EF`)** — LED Merah akan berkedip disertai bunyi alarm penolakan akses.
