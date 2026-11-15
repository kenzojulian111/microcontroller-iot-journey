# Project 25 — OpenCV Vision Pipeline & Servo Gate Control Specification

## 1. System Architecture & Anti-Crush Safety Loop

```
+-----------------------------------+       UART 115200 bps       +-----------------------------------+
|   Flask + OpenCV Vision Server    |   GATE:OPEN / GATE:CLOSE    |  Arduino / ESP32 Gate Controller  |
|  - cv2.QRCodeDetector()           +---------------------------->|  - Non-blocking Servo Ramp 0..90  |
|  - cv2.CascadeClassifier (Haar)   |<----------------------------+  - HC-SR04 Anti-Crush Interlock   |
|  - Headless Synthetic Frame Gen   |   JSON Telemetry Heartbeat  |  - Red/Green Traffic LEDs + Buzzer|
+-----------------+-----------------+                             +-----------------------------------+
                  |
                  | Multipart MJPEG Stream (/video_feed)
                  v
+-----------------------------------+
|   Operator Web Console (Browser)  |
+-----------------------------------+
```

---

## 2. Finite State Machine (`servo_gate_controller.ino`)

| State | Angle ($\theta$) | Traffic LEDs | Transition Condition |
| :--- | :--- | :--- | :--- |
| `STATE_CLOSED` | $0^\circ$ | Red ON | Receives `GATE:OPEN` or Manual Button $\rightarrow$ `STATE_OPENING` |
| `STATE_OPENING` | $0^\circ \to 90^\circ$ | Red ON | Steps $+1^\circ$ every $18\text{ ms}$; at $90^\circ \rightarrow$ `STATE_OPEN` |
| `STATE_OPEN` | $90^\circ$ | Green ON | Holds open while $d_{\text{HC-SR04}} < 35\text{ cm}$ or for $5000\text{ ms}$ dwell $\rightarrow$ `STATE_CLOSING` |
| `STATE_CLOSING` | $90^\circ \to 0^\circ$ | Red ON | Steps $-1^\circ$ every $18\text{ ms}$. **If $d_{\text{HC-SR04}} < 35\text{ cm}$, immediately aborts & reverses to `STATE_OBSTRUCTED_HOLD` ($90^\circ$)!** |
| `STATE_OBSTRUCTED_HOLD` | $90^\circ$ | Red ON + Alarm | Waits until vehicle clears ($d_{\text{HC-SR04}} \ge 35\text{ cm}$) $\rightarrow$ `STATE_OPEN` |
| `STATE_LOCKED` | $0^\circ$ | Red ON | Ignores all `GATE:OPEN` requests until `GATE:UNLOCK` is issued |

---

## 3. REST & Streaming API Specification

### 3.1 `GET /video_feed`
Streams real-time OpenCV annotated frames (`multipart/x-mixed-replace; boundary=frame`) with bounding boxes for verified QR tokens, Haar Cascade facial detections, FPS counter, and live servo angle + ultrasonic clearance HUD.

### 3.2 `POST /api/v1/gate/command`
Dispatches a serial command to the hardware controller:
```json
{
  "command": "GATE:OPEN"
}
```
Allowed commands: `GATE:OPEN`, `GATE:CLOSE`, `GATE:LOCK`, `GATE:UNLOCK`.

### 3.3 `POST /api/v1/vision/simulate`
Injects a synthetic vision scene (`VALID_QR`, `ROGUE_QR`, `FACE_OP`, `STANDBY`) and/or simulated HC-SR04 clearance (`clearance_cm`) for headless verification:
```json
{
  "scene": "VALID_QR",
  "clearance_cm": 24.5
}
```
