# Cloud Telemetry Payloads, OLED Layout & UART Output — 02 Weather & Room Monitor

## 1. UART Serial Monitor Output (`115200` baud)

When the microcontroller initializes the SSD1306 OLED over I2C, polls the DHT22 sensor, and transmits telemetry to the cloud, the UART console outputs:

```text
==================================================
  IoT Lab - 02 Real-Time Weather & Room Monitor   
  Target Board: ESP32
==================================================
[OLED] SSD1306 berhasil diinisialisasi pada alamat I2C 0x3C.
[SENSOR] Sensor DHT22 aktif pada GPIO 15.
[WIFI] Menghubungkan ke SSID: Rumah_IoT ....
[WIFI] Terhubung!
[WIFI] Alamat IP Perangkat : 192.168.1.48
[WIFI] Kekuatan Sinyal     : -52 dBm
[SENSOR] Suhu: 26.4 C | Kelembapan: 58.0 % | Heat Index: 27.1 C | Status: NYAMAN (IDEAL)
[SENSOR] Suhu: 26.5 C | Kelembapan: 58.2 % | Heat Index: 27.3 C | Status: NYAMAN (IDEAL)
[TELEMETRY] Payload JSON: {"device":"ESP32","temperature_c":26.50,"humidity_pct":58.20,"heat_index_c":27.31,"comfort":"NYAMAN (IDEAL)","rssi_dbm":-52,"uptime_s":15}
[THINGSPEAK] Berhasil kirim data! Entry ID: 142 (HTTP 200)
```

> [!NOTE]
> **Simulation / Unconfigured API Key Mode:**  
> If `THINGSPEAK_API_KEY` is left at its placeholder value in `secrets.h`, the firmware continues updating the local OLED display and serializing JSON telemetry to the UART console with status indicator `CLD:SIM-OK`.

---

## 2. SSD1306 OLED Framebuffer Layout (128×64 Pixels)

The OLED framebuffer is refreshed every **2.5 seconds** via a non-blocking timer:

```text
+-----------------------------------+
| WiFi:-52dBm            CLD:OK#14  |  <-- Top status bar: Wi-Fi RSSI & Cloud sync counter
|-----------------------------------|
| SUHU                   LEMBAP     |  <-- Primary metric headers
|  26.5 °C                58 %      |  <-- High-visibility telemetry (TextSize 2)
|-----------------------------------|
| Terasa: 27.3 C                    |  <-- Computed Heat Index (apparent temperature)
| Status: NYAMAN (IDEAL)            |  <-- Automated psychrometric comfort classification
+-----------------------------------+
```

### Automated Psychrometric Comfort Thresholds
- **`NYAMAN (IDEAL)`**: Ambient temperature within `20.0 °C – 27.5 °C` **and** relative humidity within `40% – 65%`.
- **`PANAS/LEMBAP`**: Ambient temperature `>= 32.0 °C` **or** relative humidity `>= 80%`.
- **`SEJUK/DINGIN`**: Ambient temperature `< 20.0 °C`.
- **`NORMAL`**: Transitional environmental states outside the ideal comfort zone but within non-extreme bounds.

---

## 3. Cloud Telemetry Protocol Specification

### a. ThingSpeak REST Ingestion (`http://api.thingspeak.com/update`)
When provisioning a new Channel on [ThingSpeak](https://thingspeak.com), enable the following four fields to match the firmware's uplink payload:

| ThingSpeak Field | Firmware Variable | Unit | Description |
| :--- | :--- | :--- | :--- |
| **`field1`** | `currentTempC` | `°C` | Ambient dry-bulb temperature |
| **`field2`** | `currentHumidity` | `%` | Relative humidity |
| **`field3`** | `currentHeatIndex` | `°C` | Computed apparent temperature (Heat Index) |
| **`field4`** | `WiFi.RSSI()` | `dBm` | Wi-Fi Received Signal Strength Indicator |

- **HTTP Method:** `POST /update`
- **Content-Type Header:** `application/x-www-form-urlencoded`
- **Example Request Body:**
  ```text
  api_key=XXXXXXXXXXXXXXXX&field1=26.50&field2=58.20&field3=27.31&field4=-52
  ```

### b. Generic HTTP JSON Webhook (ThingsBoard / Custom REST Server)
When `CUSTOM_JSON_ENDPOINT` is configured in `secrets.h` (for example, `http://demo.thingsboard.io/api/v1/<ACCESS_TOKEN>/telemetry`), the firmware dispatches an HTTP `POST` request with `Content-Type: application/json`:

```json
{
  "device": "ESP32",
  "temperature_c": 26.50,
  "humidity_pct": 58.20,
  "heat_index_c": 27.31,
  "comfort": "NYAMAN (IDEAL)",
  "rssi_dbm": -52,
  "uptime_s": 15
}
```
