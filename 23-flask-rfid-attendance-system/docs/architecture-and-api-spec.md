# Project 23 — System Architecture, ORM Schema & REST API Specification

## 1. End-to-End Transaction Sequence

```
+--------------------+          +-------------------------+          +-----------------------+
|  ESP32 + MFRC522   |          |   Flask REST Backend    |          |   SQLite / SQLAlchemy |
|  (GATE-01 Node)    |          |     (backend/app.py)    |          |       ORM Engine      |
+---------+----------+          +------------+------------+          +-----------+-----------+
          |                                  |                                   |
          | 1. ISO14443A UID Read            |                                   |
          |    (or Wokwi Button Press)       |                                   |
          |                                  |                                   |
          | 2. POST /api/v1/attendance/punch |                                   |
          |    {"uid":"04:A3:9B:12",         |                                   |
          |     "terminal_id":"GATE-01"}     |                                   |
          +--------------------------------->|                                   |
          |                                  | 3. SELECT * FROM employees        |
          |                                  |    WHERE uid = '04:A3:9B:12'      |
          |                                  +---------------------------------->|
          |                                  |<----------------------------------+
          |                                  | 4. Toggle currently_checked_in    |
          |                                  |    Compute shift_seconds if OUT   |
          |                                  |    INSERT INTO attendance_logs    |
          |                                  +---------------------------------->|
          |                                  |                                   |
          | 5. HTTP 200 / 403 JSON           |                                   |
          |    {"status":"allowed",          |                                   |
          |     "action":"CHECK_IN",         |                                   |
          |     "user":"Kenzo Julian", ...}  |                                   |
          |<---------------------------------+                                   |
          |                                  |                                   |
          | 6. Render SSD1306 OLED           |                                   |
          |    Energize Relay (3000 ms)      |                                   |
          |    Play Buzzer Chime             |                                   |
          +                                  +                                   +
```

---

## 2. SQLAlchemy Relational Schema

### 2.1 `employees` Table (`Employee` Model)

| Column | SQL Type | Constraints | Description |
| :--- | :--- | :--- | :--- |
| `id` | `INTEGER` | `PRIMARY KEY` | Surrogate primary key |
| `uid` | `VARCHAR(32)` | `UNIQUE, NOT NULL, INDEX` | Normalized uppercase colon-delimited UID (`04:A3:9B:12`) |
| `full_name` | `VARCHAR(96)` | `NOT NULL` | Employee display name (truncated to 15 chars on OLED) |
| `role` | `VARCHAR(64)` | `NOT NULL` | Job title rendered on OLED line 2 |
| `department` | `VARCHAR(64)` | `NOT NULL` | Organizational unit |
| `is_active` | `BOOLEAN` | `NOT NULL, DEFAULT 1` | When `False`, card taps return `HTTP 403 REVOKED` |
| `currently_checked_in` | `BOOLEAN` | `NOT NULL, DEFAULT 0` | Stateful toggle determining whether next tap is `CHECK_IN` or `CHECK_OUT` |
| `last_check_in_at` | `DATETIME` | `NULLABLE` | UTC timestamp of active shift start |
| `created_at` | `DATETIME` | `NOT NULL` | Enrollment timestamp |

### 2.2 `attendance_logs` Table (`AttendanceLog` Model)

| Column | SQL Type | Constraints | Description |
| :--- | :--- | :--- | :--- |
| `id` | `INTEGER` | `PRIMARY KEY` | Audit event ID |
| `uid` | `VARCHAR(32)` | `NOT NULL, INDEX` | Badge UID presented at reader |
| `employee_id` | `INTEGER` | `FOREIGN KEY (employees.id), NULLABLE` | `NULL` for unknown/rogue cards |
| `terminal_id` | `VARCHAR(32)` | `NOT NULL` | Reader identifier (`GATE-01`) |
| `action` | `VARCHAR(24)` | `NOT NULL` | `CHECK_IN`, `CHECK_OUT`, `DENIED_REVOKED`, or `DENIED_UNKNOWN` |
| `status` | `VARCHAR(16)` | `NOT NULL` | `allowed` or `denied` |
| `shift_seconds` | `INTEGER` | `NULLABLE` | Populated on `CHECK_OUT`: $\Delta t = t_{\text{out}} - t_{\text{in}}$ |
| `rssi` | `INTEGER` | `NULLABLE` | ESP32 Wi-Fi RSSI in dBm at time of punch |
| `punched_at` | `DATETIME` | `NOT NULL, INDEX` | UTC timestamp of event |

---

## 3. REST API Contract

### 3.1 `POST /api/v1/attendance/punch`

**Request Body:**
```json
{
  "uid": "04:A3:9B:12",
  "terminal_id": "GATE-01",
  "rssi": -49
}
```

**Response (`200 OK` — Authorized Check-In/Out):**
```json
{
  "status": "allowed",
  "action": "CHECK_IN",
  "uid": "04:A3:9B:12",
  "user": "Kenzo Julian",
  "role": "Lead Engineer",
  "department": "Embedded R&D",
  "terminal_id": "GATE-01",
  "timestamp": "08:59:12"
}
```

**Response (`403 Forbidden` — Unauthorized UID):**
```json
{
  "status": "denied",
  "action": "DENIED",
  "uid": "DE:AD:BE:EF",
  "user": "Unregistered Card",
  "role": "Unauthorized",
  "terminal_id": "GATE-01",
  "timestamp": "09:02:44"
}
```

### 3.2 `POST /api/v1/employees` (Badge Enrollment)

**Request Body:**
```json
{
  "uid": "5C:88:19:B0",
  "full_name": "Nadia Sorokin",
  "role": "RF Hardware Eng",
  "department": "Hardware Lab"
}
```

### 3.3 `PATCH /api/v1/employees/<id>/toggle`
Toggles `Employee.is_active` to immediately revoke or reinstate physical gate access without deleting historical `AttendanceLog` foreign-key records.

### 3.4 `GET /api/v1/attendance/export.csv`
Streams an RFC-4180 compliant CSV file containing all historical check-ins, check-outs, shift durations, and security denials.
