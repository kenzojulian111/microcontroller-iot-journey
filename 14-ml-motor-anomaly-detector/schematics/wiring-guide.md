# Wiring Guide — Project 14: Unsupervised Motor Anomaly Detector

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Module | Function |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 | 200 Hz vibration acquisition, `8 -> 3 -> 8` Autoencoder + K-Means engine |
| **Vibration Sensor** | MPU6050 / ADXL345 3-Axis Accelerometer | Rigidly bolted to motor bearing housing to measure radial ($X, Y$) & vertical ($Z$) vibration |
| **Protective Cutoff Relay** | 1-Channel Relay Module (`GPIO 14`) | Automatically interrupts motor contactor coil when MSE reconstruction error exceeds safety limit |
| **Self-Learn Button** | Tactile Pushbutton (`GPIO 15`) | Triggers 10-window On-Device Self-Learning Calibration (`CALIBRATE`) |
| **Fault Simulator Dial** | $10\text{ k}\Omega$ Potentiometer (`GPIO 34`) | Sweeps simulated machinery fault modes in Wokwi (`NOMINAL` $\rightarrow$ `IMBALANCE` $\rightarrow$ `BEARING` $\rightarrow$ `LOOSE`) |
| **Diagnostic LEDs** | Green, Yellow, Red 5mm LEDs + $220\ \Omega$ | Indicates `NOMINAL_HEALTHY`, `MAINTENANCE_WARNING`, and `CRITICAL_RELAY_TRIP` |

---

## 2. Pinout & Connection Matrix

| ESP32 Pin | Peripheral Pin | Signal Type | Notes |
| :--- | :--- | :--- | :--- |
| `GPIO 21` | `Accelerometer SDA` | I2C Data | $400\text{ kHz}$ I2C bus |
| `GPIO 22` | `Accelerometer SCL` | I2C Clock | $400\text{ kHz}$ I2C bus |
| `GPIO 34` | `Fault Simulator Potentiometer` | Analog Input (`ADC1_CH6`) | Selects simulated machinery vibration regime in Wokwi |
| `GPIO 15` | `Self-Learn Button` | Digital Input (`INPUT_PULLUP`) | Active-LOW trigger for on-device baseline learning |
| `GPIO 14` | `Cutoff Relay IN` | Digital Output | `HIGH` = Motor Running, `LOW` = Tripped (Fail-Safe Cutoff) |
| `GPIO 19` | `Green LED Anode` (via $220\ \Omega$) | Digital Output | Lit during `NOMINAL_HEALTHY` operation |
| `GPIO 18` | `Yellow LED Anode` (via $220\ \Omega$) | Digital Output | Lit on `SHAFT_IMBALANCE_1X` or `BEARING_WEAR_SPALLING` |
| `GPIO 23` | `Red LED Anode` (via $220\ \Omega$) | Digital Output | Lit on `LOOSE_MOUNTING_SEVERE` / Latched Relay Cutoff |

---

## 3. Mechanical Mounting Best Practices for Vibration Sensing

1. **Rigid Stud / Epoxy Coupling:** Soft double-sided foam tape acts as a mechanical low-pass elastomer damper above $60\text{ Hz}$, attenuating high-frequency bearing spalling impacts (Feature $x_6$: HF Energy Ratio and Feature $x_4$: Crest Factor). Use an M3 machine screw or cyanoacrylate adhesive directly on the motor end-bell near the drive-end bearing.
2. **Axis Alignment:** Align $+X$ and $+Y$ radially (perpendicular to the motor shaft) and $+Z$ vertically perpendicular to the mounting baseplate so the radial-to-vertical ratio $x_7 = (\text{RMS}_x + \text{RMS}_y)/(2\,\text{RMS}_z)$ cleanly separates centrifugal rotor imbalance from loose baseplate bolts.
