# Hardware Wiring Guide — Project 20: Serial Sensor Telemetry Logger

## 1. Bill of Materials (BoM)

| Ref | Component | Specification | Qty |
| :--- | :--- | :--- | :--- |
| U1 | Microcontroller | Arduino Uno R3 (ATmega328P) or ESP32 DevKit V1 | 1 |
| U2 | Environmental Sensor | DHT22 (AM2302) Digital Temp & Humidity Sensor | 1 |
| R_PU | Pull-Up Resistor | $4.7\text{ k}\Omega\text{–}10\text{ k}\Omega$ between DHT22 `DATA` and `VCC` | 1 |
| R_LDR | Photoresistor | GL5528 CdS Light-Dependent Resistor ($10\text{ k}\Omega\text{–}1\text{ M}\Omega$) | 1 |
| R_DIV | Divider Resistor | $10\text{ k}\Omega \pm 1\%$ Metal Film Fixed Resistor | 1 |

---

## 2. Pin Mapping Matrix

| Signal | ATmega328P (Uno/Nano) | ESP32 DevKit V1 | Description |
| :--- | :--- | :--- | :--- |
| **DHT22 `DATA` (`SDA`)** | `D2` | `GPIO 4` | Single-wire digital bus with $10\text{ k}\Omega$ pull-up |
| **LDR Divider Tap (`AO`)** | `A0` (10-bit ADC, $5.0\text{ V}$) | `GPIO 34` (`ADC1_CH6`, 12-bit, $3.3\text{ V}$) | $16\times$ oversampled analog input |
| **Power (`VCC`)** | `5V` | `3V3` | Match ADC reference voltage |
| **Ground (`GND`)** | `GND` | `GND` | Common analog/digital return |

---

## 3. Analog Signal Conditioning ($16\times$ Oversampling)

The photoresistor forms a ratiometric voltage divider with $R_{\text{DIV}} = 10\text{ k}\Omega$:

$$V_{\text{ADC}} = V_{\text{REF}} \cdot \frac{R_{\text{DIV}}}{R_{\text{LDR}}(\text{Lux}) + R_{\text{DIV}}}$$

To suppress power-supply ripple and quantization noise, the firmware sums $N = 16$ consecutive ADC conversions and right-shifts by 4 bits ($\div 16$), yielding an effective $+2\text{ bits}$ of resolution enhancement ($\text{SNR gain} \approx 6.02\text{ dB}$).
