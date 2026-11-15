/*
 * Project 22: Full-Duplex Bi-Directional WebSockets & Flask-SocketIO Realtime Hub
 * Target Architectures: ESP32 & ESP8266
 * Features:
 *   - Hardware Interrupt (ISR) + 35 ms debounced tactile pushbuttons (2 channels)
 *   - 2-Channel Digital Relay Outputs (GPIO 26 / GPIO 27 on ESP32)
 *   - Persistent Engine.IO v4 / Socket.IO WebSocket framing via WebSocketsClient
 *   - Sub-50ms physical button-press push to all connected browsers without HTTP polling
 *   - Immediate relay actuation upon receiving "mcu_set_relay" event from web UI
 */

#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #define IRAM_ATTR ICACHE_RAM_ATTR
#endif
#include <WebSocketsClient.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

#if defined(ESP32)
  constexpr uint8_t PIN_RELAY_1 = 26;
  constexpr uint8_t PIN_RELAY_2 = 27;
  constexpr uint8_t PIN_BTN_1   = 18;
  constexpr uint8_t PIN_BTN_2   = 19;
#else
  constexpr uint8_t PIN_RELAY_1 = 5;  // D1
  constexpr uint8_t PIN_RELAY_2 = 4;  // D2
  constexpr uint8_t PIN_BTN_1   = 12; // D6
  constexpr uint8_t PIN_BTN_2   = 13; // D7
#endif

constexpr uint32_t DEBOUNCE_MS = 35UL;

WebSocketsClient webSocket;

struct NodeState {
  uint8_t  relay1;
  uint8_t  relay2;
  uint32_t seq;
  bool     wsConnected;
};

NodeState state = {0, 0, 0, false};

// ISR flags and debounce timestamps
volatile bool     isrBtn1Triggered = false;
volatile uint32_t isrBtn1LastMs    = 0;
volatile bool     isrBtn2Triggered = false;
volatile uint32_t isrBtn2LastMs    = 0;

void IRAM_ATTR onButton1Fall() {
  uint32_t now = millis();
  if (now - isrBtn1LastMs >= DEBOUNCE_MS) {
    isrBtn1LastMs    = now;
    isrBtn1Triggered = true;
  }
}

void IRAM_ATTR onButton2Fall() {
  uint32_t now = millis();
  if (now - isrBtn2LastMs >= DEBOUNCE_MS) {
    isrBtn2LastMs    = now;
    isrBtn2Triggered = true;
  }
}

void applyRelays() {
  digitalWrite(PIN_RELAY_1, state.relay1 ? HIGH : LOW);
  digitalWrite(PIN_RELAY_2, state.relay2 ? HIGH : LOW);
}

// Emit Socket.IO event frame ("42[\"device_state_update\",{...}]")
void emitDeviceState(const char* triggerSource) {
  if (!state.wsConnected) return;
  state.seq++;
  char frame[256];
  snprintf(
    frame,
    sizeof(frame),
    "42[\"device_state_update\",{\"device_id\":\"%s\",\"seq\":%lu,\"relay1\":%u,\"relay2\":%u,\"source\":\"%s\",\"uptime_ms\":%lu}]",
    DEVICE_ID,
    static_cast<unsigned long>(state.seq),
    state.relay1,
    state.relay2,
    triggerSource,
    static_cast<unsigned long>(millis())
  );
  webSocket.sendTXT(frame);
}

void handleSocketIoEvent(const char* jsonArrayPayload) {
  // Parse ["mcu_set_relay",{"channel":1,"state":1}] or ["web_toggle_relay",...]
  if (strstr(jsonArrayPayload, "mcu_set_relay") != nullptr ||
      strstr(jsonArrayPayload, "web_toggle_relay") != nullptr) {
    const char* chPtr = strstr(jsonArrayPayload, "\"channel\":");
    const char* stPtr = strstr(jsonArrayPayload, "\"state\":");
    int channel = chPtr ? atoi(chPtr + 10) : 0;
    int targetState = stPtr ? atoi(stPtr + 8) : -1;

    if (channel == 1) {
      state.relay1 = (targetState == 0 || targetState == 1) ? static_cast<uint8_t>(targetState) : (state.relay1 ^ 1);
      applyRelays();
      emitDeviceState("WEB_SOCKETIO");
    } else if (channel == 2) {
      state.relay2 = (targetState == 0 || targetState == 1) ? static_cast<uint8_t>(targetState) : (state.relay2 ^ 1);
      applyRelays();
      emitDeviceState("WEB_SOCKETIO");
    }
  }
}

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      state.wsConnected = false;
      Serial.println(F("[WS] Disconnected from Flask-SocketIO Hub."));
      break;

    case WStype_CONNECTED:
      Serial.println(F("[WS] TCP/WebSocket connected; sending Socket.IO v4 namespace connect (40)..."));
      webSocket.sendTXT("40");
      state.wsConnected = true;
      emitDeviceState("MCU_BOOT_SYNC");
      break;

    case WStype_TEXT: {
      const char* msg = reinterpret_cast<const char*>(payload);
      if (length >= 1 && msg[0] == '2') {
        // Engine.IO PING ("2") -> reply PONG ("3") immediately for low-latency keepalive
        webSocket.sendTXT("3");
      } else if (length >= 2 && msg[0] == '4' && msg[1] == '2') {
        handleSocketIoEvent(msg + 2);
      }
      break;
    }

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  pinMode(PIN_BTN_1, INPUT_PULLUP);
  pinMode(PIN_BTN_2, INPUT_PULLUP);
  applyRelays();

  attachInterrupt(digitalPinToInterrupt(PIN_BTN_1), onButton1Fall, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_2), onButton2Fall, FALLING);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

  webSocket.begin(WS_HUB_HOST, WS_HUB_PORT, WS_HUB_PATH);
  webSocket.onEvent(onWebSocketEvent);
  webSocket.setReconnectInterval(2000);
}

void loop() {
  webSocket.loop();

  if (isrBtn1Triggered) {
    noInterrupts();
    isrBtn1Triggered = false;
    interrupts();
    state.relay1 ^= 1;
    applyRelays();
    Serial.printf("[ISR] Physical Button 1 pressed -> Relay 1 = %u\n", state.relay1);
    emitDeviceState("HW_BUTTON_1");
  }

  if (isrBtn2Triggered) {
    noInterrupts();
    isrBtn2Triggered = false;
    interrupts();
    state.relay2 ^= 1;
    applyRelays();
    Serial.printf("[ISR] Physical Button 2 pressed -> Relay 2 = %u\n", state.relay2);
    emitDeviceState("HW_BUTTON_2");
  }
}
