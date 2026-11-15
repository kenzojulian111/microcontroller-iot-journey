# OTA Deployment Guide (Local Push & Cloud Pull Release Server) — 07 OTA Firmware Manager

This document provides step-by-step operational procedures for testing both wireless firmware update workflows implemented in [`ota_firmware_manager.ino`](../src/ota_firmware_manager/ota_firmware_manager.ino).

---

## 1. Method 1: Local Push OTA Directly from Arduino IDE

Use this workflow during active development when your workstation and the target ESP32/ESP8266 share the same local Wi-Fi subnet (LAN).

### Step-by-Step Procedure:
1. Perform the **initial flash** of [`ota_firmware_manager.ino`](../src/ota_firmware_manager/ota_firmware_manager.ino) over a physical **USB cable**.
2. Open the **Serial Monitor (`115200` baud)** and record the assigned IPv4 address and mDNS hostname:
   ```text
   [WIFI] Alamat IP Diagnostik : http://192.168.1.55
   [LOCAL-OTA] Siap menerima Push OTA di Hostname: esp-ota-node-01.local
   ```
3. Disconnect the USB cable from your computer and power the ESP32 from an external 5V USB wall adapter or battery pack.
4. In **Arduino IDE**, navigate to **`Tools > Port`**. Under the **Network Ports** section, select:
   `esp-ota-node-01 at 192.168.1.55`
5. Modify the firmware (for example, increment `FIRMWARE_VERSION` to `"1.0.1"`), then click **Upload**.
6. When prompted for an authentication password, enter the `OTA_PASSWORD` defined in `secrets.h` (default: `admin-ota-rahasia`).

---

## 2. Method 2: Cloud Pull HTTP OTA (`version.json` + Compiled `.bin`)

Use this workflow to roll out automated updates across an entire fleet of deployed IoT nodes by publishing a release manifest and binary artifact to an HTTP/HTTPS server or GitHub Releases.

### Step A — Exporting the Compiled Binary (`.bin`)
1. Open [`ota_firmware_manager.ino`](../src/ota_firmware_manager/ota_firmware_manager.ino) in Arduino IDE.
2. Increment the semantic version string:
   ```cpp
   const char* FIRMWARE_VERSION = "1.1.0";
   ```
3. Select **`Sketch > Export Compiled Binary`** (or press `Ctrl + Alt + S`).
4. Arduino IDE generates a `build/` directory inside the sketch folder containing the compiled firmware image:
   `ota_firmware_manager.ino.bin`

### Step B — Authoring the Release Manifest (`version.json`)
Create a release directory on your workstation (e.g., `ota_server/`), copy `ota_firmware_manager.ino.bin` into it, and create a `version.json` file with the following schema:

```json
{
  "version": "1.1.0",
  "url": "http://192.168.1.100:8000/ota_firmware_manager.ino.bin",
  "notes": "Optimasi manajemen memori & perbaikan interval telemetri"
}
```
*(Replace `192.168.1.100` with the LAN IPv4 address of your host machine).*

### Step C — Launching the HTTP Artifact Server
Open a terminal inside the `ota_server/` directory containing both `version.json` and `ota_firmware_manager.ino.bin`, and start a lightweight HTTP server:

```bash
python3 -m http.server 8000
```

Ensure `VERSION_MANIFEST_URL` in the ESP32's `secrets.h` points to `http://<YOUR_HOST_IP>:8000/version.json`.

### Step D — Triggering the Fleet Update
You can initiate the Cloud Pull OTA check via three mechanisms:
1. **Hardware Push Button:** Press the tactile button wired to `GPIO 14`.
2. **Web Fleet Console:** Navigate to `http://<ESP32_IP>/` in a browser and click **"Check for Update Now"**.
3. **Autonomous Periodic Timer:** Wait for the non-blocking scheduler (`AUTO_CHECK_INTERVAL_MS`) to poll the manifest automatically.

---

## 3. UART Serial Monitor Output (`115200` baud)

The following UART log captures a complete Cloud Pull OTA lifecycle: the node running `v1.0.0` on partition `app0 (0x10000)` discovers `v1.1.0` on the release server, streams the 814 KB binary into partition `app1 (0x150000)`, flips the boot partition pointer, and reboots into `v1.1.0`:

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

Notice that immediately after the software reset (`SW_CPU_RESET`), the active firmware reports `v1.1.0` and the executing flash partition has cleanly transitioned from `app0 (0x10000)` to `app1 (0x150000)`.
