#ifndef SECRETS_EXAMPLE_H
#define SECRETS_EXAMPLE_H

/**
 * @file secrets.example.h
 * @brief Wi-Fi & MQTT Broker Credentials Template for Project 12 (Electronic Nose)
 *
 * Copy or rename this header to `secrets.h` for custom local network settings,
 * or leave `secrets.example.h` in place for default Wokwi-GUEST / HiveMQ simulation.
 */

#define WIFI_SSID           "Wokwi-GUEST"
#define WIFI_PASSWORD       ""

#define MQTT_BROKER_HOST    "broker.hivemq.com"
#define MQTT_BROKER_PORT    1883
#define MQTT_CLIENT_ID      "esp32-enose-node-12"
#define MQTT_USERNAME       ""
#define MQTT_PASSWORD       ""

#define MQTT_TOPIC_TELEMETRY "iot_lab/project12/enose/telemetry"
#define MQTT_TOPIC_ALERT     "iot_lab/project12/enose/alert"

#endif // SECRETS_EXAMPLE_H
