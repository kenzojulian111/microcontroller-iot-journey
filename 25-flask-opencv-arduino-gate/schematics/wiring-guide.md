# Project 25 — Hardware Wiring & Safety Interlock Guide (`servo_gate_controller`)

## 1. Dual-MCU Pinout Matrix (Arduino Uno/Nano vs ESP32)

| Peripheral / Signal | Arduino Uno / Nano | ESP32 DevKit V1 | Direction | Function |
| :--- | :--- | :--- | :--- | :--- |
| **SG90 / MG996R Servo `PWM`** | `D9` (Timer1 PWM) | `GPIO 18` (LEDC 50 Hz) | Output | $500\,\mu\text{s}$ ($0^\circ$ Closed) to $2400\,\mu\text{s}$ ($90^\circ$ Open) |
| **HC-SR04 `TRIG`** | `D10` | `GPIO 5` | Output | $10\,\mu\text{s}$ ultrasonic trigger pulse |
| **HC-SR04 `ECHO`** | `D11` | `GPIO 19` | Input | Echo pulse width ($d_{\text{cm}} = t_{\mu\text{s}} \times 0.0343 / 2$) |
| **Traffic Red LED (`STOP`)** | `D4` | `GPIO 33` | Output | Active when `CLOSED`, `CLOSING`, or `LOCKED` |
| **Traffic Green LED (`GO`)** | `D5` | `GPIO 32` | Output | Active only when fully `OPEN` ($90^\circ$) |
| **Safety Piezo Buzzer** | `D8` | `GPIO 25` | Output | Audible warning during ramp & obstruction reverse |
| **Guard Booth Manual Button** | `D2` | `GPIO 14` | `INPUT_PULLUP` | Manual open/close toggle |

---

## 2. Servo Current Supply & Decoupling Notes

When driving a high-torque **MG996R** metal-gear barrier servo (stall current up to $1.8\text{ A}$ at $5\text{ V}$):
1. Power the servo's `V+` rail from an **external 5 V / 3 A buck regulator** rather than the Arduino Uno's onboard linear 5 V regulator (`NCP1117`), which can brown out the MCU during sudden boom reversals.
2. Tie the external 5 V supply's `GND` directly to the Arduino/ESP32 `GND`.
3. Place a **$470\,\mu\text{F}$ electrolytic capacitor** across `V+` and `GND` near the servo header to absorb transient current spikes.
