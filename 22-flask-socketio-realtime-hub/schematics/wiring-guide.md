# Hardware Wiring Guide — Project 22: Full-Duplex WebSocket Realtime Hub

## 1. Bill of Materials (BoM)

| Ref | Component | Specification | Qty |
| :--- | :--- | :--- | :--- |
| U1 | Wireless MCU | ESP32 DevKitC V4 or ESP8266 NodeMCU | 1 |
| K1, K2 | Dual Relay Module | 2-Channel 5V Opto-Isolated Relay Board (`IN1`, `IN2`, `VCC`, `GND`) | 1 |
| SW1, SW2 | Tactile Pushbuttons | Momentary SPST NO Pushbuttons ($6\times6\text{ mm}$) | 2 |
| C1, C2 | Hardware RC Debounce | Optional $100\text{ nF}$ ceramic capacitor across switch contacts | 2 |

---

## 2. Pin Mapping Matrix

| Signal | ESP32 DevKit V1 | ESP8266 NodeMCU | Interrupt / Electrical Configuration |
| :--- | :--- | :--- | :--- |
| **Relay 1 Output (`IN1`)** | `GPIO 26` | `D1` (`GPIO 5`) | Digital Output (`HIGH` = Energized) |
| **Relay 2 Output (`IN2`)** | `GPIO 27` | `D2` (`GPIO 4`) | Digital Output (`HIGH` = Energized) |
| **Tactile Button 1 (`SW1`)** | `GPIO 18` | `D6` (`GPIO 12`) | `INPUT_PULLUP`, `FALLING` edge ISR (`IRAM_ATTR`) |
| **Tactile Button 2 (`SW2`)** | `GPIO 19` | `D7` (`GPIO 13`) | `INPUT_PULLUP`, `FALLING` edge ISR (`IRAM_ATTR`) |
| **Relay Supply (`VCC`)** | `5V` (`VIN`) | `VIN` (`5V`) | Powered from 5V USB rail |
| **Common Ground (`GND`)** | `GND` | `GND` | Common return for buttons and relay optocouplers |

---

## 3. Interrupt Service Routine (ISR) & Contact Bounce Suppression

Mechanical tactile switches exhibit contact bounce lasting $5\text{–}20\text{ ms}$ upon closure. The firmware attaches `IRAM_ATTR` interrupt handlers (`onButton1Fall`, `onButton2Fall`) to the falling edge (`HIGH -> LOW` transition with internal pull-ups enabled) and enforces a deterministic **$35\text{ ms}$ blanking interval**:

```cpp
void IRAM_ATTR onButton1Fall() {
  uint32_t now = millis();
  if (now - isrBtn1LastMs >= 35UL) {
    isrBtn1LastMs    = now;
    isrBtn1Triggered = true;
  }
}
```

The ISR sets only a `volatile bool` flag in SRAM and exits in $< 2\ \mu\text{s}$, delegating relay toggling and Socket.IO WebSocket frame serialization to the main `loop()` context so FreeRTOS Wi-Fi/TCP stack tasks are never starved.
