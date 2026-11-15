# Wiring Guide — Project 16: ESP32-S3 Embedded Vision Object Counter

## 1. Hardware Bill of Materials (BOM)

| Component | Part Number / Specification | Quantity | Function |
| :--- | :--- | :---: | :--- |
| Vision MCU Board | Seeed Studio XIAO ESP32S3 Sense (8MB PSRAM) or AI-Thinker ESP32-CAM | 1 | Dual-core LX7 @ 240 MHz + DVP Camera DMA + Vector SIMD (`ESP-NN`) |
| Camera Sensor | OmniVision OV2640 (2MP DVP Parallel CMOS Sensor) | 1 | Captures `96x96` `PIXFORMAT_GRAYSCALE` frames at 12–25 FPS |
| OLED Display | 0.96" SSD1306 (128x64 I2C, `0x3C`) | 1 | Renders live `12x12` FOMO heatmap grid, virtual tripwire, and occupancy counts |
| Tactile Pushbuttons | 6x6mm Momentary Switches | 3 | Wokwi / Bench synthetic pedestrian trajectory injectors (`ENTRY`, `EXIT`, `RESET`) |
| Zone Occupancy LED | 5mm Cyan LED + 220 $\Omega$ Resistor | 1 | Illuminates whenever `current_occupancy > 0` |

---

## 2. OV2640 DVP Camera Pin Mapping

When compiling for physical camera hardware (`#define ENABLE_HW_OV2640 1` in `secrets.example.h`), verify the parallel DVP camera pin map for your target board:

| OV2640 Signal | Description | XIAO ESP32S3 Sense GPIO | AI-Thinker ESP32-CAM GPIO |
| :--- | :--- | :---: | :---: |
| `XCLK` | External Master Clock (20 MHz) | `GPIO 10` | `GPIO 0` |
| `PCLK` | Pixel Clock Strobe | `GPIO 13` | `GPIO 22` |
| `VSYNC` | Vertical Frame Sync | `GPIO 38` | `GPIO 25` |
| `HREF` | Horizontal Line Reference | `GPIO 47` | `GPIO 23` |
| `SIOD` (`SDA`) | SCCB Camera Control I2C Data | `GPIO 40` | `GPIO 26` |
| `SIOC` (`SCL`) | SCCB Camera Control I2C Clock | `GPIO 39` | `GPIO 27` |
| `Y9` (`D7`) | Parallel Data Bit 7 (MSB) | `GPIO 48` | `GPIO 35` |
| `Y8` (`D6`) | Parallel Data Bit 6 | `GPIO 11` | `GPIO 34` |
| `Y7` (`D5`) | Parallel Data Bit 5 | `GPIO 12` | `GPIO 39` |
| `Y6` (`D4`) | Parallel Data Bit 4 | `GPIO 14` | `GPIO 36` |
| `Y5` (`D3`) | Parallel Data Bit 3 | `GPIO 16` | `GPIO 21` |
| `Y4` (`D2`) | Parallel Data Bit 2 | `GPIO 18` | `GPIO 19` |
| `Y3` (`D1`) | Parallel Data Bit 1 | `GPIO 17` | `GPIO 18` |
| `Y2` (`D0`) | Parallel Data Bit 0 (LSB) | `GPIO 15` | `GPIO 5` |
| `PWDN` / `RESET` | Power Down / Hardware Reset | `-1` / `-1` | `GPIO 32` / `-1` |

---

## 3. Display & Simulation Control Pinout (Wokwi & Bench)

| Peripheral | Signal | ESP32 GPIO | Electrical Notes |
| :--- | :--- | :---: | :--- |
| SSD1306 OLED | `SDA` / `SCL` | `GPIO 21` / `GPIO 22` | I2C Address `0x3C` |
| Spawn Entry Button (`L -> R`) | `BTN_ENTRY` | `GPIO 12` | `INPUT_PULLUP` to `GND` |
| Spawn Exit Button (`R -> L`) | `BTN_EXIT` | `GPIO 14` | `INPUT_PULLUP` to `GND` |
| Reset Occupancy Button | `BTN_RESET` | `GPIO 27` | `INPUT_PULLUP` to `GND` |
| Occupancy Indicator LED | `LED_OCC` | `GPIO 2` | Series $220\text{ }\Omega$ resistor to `GND` |

---

## 4. Optical Mounting & Illumination Notes

1. **Overhead Mounting Geometry:** For bidirectional doorway counting, mount the OV2640 lens overhead pointing vertically downward ($H = 2.4\text{ m}$ to $3.0\text{ m}$). With a standard $66^\circ$ FOV lens, the $96 \times 96$ grid covers a $\approx 1.5\text{ m} \times 1.5\text{ m}$ floor patch, eliminating multi-person occlusion.
2. **PSRAM vs. Internal SRAM:** A single `96x96` grayscale frame consumes only $9,216\text{ Bytes}$ ($9\text{ KB}$). By setting `config.fb_location = CAMERA_FB_IN_DRAM`, DMA frame transfers bypass external SPI PSRAM wait states, cutting frame acquisition overhead by $3.8\times$.
