#ifndef SECRETS_H
#define SECRETS_H

// Salin file ini menjadi "secrets.h" di folder yang sama,
// lalu isi dengan kredensial Wi-Fi dan Cloud IoT kamu.
// (Untuk simulasi Wokwi, gunakan SSID: "Wokwi-GUEST" dan Password: "")

#define WIFI_SSID             "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD         "GANTI_DENGAN_PASSWORD_WIFI"

// Konfigurasi ThingSpeak (https://thingspeak.com)
// Biarkan nilai default atau kosong jika hanya ingin mengetes tampilan OLED lokal.
#define THINGSPEAK_API_KEY    "GANTI_DENGAN_WRITE_API_KEY"
#define THINGSPEAK_CHANNEL_ID 0000000

// Opsional: Endpoint HTTP JSON generik (misal: ThingsBoard / Webhook / Server Lokal)
// Contoh ThingsBoard: "http://demo.thingsboard.io/api/v1/<ACCESS_TOKEN>/telemetry"
#define CUSTOM_JSON_ENDPOINT  ""

#endif // SECRETS_H
