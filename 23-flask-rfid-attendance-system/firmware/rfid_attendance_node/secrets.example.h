#ifndef SECRETS_H
#define SECRETS_H

// ============================================================================
// Wi-Fi Network Credentials
// ============================================================================
// In Wokwi simulation, use "Wokwi-GUEST" with an empty password "".
// On physical ESP32/ESP8266 hardware, replace with your 2.4 GHz WPA2 SSID.
#define WIFI_SSID       "Wokwi-GUEST"
#define WIFI_PASSWORD   ""

// ============================================================================
// Flask Attendance Server Endpoint Configuration
// ============================================================================
// Replace with the LAN IP address of the host running `python3 backend/app.py`
#define FLASK_HOST      "192.168.1.100"
#define FLASK_PORT      5023
#define PUNCH_ENDPOINT  "/api/v1/attendance/punch"

// ============================================================================
// Terminal Provisioning Metadata
// ============================================================================
#define TERMINAL_ID     "GATE-01"
#define API_SHARED_KEY  "iot-lab-rfid-hmac-2026"

#endif // SECRETS_H
