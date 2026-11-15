/**
 * ============================================================================
 * Project 25: Computer Vision OpenCV + Hardware Servo Gate Trigger
 * File: servo_gate_controller.ino
 * Target MCUs:
 *   - Arduino Uno / Nano (ATmega328P) -> Uses <Servo.h> + USB Serial Bridge
 *   - ESP32 DevKit V1                 -> Uses LEDC 50 Hz Servo PWM + Serial
 * ============================================================================
 *
 * HARDWARE & SAFETY ARCHITECTURE:
 *   - Motorized Barrier Boom: SG90 / MG996R Servo (0 deg = CLOSED, 90 deg = OPEN)
 *     Driven via non-blocking incremental S-curve/trapezoidal angular ramping
 *     (20 ms per degree step) to eliminate mechanical gear shock.
 *   - Anti-Crush Ultrasonic Safety Loop: HC-SR04 Ultrasonic Transducer
 *     Continuously measures clearance beneath the boom. If an obstacle/vehicle
 *     is detected at distance < 35.0 cm (`SAFETY_CLEARANCE_CM`), the controller:
 *       1. Refuses any `GATE:CLOSE` command (`STATE_OBSTRUCTED_HOLD`).
 *       2. Immediately reverses the boom back to 90 deg (OPEN) if a vehicle
 *          enters the lane while the boom is descending (`STATE_CLOSING`).
 *   - Traffic Signal Indicators:
 *       * Red LED   : Active when CLOSED, CLOSING, or LOCKED.
 *       * Green LED : Active when OPEN (0 deg -> 90 deg verified).
 *   - Acoustic Warning Buzzer:
 *       * Audible safety beeps during gate motion and obstruction holds.
 *
 * SERIAL COMMAND PROTOCOL (115200 bps, newline-terminated):
 *   - `GATE:OPEN`   -> Unlocks (if not hard-locked) and smoothly ramps to 90 deg
 *   - `GATE:CLOSE`  -> Initiates closing ramp to 0 deg (if ultrasonic path clear)
 *   - `GATE:LOCK`   -> Emergency lockdown: closes barrier and ignores OPEN triggers
 *   - `GATE:UNLOCK` -> Releases lockdown back to normal closed standby
 *   - `GATE:STATUS` -> Emits JSON telemetry frame over Serial
 * ============================================================================
 */

#include <Arduino.h>

#if !defined(ESP32)
  #include <Servo.h>
#endif

// ============================================================================
// Cross-Platform Pin Assignments (Arduino Uno/Nano vs ESP32)
// ============================================================================
#if defined(ESP32)
  constexpr uint8_t PIN_SERVO_PWM     = 18;
  constexpr uint8_t PIN_HCSR04_TRIG   = 5;
  constexpr uint8_t PIN_HCSR04_ECHO   = 19;
  constexpr uint8_t PIN_LED_RED       = 33;
  constexpr uint8_t PIN_LED_GREEN     = 32;
  constexpr uint8_t PIN_BUZZER        = 25;
  constexpr uint8_t PIN_BTN_MANUAL    = 14;
#else
  constexpr uint8_t PIN_SERVO_PWM     = 9;
  constexpr uint8_t PIN_HCSR04_TRIG   = 10;
  constexpr uint8_t PIN_HCSR04_ECHO   = 11;
  constexpr uint8_t PIN_LED_RED       = 4;
  constexpr uint8_t PIN_LED_GREEN     = 5;
  constexpr uint8_t PIN_BUZZER        = 8;
  constexpr uint8_t PIN_BTN_MANUAL    = 2;
#endif

// ============================================================================
// Kinematic & Safety Threshold Constants
// ============================================================================
constexpr int      ANGLE_CLOSED_DEG       = 0;
constexpr int      ANGLE_OPEN_DEG         = 90;
constexpr uint32_t SERVO_STEP_INTERVAL_MS = 18;   // ~1.62 seconds full 90-deg sweep
constexpr uint32_t AUTO_CLOSE_DWELL_MS    = 5000; // Hold open 5 s after vehicle clears
constexpr uint32_t TELEMETRY_INTERVAL_MS  = 500;
constexpr float    SAFETY_CLEARANCE_CM    = 35.0f;

enum GateState : uint8_t {
  STATE_CLOSED          = 0,
  STATE_OPENING         = 1,
  STATE_OPEN            = 2,
  STATE_CLOSING         = 3,
  STATE_OBSTRUCTED_HOLD = 4,
  STATE_LOCKED          = 5
};

#if !defined(ESP32)
Servo gateServo;
#endif

GateState currentState        = STATE_CLOSED;
int       currentAngleDeg     = ANGLE_CLOSED_DEG;
int       targetAngleDeg      = ANGLE_CLOSED_DEG;
float     clearanceDistanceCm = 120.0f;
bool      vehiclePresent      = false;

