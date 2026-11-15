# Project 23 — Hardware Wiring & Pinout Guide (`GATE-01` Access Node)

## 1. Bill of Materials (BoM)

| Ref | Component | Bus / Interface | Operating Voltage | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `U1` | ESP32 DevKit V1 (30-Pin) | Wi-Fi + SPI + I2C | 3.3 V logic (5 V USB) | Primary edge access controller |
| `U2` | MFRC522 13.56 MHz RFID Reader | SPI (Up to 10 MHz) | **3.3 V ONLY** | Do **not** connect VCC to 5 V |
| `DSP1` | SSD1306 128x64 OLED Display | I2C (`0x3C`) | 3.3 V | Renders user name, role, and shift action |
| `RLY1` | Single-Channel Opto Relay | GPIO Digital Out | 5 V Coil / 3.3 V Trig | Actuates 12 V DC Fail-Secure Door Strike |
| `BZ1` | Piezoelectric Buzzer | PWM Tone (`tone()`) | 3.3 V | Acoustic feedback for `CHECK_IN` / `DENIED` |
| `SW1/SW2` | Tactile Pushbuttons (Wokwi) | Digital Input (`INPUT_PULLUP`) | 3.3 V | Simulates Valid / Rogue RFID card taps |

---

## 2. Pin-to-Pin Interconnect Matrix

### 2.1 MFRC522 Contactless RFID Transceiver (VSPI Bus)

| MFRC522 Pin | ESP32 GPIO | Function | Electrical Notes |
| :--- | :--- | :--- | :--- |
| `SDA (SS)` | `GPIO 5` | SPI Chip Select | Active-LOW Slave Select |
| `SCK` | `GPIO 18` | VSPI Clock | 4 MHz SPI clock |
| `MOSI` | `GPIO 23` | VSPI Master Out Slave In | 3.3 V CMOS |
| `MISO` | `GPIO 19` | VSPI Master In Slave Out | 3.3 V CMOS |
| `IRQ` | *Unconnected* | Interrupt Out | Polled via `PICC_IsNewCardPresent()` |
| `GND` | `GND` | Common Ground | Star ground reference |
| `RST` | `GPIO 27` | Hard Reset | Driven HIGH during operation |
| `3.3V` | `3V3` | Supply Rail | **Max 3.6 V** |

### 2.2 SSD1306 128x64 OLED Display (I2C0 Bus)

| SSD1306 Pin | ESP32 GPIO | Function |
| :--- | :--- | :--- |
| `VCC` | `3V3` | 3.3 V Regulated Supply |
| `GND` | `GND` | Common Ground |
| `SCL` | `GPIO 22` | I2C Clock (400 kHz Fast Mode) |
| `SDA` | `GPIO 21` | I2C Data |

### 2.3 Actuators, Indicators & Wokwi Simulation Inputs

| Peripheral | ESP32 GPIO | Direction | Active State | Description |
| :--- | :--- | :--- | :--- | :--- |
| Door Lock Relay (`IN`) | `GPIO 26` | `OUTPUT` | `HIGH` (3000 ms) | Energizes solenoid door strike |
| Piezo Buzzer (`+`) | `GPIO 25` | `OUTPUT` | PWM Tone | Two-tone ascending (`CHECK_IN`), descending (`CHECK_OUT`), low buzz (`DENIED`) |
| Green LED (`GRANTED`) | `GPIO 32` | `OUTPUT` | `HIGH` | Series 220 $\Omega$ resistor to GND |
| Red LED (`DENIED`) | `GPIO 33` | `OUTPUT` | `HIGH` | Series 220 $\Omega$ resistor to GND |
| `btnValid` (Wokwi) | `GPIO 14` | `INPUT_PULLUP` | `LOW` when pressed | Cycles through `04:A3:9B:12`, `A1:7F:44:C8`, `7B:2E:90:55` |
| `btnRogue` (Wokwi) | `GPIO 12` | `INPUT_PULLUP` | `LOW` when pressed | Sends unregistered UID `DE:AD:BE:EF` |

---

## 3. Inductive Flyback Protection Note

When switching a physical 12 V DC solenoid door lock across the relay's Normally Open (`NO`) and Common (`COM`) contacts, wire a **1N4007 flyback diode** reverse-biased across the solenoid terminals (cathode to `+12 V`, anode to `GND`) to clamp inductive voltage spikes during relay de-energization and prevent EMI-induced resets on the ESP32 I2C bus.
