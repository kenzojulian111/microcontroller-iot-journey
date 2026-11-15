/*
 * Project 19: Flask Serial LED, PWM & Servo Controller
 * Target Architectures: ATmega328P (Arduino Uno/Nano) & ESP32
 * Communication: UART 115200 8N1 (Newline-delimited ASCII frames with optional XOR Checksum)
 *
 * Frame Protocol:
 *   Incoming Commands:
 *     CMD:RELAY,<0|1>[*XX]\n
 *     CMD:PWM,<0-255>[*XX]\n
 *     CMD:SERVO,<0-180>[*XX]\n
 *     CMD:STATUS[*XX]\n
 *   Where [*XX] is an optional NMEA-style hexadecimal XOR checksum of all characters
 *   preceding the '*' delimiter.
 *
 *   Outgoing JSON Response:
 *     {"ok":true,"seq":14,"relay":1,"pwm":128,"servo":90,"uptime_ms":48210}
 *     {"ok":false,"err":"CHECKSUM_MISMATCH","expected":"4A"}
 */

#include <Arduino.h>

#if defined(ESP32)
  // ESP32 Pin Assignments & LEDC Configuration
  constexpr uint8_t PIN_RELAY = 26;
  constexpr uint8_t PIN_PWM   = 25;
  constexpr uint8_t PIN_SERVO = 27;

  constexpr uint8_t LEDC_CH_PWM   = 0;
  constexpr uint8_t LEDC_CH_SERVO = 1;
#else
  // ATmega328P (Arduino Uno / Nano) Pin Assignments
  #include <Servo.h>
  constexpr uint8_t PIN_RELAY = 4;
  constexpr uint8_t PIN_PWM   = 5;  // Timer0 PWM (avoiding Timer1 conflict with Servo library)
  constexpr uint8_t PIN_SERVO = 9;  // Timer1 Servo
  Servo actuatorServo;
#endif

// Serial Receive Ring/Line Buffer
constexpr size_t RX_BUFFER_SIZE = 96;
char rxBuffer[RX_BUFFER_SIZE];
size_t rxIndex = 0;

// Hardware Actuator State
struct ActuatorState {
  uint8_t relay;      // 0 = OFF, 1 = ON
  uint8_t pwm;        // 0..255 (8-bit duty cycle)
  uint8_t servoAngle; // 0..180 degrees
  uint32_t cmdCount;  // Monotonic command counter
};

ActuatorState state = {0, 0, 90, 0};

// Compute 8-bit XOR checksum over [start, end)
uint8_t computeXorChecksum(const char* start, const char* end) {
  uint8_t crc = 0;
  while (start < end) {
    crc ^= static_cast<uint8_t>(*start++);
  }
  return crc;
}

// Hardware abstraction for setting PWM and Servo outputs
void applyHardwareState() {
  digitalWrite(PIN_RELAY, state.relay ? HIGH : LOW);

#if defined(ESP32)
  ledcWrite(LEDC_CH_PWM, state.pwm);
  // Map 0-180 deg to 500-2400 us pulse width out of 20000 us (50 Hz, 16-bit = 65535 ticks)
  uint32_t pulseUs = 500UL + (static_cast<uint32_t>(state.servoAngle) * 1900UL) / 180UL;
  uint32_t duty16  = (pulseUs * 65536UL) / 20000UL;
  ledcWrite(LEDC_CH_SERVO, duty16);
#else
  analogWrite(PIN_PWM, state.pwm);
  actuatorServo.write(state.servoAngle);
#endif
}

// Emit deterministic JSON telemetry/acknowledgment frame
void sendStateJson(bool ok, const char* errCode = nullptr) {
  Serial.print(F("{\"ok\":"));
  Serial.print(ok ? F("true") : F("false"));
  Serial.print(F(",\"seq\":"));
  Serial.print(state.cmdCount);
  Serial.print(F(",\"relay\":"));
  Serial.print(state.relay);
  Serial.print(F(",\"pwm\":"));
  Serial.print(state.pwm);
  Serial.print(F(",\"servo\":"));
  Serial.print(state.servoAngle);
  Serial.print(F(",\"uptime_ms\":"));
  Serial.print(millis());
  if (errCode != nullptr) {
    Serial.print(F(",\"err\":\""));
    Serial.print(errCode);
    Serial.print(F("\""));
  }
  Serial.println(F("}"));
}

