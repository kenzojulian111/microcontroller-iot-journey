# 09 - Edge AI Anomaly Detection (TinyML Predictive Maintenance)

**Level:** Advance  
**Board:** ESP32 + Sensor IMU Akselerometer/Giroskop I2C MPU6050

---

## Deskripsi Proyek
Alih-alih mengirimkan ratusan sampel mentah sensor getaran setiap detik ke server Cloud, **Edge AI (TinyML)** melakukan ekstraksi fitur sinyal dan inferensi kecerdasan buatan langsung di dalam mikrokontroler ESP32 dengan latensi di bawah **1 milidetik**. Proyek kesembilan ini membangun sistem **Predictive Maintenance (Pemeliharaan Prediktif)** untuk memantau kesehatan motor listrik, kipas industri, atau pompa air secara *real-time*:

1. **High-Speed Sliding Window Sampling (`100 Hz`)**: Membaca akselerasi 3-sumbu (`ax`, `ay`, `az` dalam $\text{m/s}^2$) melalui bus Fast-I2C (`400 kHz`) setiap `10 ms` ke dalam buffer jendela geser 64 sampel dengan *overlap stride* 50%.
2. **On-Device DSP Feature Extraction ([`anomaly_model.h`](./src/edge_ai_anomaly_detection/anomaly_model.h))**: Menghilangkan komponen gravitasi statis (*DC offset removal*) lalu menghitung **RMS**, **Peak-to-Peak**, **Variance**, **Crest Factor** (detektor impak *bearing*), dan **Zero-Crossing Rate** untuk setiap sumbu.
3. **Dual-Head TinyML Inference**: Menggabungkan **Softmax Neural Classifier** (`IDLE`, `NORMAL_OPERATION`, `ANOMALY_DETECTED`) dengan perhitungan **Skor Anomali Jarak Centroid Terbobot (Mahalanobis-style)** untuk menangkap pola kerusakan baru.
4. **Dual Operating Mode**: Dapat beralih antara **Mode Inferensi Real-Time** dan **Mode CSV Data Forwarder** (`edge-impulse-data-forwarder`) untuk melatih model kustom di **Edge Impulse Studio**.

---

## Fitur Utama
- **Tanpa Ketergantungan Library Eksternal:** Mengakses register I2C `0x68` MPU6050 secara langsung menggunakan `Wire.h` dan menyertakan bobot model siap pakai di [`anomaly_model.h`](./src/edge_ai_anomaly_detection/anomaly_model.h), sehingga langsung dapat di-*compile* di Arduino IDE maupun Wokwi tanpa error.
- **Ultra-Low Latency (`~200 µs`):** Total waktu ekstraksi fitur DSP + inferensi model hanya memakan waktu sekitar `0.2 milidetik` pada ESP32.
- **Built-in Fault Simulator:** Tekan tombol fisik pada `GPIO 14` (atau kirim karakter `'0'`, `'1'`, `'2'` di Serial Monitor) untuk menguji respons sistem terhadap getaran motor normal maupun kerusakan *bearing* secara instan di simulator Wokwi.

---

## Struktur File Proyek

```text
09-edge-ai-anomaly-detection/
├── README.md                                          # Dokumentasi utama proyek ini
├── src/
│   └── edge_ai_anomaly_detection/
│       ├── edge_ai_anomaly_detection.ino              # Sketch utama Akuisisi & Inferensi Edge AI
│       └── anomaly_model.h                            # Ekstraksi Fitur DSP & Model TinyML
├── schematics/
│   ├── diagram.json                                   # Rangkaian simulasi Wokwi (ESP32 + MPU6050)
│   └── wiring-guide.md                                # Pinout I2C, pemasangan mekanis & pipeline
└── docs/
    └── edge-impulse-workflow.md                       # Panduan training di Edge Impulse & log Serial
```

---

## Cara Menjalankan di Arduino IDE & Simulator Wokwi

1. Buka [`src/edge_ai_anomaly_detection/edge_ai_anomaly_detection.ino`](./src/edge_ai_anomaly_detection/edge_ai_anomaly_detection.ino) di **Arduino IDE** (pastikan [`anomaly_model.h`](./src/edge_ai_anomaly_detection/anomaly_model.h) berada di folder yang sama).
2. Pilih board **ESP32 Dev Module** / **DOIT ESP32 DEVKIT V1**, lalu klik **Upload** (tidak memerlukan instalasi library tambahan).
3. Buka **Serial Monitor (`115200` baud)**:
   - Tekan tombol pada **`GPIO 14`** (atau ketik `'1'` / `'2'` di Serial Monitor) untuk melihat perubahan klasifikasi dari `IDLE` -> `NORMAL_OPERATION` (LED Hijau menyala) -> `ANOMALY_DETECTED` (LED Merah & Buzzer berbunyi).
   - Ketik `'m'` di Serial Monitor jika ingin mengubah output menjadi format CSV `ax,ay,az` untuk direkam oleh `edge-impulse-data-forwarder`.
4. Untuk menjalankan di **Wokwi**, salin `edge_ai_anomaly_detection.ino`, tab `anomaly_model.h`, dan [`schematics/diagram.json`](./schematics/diagram.json).
