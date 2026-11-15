#ifndef SECRETS_H
#define SECRETS_H

// ============================================================================
// Wi-Fi Network Credentials (Used when compiled for ESP32 / ESP8266 targets)
// ============================================================================
// Note: When compiled for AVR (Arduino Uno / Nano), Wi-Fi is automatically
// bypassed at compile time via `#if defined(ESP32)` preprocessor guards,
// and telemetry is streamed over 115200 bps UART JSON lines instead.
#define WIFI_SSID        "Wokwi-GUEST"
#define WIFI_PASSWORD    ""

// ============================================================================
// Flask Scikit-Learn ML Inference Endpoint
// ============================================================================
#define FLASK_HOST       "192.168.1.100"
#define FLASK_PORT       5024
#define INFER_ENDPOINT   "/api/v1/ml/infer"
#define NODE_ID          "TURBINE-NODE-04"

#endif // SECRETS_H