uint32_t  lastServoStepMs     = 0;
uint32_t  openHoldStartMs     = 0;
uint32_t  lastTelemetryMs     = 0;
uint32_t  lastButtonMs        = 0;

// ============================================================================
// Cross-Platform Servo PWM Driver (500 us to 2400 us Pulse Width)
// ============================================================================
void writeServoAngle(int angleDeg) {
  angleDeg = constrain(angleDeg, ANGLE_CLOSED_DEG, ANGLE_OPEN_DEG);
#if defined(ESP32)
  // 50 Hz period = 20,000 us. Map 0..180 deg -> 500..2400 us on 16-bit duty (65535)
  uint32_t pulseUs = map(angleDeg, 0, 180, 500, 2400);
  uint32_t duty    = (pulseUs * 65535UL) / 20000UL;
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_SERVO_PWM, duty);
  #else
    ledcWrite(0, duty);
  #endif
#else
  gateServo.write(angleDeg);
#endif
}

// ============================================================================
// HC-SR04 Ultrasonic Vehicle Presence Sensor
// ============================================================================
float measureClearanceCm() {
  digitalWrite(PIN_HCSR04_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_HCSR04_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_HCSR04_TRIG, LOW);

  unsigned long durationUs = pulseIn(PIN_HCSR04_ECHO, HIGH, 25000UL);
  if (durationUs == 0) {
    return 199.0f; // No echo within range -> lane clear
  }
  return (durationUs * 0.0343f) * 0.5f;
}

const char* stateToString(GateState st) {
  switch (st) {
    case STATE_CLOSED:          return "CLOSED";
    case STATE_OPENING:         return "OPENING";
    case STATE_OPEN:            return "OPEN";
    case STATE_CLOSING:         return "CLOSING";
    case STATE_OBSTRUCTED_HOLD: return "OBSTRUCTED_HOLD";
    case STATE_LOCKED:          return "LOCKED";
    default:                    return "UNKNOWN";
  }
}

void updateTrafficIndicators() {
  if (currentState == STATE_OPEN) {
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
  } else {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
  }
}

void emitTelemetryFrame(const char* eventTag) {
  Serial.print(F("{\"event\":\""));
  Serial.print(eventTag);
  Serial.print(F("\",\"state\":\""));
  Serial.print(stateToString(currentState));
  Serial.print(F("\",\"angle_deg\":"));
  Serial.print(currentAngleDeg);
  Serial.print(F(",\"distance_cm\":"));
  Serial.print(clearanceDistanceCm, 1);
  Serial.print(F(",\"vehicle_under_gate\":"));
  Serial.print(vehiclePresent ? F("true") : F("false"));
  Serial.println(F("}"));
}

// ============================================================================
// Command State Machine Transitions
// ============================================================================
void requestGateOpen(const char* source) {
  if (currentState == STATE_LOCKED) {
    emitTelemetryFrame("IGNORED_GATE_LOCKED");
    tone(PIN_BUZZER, 320, 220);
    return;
  }
  targetAngleDeg  = ANGLE_OPEN_DEG;
  currentState    = (currentAngleDeg >= ANGLE_OPEN_DEG) ? STATE_OPEN : STATE_OPENING;
  openHoldStartMs = millis();
  tone(PIN_BUZZER, 1850, 120);
  updateTrafficIndicators();
  emitTelemetryFrame(source);
}

void requestGateClose(const char* source) {
  if (currentState == STATE_LOCKED) {
    return;
  }
  if (vehiclePresent) {
    // Refuse to close while HC-SR04 detects a vehicle underneath the barrier
    currentState   = STATE_OBSTRUCTED_HOLD;
    targetAngleDeg = ANGLE_OPEN_DEG;
    tone(PIN_BUZZER, 440, 260);
    emitTelemetryFrame("SAFETY_ABORT_VEHICLE_PRESENT");
    return;
  }
  targetAngleDeg = ANGLE_CLOSED_DEG;
  currentState   = (currentAngleDeg <= ANGLE_CLOSED_DEG) ? STATE_CLOSED : STATE_CLOSING;
  tone(PIN_BUZZER, 1100, 100);
  updateTrafficIndicators();
  emitTelemetryFrame(source);
}

void requestGateLock() {
  currentState   = STATE_LOCKED;
  targetAngleDeg = ANGLE_CLOSED_DEG;
  tone(PIN_BUZZER, 300, 400);
  updateTrafficIndicators();
  emitTelemetryFrame("CMD_LOCK_ENGAGED");
}

void requestGateUnlock() {
  currentState   = (currentAngleDeg >= ANGLE_OPEN_DEG) ? STATE_OPEN : STATE_CLOSED;
  tone(PIN_BUZZER, 1560, 120);
  updateTrafficIndicators();
  emitTelemetryFrame("CMD_UNLOCKED");
}

// ============================================================================
// Setup & Peripheral Initialization
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_HCSR04_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_ECHO, INPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_MANUAL, INPUT_PULLUP);

