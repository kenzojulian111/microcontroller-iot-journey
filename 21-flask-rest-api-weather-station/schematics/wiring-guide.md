# Hardware Wiring Guide — Project 21: Authenticated REST API Weather Station

## 1. Bill of Materials (BoM)

| Ref | Component | Specification | Qty |
| :--- | :--- | :--- | :--- |
| U1 | Wireless MCU | ESP32 DevKitC V4 or ESP8266 NodeMCU v3 | 1 |
| U2 | Barometric Sensor | Bosch BME280 Breakout ($I^2C$, $3.3\text{ V}$, Temp/RH/Pressure) | 1 |
| R1, R2 | $I^2C$ Pull-Ups | $4.7\text{ k}\Omega$ to $3.3\text{ V}$ (integrated on most BME280 modules) | 2 |
| C1 | Decoupling Capacitor | $100\text{ nF}$ ceramic across `3V3` and `GND` near BME280 | 1 |

---

## 2. Pin Mapping Matrix ($I^2C$ Bus)

| BME280 Pin | ESP32 DevKit V1 | ESP8266 NodeMCU | Signal Description |
| :--- | :--- | :--- | :--- |
| **`VIN` / `VCC`** | `3V3` | `3V3` | Regulated $3.3\text{ V}$ supply (do **not** feed $5\text{ V}$ to bare BME280) |
| **`GND`** | `GND` | `GND` | Common ground reference |
| **`SCL` / `SCK`** | `GPIO 22` | `D1` (`GPIO 5`) | $I^2C$ Serial Clock ($100\text{ kHz}$ standard mode) |
| **`SDA` / `SDI`** | `GPIO 21` | `D2` (`GPIO 4`) | $I^2C$ Serial Data |
| **`SDO` / `ADDR`** | `GND` (`0x76`) or `3V3` (`0x77`) | `GND` (`0x76`) | Firmware probes both `0x76` and `0x77` automatically |

---

## 3. Power & RF Layout Recommendations

1. **Self-Heating Mitigation:**
   Place the BME280 at least $5\text{ cm}$ away from the ESP32 voltage regulator (`AMS1117-3.3`) and Wi-Fi PCB antenna. Radiant heat from continuous Wi-Fi transmission can bias dry-bulb temperature readings upward by $+1.2\text{ }^{\circ}\text{C}$ if mounted directly above the module.
2. **Offline Ring Buffer Resilience:**
   When Wi-Fi signal drops or the Flask backend restarts, the ESP32 caches up to 32 weather frames in a circular SRAM ring buffer (`ringBuffer[32]`) and drains them in bursts of 4 frames per loop cycle once TCP connectivity returns.
