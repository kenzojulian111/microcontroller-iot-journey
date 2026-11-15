# Full-Duplex WebSocket & Socket.IO Event Specification — Project 22

## 1. Transport Layer Architecture

- **Protocol:** Socket.IO v5 / Engine.IO v4 over persistent RFC-6455 WebSocket (`ws://<host>:5003/socket.io/?EIO=4&transport=websocket`)
- **Engine.IO Framing:**
  - `40`: Namespace connection open
  - `2` / `3`: Engine.IO Ping / Pong keepalive frames
  - `42["<event_name>", <json_payload>]`: Socket.IO Event packet

---

## 2. Socket.IO Event Catalog

### 2.1 `device_state_update` (Bi-Directional: MCU $\rightarrow$ Hub $\rightarrow$ Browsers)
Emitted by the ESP32 immediately when a physical ISR button toggles a relay, or broadcast by the Flask-SocketIO hub whenever state changes.

**Payload:**
```json
{
  "device_id": "esp32-rt-node-01",
  "seq": 19,
  "relay1": 1,
  "relay2": 0,
  "source": "HW_BUTTON_1",
  "uptime_ms": 142890,
  "updated_at": "2026-11-15T14:10:22.104+00:00",
  "connected_clients": 3
}
```

---

### 2.2 `web_toggle_relay` (Browser $\rightarrow$ Hub)
Emitted when an operator clicks a relay switch in the web UI.

**Payload:**
```json
{
  "channel": 1,
  "state": 1
}
```
If `"state"` is omitted, the hub atomically toggles the specified channel (`1` or `2`).

---

### 2.3 `mcu_set_relay` (Hub $\rightarrow$ MCU)
Pushed down the open WebSocket connection to the ESP32 within `< 5 ms` of a `web_toggle_relay` event:

```json
{
  "channel": 1,
  "state": 1
}
```

---

### 2.4 `ping_latency` / `pong_latency` (RTT Instrumentation)
The browser emits `ping_latency` with `performance.now()` (`client_t0`). The hub immediately replies with `pong_latency`, allowing the browser to compute sub-millisecond round-trip time:

$$\text{RTT}_{\text{ms}} = t_{\text{now}} - t_{0,\text{client}}$$

---

## 3. Auxiliary HTTP Endpoints (Bench Testing & Diagnostics)

### 3.1 `GET /api/state`
Returns the current synchronized hub state snapshot (`relay1`, `relay2`, `seq`, `source`, `connected_clients`).

### 3.2 `POST /api/simulate/hw_button`
Simulates a physical ISR button press on Channel 1 or 2 (`{"channel": 1}`) and broadcasts `device_state_update` to all connected WebSocket clients.
