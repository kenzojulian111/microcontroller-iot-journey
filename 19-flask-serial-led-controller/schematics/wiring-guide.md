# Hardware Wiring Guide — Project 19: Flask USB Serial Hardware Bridge

## 1. Bill of Materials (BoM)

| Ref | Component | Specification | Qty |
| :--- | :--- | :--- | :--- |
| U1 | Microcontroller | Arduino Uno R3 (ATmega328P) or ESP32 DevKit V1 | 1 |
| K1 | Relay Module | 5V 1-Channel Opto-Isolated Relay (`IN`, `VCC`, `GND`) | 1 |
| D1 | PWM Indicator LED | 5mm High-Efficiency LED ($V_f \approx 2.1\text{ V}$, $I_f \le 20\text{ mA}$) | 1 |
| R1 | Current-Limiting Resistor | $220\ \Omega \pm 5\%$ (0.25 W metal film) | 1 |
| M1 | Micro Servo | TowerPro SG90 ($4.8\text{–}6.0\text{ V}$, $50\text{ Hz}$, $500\text{–}2400\ \mu\text{s}$) | 1 |
| J1 | USB Cable | USB Type-B (Uno) or Micro-USB/USB-C (ESP32) to Host PC | 1 |

---

## 2. Pin Mapping Matrix

| Peripheral Signal | ATmega328P (Uno/Nano) | ESP32 DevKit V1 | Electrical Notes |
| :--- | :--- | :--- | :--- |
| **Relay Control (`IN`)** | `D4` (PD4) | `GPIO 26` | Digital Push-Pull Output |
| **PWM LED Anode (via $220\ \Omega$)** | `D5` (PD5 / OC0B, $976\text{ Hz}$) | `GPIO 25` (LEDC Ch 0, $5\text{ kHz}$) | Uses Timer0 on Uno so Timer1 stays dedicated to Servo |
| **SG90 Servo Signal (`PWM`)** | `D9` (PB1 / OC1A, $50\text{ Hz}$) | `GPIO 27` (LEDC Ch 1, $50\text{ Hz}$) | 16-bit hardware timer resolution |
| **UART TX / RX** | `D1` (TX) / `D0` (RX) | `GPIO 1` / `GPIO 3` | Bridged over USB CDC-ACM (`115200 8N1`) |
| **Power Rail (`+5V`)** | `5V` pin | `VIN` / `5V` USB rail | Use external $5\text{ V } 2\text{ A}$ supply if Servo stalls under load |
| **Common Ground (`GND`)** | `GND` | `GND` | Tie external servo/relay PSU ground to MCU `GND` |

---

## 3. Electrical Design & Protection Notes

1. **Timer Conflict Prevention (ATmega328P):**
   The standard Arduino `Servo` library commandeers 16-bit **Timer1**, disabling hardware `analogWrite()` on pins `D9` and `D10`. Therefore, the 8-bit PWM dimmer is routed to **`D5`** (driven by 8-bit **Timer0**), guaranteeing zero jitter on the SG90 servo pulse train.
2. **DTR Auto-Reset Behavior:**
   Opening `/dev/ttyACM0` or `/dev/ttyUSB0` on Linux asserts the `DTR` line, pulsing the ATmega328P `RESET` pin low via a $100\text{ nF}$ coupling capacitor. The Python `SerialHardwareBridge` singleton opens the port once at startup, waits $1.8\text{ s}$ for the bootloader timeout, and maintains the persistent file descriptor across all HTTP requests.
