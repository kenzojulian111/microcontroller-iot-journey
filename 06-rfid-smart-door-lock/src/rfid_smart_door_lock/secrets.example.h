#ifndef SECRETS_H
#define SECRETS_H

// Salin file ini menjadi "secrets.h" di folder yang sama,
// lalu isi dengan kredensial Wi-Fi dan konfigurasi Server REST API Access Log kamu.

#define WIFI_SSID        "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD    "GANTI_DENGAN_PASSWORD_WIFI"

// Endpoint REST API Backend (contoh server Python FastAPI di jaringan lokal)
#define API_ENDPOINT_URL "http://192.168.1.100:8000/api/v1/access-log"
#define API_SECRET_KEY   "iotlab-doorlock-secret-key-2026"

// Identitas Pintu / Perangkat
#define DEVICE_ID        "door_lab_utama"

#endif // SECRETS_H
