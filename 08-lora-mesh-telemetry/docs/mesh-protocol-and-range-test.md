# Spesifikasi Protokol LoRa Mesh & Analisis Range Test

Dokumen ini menjelaskan struktur protokol paket yang digunakan antara [`lora_mesh_node.ino`](../src/lora_mesh_node/lora_mesh_node.ino) dan [`lora_mesh_gateway.ino`](../src/lora_mesh_gateway/lora_mesh_gateway.ino), pemilihan parameter modulasi Chirp Spread Spectrum (CSS), serta contoh log perjalanan paket multi-hop.

---

## 1. Struktur Frame Paket LoRa Mesh

Setiap paket dikirim sebagai *single-frame* berbatas karakter pipa (`|`) agar ringkas (di bawah batas maksimum FIFO 255 byte chip SX127x) sekaligus mudah di-*debug* secara manusiawi:

```text
MSG_ID | SRC_ID | DST_ID | VIA_ID | HOP_COUNT | MAX_HOPS | JSON_PAYLOAD
```

| Field | Tipe | Contoh Nilai | Deskripsi |
| :--- | :--- | :--- | :--- |
| `MSG_ID` | `uint16_t` | `104` | Nomor urut unik paket dari node pengirim (`srcId`) |
| `SRC_ID` | `uint8_t` | `1` (`0x01`) | ID Node asal pembuat data sensor |
| `DST_ID` | `uint8_t` | `0` (`0x00`) | ID Node tujuan akhir (`0x00` = Gateway Utama) |
| `VIA_ID` | `uint8_t` | `2` (`0x02`) | ID Node terakhir yang memancarkan/me-*relay* paket ini |
| `HOP_COUNT` | `uint8_t` | `1` | Jumlah loncatan yang sudah ditempuh paket sejauh ini |
| `MAX_HOPS` | `uint8_t` | `3` | Batas maksimum loncatan (*Time-To-Live* / TTL) |
| `JSON_PAYLOAD` | `String` | `{"t":27.4,"h":78.5,"dist":120.0,"water":180.0,"alert":false}` | Data telemetri lingkungan |

### Mekanisme Anti-Looping (*Deduplication Ring Buffer*)
Setiap node dan gateway memelihara array melingkar `seenBuffer[24]` yang menyimpan pasangan `(srcId, msgId)`.
1. Saat sebuah node menerima paket, ia memeriksa apakah `(pkt.srcId, pkt.msgId)` sudah tercatat di `seenBuffer`.
2. Jika **sudah ada**, paket langsung **dibuang (`DROP`)** sehingga tidak terjadi pantulan bolak-balik (*ping-pong broadcast storm*).
3. Jika **belum ada**, pasangan `(srcId, msgId)` dicatat ke buffer, `hopCount` ditambah `1`, `viaId` diubah menjadi ID node repeater tersebut, lalu dipancarkan ulang setelah *random backoff* `50 – 250 ms`.

---

## 2. Trade-off Spreading Factor (`SF7` s/d `SF12`)

Dalam modulasi LoRa, pemilihan **Spreading Factor (SF)** menentukan keseimbangan antara **Jarak Jangkauan (Sensitivitas)** dan **Kecepatan Kirim (Time-on-Air / Konsumsi Baterai)**:

| Spreading Factor | Bitrate (BW 125 kHz) | Sensitivitas Receiver | Batas SNR Demodulasi | Time-on-Air (Payload 64 Byte) | Rekomendasi Penggunaan |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **`SF7`** | ~5,470 bps | `-123 dBm` | `-7.5 dB` | ~118 ms | Jarak dekat (< 1.5 km), hemat baterai |
| **`SF8`** | ~3,125 bps | `-126 dBm` | `-10.0 dB` | ~215 ms | Area pinggiran kota menengah |
| **`SF9` (Default)** | ~1,760 bps | `-129 dBm` | `-12.5 dB` | ~390 ms | **Titik optimal** jaringan Mesh multi-hop |
| **`SF10`** | ~980 bps | `-132 dBm` | `-15.0 dB` | ~698 ms | Hutan lebat / perbukitan (3 – 6 km) |
| **`SF12`** | ~250 bps | `-137 dBm` | `-20.0 dB` | ~2,465 ms | Jarak ekstrem (> 10 km), rawan tabrakan paket |

---

## 3. Contoh Log Komunikasi Multi-Hop (`Node 0x01 -> Repeater 0x02 -> Gateway 0x00`)

### a. Output Serial pada Node Sensor Hulu (`NODE_ID = 0x01`)
```text
============================================================
  IoT Lab - 08 LoRa Mesh Environmental Telemetry (Node)     
  Node ID        : 0x01 | Repeater Mode: ENABLED
  Frekuensi LoRa : 915 MHz (SF9, BW 125 kHz)
============================================================
[LORA] Transceiver SX127x berhasil diinisialisasi!

------------------------------------------------------------
[SENSOR] Suhu: 26.8 C | RH: 81.0% | Jarak Sensor: 85.0 cm | Tinggi Air: 215.0 cm (SIAGA_BANJIR)
[LORA-TX] Frame Terkirim (73 bytes): 101|1|0|1|0|3|{"t":26.8,"h":81.0,"dist":85.0,"water":215.0,"alert":true}
```

### b. Output Serial pada Node Repeater Bukit (`NODE_ID = 0x02`)
```text
[LORA-RX] Dari 0x01 via 0x01 | Hop 0/3 | RSSI -96 dBm | SNR 3.2 dB | Payload: {"t":26.8,"h":81.0,"dist":85.0,"water":215.0,"alert":true}
[MESH-RELAY] Meneruskan paket MSG:101 dari 0x01 -> 0x00 (Hop 1/3, Backoff 142 ms)
[LORA-TX] Frame Terkirim (73 bytes): 101|1|0|2|1|3|{"t":26.8,"h":81.0,"dist":85.0,"water":215.0,"alert":true}
```

### c. Output Serial pada Gateway Utama (`GATEWAY_ID = 0x00`)
```text
============================================================
[GATEWAY-RX] Paket #101 Diterima!
  Node Asal (SRC) : 0x01
  Node Relay (VIA): 0x02 (Jumlah Hop: 1 dari maks 3)
  Kualitas Radio  : RSSI = -82 dBm | SNR = 7.8 dB
  Payload Sensor  : {"t":26.8,"h":81.0,"dist":85.0,"water":215.0,"alert":true}
[CLOUD-UPLINK] JSON Envelope: {"gateway_id":"0x00","src_node":1,"via_node":2,"msg_id":101,"hops":1,"rssi_dbm":-82,"snr_db":7.8,"data":{"t":26.8,"h":81.0,"dist":85.0,"water":215.0,"alert":true}}
[CLOUD-UPLINK] HTTP POST Status: 200
============================================================
```
