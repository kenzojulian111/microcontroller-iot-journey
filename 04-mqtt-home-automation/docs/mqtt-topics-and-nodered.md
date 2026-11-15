# MQTT Topic Specification, Home Assistant YAML & Node-RED Flows — 04 MQTT Home Automation Hub

This document defines the MQTT topic hierarchy implemented by **Project 04 (`04-mqtt-home-automation`)**, provides CLI verification commands using `mosquitto_pub`/`mosquitto_sub`, and supplies ready-to-use configurations for **Home Assistant** and **Node-RED**.

---

## 1. MQTT Topic Hierarchy (`NODE_ID = "node01"`)

| MQTT Topic | Direction (ESP Perspective) | QoS / Retained | Payload Schema | Description |
| :--- | :--- | :--- | :--- | :--- |
| `home/node01/status` | **Publish** (+ Broker LWT) | QoS 1, Retained | `"online"` or `"offline"` | Device availability state (Last Will and Testament) |
| `home/node01/telemetry` | **Publish** (every 5 s) | QoS 0, Non-Retained | JSON Object | Temperature (°C), humidity (%RH), PIR motion flag, relay state, RSSI, uptime, and free heap |
| `home/node01/motion` | **Publish** (on edge change) | QoS 0, Retained | `"DETECTED"` or `"CLEAR"` | Instantaneous edge-triggered event from the HC-SR501 PIR sensor |
| `home/node01/relay/state` | **Publish** (on state change) | QoS 0, Retained | `"ON"` or `"OFF"` | Authoritative relay state (updated by both MQTT commands and physical button) |
| `home/node01/relay/set` | **Subscribe** | QoS 0 | `"ON"`, `"OFF"`, `"TOGGLE"`, or JSON `{"state":"ON"}` | Inbound command topic for remote relay actuation |

### Example JSON Payload on `home/node01/telemetry`

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

## 2. UART Serial Monitor Output (`115200` baud)

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

## 3. Verification via Mosquitto CLI & MQTT Explorer

You can inspect telemetry streams and dispatch actuation commands directly from a Linux/macOS/Windows terminal (`mosquitto-clients` package):

### a. Subscribe to the Complete Node Topic Tree
```bash
mosquitto_sub -h broker.emqx.io -p 1883 -t "home/node01/#" -v
```

### b. Energize the Relay via Plain-Text Command
```bash
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m "ON"
```

### c. De-energize the Relay via Plain-Text Command
```bash
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m "OFF"
```

### d. Toggle the Relay or Send a JSON Command Payload
```bash
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m "TOGGLE"
mosquitto_pub -h broker.emqx.io -p 1883 -t "home/node01/relay/set" -m '{"state":"ON"}'
```

---

## 4. Home Assistant Integration (`configuration.yaml`)

Add the following `mqtt:` block to your **Home Assistant** `configuration.yaml` to register the switch, temperature/humidity sensors, and PIR binary occupancy sensor with automatic LWT availability tracking:

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

## 5. Node-RED Automation Flow (Importable JSON)

Copy the JSON array below into **Node-RED** via **Menu > Import (`Ctrl+I`)** to deploy a telemetry monitor and an automated motion-activated lighting rule:

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
