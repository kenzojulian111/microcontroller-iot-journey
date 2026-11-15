# InfluxDB v2 & Grafana Provisioning Guide, Line Protocol Spec & UART Output — 05 Smart Energy Meter

This document details how to deploy the **InfluxDB v2** time-series database and **Grafana** visualization stack using Docker Compose, provides production **Flux** queries for power and billing dashboards, and documents the UART telemetry format for **Project 05 (`05-smart-energy-meter`)**.

---

## 1. UART Serial Monitor Output (`115200` baud)

When the firmware runs on the ESP32 (either connected to a physical PZEM-004T v3.0 module or running in potentiometer simulation mode), the UART console logs:

```text
============================================================
  IoT Lab - 05 Smart Energy Meter (PZEM-004T & InfluxDB v2) 
  Mode Sensor : SIMULASI (Potensiometer GPIO34)
============================================================
[WIFI] Menghubungkan ke SSID: Wokwi-GUEST ....
[WIFI] Terhubung!
[WIFI] Alamat IP : 10.13.37.2
------------------------------------------------------------
[METER] V: 220.4 V | I: 5.13 A | P: 1039.8 W | E: 0.0173 kWh | Freq: 50.0 Hz | PF: 0.92
[BIAYA] Estimasi Tagihan PLN: Rp 25.04 (Tarif: Rp 1444.70/kWh)
[INFLUX LINE] power_consumption,location=panel_utama,mode=simulation voltage=220.40,current=5.128,power=1039.82,energy_kwh=0.0173,frequency=50.00,pf=0.92,cost_idr=25.04
[INFLUX] Sukses menulis data ke InfluxDB v2 (HTTP 204 No Content).
------------------------------------------------------------
[METER] V: 219.8 V | I: 8.40 A | P: 1735.5 W | E: 0.1137 kWh | Freq: 50.0 Hz | PF: 0.94
[BIAYA] Estimasi Tagihan PLN: Rp 164.33 (Tarif: Rp 1444.70/kWh)
[INFLUX LINE] power_consumption,location=panel_utama,mode=simulation voltage=219.80,current=8.402,power=1735.54,energy_kwh=0.1137,frequency=50.00,pf=0.94,cost_idr=164.33
[INFLUX] Sukses menulis data ke InfluxDB v2 (HTTP 204 No Content).
```

---

## 2. Deploying InfluxDB v2 & Grafana via Docker Compose

Create a `docker-compose.yml` file on your host server or workstation and start the stack with `docker compose up -d`:

```yaml
services:
  influxdb:
    image: influxdb:2.7
    container_name: iotlab_influxdb
    restart: unless-stopped
    ports:
      - "8086:8086"
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=adminpassword123
      - DOCKER_INFLUXDB_INIT_ORG=iot_lab_org
      - DOCKER_INFLUXDB_INIT_BUCKET=energy_meter
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=iotlab-super-secret-influx-token
    volumes:
      - influxdb_data:/var/lib/influxdb2

  grafana:
    image: grafana/grafana:latest
    container_name: iotlab_grafana
    restart: unless-stopped
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_USER=admin
      - GF_SECURITY_ADMIN_PASSWORD=admin
    depends_on:
      - influxdb
    volumes:
      - grafana_data:/var/lib/grafana

volumes:
  influxdb_data:
  grafana_data:
```

### Connecting the ESP32 Node to the Docker Stack:
1. Run `docker compose up -d`.
2. Verify **InfluxDB UI** accessibility at `http://localhost:8086` (`admin` / `adminpassword123`).
3. Update `src/smart_energy_meter/secrets.h` with your host machine's LAN IPv4 address:
   ```cpp
   #define INFLUXDB_URL    "http://<YOUR_HOST_LAN_IP>:8086"
   #define INFLUXDB_TOKEN  "iotlab-super-secret-influx-token"
   #define INFLUXDB_ORG    "iot_lab_org"
   #define INFLUXDB_BUCKET "energy_meter"
   ```

---

## 3. InfluxDB v2 Line Protocol Schema Specification

The ESP32 transmits time-series points to `POST /api/v2/write?org=iot_lab_org&bucket=energy_meter&precision=s` using the following schema:

- **Measurement:** `power_consumption`
- **Indexed Tag Set:**
  - `location`: Physical sub-panel identifier (default: `panel_utama`)
  - `mode`: Acquisition source (`hardware` or `simulation`)
- **Unindexed Field Set:**
  - `voltage` (`float`, Volts RMS)
  - `current` (`float`, Amperes RMS)
  - `power` (`float`, Active Power in Watts)
  - `energy_kwh` (`float`, Cumulative Energy in kWh)
  - `frequency` (`float`, AC Line Frequency in Hz)
  - `pf` (`float`, Power Factor `0.00–1.00`)
  - `cost_idr` (`float`, Estimated PLN utility cost in IDR)

---

## 4. Production Flux Queries for Grafana Dashboards

In **Grafana** (`http://localhost:3000`), navigate to **Connections > Data Sources > Add data source > InfluxDB**, set the **Query Language** to **Flux**, set the URL to `http://influxdb:8086`, Organization to `iot_lab_org`, Token to `iotlab-super-secret-influx-token`, and Default Bucket to `energy_meter`.

Use the following Flux queries to populate your dashboard panels:

### a. Time-Series Panel: Active Power (Watts) & Line Voltage (Volts)
```flux
from(bucket: "energy_meter")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "power_consumption")
  |> filter(fn: (r) => r["location"] == "panel_utama")
  |> filter(fn: (r) => r["_field"] == "power" or r["_field"] == "voltage")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "mean")
```

### b. Stat / Gauge Panel: Latest Cumulative Energy (kWh) & Utility Bill (IDR)
```flux
from(bucket: "energy_meter")
  |> range(start: -24h)
  |> filter(fn: (r) => r["_measurement"] == "power_consumption")
  |> filter(fn: (r) => r["location"] == "panel_utama")
  |> filter(fn: (r) => r["_field"] == "energy_kwh" or r["_field"] == "cost_idr")
  |> last()
```

### c. Bar Chart Panel: Daily Energy Consumption Delta (kWh per Day)
```flux
from(bucket: "energy_meter")
  |> range(start: -7d)
  |> filter(fn: (r) => r["_measurement"] == "power_consumption")
  |> filter(fn: (r) => r["_field"] == "energy_kwh")
  |> aggregateWindow(every: 1d, fn: spread, createEmpty: false)
  |> yield(name: "daily_kwh")
```