// Parse and execute a single null-terminated command frame
void processFrame(char* frame) {
  // Strip trailing CR/whitespace
  size_t len = strlen(frame);
  while (len > 0 && (frame[len - 1] == '\r' || frame[len - 1] == ' ' || frame[len - 1] == '\t')) {
    frame[--len] = '\0';
  }
  if (len == 0) return;

  // Optional Checksum Verification: CMD:PWM,128*3F
  char* starPtr = strchr(frame, '*');
  if (starPtr != nullptr) {
    uint8_t expectedCrc = computeXorChecksum(frame, starPtr);
    uint8_t providedCrc = static_cast<uint8_t>(strtoul(starPtr + 1, nullptr, 16));
    if (expectedCrc != providedCrc) {
      sendStateJson(false, "CHECKSUM_MISMATCH");
      return;
    }
    *starPtr = '\0'; // Terminate payload at '*'
  }

  // Verify "CMD:" prefix
  if (strncmp(frame, "CMD:", 4) != 0) {
    sendStateJson(false, "INVALID_PREFIX");
    return;
  }

  const char* body = frame + 4;

  if (strcmp(body, "STATUS") == 0) {
    state.cmdCount++;
    sendStateJson(true);
    return;
  }

  // Split "<TARGET>,<VALUE>"
  const char* comma = strchr(body, ',');
  if (comma == nullptr) {
    sendStateJson(false, "MISSING_ARGUMENT");
    return;
  }

  char* endPtr = nullptr;
  long val = strtol(comma + 1, &endPtr, 10);
  if (endPtr == comma + 1 || *endPtr != '\0') {
    sendStateJson(false, "NON_NUMERIC_VALUE");
    return;
  }

  if (strncmp(body, "RELAY,", 6) == 0) {
    if (val != 0 && val != 1) {
      sendStateJson(false, "RELAY_OUT_OF_RANGE");
      return;
    }
    state.relay = static_cast<uint8_t>(val);
    state.cmdCount++;
    applyHardwareState();
    sendStateJson(true);
  } else if (strncmp(body, "PWM,", 4) == 0) {
    if (val < 0 || val > 255) {
      sendStateJson(false, "PWM_OUT_OF_RANGE");
      return;
    }
    state.pwm = static_cast<uint8_t>(val);
    state.cmdCount++;
    applyHardwareState();
    sendStateJson(true);
  } else if (strncmp(body, "SERVO,", 6) == 0) {
    if (val < 0 || val > 180) {
      sendStateJson(false, "SERVO_OUT_OF_RANGE");
      return;
    }
    state.servoAngle = static_cast<uint8_t>(val);
    state.cmdCount++;
    applyHardwareState();
    sendStateJson(true);
  } else {
    sendStateJson(false, "UNKNOWN_COMMAND");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY, OUTPUT);

#if defined(ESP32)
  ledcSetup(LEDC_CH_PWM, 5000, 8);
  ledcAttachPin(PIN_PWM, LEDC_CH_PWM);
  ledcSetup(LEDC_CH_SERVO, 50, 16);
  ledcAttachPin(PIN_SERVO, LEDC_CH_SERVO);
#else
  pinMode(PIN_PWM, OUTPUT);
  actuatorServo.attach(PIN_SERVO);
#endif

  applyHardwareState();
  delay(50);
  sendStateJson(true);
}

void loop() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      rxBuffer[rxIndex] = '\0';
      processFrame(rxBuffer);
      rxIndex = 0;
    } else if (rxIndex < RX_BUFFER_SIZE - 1) {
      rxBuffer[rxIndex++] = c;
    } else {
      // Buffer overflow protection: flush and report error
      rxIndex = 0;
      sendStateJson(false, "RX_BUFFER_OVERFLOW");
    }
  }
}
