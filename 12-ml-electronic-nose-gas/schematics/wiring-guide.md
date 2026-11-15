# Wiring Guide — Project 12: Multi-Gas Electronic Nose (E-Nose)

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Module | Function |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 | Wi-Fi + dual-channel 12-bit SAR ADC + 2-layer FCNN inference engine |
| **Gas Sensor 1** | MQ-135 Analog Module | High sensitivity to $\text{NH}_3$, Alcohol, Benzene, $\text{CO}_2$, and Smoke |
| **Gas Sensor 2** | MQ-2 Analog Module | High sensitivity to LPG, Propane, Methane, $\text{H}_2$, and Combustible Smoke |
| **Thermo-Hygrometer** | DHT22 (or Bosch BME680) | Ambient Temperature ($^\circ\text{C}$) and Relative Humidity ($\%$) for $R_s/R_0$ drift compensation |
| **Acoustic Alarm** | Piezo Buzzer | Emits $2.6\text{ kHz}$ continuous pulse on `LPG_LEAK_HAZARD` and $1.2\text{ kHz}$ chirp on `CIGARETTE_SMOKE` |
| **Status Indicators** | Green & Red 5mm LEDs + $220\ \Omega$ | Visual indication of `CLEAN_AIR` vs. active gas/VOC hazard |
| **Calibration Button** | Tactile Pushbutton | Triggers 16-sample clean-air baseline ($R_0$) calibration |

---

## 2. Pinout & Connection Matrix

| ESP32 Pin | Peripheral Pin | Signal Type | Notes |
| :--- | :--- | :--- | :--- |
| `GPIO 34` | `MQ-135 AOUT` | Analog Input (`ADC1_CH6`) | Uses `ADC1` (compatible with simultaneous Wi-Fi operation!) |
| `GPIO 35` | `MQ-2 AOUT` | Analog Input (`ADC1_CH7`) | Uses `ADC1` ($10\text{ k}\Omega$ voltage divider on module) |
| `GPIO 4` | `DHT22 SDA` | 1-Wire Digital | Temperature & humidity compensation source |
| `GPIO 18` | `Buzzer (+)` | PWM Tone Output | Acoustic hazard alarm |
| `GPIO 19` | `Green LED Anode` (via $220\ \Omega$) | Digital Output | Lit when `CLEAN_AIR` is classified |
| `GPIO 23` | `Red LED Anode` (via $220\ \Omega$) | Digital Output | Flashes on `ALCOHOL_VOC`/`CIGARETTE_SMOKE`, solid on `LPG_LEAK_HAZARD` |
| `GPIO 15` | `Calibrate Button` | Digital Input (`INPUT_PULLUP`) | Active-LOW baseline $R_0$ calibration |

---

## 3. Critical Hardware Design Notes

1. **Why `ADC1` (`GPIO 34`, `GPIO 35`) is Mandatory:** On the ESP32, `ADC2` pins cannot be sampled while the Wi-Fi radio is active (`WiFi.begin()`). Routing both MQ sensors to `ADC1` guarantees glitch-free simultaneous ADC sampling and MQTT publishing.
2. **5V Heater Supply & 3.3V ADC Protection:** Physical MQ-135 and MQ-2 heaters require $5\text{ V}$ (`VIN`) at $\sim 150\text{ mA}$ each. Use a resistive divider ($20\text{ k}\Omega / 10\text{ k}\Omega$) on `AOUT` when powering physical MQ modules from $5\text{ V}$ so the ESP32 ADC input never exceeds $3.3\text{ V}$.
