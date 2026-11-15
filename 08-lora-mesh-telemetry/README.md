# 08 - LoRa Mesh Environmental Telemetry (Long-Range IoT)

**Level:** Advance  
**Board:** ESP32 + Modul Transceiver LoRa SX1278 (433 MHz) / RFM95 (915 MHz)

---

## Deskripsi Proyek
Ketika memantau ketinggian permukaan air sungai untuk **Deteksi Dini Banjir (Flood Early Warning System)** atau kondisi mikroklimat di hutan dan perkebunan terpencil, jaringan Wi-Fi maupun seluler sering kali tidak tersedia. Proyek kedelapan ini membangun jaringan komunikasi radio jarak jauh **LoRa Mesh Multi-Hop** yang terdiri dari 2 firmware terpisah:

1. **Sensor & Repeater Node ([`lora_mesh_node.ino`](./src/lora_mesh_node/lora_mesh_node.ino))**:
   - Mengukur jarak permukaan air menggunakan sensor ultrasonik **HC-SR04** serta suhu dan kelembapan udara menggunakan **DHT22**.
   - Membungkus data ke dalam frame protokol Mesh (`MSG_ID|SRC_ID|DST_ID|VIA_ID|HOP|MAX_HOP|JSON`) dan memancarkannya melalui modul radio **LoRa SX127x**.
   - Bertindak sekaligus sebagai **Repeater Cerdas**: Meneruskan paket milik node tetangga menuju Gateway dengan *Deduplication Ring Buffer* (mencegah *broadcast storm*) dan *random backoff jitter* (mencegah tabrakan frekuensi).
2. **LoRa-to-Cloud Gateway ([`lora_mesh_gateway.ino`](./src/lora_mesh_gateway/lora_mesh_gateway.ino))**:
   - Mendengarkan seluruh paket dari jaringan LoRa Mesh, mencatat metrik kualitas radio fisik (**RSSI** dan **SNR**) serta rute loncatan (`viaId` & `hopCount`).
   - Menampilkan telemetri terakhir di layar **OLED SSD1306** dan meneruskan JSON Envelope ke server Cloud / HTTP Webhook melalui Wi-Fi.

---

## Fitur Utama
- **Multi-Hop Auto-Forwarding & Deduplication:** Memperluas jangkauan komunikasi berkali-kali lipat melewati bukit atau penghalang gedung menggunakan *ring buffer* `(srcId, msgId)`.
- **Flood Early Warning Logic:** Menghitung tinggi muka air sebenarnya (`SENSOR_MOUNT_HEIGHT_CM - jarak_sensor`) dan menyertakan flag `alert: true` saat melewati ambang siaga banjir.
- **Dual-Mode (Real SX127x Hardware & Wokwi Simulation):** Tetap dapat disimulasikan di Wokwi maupun diuji injeksi paketnya melalui Serial Monitor meskipun tanpa modul RF fisik.

---

## Struktur File Proyek

```text
08-lora-mesh-telemetry/
├── README.md                                  # Dokumentasi utama proyek ini
├── src/
│   ├── lora_mesh_node/
│   │   └── lora_mesh_node.ino                 # Firmware Sensor & Repeater Node
│   └── lora_mesh_gateway/
│       ├── lora_mesh_gateway.ino              # Firmware Gateway LoRa-ke-WiFi Cloud
│       └── secrets.example.h                  # Template Wi-Fi & URL Webhook Gateway
├── schematics/
│   ├── diagram.json                           # Rangkaian Wokwi Node Sensor + OLED
│   └── wiring-guide.md                        # Pinout SPI SX127x, Antena & Link Budget
└── docs/
    └── mesh-protocol-and-range-test.md        # Spesifikasi frame Mesh, SF7-SF12 & log Serial
```

---

## Library Arduino IDE yang Dibutuhkan

Instal library berikut melalui **Arduino IDE Library Manager** (`Sketch > Include Library > Manage Libraries...`):
1. **LoRa** oleh *Sandeep Mistry*
2. **DHT sensor library** oleh *Adafruit* (beserta *Adafruit Unified Sensor*)
3. **Adafruit SSD1306** & **Adafruit GFX Library**

---

## Cara Menjalankan di Arduino IDE & Simulator Wokwi

1. **Untuk Node Sensor / Repeater**:
   - Buka [`src/lora_mesh_node/lora_mesh_node.ino`](./src/lora_mesh_node/lora_mesh_node.ino).
   - Atur `NODE_ID` unik untuk setiap board (misal `0x01` untuk Node Hulu, `0x02` untuk Node Repeater) dan sesuaikan `LORA_FREQUENCY` (`433E6` atau `915E6`).
   - Untuk simulasi di **Wokwi**, gunakan [`lora_mesh_node.ino`](./src/lora_mesh_node/lora_mesh_node.ino) bersama [`schematics/diagram.json`](./schematics/diagram.json) dan ubah slider jarak sensor ultrasonik HC-SR04 untuk menguji pemicu `SIAGA_BANJIR`.
2. **Untuk Gateway Utama**:
   - Salin [`secrets.example.h`](./src/lora_mesh_gateway/secrets.example.h) menjadi `secrets.h` di dalam folder [`src/lora_mesh_gateway/`](./src/lora_mesh_gateway/).
   - Unggah [`lora_mesh_gateway.ino`](./src/lora_mesh_gateway/lora_mesh_gateway.ino) ke board ESP32 kedua yang berada di area terjangkau Wi-Fi.
