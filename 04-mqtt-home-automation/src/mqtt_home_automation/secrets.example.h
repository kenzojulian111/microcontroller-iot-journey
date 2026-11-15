#ifndef SECRETS_H
#define SECRETS_H

// Salin file ini menjadi "secrets.h" di folder yang sama,
// lalu sesuaikan dengan kredensial Wi-Fi dan MQTT Broker kamu.
// (Untuk simulasi Wokwi, biarkan atau gunakan SSID: "Wokwi-GUEST" dan broker publik)

#define WIFI_SSID     "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD "GANTI_DENGAN_PASSWORD_WIFI"

// Konfigurasi MQTT Broker
// Gunakan IP lokal (misal "192.168.1.100") untuk Mosquitto lokal / Home Assistant,
// atau gunakan broker publik gratis seperti "broker.emqx.io" / "test.mosquitto.org" untuk uji coba.
#define MQTT_BROKER   "broker.emqx.io"
#define MQTT_PORT     1883
#define MQTT_USER     ""
#define MQTT_PASSWORD ""

// Identitas Node MQTT (Ubah jika memiliki beberapa node di rumah, misal: "node02")
#define NODE_ID       "node01"

#endif // SECRETS_H
