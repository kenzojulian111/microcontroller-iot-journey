# Panduan Konfigurasi AWS IoT Core (mTLS) & HiveMQ Cloud (TLS)

Dokumen ini memandu Anda langkah demi langkah untuk menghubungkan [`secure_tls_telemetry.ino`](../src/secure_tls_telemetry/secure_tls_telemetry.ino) ke **AWS IoT Core** (menggunakan sertifikat X.509 Mutual TLS) maupun **HiveMQ Cloud Serverless** (menggunakan TLS + Username/Password).

---

## 1. Opsi A: Mengatur Mutual TLS (mTLS) di AWS IoT Core

### Langkah 1 — Membuat Thing & Sertifikat X.509
1. Masuk ke **AWS Management Console** dan buka layanan **AWS IoT Core**.
2. Buka menu **Manage > All devices > Things**, lalu klik **Create things**.
3. Pilih **Create single thing**, beri nama Thing Anda: `esp32-secure-node-01`.
4. Pada bagian **Device certificate**, pilih **Auto-generate a new certificate (recommended)**.
5. Unduh **ketiga file** berikut dan simpan di komputer Anda:
   - **Device certificate** (`xxxx-certificate.pem.crt`) -> Salin isinya ke `CLIENT_CERT_PEM` di `secrets.h`.
   - **Private key file** (`xxxx-private.pem.key`) -> Salin isinya ke `PRIVATE_KEY_PEM` di `secrets.h`.
   - **Amazon Root CA 1** (`AmazonRootCA1.pem`) -> Salin isinya ke `ROOT_CA_PEM` di `secrets.h`.

### Langkah 2 — Membuat & Melampirkan IoT Policy
Buat Policy baru di menu **Security > Policies** dengan izin JSON berikut, lalu lampirkan (*Attach*) ke sertifikat yang baru Anda buat:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": ["iot:Connect"],
      "Resource": ["*"]
    },
    {
      "Effect": "Allow",
      "Action": ["iot:Publish", "iot:Receive"],
      "Resource": ["arn:aws:iot:*:*:topic/iotlab/devices/*"]
    },
    {
      "Effect": "Allow",
      "Action": ["iot:Subscribe"],
      "Resource": ["arn:aws:iot:*:*:topicfilter/iotlab/devices/*"]
    }
  ]
}
```

### Langkah 3 — Menyalin Endpoint ATS ke `secrets.h`
1. Di menu kiri AWS IoT Core, buka **Settings**.
2. Salin nilai **Endpoint** di bawah *Device data endpoint* (format: `xxxxxx-ats.iot.ap-southeast-1.amazonaws.com`).
3. Masukkan ke `#define MQTTS_BROKER_HOST` di `secrets.h` dan pastikan `#define USE_MUTUAL_TLS_X509 1`.

---

## 2. Opsi B: Mengatur TLS + Username/Password di HiveMQ Cloud (Gratis)

Jika Anda tidak memiliki akun AWS, Anda dapat membuat cluster MQTTS gratis di **HiveMQ Cloud Serverless**:

1. Daftar di [HiveMQ Cloud](https://console.hivemq.cloud/) dan buat cluster **Serverless (Free)** baru.
2. Buka tab **Access Management**, lalu buat kredensial baru (misal Username: `hivemq_device_user` dan Password pilihan Anda).
3. Buka tab **Overview** dan salin **Cluster URL** (contoh: `8f9a0b...s1.eu.hivemq.cloud`).
4. Di file `secrets.h`:
   - Ubah `#define USE_MUTUAL_TLS_X509` menjadi `0`.
   - Isi `MQTTS_BROKER_HOST` dengan Cluster URL Anda dan `MQTTS_BROKER_PORT` dengan `8883`.
   - Isi `MQTTS_USERNAME` dan `MQTTS_PASSWORD`.
   - Masukkan sertifikat **ISRG Root X1 (Let's Encrypt)** ke dalam `ROOT_CA_PEM`.

---

## 3. Contoh Output Serial Monitor (`115200` baud)

Berikut adalah tampilan log lengkap di Serial Monitor saat ESP32 menyinkronkan waktu NTP, menyelesaikan jabat tangan kriptografi TLS pada port `8883`, mengirim telemetri bertanda waktu ISO-8601, dan menerima perintah jarak jauh terenkripsi:

```text
============================================================
  IoT Lab - 10 Secure End-to-End Encrypted Telemetry (TLS)  
  Device Thing Name : esp32-secure-node-01
  Target Broker     : a1b2c3d4e5f6g7-ats.iot.ap-southeast-1.amazonaws.com:8883 (MQTTS)
============================================================
[WIFI] Menghubungkan ke SSID: Lab_IoT_WiFi ....
[WIFI] Terhubung! IP: 192.168.1.60 (RSSI: -49 dBm)
[NTP] Menyinkronkan jam sistem dengan server NTP ..
[NTP] Waktu Sistem Tervalidasi: 2026-11-15 21:48:10 WIB (Epoch: 1794754090)
[TLS] Mode Keamanan: Mutual TLS (mTLS X.509 Client Cert + Private Key)

[MQTTS] Memulai TLS Handshake ke a1b2c3d4e5f6g7-ats.iot.ap-southeast-1.amazonaws.com:8883 ...
[MQTTS] TLS Handshake & Koneksi Broker BERHASIL dalam 1420 ms!
[MQTTS] Sisa Free Heap selama sesi TLS: 214680 bytes
[MQTTS] Berlangganan ke topik perintah: iotlab/devices/esp32-secure-node-01/command
[MQTTS-TX] (TERKIRIM) Topik: iotlab/devices/esp32-secure-node-01/telemetry
           Payload: {"device_id":"esp32-secure-node-01","seq":1,"timestamp":"2026-11-15T14:48:12Z","epoch":1794754092,"trigger":"startup","temperature_c":26.50,"humidity_pct":68.00,"actuator":false,"rssi_dbm":-49,"free_heap":214680}

------------------------------------------------------------
[MQTTS-RX] Perintah Terenkripsi Diterima pada Topik: iotlab/devices/esp32-secure-node-01/command
[MQTTS-RX] Payload: {"actuator":true}
[SHADOW] Aktuator Keamanan -> ON (LED Hijau Menyala)
------------------------------------------------------------
```
