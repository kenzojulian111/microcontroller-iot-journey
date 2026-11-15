# Spesifikasi Topik MQTT, Integrasi Home Assistant & Node-RED

Dokumen ini menjelaskan daftar topik MQTT yang digunakan oleh **Proyek 04 (`04-mqtt-home-automation`)**, cara mengujinya dengan CLI `mosquitto_pub`/`mosquitto_sub`, serta contoh integrasi langsung ke **Home Assistant** dan **Node-RED**.

---

## 1. Daftar Topik MQTT (`NODE_ID = "node01"`)

| Topik MQTT | Arah ( dari sisi ESP ) | QoS / Retained | Format Payload | Deskripsi |
| :--- | :--- | :--- | :--- | :--- |
| `home/node01/status` | **Publish** (+ LWT) | QoS 1, Retained | `"online"` atau `"offline"` | Status ketersediaan perangkat (Last Will and Testament) |
| `home/node01/telemetry` | **Publish** (tiap 5 dtk) | QoS 0, Non-Retained | Objek JSON | Data suhu (°C), kelembapan (%), PIR, status relay, RSSI, uptime, dan free heap |
| `home/node01/motion` | **Publish** (saat berubah) | QoS 0, Retained | `"DETECTED"` atau `"CLEAR"` | Event instan saat sensor gerak PIR mendeteksi pergerakan |
| `home/node01/relay/state` | **Publish** (saat berubah) | QoS 0, Retained | `"ON"` atau `"OFF"` | State aktual relay (baik diubah dari MQTT maupun tombol fisik) |
| `home/node01/relay/set` | **Subscribe** | QoS 0 | `"ON"`, `"OFF"`, `"TOGGLE"`, atau JSON `{"state":"ON"}` | Topik perintah untuk menyalakan/mematikan relay dari jarak jauh |

### Contoh Payload JSON pada `home/node01/telemetry`:

```json
{
  "node": "node01",
  "board": "ESP32",
  "temperature": 27.4,
  "humidity": 65.0,
  "motion": false,
  "relay": "ON",
  "rssi": -52,
  "uptime": 145,
  "heap": 291840
}
```

---

## 2. Contoh Output Serial Monitor (`115200` baud)

```text
==================================================
  IoT Lab - 04 MQTT Home Automation Hub
  Node ID : node01 | Target Board : ESP32
==================================================
[WIFI] Menghubungkan ke SSID: Wokwi-GUEST ....
[WIFI] Terhubung!
[WIFI] Alamat IP : 10.13.37.2
[WIFI] Sinyal    : -52 dBm
[MQTT] Menghubungkan ke broker broker.emqx.io:1883 sebagai iotlab-node01-A4CF12...
[MQTT] Berhasil terhubung ke Broker!
[MQTT] Subscribed ke topik: home/node01/relay/set
[MQTT TX] home/node01/telemetry -> {"node":"node01","board":"ESP32","temperature":27.4,"humidity":65.0,"motion":false,"relay":"OFF","rssi":-52,"uptime":3,"heap":291840} (OK)
[PIR] Status Gerakan berubah -> DETECTED
[MQTT TX] home/node01/telemetry -> {"node":"node01","board":"ESP32","temperature":27.4,"humidity":65.0,"motion":true,"relay":"OFF","rssi":-52,"uptime":7,"heap":291712} (OK)
[MQTT RX] Topik: home/node01/relay/set | Payload: ON
[RELAY] State -> ON (Sumber: MQTT)
```

---

## 3. Pengujian Menggunakan Mosquitto CLI / MQTT Explorer

Anda dapat memantau dan mengontrol perangkat langsung dari terminal komputer (pastikan paket `mosquitto-clients` sudah terinstal):

### a. Berlangganan (Subscribe) ke Seluruh Topik Node:
```bash
mosquitto_sub -h broker.emqx.io -p 1883 -t "home/node01/#" -v
```

### b. Menyalakan Relay via Perintah Teks:
```bash
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m "ON"
```

### c. Mematikan Relay via Perintah Teks:
```bash
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m "OFF"
```

