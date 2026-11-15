#pragma once

// Copy this file to secrets.h and populate with your local network & API credentials.
// Do NOT commit secrets.h to version control.

#define WIFI_SSID        "IOT_LAB_24GHZ"
#define WIFI_PASSWORD    "ReplaceWithStrongWPA2Passphrase"

// Flask REST API Ingestion Endpoint & Shared Secret Token
#define API_ENDPOINT_URL "http://192.168.1.100:5002/api/v1/telemetry"
#define API_KEY_SECRET   "iot-lab-track3-weather-secret-key-2026"
#define NODE_ID          "esp32-weather-01"
