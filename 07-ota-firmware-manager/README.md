# 07 - OTA Firmware Update & Fleet Manager

**Level:** Advance  
**Board:** ESP32 & ESP8266 (Otomatis menyesuaikan melalui preprocessor `#ifdef`)

---

## Deskripsi Proyek
Dalam deployment IoT skala industri (ratusan sensor yang tersebar di atap gedung, lahan pertanian, atau pabrik), memperbarui program menggunakan kabel USB satu per satu adalah hal yang mustahil. Proyek ketujuh ini mengimplementasikan sistem **Manajemen Pembaruan Firmware Nirkabel (Over-The-Air / OTA)** dengan **2 metode sekaligus**:

1. **Local Push OTA (`ArduinoOTA.h`)**: Memungkinkan developer mengunggah kode baru langsung dari **Arduino IDE** melalui jaringan Wi-Fi lokal (mDNS Network Port) lengkap dengan proteksi kata sandi dan laporan persentase (`0% - 100%`).
2. **Cloud Pull HTTP/HTTPS OTA (`HTTPUpdate.h` / `ESP8266httpUpdate.h`)**: Perangkat secara mandiri memeriksa file manifest `version.json` di server/GitHub Releases, membandingkan nomor versi menggunakan **Semantic Versioning (`MAJOR.MINOR.PATCH`)**, lalu otomatis mengunduh dan mem-*flash* file binary `.bin` ke partisi cadangan (`app0` <-> `app1`) jika tersedia versi yang lebih tinggi.
3. **Web Fleet Diagnostic Dashboard (`GET /`)**: Menyajikan halaman web diagnostik yang menampilkan versi firmware aktif, label partisi flash yang sedang berjalan (`esp_ota_get_running_partition()`), kekuatan sinyal Wi-Fi, sisa RAM, serta tombol **"Check for Update Now"**.

---

## Fitur Utama
- **Dual-Mode OTA (Push & Pull):** Fleksibel untuk debugging cepat di meja kerja maupun distribusi massal (*fleet rollout*) di server produksi.
- **Anti-Brick Dual-Bank Partitioning:** Memanfaatkan arsitektur partisi ping-pong (`app0` dan `app1`) sehingga kegagalan jaringan saat mengunduh tidak merusak firmware yang sedang berjalan.
- **Semantic Version Comparator:** Hanya mengunduh binary apabila versi di `version.json` benar-benar lebih baru daripada `FIRMWARE_VERSION` lokal.
- **Trigger Multi-Jalur:** Pemeriksaan Cloud OTA dapat dipicu secara berkala (timer otomatis), melalui tombol fisik pada `GPIO 14`, atau melalui klik tombol di Web Dashboard.

---

## Struktur File Proyek

```text
07-ota-firmware-manager/
├── README.md                              # Dokumentasi utama proyek ini
├── src/
│   └── ota_firmware_manager/
│       ├── ota_firmware_manager.ino       # Sketch utama Dual-OTA & Web Fleet Manager
│       └── secrets.example.h              # Template Wi-Fi, Password OTA & URL Manifest
├── schematics/
│   ├── diagram.json                       # File rangkaian simulasi Wokwi
│   └── wiring-guide.md                    # Pinout, sequenceDiagram OTA & skema partisi Flash
└── docs/
    └── ota-deployment-guide.md            # Panduan Export Binary .bin, version.json & log Serial
```

---

## Cara Menjalankan di Arduino IDE (Hardware Asli)

1. Buka folder [`src/ota_firmware_manager/`](./src/ota_firmware_manager/) lalu salin [`secrets.example.h`](./src/ota_firmware_manager/secrets.example.h) menjadi `secrets.h`:
   ```bash
   cp src/ota_firmware_manager/secrets.example.h src/ota_firmware_manager/secrets.h
   ```
2. Edit `secrets.h` dan sesuaikan `WIFI_SSID`, `WIFI_PASSWORD`, `OTA_PASSWORD`, serta `VERSION_MANIFEST_URL`.
3. Buka [`ota_firmware_manager.ino`](./src/ota_firmware_manager/ota_firmware_manager.ino) di **Arduino IDE**.
4. Pastikan skema partisi pada menu `Tools > Partition Scheme` menggunakan opsi yang mendukung OTA (contoh: `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` atau `Minimal SPIFFS (1.9MB APP with OTA)`).
5. Unggah pertama kali menggunakan kabel USB, lalu ikuti panduan lengkap di [`docs/ota-deployment-guide.md`](./docs/ota-deployment-guide.md) untuk melakukan pembaruan berikutnya secara nirkabel!

---

## Cara Menjalankan di Simulator Wokwi

1. Tanpa membuat `secrets.h`, sketch otomatis terhubung ke jaringan virtual `Wokwi-GUEST`.
2. Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32) atau ekstensi **Wokwi for VS Code**.
3. Salin kode [`ota_firmware_manager.ino`](./src/ota_firmware_manager/ota_firmware_manager.ino) dan [`schematics/diagram.json`](./schematics/diagram.json).
4. Tekan tombol biru **"Check OTA Update"** pada simulasi untuk melihat alur pemeriksaan manifest versi di Serial Monitor.
