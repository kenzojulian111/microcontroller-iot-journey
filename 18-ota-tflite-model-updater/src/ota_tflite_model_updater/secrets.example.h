/**
 * @file secrets.example.h
 * @brief Wi-Fi and Edge MLOps Model Registry endpoint configuration for Project 18
 *        (OTA TensorFlow Lite / Binary Model Hot-Swapping Pipeline).
 */

#ifndef SECRETS_EXAMPLE_H
#define SECRETS_EXAMPLE_H

#define WIFI_SSID               "Wokwi-GUEST"
#define WIFI_PASSWORD           ""

// Remote Model Registry Manifest URL (JSON containing model_version, sha256, download_url)
#define MODEL_MANIFEST_URL      "http://192.168.1.100:8080/models/model_manifest.json"
#define MANIFEST_POLL_INTERVAL_MS 30000

// LittleFS Storage Paths for Atomic Dual-Slot Model Management
#define ACTIVE_MODEL_PATH       "/models/active_model.bin"
#define STAGING_MODEL_PATH      "/models/staging_model.bin"
#define BACKUP_MODEL_PATH       "/models/backup_model.bin"

#endif // SECRETS_EXAMPLE_H
