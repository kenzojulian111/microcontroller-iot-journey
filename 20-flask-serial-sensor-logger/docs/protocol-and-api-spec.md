# Telemetry Stream & REST API Specification — Project 20

## 1. UART NDJSON Telemetry Frame Format

The MCU transmits one UTF-8 JSON line (`\n`-delimited) every `2000 ms` over `115200 8N1`:

```json
{"seq":42,"temp_c":26.40,"hum_pct":61.20,"lux_raw":512,"v_adc":2.502,"sensor_ok":true,"uptime_ms":84002}
```

| Field | Type | Units / Range | Description |
| :--- | :--- | :--- | :--- |
| `seq` | `uint32` | $\ge 1$ | Monotonically increasing frame sequence counter |
| `temp_c` | `float` | $^{\circ}\text{C}$ (`-40.0` to `80.0`) | Ambient temperature from DHT22 |
| `hum_pct` | `float` | `%RH` (`0.0` to `100.0`) | Relative humidity percentage |
| `lux_raw` | `uint16` | `0` to `1023` | Normalized 10-bit illuminance ADC reading |
| `v_adc` | `float` | Volts (`0.000` to `5.000`) | Measured voltage at the LDR voltage divider node |
| `sensor_ok` | `bool` | `true` / `false` | `false` if DHT22 checksum/timeout occurred |
| `uptime_ms` | `uint32` | Milliseconds | MCU `millis()` counter |

---

## 2. SQLite3 Persistence Architecture (WAL Mode)

The backend initializes `telemetry.db` with `PRAGMA journal_mode=WAL;` and `PRAGMA synchronous=NORMAL;` so that the background `SerialTelemetryIngestor` daemon thread never blocks concurrent HTTP read transactions or CSV streaming exports.

```sql
CREATE TABLE IF NOT EXISTS sensor_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_utc TEXT NOT NULL,
    seq INTEGER NOT NULL,
    temp_c REAL NOT NULL,
    hum_pct REAL NOT NULL,
    lux_raw INTEGER NOT NULL,
    v_adc REAL NOT NULL,
    source TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_sensor_readings_ts
    ON sensor_readings(id DESC, ts_utc DESC);
```

---

## 3. REST API Endpoints

### 3.1 `GET /api/telemetry/latest`
Returns the newest sensor record along with aggregate statistics (`MIN`, `MAX`, `AVG`, `COUNT`) across the historian table.

**Response (`200 OK`):**
```json
{
  "latest": {
    "id": 18,
    "ts_utc": "2026-11-15T14:00:12.410+00:00",
    "seq": 18,
    "temp_c": 26.41,
    "hum_pct": 57.12,
    "lux_raw": 612,
    "v_adc": 2.991,
    "source": "UART:/dev/ttyACM0"
  },
  "mode": "HARDWARE",
  "port": "/dev/ttyACM0",
  "stats": {
    "total_count": 18,
    "temp_min": 24.1,
    "temp_max": 27.8,
    "temp_avg": 25.92,
    "hum_min": 54.3,
    "hum_max": 62.1,
    "hum_avg": 58.44,
    "lux_min": 310,
    "lux_max": 815,
    "lux_avg": 542.6
  }
}
```

### 3.2 `GET /api/telemetry/history?limit=100`
Returns up to `limit` (clamped to `1..5000`) historical rows ordered newest-first.

### 3.3 `GET /api/export/csv`
Streams the entire `sensor_readings` table as an **RFC-4180** compliant CSV attachment (`CRLF` line endings, header row included) using a generator function for $O(1)$ memory overhead.
