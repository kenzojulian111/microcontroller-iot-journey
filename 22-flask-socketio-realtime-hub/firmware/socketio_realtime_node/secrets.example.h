#pragma once

// Copy this file to secrets.h and configure for your local Wi-Fi and Flask-SocketIO Hub.

#define WIFI_SSID        "IOT_LAB_24GHZ"
#define WIFI_PASSWORD    "ReplaceWithStrongWPA2Passphrase"

// Flask-SocketIO / WebSocket Hub Host & Port
#define WS_HUB_HOST      "192.168.1.100"
#define WS_HUB_PORT      5003
#define WS_HUB_PATH      "/socket.io/?EIO=4&transport=websocket"

#define DEVICE_ID        "esp32-rt-node-01"
#define DEVICE_TOKEN     "iot-lab-track3-ws-token-2026"
