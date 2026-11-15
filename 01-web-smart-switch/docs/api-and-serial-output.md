# Dokumentasi API & Contoh Output Serial Monitor

## 1. Contoh Output Serial Monitor (115200 baud)

Saat mikrokontroler dinyalakan dan berhasil terhubung ke Wi-Fi, berikut tampilan pada **Serial Monitor**:

```text
============================================
  IoT Lab - 01 Web-Controlled Smart Switch  
  Target Board: ESP32
============================================
[SWITCH] Relay 1 -> OFF
[SWITCH] Relay 2 -> OFF
[WIFI] Menghubungkan ke SSID: Rumah_IoT .....
[WIFI] Terhubung!
[WIFI] Alamat IP Web Server : http://192.168.1.42
[WIFI] Kekuatan Sinyal      : -54 dBm
[HTTP] Web Server aktif pada port 80.
[SWITCH] Relay 1 -> ON
[SWITCH] Relay 2 -> ON
[SWITCH] Relay 1 -> OFF
```

---

## 2. Daftar Endpoint REST API Lokal

Selain menggunakan tampilan antarmuka web di `http://<IP_ADDRESS>/`, perangkat ini juga menyediakan endpoint JSON yang dapat diakses melalui `curl`, Postman, atau aplikasi pihak ketiga:

### a. Ambil Status Perangkat & Relay
- **Endpoint:** `GET /api/status`
- **Contoh Request:**
  ```bash
  curl http://192.168.1.42/api/status
  ```
- **Contoh Response (`200 OK`):**
  ```json
  {
    "board": "ESP32",
    "relay1": true,
    "relay2": false,
    "ip": "192.168.1.42",
    "rssi": -54,
    "uptime": 148,
    "heap": 294512
  }
  ```

### b. Toggle Salah Satu Channel Relay
- **Endpoint:** `POST /api/toggle?ch=<1|2>` (mendukung `GET` juga untuk kemudahan tes di browser)
- **Contoh Request:**
  ```bash
  curl -X POST "http://192.168.1.42/api/toggle?ch=1"
  ```
- **Response:** Mengembalikan JSON status terbaru (sama seperti `/api/status`).

### c. Nyalakan atau Matikan Semua Relay Sekaligus
- **Endpoint:** `POST /api/all?state=<0|1>`
- **Contoh Request (Nyalakan Semua):**
  ```bash
  curl -X POST "http://192.168.1.42/api/all?state=1"
  ```
- **Contoh Request (Matikan Semua):**
  ```bash
  curl -X POST "http://192.168.1.42/api/all?state=0"
  ```
