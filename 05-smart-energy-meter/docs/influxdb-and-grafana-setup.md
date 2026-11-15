# Panduan Setup InfluxDB v2, Grafana Dashboard & Output Serial Monitor

Dokumen ini menjelaskan cara menyiapkan *Time-Series Database* **InfluxDB v2** beserta **Grafana Dashboard** menggunakan Docker, contoh *query* Flux untuk menampilkan grafik daya & estimasi tagihan listrik, serta contoh log Serial Monitor dari **Proyek 05 (`05-smart-energy-meter`)**.

---

## 1. Contoh Output Serial Monitor (`115200` baud)

Saat sketch dijalankan pada ESP32 (baik dengan sensor PZEM-004T asli maupun mode simulasi di Wokwi), berikut adalah tampilan pada **Serial Monitor**:

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

## 2. Menjalankan InfluxDB v2 & Grafana dengan Docker Compose

Buat file `docker-compose.yml` di komputer atau server lokal Anda, lalu jalankan `docker compose up -d`:

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

### Langkah Menghubungkan ESP32 ke Stack Docker di Atas:
1. Jalankan `docker compose up -d`.
2. Buka **InfluxDB UI** di `http://localhost:8086` (Login: `admin` / `adminpassword123`).
3. Pada file `secrets.h` di proyek Arduino Anda, sesuaikan nilai berikut:
   ```cpp
   #define INFLUXDB_URL    "http://<IP_KOMPUTER_ANDA>:8086"
   #define INFLUXDB_TOKEN  "iotlab-super-secret-influx-token"
   #define INFLUXDB_ORG    "iot_lab_org"
   #define INFLUXDB_BUCKET "energy_meter"
   ```

---

## 3. Spesifikasi InfluxDB Line Protocol

ESP32 mengirimkan data ke endpoint `POST /api/v2/write?org=iot_lab_org&bucket=energy_meter&precision=s` dengan struktur berikut:

- **Measurement:** `power_consumption`
- **Tags:**
  - `location`: Lokasi panel meter (contoh: `panel_utama`)
  - `mode`: `hardware` atau `simulation`
- **Fields:**
  - `voltage` (Float, Volt)
  - `current` (Float, Ampere)
  - `power` (Float, Watt)
  - `energy_kwh` (Float, kWh)
  - `frequency` (Float, Hz)
  - `pf` (Float, Power Factor 0.00 - 1.00)
  - `cost_idr` (Float, Estimasi Biaya PLN dalam Rupiah)

---

## 4. Contoh Query Flux untuk Dashboard Grafana

Di **Grafana** (`http://localhost:3000`), tambahkan **Data Source > InfluxDB**, pilih Query Language **Flux**, masukkan URL `http://influxdb:8086`, Organization `iot_lab_org`, Token `iotlab-super-secret-influx-token`, dan Default Bucket `energy_meter`.

Gunakan query Flux berikut untuk membuat panel visualisasi:

### a. Grafik Time-Series Daya Aktif (Watt) & Tegangan (Volt)
```flux
from(bucket: "energy_meter")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "power_consumption")
  |> filter(fn: (r) => r["location"] == "panel_utama")
  |> filter(fn: (r) => r["_field"] == "power" or r["_field"] == "voltage")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "mean")
```

### b. Panel Gauge / Stat: Akumulasi Konsumsi Energi (kWh) & Tagihan (Rp) Terakhir
```flux
from(bucket: "energy_meter")
  |> range(start: -24h)
  |> filter(fn: (r) => r["_measurement"] == "power_consumption")
  |> filter(fn: (r) => r["location"] == "panel_utama")
  |> filter(fn: (r) => r["_field"] == "energy_kwh" or r["_field"] == "cost_idr")
  |> last()
```

### c. Pemakaian Energi Harian (Selisih kWh per Hari)
```flux
from(bucket: "energy_meter")
  |> range(start: -7d)
  |> filter(fn: (r) => r["_measurement"] == "power_consumption")
  |> filter(fn: (r) => r["_field"] == "energy_kwh")
  |> aggregateWindow(every: 1d, fn: spread, createEmpty: false)
  |> yield(name: "daily_kwh")
```
