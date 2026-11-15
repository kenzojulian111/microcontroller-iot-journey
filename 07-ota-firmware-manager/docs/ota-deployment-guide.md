# Panduan Deployment OTA (Local Push & Cloud Pull Server)

Dokumen ini menjelaskan cara menguji kedua metode pembaruan firmware tanpa kabel USB (**Over-The-Air**) yang diimplementasikan pada [`ota_firmware_manager.ino`](../src/ota_firmware_manager/ota_firmware_manager.ino).

---

## 1. Metode 1: Local Push OTA langsung dari Arduino IDE

Metode ini digunakan saat laptop Anda dan perangkat ESP32/ESP8266 berada di jaringan Wi-Fi (LAN) yang sama.

### Langkah-langkah:
1. Untuk pertama kali, unggah sketch [`ota_firmware_manager.ino`](../src/ota_firmware_manager/ota_firmware_manager.ino) menggunakan **kabel USB** seperti biasa.
2. Buka **Serial Monitor (`115200` baud)** dan catat alamat IP serta Hostname mDNS yang muncul:
   ```text
   [WIFI] Alamat IP Diagnostik : http://192.168.1.55
   [LOCAL-OTA] Siap menerima Push OTA di Hostname: esp-ota-node-01.local
   ```
3. Lepaskan kabel USB dan nyalakan ESP32 menggunakan adaptor daya / powerbank.
4. Di **Arduino IDE**, buka menu **`Tools > Port`**. Di bawah bagian **Network Ports**, pilih:
   `esp-ota-node-01 at 192.168.1.55`
5. Ubah sesuatu di kode (misal ubah `FIRMWARE_VERSION` menjadi `"1.0.1"`), lalu klik tombol **Upload**.
6. Saat diminta password, masukkan nilai `OTA_PASSWORD` dari `secrets.h` (default: `admin-ota-rahasia`).

---

## 2. Metode 2: Cloud Pull HTTP OTA (`version.json` + Binary `.bin`)

Metode ini digunakan untuk memperbarui ratusan perangkat IoT di lapangan secara otomatis cukup dengan mengunggah file binary ke web server atau GitHub Releases.

### Langkah A — Mengekspor File Binary (`.bin`) dari Arduino IDE
1. Buka [`ota_firmware_manager.ino`](../src/ota_firmware_manager/ota_firmware_manager.ino) di Arduino IDE.
2. Naikkan nomor versi pada baris:
   ```cpp
   const char* FIRMWARE_VERSION = "1.1.0";
   ```
3. Klik menu **`Sketch > Export Compiled Binary`** (atau tekan `Ctrl + Alt + S`).
4. Arduino IDE akan membuat folder `build/` di dalam folder sketch yang berisi file binary bernama:
   `ota_firmware_manager.ino.bin`

### Langkah B — Membuat File Manifest `version.json`
Buat sebuah folder di laptop Anda (misalnya `ota_server/`), salin file `ota_firmware_manager.ino.bin` ke dalamnya, lalu buat file `version.json` dengan isi berikut:

```json
{
  "version": "1.1.0",
  "url": "http://192.168.1.100:8000/ota_firmware_manager.ino.bin",
  "notes": "Optimasi manajemen memori & perbaikan interval telemetri"
}
```
*(Ganti `192.168.1.100` dengan alamat IP laptop/server Anda di jaringan Wi-Fi).*

### Langkah C — Menjalankan Server HTTP untuk Pengujian
Buka terminal di dalam folder `ota_server/` yang berisi `version.json` dan `ota_firmware_manager.ino.bin`, lalu jalankan server HTTP Python satu baris:

```bash
python3 -m http.server 8000
```

Pastikan URL di `secrets.h` pada ESP32 mengarah ke `http://<IP_LAPTOP>:8000/version.json`.

### Langkah D — Memicu Update
Anda dapat memicu pemeriksaan update dengan 3 cara:
1. **Tekan Tombol Fisik** pada `GPIO 14`.
2. **Buka Web Dashboard** di `http://<IP_ESP32>/` lalu klik tombol hijau **"Check for Update Now"**.
3. **Biarkan Otomatis**: Perangkat akan memeriksa server setiap interval `AUTO_CHECK_INTERVAL_MS`.

---

## 3. Contoh Output Serial Monitor (`115200` baud)

Berikut adalah tampilan log lengkap di Serial Monitor saat perangkat mendeteksi versi baru (`v1.1.0`) di server, mengunduh binary, mem-flash partisi `app1`, dan me-reboot dirinya sendiri:

```text
============================================
  IoT Lab - 07 OTA Firmware & Fleet Manager 
  Board           : ESP32
  Firmware Aktif  : v1.0.0 (Nov 15 2026 21:30:00)
  Partisi Berjalan: app0 (0x10000)
============================================
[WIFI] Menghubungkan ke SSID: Lab_IoT_WiFi ....
[WIFI] Terhubung!
[WIFI] Alamat IP Diagnostik : http://192.168.1.55
[WIFI] Kekuatan Sinyal      : -51 dBm
[LOCAL-OTA] Siap menerima Push OTA di Hostname: esp-ota-node-01.local
[HTTP] Web Fleet Diagnostik aktif di port 80.
[INFO] Tekan tombol fisik pada GPIO14 atau klik tombol di Web untuk cek Cloud OTA.

[BUTTON] Tombol OTA ditekan! Memicu pemeriksaan Cloud OTA...

--------------------------------------------
[CLOUD-OTA] Memeriksa manifest versi terbaru...
[CLOUD-OTA] Manifest URL : http://192.168.1.100:8000/version.json
[CLOUD-OTA] Versi Lokal  : v1.0.0
[CLOUD-OTA] Versi Server : v1.1.0
[CLOUD-OTA] Catatan Rilis: Optimasi manajemen memori & perbaikan interval telemetri
[CLOUD-OTA] UPDATE TERSEDIA! Mengunduh binary dari: http://192.168.1.100:8000/ota_firmware_manager.ino.bin
[CLOUD-OTA] Memulai penulisan ke partisi OTA berikutnya...
[CLOUD-OTA] Mengunduh Firmware: 0% (0 / 814752 bytes)
[CLOUD-OTA] Mengunduh Firmware: 10% (81920 / 814752 bytes)
[CLOUD-OTA] Mengunduh Firmware: 20% (163840 / 814752 bytes)
[CLOUD-OTA] Mengunduh Firmware: 50% (409600 / 814752 bytes)
[CLOUD-OTA] Mengunduh Firmware: 80% (655360 / 814752 bytes)
[CLOUD-OTA] Mengunduh Firmware: 100% (814752 / 814752 bytes)
[CLOUD-OTA] Penulisan Flash selesai! Perangkat akan segera reboot...

ets Jul 29 2019 12:21:46
rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
============================================
  IoT Lab - 07 OTA Firmware & Fleet Manager 
  Board           : ESP32
  Firmware Aktif  : v1.1.0 (Nov 15 2026 21:35:12)
  Partisi Berjalan: app1 (0x150000)
============================================
```
Perhatikan bahwa setelah reboot, **Firmware Aktif** otomatis berubah menjadi `v1.1.0` dan **Partisi Berjalan** berpindah dari `app0 (0x10000)` ke `app1 (0x150000)`!
