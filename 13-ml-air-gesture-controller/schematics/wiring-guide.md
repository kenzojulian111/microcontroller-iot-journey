# Wiring Guide — Project 13: 6-DoF Inertial Air Gesture Controller

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Module | Function |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 / ESP32-S3 | 100 Hz IMU motion capture, temporal descriptor extraction, and TFLM neural inference |
| **6-DoF Inertial Sensor** | InvenSense MPU6050 Breakout (`GY-521`) | 3-axis MEMS accelerometer ($\pm 4g$) + 3-axis gyroscope ($\pm 500\,^\circ/\text{s}$) |
| **RGB Luminaires** | 5mm Common-Cathode RGB LED + $3 \times 220\ \Omega$ | Controlled via PWM brightness (`SWIPE_UP`/`SWIPE_DOWN`) and color presets (`CIRCLE_CW`) |
| **Appliance Relay** | 1-Channel Opto-Isolated Relay Module | Toggled ON/OFF via `DOUBLE_TAP` gesture |
| **Synthetic Gesture Button** | Momentary Tactile Pushbutton | Injects recorded 80-sample 100 Hz IMU waveforms for instant Wokwi testing |

---

## 2. Pinout & Connection Matrix

| ESP32 Pin | Peripheral Pin | Signal Type | Notes |
| :--- | :--- | :--- | :--- |
| `GPIO 21` | `MPU6050 SDA` | I2C Data | I2C address `0x68` (`AD0` pulled LOW) |
| `GPIO 22` | `MPU6050 SCL` | I2C Clock | $400\text{ kHz}$ Fast Mode |
| `GPIO 25` | `RGB LED Red` (via $220\ \Omega$) | PWM Output | Red channel (`analogWrite`) |
| `GPIO 26` | `RGB LED Green` (via $220\ \Omega$) | PWM Output | Green channel (`analogWrite`) |
| `GPIO 27` | `RGB LED Blue` (via $220\ \Omega$) | PWM Output | Blue channel (`analogWrite`) |
| `GPIO 14` | `Relay Module IN` | Digital Output | Master load switch (`DOUBLE_TAP`) |
| `GPIO 15` | `Demo Button Pin 1` | Digital Input (`INPUT_PULLUP`) | Cycles `SWIPE_UP` $\rightarrow$ `SWIPE_DOWN` $\rightarrow$ `CIRCLE_CW` $\rightarrow$ `DOUBLE_TAP` |

---

## 3. Sensor Orientation & DLPF Configuration

1. **Coordinate Frame:** Hold the wand/breadboard with the MPU6050 `+X` axis pointing forward, `+Y` pointing left, and `+Z` pointing vertically upward ($+1g$ at rest).
2. **Digital Low-Pass Filter (DLPF):** Configured to `MPU6050_BAND_44_HZ` in hardware so 100 Hz sampling satisfies the Nyquist criterion ($f_{\text{Nyquist}} = 50\text{ Hz} > 44\text{ Hz}$) without high-frequency mechanical aliasing.
