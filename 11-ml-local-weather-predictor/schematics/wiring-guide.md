# Wiring Guide — Project 11: Edge TinyML Local Weather Predictor

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Module | Function |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 (or ESP8266 NodeMCU) | 240 MHz dual-core MCU running feature extraction & Random Forest inference |
| **Primary Environmental Sensor** | Bosch BME280 Breakout (I2C) | Precision barometric pressure ($\pm 0.12\text{ hPa}$), temperature, and relative humidity |
| **OLED Display** | SSD1306 $128 \times 64$ I2C OLED | Displays live meteorological features, class forecast, and ensemble confidence |
| **Simulation Fallback Sensors** | DHT22 + $10\text{ k}\Omega$ Potentiometer | Used in Wokwi simulation to sweep pressure ($980\text{–}1030\text{ hPa}$) and humidity |
| **Storm Front Trigger** | Momentary Tactile Pushbutton | Injects a synthetic $-5.4\text{ hPa/hr}$ cold-front barometric drop for validation |
| **Alert Indicator** | 5mm Red LED + $220\ \Omega$ Resistor | Blinks on `RAIN_IMMINENT` and latches solid on `STORM_FRONT_RAPID_DROP` |

---

## 2. Pinout & Connection Matrix

| ESP32 Pin | Peripheral Pin | Signal Type | Notes |
| :--- | :--- | :--- | :--- |
| `GPIO 21` | `BME280 SDA` & `SSD1306 SDA` | I2C Data | Shared I2C bus ($4.7\text{ k}\Omega$ pull-up to 3.3V) |
| `GPIO 22` | `BME280 SCL` & `SSD1306 SCL` | I2C Clock | Shared I2C bus ($400\text{ kHz}$ Fast Mode) |
| `GPIO 4` | `DHT22 SDA` | 1-Wire Digital | Wokwi fallback temperature/humidity sensor |
| `GPIO 34` | `Potentiometer SIG` | Analog Input (`ADC1_CH6`) | Wokwi fallback barometric pressure dial ($0\text{–}3.3\text{ V} \rightarrow 980\text{–}1030\text{ hPa}$) |
| `GPIO 15` | `Pushbutton Pin 1` | Digital Input (`INPUT_PULLUP`) | Active-LOW cold-front simulator trigger |
| `GPIO 2` | `Red LED Anode` (via $220\ \Omega$) | Digital Output | Storm/Rain visual alarm |
| `3V3` | `VCC` (All modules) | Power Supply | $3.3\text{ V}$ regulated rail |
| `GND` | `GND` (All modules) | Common Ground | Shared ground reference |

---

## 3. Barometric Sensor Placement & Hardware Filtering Notes

1. **Static Pressure Venting:** Ensure the Bosch BME280 vent hole is shielded from direct fan airflow or wind gusts (dynamic pressure stagnation $\Delta P = \frac{1}{2}\rho v^2$ can induce false $0.5\text{–}1.5\text{ hPa}$ spikes).
2. **Hardware IIR Filter:** The firmware configures the BME280 internal IIR filter to `FILTER_X16` and pressure oversampling to `SAMPLING_X16`, reducing RMS pressure noise to $< 0.02\text{ hPa}$ so that the Ordinary Least Squares (OLS) derivative $\Delta P / \Delta t$ reliably resolves microbarometric trends down to $\pm 0.1\text{ hPa/hr}$.
3. **Thermal Isolation:** Mount the BME280 at least $3\text{ cm}$ away from the ESP32 voltage regulator and Wi-Fi antenna to prevent self-heating from skewing the Magnus-Tetens dew point spread ($T - T_{dew}$).
