# Authenticated REST API & Psychrometric Specification — Project 21

## 1. Authentication Security Model

All telemetry ingestion requests (`POST /api/v1/telemetry`) must include the HTTP header:

```http
X-API-Key: iot-lab-track3-weather-secret-key-2026
```

The `@require_api_key` decorator validates the token using Python's `hmac.compare_digest(provided, expected)` to prevent timing side-channel attacks. Missing or mismatched keys receive `401 Unauthorized`:

```json
{"ok": false, "err": "UNAUTHORIZED_API_KEY"}
```

---

## 2. Psychrometric Dew Point Calculation (Magnus-Tetens Equation)

Upon validating an incoming frame, the backend computes the atmospheric **Dew Point** ($T_{\text{dp}}$ in $^{\circ}\text{C}$) from dry-bulb temperature $T$ ($^{\circ}\text{C}$) and relative humidity $\text{RH}$ ($\%$):

$$\gamma(T, \text{RH}) = \ln\left(\frac{\text{RH}}{100}\right) + \frac{b \cdot T}{c + T}$$

$$T_{\text{dp}} = \frac{c \cdot \gamma(T, \text{RH})}{b - \gamma(T, \text{RH})}$$

where Sonnypar / Magnus coefficients are $b = 17.625$ and $c = 243.04\text{ }^{\circ}\text{C}$ (accurate to $\pm 0.15\text{ }^{\circ}\text{C}$ over $-40\text{ }^{\circ}\text{C} \le T \le +50\text{ }^{\circ}\text{C}$).

---

## 3. REST API Endpoints

### 3.1 `POST /api/v1/telemetry` (Protected by `X-API-Key`)

**Request Headers:**
```http
Content-Type: application/json
X-API-Key: iot-lab-track3-weather-secret-key-2026
```

**Request Body:**
```json
{
  "node_id": "esp32-weather-01",
  "seq": 104,
  "temp_c": 25.40,
  "hum_pct": 62.10,
  "press_hpa": 1012.85,
  "rssi_dbm": -58,
  "uptime_ms": 520000
}
```

**Responses:**
- `201 Created`: Returns persisted record including computed `dew_point_c`.
- `400 Bad Request`: Missing required keys or non-numeric types.
- `401 Unauthorized`: Invalid or missing `X-API-Key`.
- `422 Unprocessable Entity`: Physical bounds check failed (`temp_c` outside `[-50, 85]`, `hum_pct` outside `[0, 100]`, or `press_hpa` outside `[800, 1200]`).

---

### 3.2 `GET /api/v1/telemetry?limit=60`
Returns up to `limit` samples in chronological order (`oldest -> newest`) ready for direct binding into Chart.js datasets.

---

### 3.3 `GET /api/v1/analytics`
Returns SQL aggregate metrics (`MIN`, `MAX`, `AVG` for temperature, humidity, pressure, and mean dew point) alongside the most recent sample.
