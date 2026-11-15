/**
 * @file secrets.example.h
 * @brief Wi-Fi credentials and HTTP telemetry configuration for Project 16
 *        (ESP32-S3 FOMO Embedded Vision Object Counter).
 */

#ifndef SECRETS_EXAMPLE_H
#define SECRETS_EXAMPLE_H

#define WIFI_SSID           "Wokwi-GUEST"
#define WIFI_PASSWORD       ""
#define HTTP_SERVER_PORT    80

// Set to 1 on physical XIAO ESP32S3 Sense or AI-Thinker ESP32-CAM with OV2640 attached;
// leave 0 (or auto-fallback) to use the synthetic 96x96 moving-blob optical generator in Wokwi.
#define ENABLE_HW_OV2640    0

#endif // SECRETS_EXAMPLE_H
