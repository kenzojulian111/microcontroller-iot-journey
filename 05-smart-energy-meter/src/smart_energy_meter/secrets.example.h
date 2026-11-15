#ifndef SECRETS_H
#define SECRETS_H

// Salin file ini menjadi "secrets.h" di folder yang sama,
// lalu isi dengan kredensial Wi-Fi dan konfigurasi server InfluxDB v2 kamu.

#define WIFI_SSID       "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD   "GANTI_DENGAN_PASSWORD_WIFI"

// Konfigurasi Server InfluxDB v2 (Cloud maupun Self-Hosted Docker)
// Contoh Self-Hosted : "http://192.168.1.100:8086"
// Contoh InfluxCloud : "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_URL    "http://192.168.1.100:8086"
#define INFLUXDB_TOKEN  "GANTI_DENGAN_API_TOKEN_INFLUXDB"
#define INFLUXDB_ORG    "iot_lab_org"
#define INFLUXDB_BUCKET "energy_meter"

// Identitas Lokasi / Panel Meter
#define METER_LOCATION  "panel_utama"

#endif // SECRETS_H
