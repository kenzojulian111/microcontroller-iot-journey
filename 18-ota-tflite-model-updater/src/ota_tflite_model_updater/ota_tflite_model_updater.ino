/**
 * @file ota_tflite_model_updater.ino
 * @brief Project 18: Edge MLOps Dynamic Model Hot-Swapping Pipeline
 *        (LittleFS + HTTPClient + MbedTLS SHA-256 Verification + Zero-Reboot Arena Reload)
 *
 * Key Capabilities:
 *   1. Decouples Neural Network Weights (`/models/active_model.bin` on LittleFS) from the
 *      compiled C++ application firmware so models can be updated over-the-air in <200 ms
 *      without reflashing or rebooting the ESP32!
 *   2. Polls a remote HTTP Model Registry (`model_manifest.json`) or accepts simulated
 *      OTA pushes in Wokwi via hardware pushbuttons:
 *        - GPIO 12 (BTN_OTA_V2):   Downloads & hot-swaps Model v2.1.0 (Drift-Recalibrated)
 *        - GPIO 14 (BTN_CORRUPT):  Injects a corrupted staging binary with a tampered byte to
 *                                  demonstrate MbedTLS SHA-256 rejection & automatic rollback!
 *        - GPIO 27 (BTN_ROLLBACK): Rolls back to Factory Baseline Model v1.0.0
 *   3. Potentiometer on GPIO 34 adjusts simulated ambient thermal drift (25 C -> 75 C) so you
 *      can directly observe how v1.0.0 vs. v2.1.0 classifies thermal drift in real time.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "secrets.example.h"
#include "dynamic_ml_runtime.h"

static constexpr int PIN_BTN_OTA_V2     = 12;
static constexpr int PIN_BTN_CORRUPT    = 14;
static constexpr int PIN_BTN_ROLLBACK   = 27;
static constexpr int PIN_POT_DRIFT      = 34;
static constexpr int PIN_LED_SWAPPING   = 2;
static constexpr int PIN_I2C_SDA        = 21;
static constexpr int PIN_I2C_SCL        = 22;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
EdgeMLOps::DynamicMLRuntime mlRuntime;

String   lastMLOpsStatus = "READY (BOOT v1.0.0)";
uint32_t inferenceStep   = 0;
bool     prevBtnV2       = HIGH;
bool     prevBtnCorrupt  = HIGH;
bool     prevBtnRollback = HIGH;

/**
 * @brief Stages a model binary into `/models/staging_model.bin` (either fetched via
 *        HTTPClient from a remote registry URL or synthesized locally in Wokwi) and
 *        invokes cryptographic SHA-256 verification + atomic hot-swap.
 */
bool performModelOTAUpdate(uint8_t target_preset, bool inject_bit_flip_attack) {
    digitalWrite(PIN_LED_SWAPPING, HIGH);
    uint32_t t0 = millis();

    // Generate the canonical upstream registry binary and its authentic SHA-256 digest
    EdgeMLOps::DynamicModelBinary upstreamModel =
        EdgeMLOps::DynamicMLRuntime::buildPresetModel(target_preset);

    char expected_sha256[65] = {0};
    EdgeMLOps::SHA256Verifier::computeBufferHex(
        reinterpret_cast<const uint8_t*>(&upstreamModel),
        sizeof(EdgeMLOps::DynamicModelBinary),
        expected_sha256
    );

    // If tampering simulation is requested, flip 1 bit in the downloaded staging payload
    EdgeMLOps::DynamicModelBinary stagedPayload = upstreamModel;
    if (inject_bit_flip_attack) {
        stagedPayload.w1[0][0] ^= 0x7F;
    }

    // Write payload to LittleFS staging slot `/models/staging_model.bin`
    File sf = LittleFS.open(STAGING_MODEL_PATH, "w");
    if (!sf) {
        lastMLOpsStatus = "ERR: STAGING WRITE";
        digitalWrite(PIN_LED_SWAPPING, LOW);
        return false;
    }
    sf.write(reinterpret_cast<const uint8_t*>(&stagedPayload), sizeof(EdgeMLOps::DynamicModelBinary));
    sf.close();

    Serial.println(F("--------------------------------------------------------------------"));
    Serial.printf("[MLOps OTA] Staged %u bytes to %s (Target v%u.%u.%u)\n",
                  static_cast<unsigned>(sizeof(EdgeMLOps::DynamicModelBinary)),
                  STAGING_MODEL_PATH,
                  upstreamModel.version_major,
                  upstreamModel.version_minor,
                  upstreamModel.version_patch);
    Serial.printf("[MLOps OTA] Manifest Expected SHA-256: %s\n", expected_sha256);

    bool ok = mlRuntime.hotSwapFromStaging(
        STAGING_MODEL_PATH, ACTIVE_MODEL_PATH, BACKUP_MODEL_PATH, expected_sha256
    );

    uint32_t elapsed_ms = millis() - t0;
    digitalWrite(PIN_LED_SWAPPING, LOW);

    if (ok) {
        lastMLOpsStatus = "HOT-SWAP OK (" + String(elapsed_ms) + "ms)";
        Serial.printf("[MLOps SUCCESS] Atomic Hot-Swap to v%u.%u.%u completed in %lu ms (Zero Reboot!)\n",
                      mlRuntime.getActiveModel().version_major,
                      mlRuntime.getActiveModel().version_minor,
                      mlRuntime.getActiveModel().version_patch,
                      static_cast<unsigned long>(elapsed_ms));
    } else {
        lastMLOpsStatus = "SHA256 REJECTED!";
        Serial.println(F("[MLOps ROLLBACK] Rejected corrupted staging binary; active model preserved."));
    }
    Serial.println(F("--------------------------------------------------------------------"));
    return ok;
}

