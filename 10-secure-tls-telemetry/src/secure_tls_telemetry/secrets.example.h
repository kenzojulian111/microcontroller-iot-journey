#ifndef SECRETS_H
#define SECRETS_H

#include <pgmspace.h>

// ============================================================================
// 1. Konfigurasi Jaringan Wi-Fi
// ============================================================================
// (Untuk simulasi Wokwi, gunakan SSID: "Wokwi-GUEST" dan Password: "")
#define WIFI_SSID               "GANTI_DENGAN_SSID_WIFI"
#define WIFI_PASSWORD           "GANTI_DENGAN_PASSWORD_WIFI"

// ============================================================================
// 2. Pilih Mode Autentikasi TLS Broker Cloud:
//    - Set USE_MUTUAL_TLS_X509 = 1 untuk AWS IoT Core (Root CA + Client Cert + Private Key)
//    - Set USE_MUTUAL_TLS_X509 = 0 untuk HiveMQ Cloud Serverless (Root CA + Username/Password)
// ============================================================================
#define USE_MUTUAL_TLS_X509     1

// Endpoint MQTTS Broker (Port standar MQTT over TLS adalah 8883)
// Contoh AWS IoT Core : "a1b2c3d4e5f6g7-ats.iot.ap-southeast-1.amazonaws.com"
// Contoh HiveMQ Cloud : "8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c.s1.eu.hivemq.cloud"
#define MQTTS_BROKER_HOST       "ganti-endpoint-kamu-ats.iot.ap-southeast-1.amazonaws.com"
#define MQTTS_BROKER_PORT       8883
#define DEVICE_THING_NAME       "esp32-secure-node-01"

// Kredensial Username & Password (Digunakan jika USE_MUTUAL_TLS_X509 == 0, misal HiveMQ Cloud)
#define MQTTS_USERNAME          "hivemq_device_user"
#define MQTTS_PASSWORD          "PasswordKuYangSangatKuat123!"

// Topik Telemetri & Kontrol Device Shadow
#define TOPIC_TELEMETRY_PUB     "iotlab/devices/" DEVICE_THING_NAME "/telemetry"
#define TOPIC_COMMAND_SUB       "iotlab/devices/" DEVICE_THING_NAME "/command"

// ============================================================================
// 3. Sertifikat X.509 dalam Memori Flash (PROGMEM)
// ============================================================================

// A. Sertifikat Root CA Server (Contoh: Amazon Root CA 1 atau ISRG Root X1 Let's Encrypt)
static const char ROOT_CA_PEM[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
... GANTI DENGAN ISI FILE AmazonRootCA1.pem ATAU isrgrootx1.pem ...
-----END CERTIFICATE-----
)EOF";

// B. Sertifikat Klien Perangkat (Khusus AWS IoT Core mTLS: xxxx-certificate.pem.crt)
static const char CLIENT_CERT_PEM[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUVx...
... GANTI DENGAN ISI FILE DEVICE CERTIFICATE (.pem.crt) DARI AWS IOT ...
-----END CERTIFICATE-----
)EOF";

// C. Kunci Privat Perangkat (Khusus AWS IoT Core mTLS: xxxx-private.pem.key)
// PERINGATAN: Jangan pernah mengunggah kunci privat asli ke repositori Git publik!
static const char PRIVATE_KEY_PEM[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA...
... GANTI DENGAN ISI FILE PRIVATE KEY (.pem.key) DARI AWS IOT ...
-----END RSA PRIVATE KEY-----
)EOF";

#endif // SECRETS_H
