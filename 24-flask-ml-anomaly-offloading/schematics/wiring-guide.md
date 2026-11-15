# Project 24 — Hardware Wiring Guide (`TURBINE-NODE-04` Edge Sensor Streamer)

## 1. Dual-Architecture Pinout Matrix (ESP32 & Arduino Uno/Nano)

The firmware (`ml_sensor_streamer.ino`) supports both **32-bit ESP32** (Wi-Fi HTTP POST + UART) and **8-bit ATmega328P Arduino Uno/Nano** (115200 bps USB UART) via compile-time `#if defined(ESP32)` guards.

| Peripheral / Signal | ESP32 DevKit V1 | Arduino Uno / Nano | Direction | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **MPU6050 `SDA`** | `GPIO 21` | `A4` | Bidir (I2C) | 4.7 k$\Omega$ pull-up to 3.3 V |
| **MPU6050 `SCL`** | `GPIO 22` | `A5` | Output (I2C) | 400 kHz Fast-Mode I2C (`0x68`) |
| **MQ-135 Gas `AOUT`** | `GPIO 34` (`ADC1_6`) | `A0` | Analog In | Voltage divider if MQ-135 powered at 5 V on ESP32 |
| **Emergency Contactor Relay** | `GPIO 26` | `D7` | Digital Out | `HIGH` = Motor Running, `LOW` = Emergency Trip |
| **Piezo Alarm Buzzer** | `GPIO 25` | `D8` | PWM Out | 1480 Hz chirp (`WARNING`), 2400 Hz alarm (`CRITICAL`) |
| **Green LED (`NORMAL`)** | `GPIO 32` | `D4` | Digital Out | 220 $\Omega$ current-limiting resistor |
| **Yellow LED (`WARNING`)** | `GPIO 33` | `D5` | Digital Out | 220 $\Omega$ current-limiting resistor |
| **Red LED (`CRITICAL`)** | `GPIO 27` | `D6` | Digital Out | 220 $\Omega$ current-limiting resistor |
| **Fault Regime Button** | `GPIO 14` | `D2` | `INPUT_PULLUP` | Cycles Nominal $\rightarrow$ Bearing $\rightarrow$ Thermal $\rightarrow$ Cavitation |

---

## 2. MQ-135 5 V to 3.3 V ADC Scaling Note (ESP32)

The MQ-135 tin-dioxide ($\text{SnO}_2$) heater coil requires 5 V (`VIN`), meaning its analog output `AOUT` can swing up to 4.5 V under high VOC/smoke concentration. When interfacing with the ESP32's 3.3 V ADC (`GPIO 34`), insert a **10 k$\Omega$ / 20 k$\Omega$ resistive voltage divider** between `AOUT` and `GND` so the maximum voltage at `GPIO 34` never exceeds $3.0\text{ V}$.
