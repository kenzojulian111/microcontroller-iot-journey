# Wiring Guide — Project 15: Edge Offline Voice Command Node

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Specification | Quantity | Function |
| :--- | :--- | :---: | :--- |
| Microcontroller | ESP32-WROOM-32D or ESP32-S3-WROOM-1 | 1 | Dual-core MCU running I2S DMA + Audio DSP + 1D-CNN KWS |
| Digital MEMS Microphone | InvenSense INMP441 (I2S, 24-bit / 16-bit PCM) | 1 | Omnidirectional far-field acoustic capture (61 dBA SNR) |
| OLED Display | 0.96" SSD1306 (128x64 I2C, `0x3C`) | 1 | Real-time keyword confidence & 13-band MFCC bar visualizer |
| Relay Module | 5V Opto-isolated SPDT Relay (10A @ 250VAC) | 1 | Voice-actuated smart appliance switch |
| Tactile Pushbuttons | 6x6mm Momentary Tactile Switch | 3 | Wokwi / Bench synthetic voice utterance injectors |
| Status LED + Resistor | 5mm Emerald LED + 220 $\Omega$ 1/4W Resistor | 1 | Visual confirmation of recognized `"NYALAKAN"` state |

---

## 2. INMP441 I2S Digital Microphone Pinout

The **INMP441** operates strictly at **3.3V** logic and power. Never connect `VDD` to `5V`.

| INMP441 Pin | Signal Description | ESP32 DevKitC V4 Pin | ESP32-S3 DevKitC-1 Pin | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `VDD` | 3.3V Supply (2.5 mA max) | `3V3` | `3V3` | Place a $100\text{ nF}$ ceramic decoupling capacitor close to `VDD` |
| `GND` | Ground Reference | `GND` | `GND` | Keep ground return short to minimize digital clock coupling |
| `SCK` | Serial Bit Clock (`BCLK`) | `GPIO 26` | `GPIO 41` | $16000\text{ Hz} \times 32 = 512\text{ kHz}$ or $1.024\text{ MHz}$ bit clock |
| `WS` | Word Select (`LRCLK`) | `GPIO 25` | `GPIO 42` | $16\text{ kHz}$ frame synchronization strobe |
| `SD` | Serial Data Output (`DOUT`) | `GPIO 33` | `GPIO 2` | Tri-state digital PCM output into MCU I2S RX |
| `L/R` | Left/Right Channel Select | `GND` | `GND` | Tie to `GND` for **Left Channel** (`I2S_CHANNEL_FMT_ONLY_LEFT`) |

---

## 3. Peripherals & Simulation Trigger Pinout

| Peripheral | Pin Name | ESP32 GPIO | Direction | Electrical Configuration |
| :--- | :--- | :--- | :--- | :--- |
| SSD1306 OLED | `SDA` / `SCL` | `GPIO 21` / `GPIO 22` | Bi-dir / Out | $4.7\text{ k}\Omega$ pull-ups to `3V3` (400 kHz Fast I2C) |
| Relay Module | `IN` | `GPIO 4` | Output | Active-HIGH logic driver |
| Status Indicator | `LED_A` | `GPIO 2` | Output | Series $220\text{ }\Omega$ current-limiting resistor to `GND` |
| Trigger `"NYALAKAN"` | `BTN_1` | `GPIO 12` | Input | Internal `INPUT_PULLUP`, active-LOW to `GND` |
| Trigger `"MATIKAN"` | `BTN_2` | `GPIO 14` | Input | Internal `INPUT_PULLUP`, active-LOW to `GND` |
| Trigger `"STATUS"` | `BTN_3` | `GPIO 27` | Input | Internal `INPUT_PULLUP`, active-LOW to `GND` |

---

## 4. Acoustic Layout & Signal Integrity Guidelines

1. **Port Alignment:** The INMP441 is a bottom-port MEMS microphone. Ensure the PCB or enclosure has an unobstructed $1.0\text{ mm}$ acoustic port hole sealed with a gasket against the bottom of the sensor package to prevent acoustic phase cancellation.
2. **I2S Trace Length:** Keep `SCK`, `WS`, and `SD` traces or jumper wires under $12\text{ cm}$. A $33\text{ }\Omega$ series damping resistor on `SCK` near the ESP32 output suppresses ringing and high-frequency EMI.
3. **Power Supply Ripple Rejection:** Wi-Fi or relay coil switching spikes on the `5V` rail can couple into the `3.3V` LDO. Power the relay coil from the `5V` (`VIN`) rail and decouple the microphone `3V3` pin with both $10\text{ }\mu\text{F}$ electrolytic and $100\text{ nF}$ ceramic capacitors.
