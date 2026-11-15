#ifndef SECRETS_H
#define SECRETS_H

// Salin file ini menjadi "secrets.h" di folder yang sama,
// lalu sesuaikan dengan kredensial Wi-Fi, password OTA, dan URL server firmware kamu.
// (Untuk simulasi Wokwi, gunakan SSID: "Wokwi-GUEST" dan Password: "")

#define WIFI_SSID             "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD         "GANTI_DENGAN_PASSWORD_WIFI"

// Password untuk autentikasi Local Push OTA dari Arduino IDE (ArduinoOTA)
#define OTA_HOSTNAME          "esp-ota-node-01"
#define OTA_PASSWORD          "admin-ota-rahasia"

// URL Endpoint Cloud Pull OTA (File manifest version.json & binary .bin)
// Contoh hosting di GitHub Releases / Python HTTP Server lokal
#define VERSION_MANIFEST_URL  "http://192.168.1.100:8000/version.json"
#define FIRMWARE_BIN_URL      "http://192.168.1.100:8000/ota_firmware_manager.ino.bin"

#endif // SECRETS_H