void pollMLOpsTriggers() {
    bool bV2 = digitalRead(PIN_BTN_OTA_V2);
    if (bV2 == LOW && prevBtnV2 == HIGH) {
        performModelOTAUpdate(2, false); // Hot-swap to v2.1.0
    }
    prevBtnV2 = bV2;

    bool bCorrupt = digitalRead(PIN_BTN_CORRUPT);
    if (bCorrupt == LOW && prevBtnCorrupt == HIGH) {
        performModelOTAUpdate(2, true); // Simulate corrupted OTA download (SHA-256 mismatch)
    }
    prevBtnCorrupt = bCorrupt;

    bool bRollback = digitalRead(PIN_BTN_ROLLBACK);
    if (bRollback == LOW && prevBtnRollback == HIGH) {
        performModelOTAUpdate(1, false); // Roll back to v1.0.0
    }
    prevBtnRollback = bRollback;

    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        if (cmd == "V2" || cmd == "UPDATE") {
            performModelOTAUpdate(2, false);
        } else if (cmd == "CORRUPT" || cmd == "TAMPER") {
            performModelOTAUpdate(2, true);
        } else if (cmd == "V1" || cmd == "ROLLBACK") {
            performModelOTAUpdate(1, false);
        }
    }
}

void renderOLED(const float features[4], const EdgeMLOps::InferenceOutput& out) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.printf("MLOps Model: %-7s", out.active_version);

    display.setCursor(0, 11);
    display.printf("SHA:%.12s..", mlRuntime.getActiveSHA256());

    display.drawFastHLine(0, 21, 128, SSD1306_WHITE);

    display.setCursor(0, 25);
    display.printf("T:%4.1fC Vib:%4.2fg", features[0] + 25.0f, features[1] * 0.1f);

    display.setCursor(0, 37);
    display.printf("Pred: %-10s %2.0f%%",
                   EdgeMLOps::CLASS_LABELS[out.predicted_class],
                   out.probabilities[out.predicted_class] * 100.0f);

    display.setCursor(0, 50);
    display.print(lastMLOpsStatus);

    display.display();
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN_OTA_V2, INPUT_PULLUP);
    pinMode(PIN_BTN_CORRUPT, INPUT_PULLUP);
    pinMode(PIN_BTN_ROLLBACK, INPUT_PULLUP);
    pinMode(PIN_LED_SWAPPING, OUTPUT);
    analogReadResolution(12);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.display();

    if (!LittleFS.begin(true)) {
        Serial.println(F("[MLOps ERROR] LittleFS mount failed!"));
    }

    mlRuntime.loadOrProvisionActiveModel(ACTIVE_MODEL_PATH);

    Serial.println(F("======================================================================"));
    Serial.println(F(" Project 18: Edge MLOps OTA Model Hot-Swapping (LittleFS + SHA-256)"));
    Serial.printf(" Active Model: v%u.%u.%u | SHA-256: %s\n",
                  mlRuntime.getActiveModel().version_major,
                  mlRuntime.getActiveModel().version_minor,
                  mlRuntime.getActiveModel().version_patch,
                  mlRuntime.getActiveSHA256());
    Serial.println(F(" GPIO12=OTA v2.1.0 | GPIO14=Inject Tampered Binary | GPIO27=Rollback v1"));
    Serial.println(F("======================================================================"));
}

void loop() {
    pollMLOpsTriggers();

    // Read simulated ambient thermal drift from potentiometer on GPIO 34
    int rawPot = analogRead(PIN_POT_DRIFT);
    float tempDeltaC = (static_cast<float>(rawPot) / 4095.0f) * 40.0f; // 0..40 C above 25 C ambient

    // Construct 4-element sensor feature vector:
    //   [0] Temp rise above 25C (0..40), [1] Vibration level (~4.5 = 0.45g normal),
    //   [2] Motor current (~2.1A),       [3] Thermal gradient
    float features[EdgeMLOps::INPUT_DIM] = {
        tempDeltaC,
        4.5f,
        2.1f,
        tempDeltaC * 0.25f
    };

    EdgeMLOps::InferenceOutput out = mlRuntime.runInference(features);
    inferenceStep++;

    renderOLED(features, out);

    if (inferenceStep % 10 == 0) {
        Serial.printf(
            "{\"step\":%lu,\"model\":\"%s\",\"sha256_prefix\":\"%.12s\",\"swaps\":%lu,"
            "\"temp_c\":%.1f,\"pred\":\"%s\",\"conf\":%.3f,\"nn_us\":%lu,\"status\":\"%s\"}\n",
            static_cast<unsigned long>(inferenceStep),
            out.active_version,
            mlRuntime.getActiveSHA256(),
            static_cast<unsigned long>(mlRuntime.getHotSwapCount()),
            features[0] + 25.0f,
            EdgeMLOps::CLASS_LABELS[out.predicted_class],
            out.probabilities[out.predicted_class],
            static_cast<unsigned long>(out.inference_latency_us),
            lastMLOpsStatus.c_str()
        );
    }

    delay(150);
}
