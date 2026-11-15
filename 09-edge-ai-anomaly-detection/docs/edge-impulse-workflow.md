# Panduan Workflow Pelatihan TinyML di Edge Impulse Studio & Output Serial

Proyek ini sudah dilengkapi model inferensi bawaan di [`anomaly_model.h`](../src/edge_ai_anomaly_detection/anomaly_model.h) yang langsung dapat dikompilasi dan diuji tanpa instalasi tambahan. Namun, jika Anda ingin melatih model **TinyML** khusus untuk profil getaran mesin Anda sendiri menggunakan **Edge Impulse Studio**, ikuti langkah-langkah di bawah ini.

---

## 1. Langkah demi Langkah Melatih Model di Edge Impulse

### Langkah 1 — Mengaktifkan Mode CSV Data Forwarder pada ESP32
1. Unggah sketch [`edge_ai_anomaly_detection.ino`](../src/edge_ai_anomaly_detection/edge_ai_anomaly_detection.ino) ke ESP32.
2. Buka **Serial Monitor (`115200` baud)**, ketik huruf **`m`** lalu tekan **Enter** untuk beralih ke **Mode CSV Data Forwarder**.
3. ESP32 akan mulai mengirimkan baris CSV `ax,ay,az` dengan frekuensi tepat **100 Hz**:
   ```text
   ax,ay,az
   0.120,-0.085,9.812
   0.145,-0.092,9.798
   ...
   ```
4. Tutup Serial Monitor Arduino IDE (agar port serial tidak terkunci), lalu jalankan CLI Edge Impulse di terminal laptop Anda:
   ```bash
   edge-impulse-data-forwarder --frequency 100
   ```
5. Beri nama sumbu `accX, accY, accZ` ketika diminta oleh CLI.

### Langkah 2 — Mengumpulkan Dataset untuk 3 Kondisi Mesin
Di dashboard **Edge Impulse Studio > Data acquisition**, rekam sampel berdurasi masing-masing **10 detik** (total minimal 2–3 menit data) untuk setiap label berikut:
1. **`IDLE`**: Mesin dalam keadaan mati.
2. **`NORMAL_OPERATION`**: Mesin/kipas menyala normal pada kecepatan kerja standar.
3. **`ANOMALY_DETECTED`**: Tempelkan beban tidak seimbang (misalnya koin/penjepit kertas pada salah satu bilah kipas) atau longgarkan dudukan baut untuk menyimulasikan kerusakan mekanis.

### Langkah 3 — Merancang Impulse (DSP + Neural Network + Anomaly Detection)
Pada menu **Impulse design > Create impulse**:
1. **Time series data**:
   - *Window size*: `640 ms`
   - *Window increase (stride)*: `320 ms`
   - *Frequency*: `100 Hz`
2. **Processing block (DSP)**: Pilih **Spectral Analysis** (menghitung RMS, Skewness, Kurtosis, dan puncak FFT otomatis).
3. **Learning block 1**: Pilih **Classification (Keras)** untuk membedakan `IDLE`, `NORMAL_OPERATION`, dan `ANOMALY_DETECTED`.
4. **Learning block 2**: Pilih **Anomaly Detection (K-means)** pada sumbu RMS untuk mendeteksi pola getaran asing yang belum pernah terlihat saat pelatihan.

### Langkah 4 — Ekspor ke Library Arduino IDE
1. Buka menu **Deployment**, pilih **Arduino library**, aktifkan **Quantized (int8)** atau **Unoptimized (float32)**, lalu klik **Build**.
2. Di Arduino IDE, impor file `.zip` hasil unduhan melalui menu **`Sketch > Include Library > Add .ZIP Library...`**.

---

## 2. Contoh Output Serial Monitor (`115200` baud)

Berikut adalah tampilan log pada Serial Monitor ketika menguji ketiga kondisi operasi mesin (baik menggunakan getaran fisik pada MPU6050 maupun menekan tombol simulasi pada `GPIO 14`):

```text
======================================================================
  IoT Lab - 09 Edge AI Anomaly Detection (TinyML Vibration Monitor)   
  Mode Aktif : EDGE AI INFERENCE (Real-Time)
  Perintah Serial Monitor:
    'm' -> Ganti Mode (Edge AI Inference <-> CSV Data Forwarder)
    '0' -> Mode Sensor MPU6050 Murni (IDLE / Sesuai Slider Wokwi)
    '1' -> Simulasikan Getaran Motor NORMAL_OPERATION
    '2' -> Simulasikan Getaran Rusak ANOMALY_DETECTED (Bearing/Unbalance)
======================================================================

[MPU6050] Sensor Akselerometer I2C (0x68) berhasil diinisialisasi!
[EDGE-AI] Status: IDLE              | AnomalyScore: 0.14 | Conf(I/N/A): 94%/6%/0%  | RMS: 0.02 m/s2 | P2P: 0.05 | Crest: 1.42 | DSP+NN: 165+38 us
[EDGE-AI] Status: IDLE              | AnomalyScore: 0.14 | Conf(I/N/A): 94%/6%/0%  | RMS: 0.02 m/s2 | P2P: 0.06 | Crest: 1.45 | DSP+NN: 162+36 us

[BUTTON] Tombol ditekan! Beralih ke Profil Getaran Uji #1 (MOTOR_NORMAL)
[EDGE-AI] Status: NORMAL_OPERATION  | AnomalyScore: 0.21 | Conf(I/N/A): 4%/93%/3%  | RMS: 1.13 m/s2 | P2P: 2.18 | Crest: 1.58 | DSP+NN: 168+39 us
[EDGE-AI] Status: NORMAL_OPERATION  | AnomalyScore: 0.19 | Conf(I/N/A): 3%/94%/3%  | RMS: 1.14 m/s2 | P2P: 2.20 | Crest: 1.56 | DSP+NN: 166+37 us

[BUTTON] Tombol ditekan! Beralih ke Profil Getaran Uji #2 (MOTOR_ANOMALY)
[EDGE-AI] Status: ANOMALY_DETECTED  | AnomalyScore: 2.48 | Conf(I/N/A): 0%/1%/99%  | RMS: 4.52 m/s2 | P2P: 11.84 | Crest: 3.12 | DSP+NN: 171+41 us
[EDGE-AI] Status: ANOMALY_DETECTED  | AnomalyScore: 2.61 | Conf(I/N/A): 0%/1%/99%  | RMS: 4.68 m/s2 | P2P: 12.30 | Crest: 3.25 | DSP+NN: 169+38 us
```
Perhatikan betapa cepatnya waktu eksekusi di ESP32: proses ekstraksi fitur DSP 64 sampel 3-sumbu hanya membutuhkan **~168 mikrodetik (`0.168 ms`)** dan inferensi klasifikasi + skor anomali hanya membutuhkan **~38 mikrodetik (`0.038 ms`)**!
