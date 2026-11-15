# Wiring Guide — Project 18: Edge MLOps OTA Model Hot-Swapping Pipeline

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Specification | Quantity | Function |
| :--- | :--- | :---: | :--- |
| Microcontroller | ESP32-WROOM-32D (4MB Flash with `LittleFS` Partition) | 1 | Hosts decoupled `/models/*.bin` slots, MbedTLS SHA-256, and inference arena |
| OLED Display | 0.96" SSD1306 (128x64 I2C, `0x3C`) | 1 | Displays active model version (`v1.0.0` vs `v2.1.0`), SHA-256 prefix & prediction |
| Drift Simulator Potentiometer | $10\text{ k}\Omega$ Linear Potentiometer | 1 | Sweeps ambient thermal drift ($25^\circ\text{C} \rightarrow 65^\circ\text{C}$) on `GPIO 34` |
| Tactile Pushbuttons | 6x6mm Momentary Switches | 3 | Triggers OTA v2.1.0 hot-swap, tampered SHA-256 fault injection, and v1.0.0 rollback |
| OTA Status LED | 5mm Purple LED + 220 $\Omega$ Resistor | 1 | Flashes during LittleFS staging write & SHA-256 verification |

---

## 2. Pin Assignment Table

| Peripheral / Control | Signal | ESP32 GPIO | Electrical Configuration |
| :--- | :--- | :---: | :--- |
| SSD1306 OLED | `SDA` / `SCL` | `GPIO 21` / `GPIO 22` | I2C Address `0x3C` |
| Thermal Drift Potentiometer | `SIG` | `GPIO 34` | Analog input `ADC1_CH6` ($0 - 3.3\text{ V}$) |
| Trigger OTA Model `v2.1.0` | `BTN_OTA_V2` | `GPIO 12` | `INPUT_PULLUP` to `GND` (Authentic SHA-256 hot-swap) |
| Inject Corrupted Binary | `BTN_CORRUPT` | `GPIO 14` | `INPUT_PULLUP` to `GND` (1-bit flip in `w1[0][0]` -> SHA-256 reject) |
| Rollback to `v1.0.0` | `BTN_ROLLBACK` | `GPIO 27` | `INPUT_PULLUP` to `GND` (Restores baseline factory model) |
| OTA Activity LED | `LED_SWAP` | `GPIO 2` | Series $220\text{ }\Omega$ resistor to `GND` |

---

## 3. Flash Partition Layout Recommendation (`partitions.csv`)

Because neural network weights (`.bin` / `.tflite` files) are stored in **LittleFS** rather than compiled into `PROGMEM`, your ESP32 flash partition table only needs a single firmware slot plus a dedicated `spiffs`/`littlefs` data partition for dual-slot (`active_model.bin`, `staging_model.bin`, `backup_model.bin`) atomic model storage:

```csv
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x180000,
spiffs,   data, spiffs,  0x190000,0x170000,
```
This allocates **$1.44\text{ MB}$** of non-volatile flash to LittleFS, accommodating dozens of quantized `INT8` models while allowing zero-reboot model updates in $<25\text{ ms}$.