#if defined(ESP32)
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_SERVO_PWM, 50, 16);
  #else
    ledcSetup(0, 50, 16);
    ledcAttachPin(PIN_SERVO_PWM, 0);
  #endif
#else
  gateServo.attach(PIN_SERVO_PWM);
#endif

  writeServoAngle(ANGLE_CLOSED_DEG);
  updateTrafficIndicators();

  Serial.println(F("\n================================================================"));
  Serial.println(F("Project 25: OpenCV Smart Gate Servo & Ultrasonic Safety Controller"));
  Serial.println(F("Commands: GATE:OPEN | GATE:CLOSE | GATE:LOCK | GATE:UNLOCK"));
  Serial.println(F("================================================================"));
  emitTelemetryFrame("BOOT_READY");
}

// ============================================================================
// Main Non-Blocking Kinematic & Safety Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // 1. Sample HC-SR04 Ultrasonic Vehicle Presence Sensor
  clearanceDistanceCm = measureClearanceCm();
  vehiclePresent      = (clearanceDistanceCm < SAFETY_CLEARANCE_CM);

  // 2. Anti-Crush Safety Interlock: If closing and a vehicle enters the lane,
  //    immediately abort closure and reverse boom to 90 deg!
  if (vehiclePresent && (currentState == STATE_CLOSING || currentState == STATE_OPEN)) {
    if (currentState == STATE_CLOSING) {
      targetAngleDeg = ANGLE_OPEN_DEG;
      currentState   = STATE_OBSTRUCTED_HOLD;
      tone(PIN_BUZZER, 2200, 200);
      emitTelemetryFrame("EMERGENCY_REVERSE_OBSTACLE");
    }
    openHoldStartMs = now; // Reset auto-close dwell timer while vehicle is underneath
  }

  // Clear obstructed hold once vehicle clears the ultrasonic zone
  if (!vehiclePresent && currentState == STATE_OBSTRUCTED_HOLD && currentAngleDeg >= ANGLE_OPEN_DEG) {
    currentState    = STATE_OPEN;
    openHoldStartMs = now;
    updateTrafficIndicators();
    emitTelemetryFrame("OBSTACLE_CLEARED_HOLDING");
  }

  // 3. Auto-close dwell timer after vehicle has passed
  if (currentState == STATE_OPEN && !vehiclePresent &&
      (now - openHoldStartMs) >= AUTO_CLOSE_DWELL_MS) {
    requestGateClose("AUTO_CLOSE_TIMER");
  }

  // 4. Incremental Servo Angular Ramping (Smooth motion without `delay()`)
  if ((now - lastServoStepMs) >= SERVO_STEP_INTERVAL_MS) {
    lastServoStepMs = now;
    if (currentAngleDeg < targetAngleDeg) {
      currentAngleDeg++;
      writeServoAngle(currentAngleDeg);
      if (currentAngleDeg >= ANGLE_OPEN_DEG && currentState == STATE_OPENING) {
        currentState    = STATE_OPEN;
        openHoldStartMs = now;
        updateTrafficIndicators();
        emitTelemetryFrame("REACHED_OPEN_90_DEG");
      }
    } else if (currentAngleDeg > targetAngleDeg) {
      // Only step downward if ultrasonic path is unobstructed (or forced lock)
      if (!vehiclePresent || currentState == STATE_LOCKED) {
        currentAngleDeg--;
        writeServoAngle(currentAngleDeg);
        if (currentAngleDeg <= ANGLE_CLOSED_DEG && currentState == STATE_CLOSING) {
          currentState = STATE_CLOSED;
          updateTrafficIndicators();
          emitTelemetryFrame("REACHED_CLOSED_0_DEG");
        }
      }
    }
  }

  // 5. Manual Override Pushbutton (for Wokwi / Guard Booth testing)
  if (digitalRead(PIN_BTN_MANUAL) == LOW && (now - lastButtonMs) > 450) {
    lastButtonMs = now;
    if (currentState == STATE_CLOSED || currentState == STATE_CLOSING) {
      requestGateOpen("MANUAL_BUTTON_OPEN");
    } else {
      requestGateClose("MANUAL_BUTTON_CLOSE");
    }
  }

  // 6. Parse Incoming Serial Commands from Python OpenCV Backend
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "GATE:OPEN") {
      requestGateOpen("OPENCV_SERIAL_OPEN");
    } else if (cmd == "GATE:CLOSE") {
      requestGateClose("OPENCV_SERIAL_CLOSE");
    } else if (cmd == "GATE:LOCK") {
      requestGateLock();
    } else if (cmd == "GATE:UNLOCK") {
      requestGateUnlock();
    } else if (cmd == "GATE:STATUS") {
      emitTelemetryFrame("STATUS_POLL");
    }
  }

  // 7. Periodic JSON status heartbeat
  if ((now - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    emitTelemetryFrame("HEARTBEAT");
  }
}
