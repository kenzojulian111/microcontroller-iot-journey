# Wire Protocol & REST API Specification — Project 19

## 1. Serial Link Layer Specification

- **Physical / Virtual Interface:** USB CDC-ACM (`/dev/ttyACM*`, `/dev/ttyUSB*`, `COM*`)
- **Baud Rate & Framing:** `115200 bps`, 8 Data Bits, No Parity, 1 Stop Bit (`8N1`)
- **Flow Control:** None (`XON/XOFF` and `RTS/CTS` disabled)
- **Delimiter:** Line Feed (`\n`, `0x0A`); optional leading Carriage Return (`\r`, `0x0D`) is stripped.

---

## 2. Host-to-MCU Command Frame Format

Every command frame follows the deterministic ASCII grammar:

```text
CMD:<VERB>[,<INT_ARG>][*<HEX_XOR_CRC>]\n
```

### 2.1 XOR Checksum Calculation
When the `*` delimiter is present, the two hexadecimal digits following `*` represent the 8-bit bitwise XOR of all ASCII bytes preceding `*`:

$$\text{CRC} = \bigoplus_{i=0}^{N-1} \text{byte}[i]$$

Example for `CMD:PWM,128`:
- ASCII bytes: `43 4D 44 3A 50 57 4D 2C 31 32 38`
- XOR checksum: `0x0B` $\rightarrow$ Wire frame: `CMD:PWM,128*0B\n`

### 2.2 Supported Command Table

| Command Verb | Argument Range | Example Frame (with CRC) | Description |
| :--- | :--- | :--- | :--- |
| `RELAY` | `0` or `1` | `CMD:RELAY,1*2A\n` | De-energize (`0`) or energize (`1`) Relay Ch 1 |
| `PWM` | `0` to `255` | `CMD:PWM,128*0B\n` | Set 8-bit PWM duty cycle register |
| `SERVO` | `0` to `180` | `CMD:SERVO,90*1F\n` | Set SG90 servo target angle in degrees |
| `STATUS` | *(none)* | `CMD:STATUS*78\n` | Query full actuator state without mutation |

---

## 3. MCU-to-Host Response Frame Format

The microcontroller replies synchronously within `< 3 ms` with a single-line JSON object terminated by `\r\n`:

### 3.1 Success Response
```json
{"ok":true,"seq":14,"relay":1,"pwm":128,"servo":90,"uptime_ms":48210}
```

### 3.2 Error Response
```json
{"ok":false,"seq":14,"relay":1,"pwm":128,"servo":90,"uptime_ms":48215,"err":"CHECKSUM_MISMATCH"}
```

**Error Codes:**
- `CHECKSUM_MISMATCH` — Computed XOR checksum did not match trailing `*XX`.
- `INVALID_PREFIX` — Frame did not start with `CMD:`.
- `MISSING_ARGUMENT` — Missing comma-separated integer parameter.
- `NON_NUMERIC_VALUE` — Argument contained non-integer characters.
- `RELAY_OUT_OF_RANGE` / `PWM_OUT_OF_RANGE` / `SERVO_OUT_OF_RANGE` — Value outside hardware bounds.
- `UNKNOWN_COMMAND` — Unrecognized command verb.
- `RX_BUFFER_OVERFLOW` — Input line exceeded 95 bytes without `\n`.

---

## 4. Flask HTTP REST API Specification

### 4.1 `GET /api/state`
Queries the MCU (`CMD:STATUS`) under the `SerialHardwareBridge` mutex and returns current state metadata.

**Response (`200 OK`):**
```json
{
  "ok": true,
  "seq": 15,
  "relay": 1,
  "pwm": 128,
  "servo": 90,
  "uptime_ms": 51204,
  "mode": "HARDWARE",
  "port": "/dev/ttyACM0",
  "last_frame_tx": "CMD:STATUS*78",
  "last_frame_rx": "{\"ok\":true,\"seq\":15,\"relay\":1,\"pwm\":128,\"servo\":90,\"uptime_ms\":51204}",
  "last_latency_ms": 2.41
}
```

### 4.2 `POST /api/control`
Accepts one or more actuator targets (`relay`, `pwm`, `servo`) in a single JSON request body.

**Request Body:**
```json
{
  "relay": 1,
  "pwm": 200,
  "servo": 45
}
```

**Responses:**
- `200 OK`: Returns updated state object.
- `400 Bad Request`: Returns `{"ok": false, "err": "<REASON>"}` when validation fails.
