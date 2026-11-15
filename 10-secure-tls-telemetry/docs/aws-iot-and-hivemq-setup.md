# AWS IoT Core (mTLS X.509) & HiveMQ Cloud (TLS) Configuration Guide — 10 Secure TLS Telemetry

This guide provides step-by-step instructions for provisioning X.509 credentials and connecting [`secure_tls_telemetry.ino`](../src/secure_tls_telemetry/secure_tls_telemetry.ino) to **AWS IoT Core** (using Mutual TLS) or **HiveMQ Cloud Serverless** (using Server-Verified TLS + Username/Password).

---

## 1. Option A: Provisioning Mutual TLS (mTLS X.509) on AWS IoT Core

### Step 1 — Create a Thing & Generate X.509 Certificates
1. Sign in to the **AWS Management Console** and open **AWS IoT Core**.
2. Navigate to **Manage > All devices > Things** and click **Create things**.
3. Select **Create single thing** and name your device: `esp32-secure-node-01`.
4. Under **Device certificate**, select **Auto-generate a new certificate (recommended)**.
5. Download all **three PEM files** and copy their contents into `src/secure_tls_telemetry/secrets.h`:
   - **Device certificate** (`xxxx-certificate.pem.crt`) $\rightarrow$ Paste into `CLIENT_CERT_PEM`.
   - **Private key file** (`xxxx-private.pem.key`) $\rightarrow$ Paste into `PRIVATE_KEY_PEM`.
   - **Amazon Root CA 1** (`AmazonRootCA1.pem`) $\rightarrow$ Paste into `ROOT_CA_PEM`.

### Step 2 — Author & Attach an AWS IoT Core Policy
Navigate to **Security > Policies**, create a new policy with the following least-privilege JSON document, and **Attach** it to the device certificate created in Step 1:

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

### Step 3 — Configure the ATS Device Data Endpoint in `secrets.h`
1. In the left navigation pane of AWS IoT Core, open **Settings**.
2. Copy the **Endpoint** hostname under *Device data endpoint* (format: `xxxxxx-ats.iot.ap-southeast-1.amazonaws.com`).
3. Paste the hostname into `#define MQTTS_BROKER_HOST` in `secrets.h` and ensure `#define USE_MUTUAL_TLS_X509 1`.

---

## 2. Option B: Provisioning TLS + Username/Password on HiveMQ Cloud (Free Tier)

If you prefer a zero-cost serverless MQTTS broker without an AWS account, use **HiveMQ Cloud Serverless**:

1. Sign up at [HiveMQ Cloud](https://console.hivemq.cloud/) and create a **Serverless (Free)** cluster.
2. Open the **Access Management** tab and create device credentials (e.g., Username: `hivemq_device_user` and a strong password).
3. Open the **Overview** tab and copy your **Cluster URL** (e.g., `8f9a0b...s1.eu.hivemq.cloud`) and TLS Port (`8883`).
4. In `src/secure_tls_telemetry/secrets.h`:
   - Set `#define USE_MUTUAL_TLS_X509 0`.
   - Set `MQTTS_BROKER_HOST` to your Cluster URL and `MQTTS_BROKER_PORT` to `8883`.
   - Populate `MQTTS_USERNAME` and `MQTTS_PASSWORD`.
   - Place the **ISRG Root X1 (Let's Encrypt)** root certificate inside `ROOT_CA_PEM`.

---

## 3. UART Serial Monitor Output (`115200` baud)

The following UART console log captures a full production lifecycle on the ESP32: Wi-Fi connection, SNTP system clock verification, X.509 Mutual TLS handshake completion on port `8883` (`1420 ms`), free SRAM heap telemetry during the active mbedTLS session, outbound ISO-8601 encrypted telemetry publication, and inbound encrypted downlink actuation:

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
