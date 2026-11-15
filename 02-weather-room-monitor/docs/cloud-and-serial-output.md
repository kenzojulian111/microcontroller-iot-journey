# Dokumentasi Cloud Telemetry, Layar OLED & Output Serial Monitor

## 1. Contoh Output Serial Monitor (115200 baud)

Saat mikrokontroler dinyalakan, menginisialisasi layar OLED serta sensor DHT22, lalu mengirimkan data ke Cloud, berikut tampilan pada **Serial Monitor**:

```text
==================================================
  IoT Lab - 02 Real-Time Weather & Room Monitor   
  Target Board: ESP32
==================================================
[OLED] SSD1306 berhasil diinisialisasi pada alamat I2C 0x3C.
[SENSOR] Sensor DHT22 aktif pada GPIO 15.
[WIFI] Menghubungkan ke SSID: Rumah_IoT ....
[WIFI] Terhubung!
[WIFI] Alamat IP Perangkat : 192.168.1.48
[WIFI] Kekuatan Sinyal     : -52 dBm
[SENSOR] Suhu: 26.4 C | Kelembapan: 58.0 % | Heat Index: 27.1 C | Status: NYAMAN (IDEAL)
[SENSOR] Suhu: 26.5 C | Kelembapan: 58.2 % | Heat Index: 27.3 C | Status: NYAMAN (IDEAL)
[TELEMETRY] Payload JSON: {"device":"ESP32","temperature_c":26.50,"humidity_pct":58.20,"heat_index_c":27.31,"comfort":"NYAMAN (IDEAL)","rssi_dbm":-52,"uptime_s":15}
[THINGSPEAK] Berhasil kirim data! Entry ID: 142 (HTTP 200)
```

> **Catatan Mode Simulasi / Offline:**  
> Apabila `THINGSPEAK_API_KEY` belum diubah di `secrets.h`, perangkat tetap berjalan normal menampilkan data di layar OLED dan mencetak JSON telemetri di Serial Monitor dengan status `CLD:SIM-OK`.

---

## 2. Tata Letak Tampilan Layar OLED SSD1306 (128x64)

Layar OLED diperbarui setiap **2,5 detik** tanpa memblokir eksekusi program utama:

```text
+-----------------------------------+
| WiFi:-52dBm            CLD:OK#14  |  <-- Baris status Wi-Fi RSSI & sinkronisasi Cloud
|-----------------------------------|
| SUHU                   LEMBAP     |  <-- Label parameter utama
|  26.5 °C                58 %      |  <-- Angka ukuran besar (TextSize 2)
|-----------------------------------|
| Terasa: 27.3 C                    |  <-- Heat Index (suhu yang dirasakan tubuh)
| Status: NYAMAN (IDEAL)            |  <-- Indeks kenyamanan ruangan otomatis
+-----------------------------------+
```

### Kategori Kenyamanan Ruangan Otomatis:
- **`NYAMAN (IDEAL)`**: Suhu `20.0 °C – 27.5 °C` dan kelembapan `40% – 65%`.
- **`PANAS/LEMBAP`**: Suhu `>= 32.0 °C` atau kelembapan `>= 80%`.
- **`SEJUK/DINGIN`**: Suhu `< 20.0 °C`.
- **`NORMAL`**: Kondisi di luar rentang di atas namun masih dalam batas wajar.

---

## 3. Format Pengiriman Data ke Cloud

### a. ThingSpeak (`http://api.thingspeak.com/update`)
Saat kamu membuat Channel baru di [ThingSpeak](https://thingspeak.com), aktifkan 4 *Field* berikut agar sesuai dengan data yang dikirimkan oleh sketch:

| Field ThingSpeak | Parameter yang Dikirim | Satuan |
| :--- | :--- | :--- |
| **`field1`** | Suhu Ruangan (`currentTempC`) | `°C` |
| **`field2`** | Kelembapan Udara (`currentHumidity`) | `%` |
| **`field3`** | Indeks Panas / *Heat Index* (`currentHeatIndex`) | `°C` |
| **`field4`** | Kekuatan Sinyal Wi-Fi (`WiFi.RSSI()`) | `dBm` |

- **Metode:** `POST /update`
- **Header:** `Content-Type: application/x-www-form-urlencoded`
- **Contoh Body:**
  ```text
  api_key=XXXXXXXXXXXXXXXX&field1=26.50&field2=58.20&field3=27.31&field4=-52
  ```

### b. Endpoint HTTP JSON Generik (ThingsBoard / Webhook)
Jika `CUSTOM_JSON_ENDPOINT` diisi di `secrets.h` (misalnya `http://demo.thingsboard.io/api/v1/<TOKEN>/telemetry`), mikrokontroler juga akan mengirimkan `POST` berformat `application/json`:

```json
{
  "device": "ESP32",
  "temperature_c": 26.50,
  "humidity_pct": 58.20,
  "heat_index_c": 27.31,
  "comfort": "NYAMAN (IDEAL)",
  "rssi_dbm": -52,
  "uptime_s": 15
}
```