### d. Toggle Relay atau Mengirim Payload JSON:
```bash
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m "TOGGLE"
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m '{"state":"ON"}'
```

---

## 4. Konfigurasi YAML untuk Home Assistant (`configuration.yaml`)

Tambahkan blok berikut ke file `configuration.yaml` pada instalasi **Home Assistant** Anda agar saklar, sensor suhu/kelembapan, dan sensor gerak PIR otomatis muncul beserta deteksi *availability* (LWT):

```yaml
mqtt:
  switch:
    - name: "Lampu Ruangan Node 01"
      unique_id: "iotlab_node01_relay"
      state_topic: "home/node01/relay/state"
      command_topic: "home/node01/relay/set"
      availability_topic: "home/node01/status"
      payload_on: "ON"
      payload_off: "OFF"
      state_on: "ON"
      state_off: "OFF"
      payload_available: "online"
      payload_not_available: "offline"
      qos: 1
      retain: false

  sensor:
    - name: "Suhu Ruangan Node 01"
      unique_id: "iotlab_node01_temperature"
      state_topic: "home/node01/telemetry"
      availability_topic: "home/node01/status"
      unit_of_measurement: "°C"
      device_class: temperature
      value_template: "{{ value_json.temperature }}"

    - name: "Kelembapan Ruangan Node 01"
      unique_id: "iotlab_node01_humidity"
      state_topic: "home/node01/telemetry"
      availability_topic: "home/node01/status"
      unit_of_measurement: "%"
      device_class: humidity
      value_template: "{{ value_json.humidity }}"

  binary_sensor:
    - name: "Sensor Gerak PIR Node 01"
      unique_id: "iotlab_node01_motion"
      state_topic: "home/node01/motion"
      availability_topic: "home/node01/status"
      payload_on: "DETECTED"
      payload_off: "CLEAR"
      device_class: motion
```

---

## 5. Contoh Flow Node-RED (Import JSON)

Salin JSON di bawah ini ke menu **Import (`Ctrl+I`)** pada **Node-RED** untuk membuat alur pemantauan telemetri dan otomasi menyalakan lampu saat gerakan terdeteksi:

```json
[
  {
    "id": "mqtt_telemetry_in",
    "type": "mqtt in",
    "name": "Node01 Telemetry",
    "topic": "home/node01/telemetry",
    "qos": "0",
    "datatype": "json",
    "broker": "local_mqtt_broker",
    "x": 150,
    "y": 100,
    "wires": [["debug_telemetry"]]
  },
  {
    "id": "debug_telemetry",
    "type": "debug",
    "name": "Log Telemetri JSON",
    "active": true,
    "tosidebar": true,
    "complete": "payload",
    "x": 390,
    "y": 100,
    "wires": []
  },
  {
    "id": "mqtt_motion_in",
    "type": "mqtt in",
    "name": "Node01 PIR Motion",
    "topic": "home/node01/motion",
    "qos": "0",
    "datatype": "utf8",
    "broker": "local_mqtt_broker",
    "x": 150,
    "y": 200,
    "wires": [["auto_light_rule"]]
  },
  {
    "id": "auto_light_rule",
    "type": "function",
    "name": "Nyalakan Lampu Saat Ada Gerakan",
    "func": "if (msg.payload === 'DETECTED') {\n    msg.payload = 'ON';\n    return msg;\n}\nreturn null;",
    "outputs": 1,
    "x": 420,
    "y": 200,
    "wires": [["mqtt_relay_out"]]
  },
  {
    "id": "mqtt_relay_out",
    "type": "mqtt out",
    "name": "Set Relay Node01",
    "topic": "home/node01/relay/set",
    "qos": "0",
    "retain": "false",
    "broker": "local_mqtt_broker",
    "x": 690,
    "y": 200,
    "wires": []
  },
  {
    "id": "local_mqtt_broker",
    "type": "mqtt-broker",
    "name": "EMQX Public Broker",
    "broker": "broker.emqx.io",
    "port": "1883",
    "clientid": "",
    "usetls": false,
    "protocolVersion": "4",
    "keepalive": "60",
    "cleansession": true
  }
]
```
