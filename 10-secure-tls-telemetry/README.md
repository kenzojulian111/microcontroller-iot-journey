# 10 - Secure End-to-End Encrypted Telemetry (TLS/SSL & mTLS MQTTS)

**Level:** Advance  
**Board:** ESP32

---

## Deskripsi Proyek
Mengirimkan data sensor atau perintah aktuator menggunakan protokol MQTT standar (`Port 1883`) sangat berbahaya untuk sistem produksi karena seluruh username, password, dan isi payload dikirim dalam bentuk teks terbuka (*plaintext*) yang mudah disadap maupun dipalsukan. Proyek kesepuluh ini mengimplementasikan arsitektur **Keamanan IoT Standar Industri (MQTTS Port 8883)** menggunakan **TLS 1.2/1.3 & Sertifikat X.509**:

1. **Sinkronisasi Waktu NTP Pra-Handshake (`configTime`)**: Menyinkronkan jam internal ESP32 dengan server `pool.ntp.org` sebelum koneksi TLS dimulai agar validasi masa berlaku sertifikat X.509 (`NotBefore` dan `NotAfter`) berjalan akurat.
2. **Dukungan Mutual TLS (mTLS X.509) & Server-Verified TLS**:
   - **Mode AWS IoT Core (mTLS)**: Menggunakan tiga kredensial kriptografi sekaligus (`ROOT_CA_PEM`, `CLIENT_CERT_PEM`, dan `PRIVATE_KEY_PEM`) yang disimpan di memori Flash (`PROGMEM`).
   - **Mode HiveMQ Cloud Serverless**: Memverifikasi sertifikat server menggunakan `ROOT_CA_PEM` ditambah autentikasi `MQTTS_USERNAME` dan `MQTTS_PASSWORD`.
   - **Mode Demo Simulator Wokwi**: Jika `secrets.h` belum dibuat, otomatis terhubung ke broker MQTTS publik (`broker.emqx.io:8883`) melalui kanal terenkripsi TLS sehingga dapat langsung diuji di simulator tanpa konfigurasi rumit.
3. **Time-Stamped JSON Telemetry & Secure Downlink Command**: Mengirimkan data suhu dan kelembapan **DHT22** lengkap dengan *timestamp* UTC ISO-8601 (`2026-11-15T14:48:12Z`) serta menerima perintah kontrol jarak jauh secara aman.

---

## Fitur Utama
- **Zero-RAM Certificate Storage (`PROGMEM`):** Seluruh sertifikat X.509 disimpan di memori Flash sehingga menghemat puluhan kilobyte RAM Heap yang sangat dibutuhkan oleh *stack* kriptografi **mbedTLS**.
- **Diagnostik Error mbedTLS Lengkap:** Menampilkan durasi jabat tangan TLS (dalam milidetik), sisa *Free Heap* selama sesi TLS aktif, serta pesan error spesifik mbedTLS apabila sertifikat kedaluwarsa atau ditolak.
- **Instant Publish Trigger:** Dilengkapi tombol fisik pada `GPIO 14` untuk mengirim paket telemetri terenkripsi kapan saja secara instan.

---

## Struktur File Proyek

```text
10-secure-tls-telemetry/
├── README.md                                      # Dokumentasi utama proyek ini
├── src/
│   └── secure_tls_telemetry/
│       ├── secure_tls_telemetry.ino               # Sketch utama MQTTS TLS/mTLS & NTP
│       └── secrets.example.h                      # Template X.509 PEM (Root CA, Cert, Private Key)
├── schematics/
│   ├── diagram.json                               # Rangkaian simulasi Wokwi (ESP32 + DHT22)
│   └── wiring-guide.md                            # Pinout, sequenceDiagram mTLS & keamanan kunci
└── docs/
    └── aws-iot-and-hivemq-setup.md                # Panduan setup AWS IoT Core, HiveMQ & log Serial
```

---

## Library Arduino IDE yang Dibutuhkan

Instal library berikut melalui **Arduino IDE Library Manager**:
1. **PubSubClient** oleh *Nick O'Leary*
2. **DHT sensor library** oleh *Adafruit* (beserta *Adafruit Unified Sensor*)
*(Library `WiFiClientSecure.h` dan `time.h` sudah tersedia secara bawaan di dalam core Arduino-ESP32).*

---

## Cara Menjalankan di Arduino IDE & Simulator Wokwi

1. **Untuk Pengujian Cepat di Simulator Wokwi (Tanpa `secrets.h`)**:
   - Buka [Wokwi ESP32 Simulator](https://wokwi.com/projects/new/esp32), salin [`secure_tls_telemetry.ino`](./src/secure_tls_telemetry/secure_tls_telemetry.ino) dan [`schematics/diagram.json`](./schematics/diagram.json), lalu tambahkan library `PubSubClient` serta `DHT sensor library`.
   - Jalankan simulasi: ESP32 akan menyinkronkan waktu NTP dan melakukan TLS Handshake ke `broker.emqx.io:8883` secara otomatis!
2. **Untuk Koneksi ke AWS IoT Core atau HiveMQ Cloud (Hardware Asli)**:
   - Salin [`secrets.example.h`](./src/secure_tls_telemetry/secrets.example.h) menjadi `secrets.h`:
     ```bash
     cp src/secure_tls_telemetry/secrets.example.h src/secure_tls_telemetry/secrets.h
     ```
   - Ikuti panduan langkah demi langkah di [`docs/aws-iot-and-hivemq-setup.md`](./docs/aws-iot-and-hivemq-setup.md) untuk mengisi sertifikat X.509 dan endpoint broker Anda.
