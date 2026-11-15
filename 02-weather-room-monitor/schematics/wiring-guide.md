# Hardware Wiring & I2C Bus Guide — 02 Real-Time Weather & Room Monitor

This document specifies the electrical wiring between the microcontroller (**ESP32** or **ESP8266 NodeMCU**), the **DHT22** temperature/humidity sensor (or optional **BME280**), and the **0.96" SSD1306 I2C OLED** display.

---

## 1. Pinout Mapping Table (ESP32 vs. ESP8266)

The firmware in [`weather_room_monitor.ino`](../src/weather_room_monitor/weather_room_monitor.ino) automatically maps the following pins based on the target board architecture:

| Component | Module Pin | ESP32 (DevKit V1) | ESP8266 (NodeMCU) | Electrical Function & Notes |
| :--- | :--- | :--- | :--- | :--- |
| **DHT22 Sensor** | `VCC` / `+` | `3V3` (3.3V) | `3V3` (3.3V) | Sensor supply rail (3.3V–5V DC; 3.3V recommended for 3.3V MCU logic) |
| **DHT22 Sensor** | `DATA` / `SDA` | `GPIO 15` | `D5` (`GPIO 14`) | 1-Wire digital data bus |
| **DHT22 Sensor** | `GND` / `-` | `GND` | `GND` | Common ground reference |
| **OLED SSD1306 (I2C)** | `VCC` / `VDD` | `3V3` (3.3V) | `3V3` (3.3V) | I2C OLED display power supply |
| **OLED SSD1306 (I2C)** | `GND` | `GND` | `GND` | Common ground reference |
| **OLED SSD1306 (I2C)** | `SDA` | `GPIO 21` | `D2` (`GPIO 4`) | I2C Serial Data line |
| **OLED SSD1306 (I2C)** | `SCL` / `SCK` | `GPIO 22` | `D1` (`GPIO 5`) | I2C Serial Clock line |
| **BME280 (Optional)** | `SDA` & `SCL` | `GPIO 21` & `GPIO 22` | `D2` & `D1` | Shared parallel I2C bus (`0x76` or `0x77`) |
| **Wi-Fi Indicator** | Anode | `GPIO 2` (Onboard) | `LED_BUILTIN` (`D4`) | Flashes during connection; solid when associated |

---

## 2. I2C Bus & Cloud Telemetry Architecture Diagram

```mermaid
flowchart LR
    subgraph Sensors["Environmental Sensor Inputs"]
        DHT["DHT22 Sensor (Temp & RH)"] -->|1-Wire: GPIO15 / D5| MCU
        BME["Optional: BME280 (Temp, RH, Pressure)"] -.->|Shared I2C Bus| MCU
    end

    subgraph Controller["Microcontroller"]
        MCU["ESP32 / ESP8266"]
    end

    subgraph Display["Local Edge Display"]
        MCU -->|I2C: SDA & SCL (0x3C)| OLED["OLED 0.96 inch SSD1306 (128x64)"]
    end

    subgraph Cloud["Cloud IoT Platforms"]
        MCU -->|HTTP POST Every 15s| TS["ThingSpeak Channel"]
        MCU -.->|HTTP POST JSON| TB["ThingsBoard / Custom Endpoint"]
    end
```

---

## 3. Signal Integrity & Pull-Up Resistor Requirements for DHT22

- **3-Pin Breakout PCB Modules:** Commercial 3-pin DHT22 modules include an onboard **4.7 kΩ or 10 kΩ pull-up resistor** between `VCC` and `DATA`, allowing direct connection to the MCU GPIO pin.
- **Bare 4-Pin DHT22 / AM2302 Package:** If using a standalone 4-pin sensor without a breakout PCB, you **must** connect an external **4.7 kΩ–10 kΩ pull-up resistor** between `VCC` (`3.3V`) and `DATA`. Without a pull-up resistor, the open-drain data line cannot return to logic `HIGH` cleanly, resulting in framing errors and `NaN` readings.

---

## 4. I2C Bus Addressing & Troubleshooting (`0x3C` vs. `0x3D`)

Monochrome 0.96" SSD1306 I2C OLED modules typically respond to the 7-bit I2C slave address **`0x3C`**, though certain hardware revisions are strapped for **`0x3D`**.

If the display remains blank or the UART console logs:
```text
[OLED] Peringatan: Layar SSD1306 tidak terdeteksi di 0x3C! Lanjut tanpa OLED.
```

Perform the following diagnostic steps:
1. Verify that **`SDA`** (`GPIO 21` / `D2`) and **`SCL`** (`GPIO 22` / `D1`) are not cross-wired.
2. Inspect the I2C address selection resistor on the rear silkscreen of the OLED module, or update the address macro in [`weather_room_monitor.ino`](../src/weather_room_monitor/weather_room_monitor.ino):
   ```cpp
   #define OLED_I2C_ADDR 0x3D
   ```
3. When wiring an optional **BME280** sensor in parallel on the same `SDA`/`SCL` lines, no address collision occurs because BME280 uses slave address `0x76` or `0x77` while the SSD1306 uses `0x3C`/`0x3D`.
