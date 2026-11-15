# REST API Specification & UART Diagnostic Output — 01 Web-Controlled Smart Switch

## 1. UART Serial Monitor Output (`115200` baud)

Upon power-on reset, GPIO initialization, and 802.11 Wi-Fi station association, the firmware emits structured diagnostic telemetry over the UART console:

```text
============================================
  IoT Lab - 01 Web-Controlled Smart Switch  
  Target Board: ESP32
============================================
[SWITCH] Relay 1 -> OFF
[SWITCH] Relay 2 -> OFF
[WIFI] Menghubungkan ke SSID: Rumah_IoT .....
[WIFI] Terhubung!
[WIFI] Alamat IP Web Server : http://192.168.1.42
[WIFI] Kekuatan Sinyal      : -54 dBm
[HTTP] Web Server aktif pada port 80.
[SWITCH] Relay 1 -> ON
[SWITCH] Relay 2 -> ON
[SWITCH] Relay 1 -> OFF
```

---

## 2. Local HTTP REST API Specification

In addition to serving the interactive web dashboard at `GET http://<IP_ADDRESS>/`, the embedded HTTP server exposes JSON endpoints on TCP port `80` for programmatic automation via `curl`, Postman, Home Assistant, or Node-RED. All API responses include `Access-Control-Allow-Origin: *`.

### a. Retrieve Device Telemetry & Relay States
- **Endpoint:** `GET /api/status`
- **Example Request:**
  ```bash
  curl http://192.168.1.42/api/status
  ```
- **Example Response (`200 OK`, `application/json`):**
  ```json
  {
    "board": "ESP32",
    "relay1": true,
    "relay2": false,
    "ip": "192.168.1.42",
    "rssi": -54,
    "uptime": 148,
    "heap": 294512
  }
  ```

| JSON Field | Type | Unit | Description |
| :--- | :--- | :--- | :--- |
| `board` | `string` | — | Target MCU architecture (`"ESP32"` or `"ESP8266"`) |
| `relay1` | `boolean` | — | Logical state of Channel 1 (`true` = ON, `false` = OFF) |
| `relay2` | `boolean` | — | Logical state of Channel 2 (`true` = ON, `false` = OFF) |
| `ip` | `string` | IPv4 | Station IP address assigned via DHCP |
| `rssi` | `integer` | `dBm` | Wi-Fi Received Signal Strength Indicator |
| `uptime` | `integer` | `seconds` | Monotonic seconds elapsed since boot (`millis() / 1000`) |
| `heap` | `integer` | `bytes` | Available free dynamic SRAM (`ESP.getFreeHeap()`) |

### b. Toggle a Specific Relay Channel
- **Endpoint:** `POST /api/toggle?ch=<1|2>` *(also accepts `GET` for browser testing)*
- **Query Parameters:** `ch` — Target relay channel (`1` or `2`). Returns `400 Bad Request` if omitted or out of bounds.
- **Example Request:**
  ```bash
  curl -X POST "http://192.168.1.42/api/toggle?ch=1"
  ```
- **Response (`200 OK`):** Returns the updated JSON status payload identical in schema to `/api/status`.

### c. Batch Set All Relay Channels
- **Endpoint:** `POST /api/all?state=<0|1>` *(also accepts `GET`)*
- **Query Parameters:** `state` — Target state for all channels (`1` or `on` to energize all relays; `0` or `off` to de-energize all relays).
- **Example Request (Energize All Channels):**
  ```bash
  curl -X POST "http://192.168.1.42/api/all?state=1"
  ```
- **Example Request (De-energize All Channels):**
  ```bash
  curl -X POST "http://192.168.1.42/api/all?state=0"
  ```
