#ifndef SECRETS_H
#define SECRETS_H

// Salin file ini menjadi "secrets.h" di folder lora_mesh_gateway,
// lalu isi dengan SSID/Password Wi-Fi dan URL HTTP Webhook / Server Cloud kamu.

#define WIFI_SSID           "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD       "GANTI_DENGAN_PASSWORD_WIFI"

// Endpoint HTTP Webhook / REST API tujuan penerusan paket dari LoRa Mesh Gateway
// (Kosongkan "" jika hanya ingin memantau melalui Serial Monitor & Web Lokal)
#define CLOUD_WEBHOOK_URL   "http://192.168.1.100:8080/api/lora-telemetry"
#define GATEWAY_API_TOKEN   "token-rahasia-gateway-01"

#endif // SECRETS_H
