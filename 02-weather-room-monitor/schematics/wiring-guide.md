# Panduan Pengkabelan (Wiring Guide) — 02 Real-Time Weather & Room Monitor

Dokumen ini menjelaskan koneksi pin antara mikrokontroler (**ESP32** atau **ESP8266 NodeMCU**), sensor suhu & kelembapan **DHT22** (atau opsional **BME280**), dan layar **OLED 0.96" SSD1306 (I2C)**.

---

## 1. Tabel Pinout (ESP32 vs ESP8266)

Kode program di [`weather_room_monitor.ino`](../src/weather_room_monitor/weather_room_monitor.ino) secara otomatis memilih konfigurasi pin berikut berdasarkan board yang dipilih di Arduino IDE:

| Komponen | Pin Modul | Pin ESP32 (DevKit V1) | Pin ESP8266 (NodeMCU) | Keterangan |
| :--- | :--- | :--- | :--- | :--- |
| **Sensor DHT22** | `VCC` / `+` | `3V3` (3.3V) | `3V3` (3.3V) | Tegangan kerja sensor (3.3V – 5V) |
| **Sensor DHT22** | `DATA` / `SDA` | `GPIO 15` | `D5` (`GPIO 14`) | Jalur komunikasi 1-Wire DHT22 |
| **Sensor DHT22** | `GND` / `-` | `GND` | `GND` | Ground bersama |
| **OLED SSD1306 (I2C)** | `VCC` / `VDD` | `3V3` (3.3V) | `3V3` (3.3V) | Catu daya modul OLED I2C |
| **OLED SSD1306 (I2C)** | `GND` | `GND` | `GND` | Ground bersama |
| **OLED SSD1306 (I2C)** | `SDA` | `GPIO 21` | `D2` (`GPIO 4`) | Jalur I2C Serial Data |
| **OLED SSD1306 (I2C)** | `SCL` / `SCK` | `GPIO 22` | `D1` (`GPIO 5`) | Jalur I2C Serial Clock |
| **Sensor BME280 (Opsional)** | `SDA` & `SCL` | `GPIO 21` & `GPIO 22` | `D2` & `D1` | Paralel pada bus I2C yang sama (`0x76` / `0x77`) |
| **Indikator Wi-Fi** | Anoda | `GPIO 2` (Onboard) | `LED_BUILTIN` (`D4`) | Berkedip saat *connecting*, menyala saat terhubung |

---

## 2. Diagram Alur Komunikasi I2C & Cloud

```mermaid
flowchart LR
    subgraph Sensors["Input Sensor Lingkungan"]
        DHT["Sensor DHT22 (Suhu & Kelembapan)"] -->|1-Wire: GPIO15 / D5| MCU
        BME["Opsional: BME280 (Suhu, Lembap, Tekanan)"] -.->|Bus I2C| MCU
    end

    subgraph Controller["Mikrokontroler"]
        MCU["ESP32 / ESP8266"]
    end

    subgraph Display["Tampilan Lokal"]
        MCU -->|I2C: SDA & SCL (0x3C)| OLED["OLED 0.96 inch SSD1306 (128x64)"]
    end

    subgraph Cloud["Platform Cloud IoT"]
        MCU -->|HTTP POST Tiap 15 Detik| TS["ThingSpeak Channel"]
        MCU -.->|HTTP POST JSON| TB["ThingsBoard / Custom Endpoint"]
    end
```

---

## 3. Catatan Resistor Pull-Up pada DHT22

- Jika Anda menggunakan **modul DHT22 3-pin** (sudah terpasang di PCB kecil), resistor *pull-up* sudah tersedia di modul sehingga pin `DATA` bisa langsung dihubungkan ke pin mikrokontroler.
- Jika Anda menggunakan **komponen DHT22 4-pin telanjang (tanpa PCB)**, pasang resistor **4.7 kΩ – 10 kΩ** di antara pin `VCC` (3.3V) dan pin `DATA` agar sinyal pembacaan stabil dan tidak menghasilkan `NaN`.

---

## 4. Kalibrasi & Troubleshooting Alamat I2C (`0x3C` vs `0x3D`)

Layar OLED 0.96" SSD1306 umumnya menggunakan alamat I2C **`0x3C`**. Namun, beberapa modul di pasaran dikonfigurasi pada alamat **`0x3D`**.

Jika layar OLED tidak menyala atau di Serial Monitor muncul pesan:
```text
[OLED] Peringatan: Layar SSD1306 tidak terdeteksi di 0x3C! Lanjut tanpa OLED.
```

Lakukan langkah pemeriksaan berikut:
1. Pastikan kabel **`SDA`** dan **`SCL`** tidak tertukar.
2. Periksa jumper resistor alamat di bagian belakang modul OLED, atau ubah konstanta pada [`weather_room_monitor.ino`](../src/weather_room_monitor/weather_room_monitor.ino):
   ```cpp
   #define OLED_I2C_ADDR 0x3D
   ```
3. Jika Anda juga memasang sensor **BME280** secara paralel di jalur `SDA`/`SCL` yang sama, alamat default BME280 adalah `0x76` atau `0x77` sehingga tidak akan bentrok dengan OLED (`0x3C`).
