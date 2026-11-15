/**
 * @file edge_offline_voice_command.ino
 * @brief Project 15: Edge Offline Voice Command Node (Audio DSP + MFCC + INT8 1D-CNN KWS)
 *
 * Hardware Architecture:
 *   - MCU: ESP32 / ESP32-S3
 *   - Microphone: INMP441 Omnidirectional I2S MEMS Microphone (16 kHz, 16-bit DMA)
 *     - I2S SCK (BCLK): GPIO 26
 *     - I2S WS  (LRCLK): GPIO 25
 *     - I2S SD  (DOUT):  GPIO 33
 *   - Simulation Utterance Trigger Buttons (for Wokwi / Bench Testing):
 *     - GPIO 12 (INPUT_PULLUP): Injects "NYALAKAN" (Turn ON) utterance
 *     - GPIO 14 (INPUT_PULLUP): Injects "MATIKAN"  (Turn OFF) utterance
 *     - GPIO 27 (INPUT_PULLUP): Injects "STATUS"   (Telemetry Report) utterance
 *   - Actuators & Display:
 *     - Relay Module: GPIO 4
 *     - Status LED:   GPIO 2
 *     - SSD1306 OLED: I2C (SDA=GPIO 21, SCL=GPIO 22)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

#include "kws_mfcc_model.h"

// Pin Assignments
static constexpr int PIN_I2S_SCK       = 26;
static constexpr int PIN_I2S_WS        = 25;
static constexpr int PIN_I2S_SD        = 33;

static constexpr int PIN_BTN_NYALAKAN  = 12;
static constexpr int PIN_BTN_MATIKAN   = 14;
static constexpr int PIN_BTN_STATUS    = 27;

static constexpr int PIN_RELAY_LOAD    = 4;
static constexpr int PIN_STATUS_LED    = 2;
static constexpr int PIN_I2C_SDA       = 21;
static constexpr int PIN_I2C_SCL       = 22;

static constexpr i2s_port_t I2S_PORT   = I2S_NUM_0;
static constexpr float CONFIDENCE_THRESHOLD = 0.72f;
static constexpr uint32_t LOCKOUT_MS        = 1200;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
EdgeKWS::MFCCKeywordSpotter kwsEngine;

int16_t pcmFrame[EdgeKWS::FFT_SIZE];
bool    i2sHardwareActive = false;
bool    relayState        = false;
uint32_t frameCounter     = 0;
uint32_t lastCommandMs    = 0;
uint32_t totalCommands    = 0;

EdgeKWS::KeywordClass simulatedUtterance = EdgeKWS::KW_SILENCE;
uint8_t               simulatedFramesRemaining = 0;

/**
 * @brief Initializes ESP32 I2S Peripheral in Master RX mode at 16 kHz for INMP441.
 */
bool initI2SMicrophone() {
    i2s_config_t i2s_config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = EdgeKWS::SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_SCK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD
    };

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr) != ESP_OK) {
        return false;
    }
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        return false;
    }
    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

/**
 * @brief Checks pushbuttons or Serial commands to inject synthetic voice utterances
 *        when testing in Wokwi or without an acoustic source.
 */
void pollSimulationTriggers() {
    if (digitalRead(PIN_BTN_NYALAKAN) == LOW && simulatedFramesRemaining == 0) {
        simulatedUtterance = EdgeKWS::KW_NYALAKAN;
        simulatedFramesRemaining = 18;
    } else if (digitalRead(PIN_BTN_MATIKAN) == LOW && simulatedFramesRemaining == 0) {
        simulatedUtterance = EdgeKWS::KW_MATIKAN;
        simulatedFramesRemaining = 18;
    } else if (digitalRead(PIN_BTN_STATUS) == LOW && simulatedFramesRemaining == 0) {
        simulatedUtterance = EdgeKWS::KW_STATUS;
        simulatedFramesRemaining = 18;
    }

    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        if (cmd == "NYALAKAN" || cmd == "1") {
            simulatedUtterance = EdgeKWS::KW_NYALAKAN;
            simulatedFramesRemaining = 18;
        } else if (cmd == "MATIKAN" || cmd == "0") {
            simulatedUtterance = EdgeKWS::KW_MATIKAN;
            simulatedFramesRemaining = 18;
        } else if (cmd == "STATUS" || cmd == "S") {
            simulatedUtterance = EdgeKWS::KW_STATUS;
            simulatedFramesRemaining = 18;
        }
    }
}

/**
 * @brief Captures a 256-sample PCM frame from I2S DMA or falls back to the
 *        synthetic formant generator if the I2S data line is silent/unconnected.
 */
void captureAudioFrame(int16_t* buffer) {
    if (simulatedFramesRemaining > 0) {
        EdgeKWS::MFCCKeywordSpotter::synthesizeUtteranceFrame(
            simulatedUtterance, frameCounter, buffer
        );
        simulatedFramesRemaining--;
        if (simulatedFramesRemaining == 0) {
            simulatedUtterance = EdgeKWS::KW_SILENCE;
        }
        return;
    }

    if (i2sHardwareActive) {
        size_t bytesRead = 0;
        esp_err_t err = i2s_read(
            I2S_PORT, buffer, EdgeKWS::FFT_SIZE * sizeof(int16_t),
            &bytesRead, pdMS_TO_TICKS(15)
        );
        if (err == ESP_OK && bytesRead == EdgeKWS::FFT_SIZE * sizeof(int16_t)) {
            // Check if hardware I2S stream has non-zero variance (connected INMP441)
            int32_t energy = 0;
            for (size_t i = 0; i < EdgeKWS::FFT_SIZE; ++i) {
                energy += abs(buffer[i]);
            }
            if (energy > 64) {
                return;
            }
        }
    }

    // Default background floor when idle in simulation
    EdgeKWS::MFCCKeywordSpotter::synthesizeUtteranceFrame(
        EdgeKWS::KW_SILENCE, frameCounter, buffer
    );
}

void updateOLED(const EdgeKWS::KWSInferenceResult& res) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print(F("EDGE KWS MFCC+1D-CNN"));

    display.setCursor(0, 12);
    display.printf("Word: %-9s %3.0f%%",
                   EdgeKWS::KEYWORD_LABELS[res.smoothed_class],
                   res.smoothed_probabilities[res.smoothed_class] * 100.0f);

    display.setCursor(0, 24);
    display.printf("RMS:%5.1fdB Fc:%4.0fHz", res.frame_rms_db, res.spectral_centroid_hz);

    display.setCursor(0, 36);
    display.printf("DSP:%4luus NN:%4luus",
                   static_cast<unsigned long>(res.dsp_latency_us),
                   static_cast<unsigned long>(res.nn_latency_us));

    display.setCursor(0, 48);
    display.printf("Relay: %-3s Cmds: %lu",
                   relayState ? "ON" : "OFF",
                   static_cast<unsigned long>(totalCommands));

    // Draw real-time 13-band MFCC bar graph on bottom row
    const auto& spec = kwsEngine.getSpectrogram();
    for (size_t c = 0; c < EdgeKWS::NUM_MFCC_COEFFS; ++c) {
        int val = abs(spec[EdgeKWS::NUM_TIME_SLICES - 1][c]) / 16;
        if (val > 7) val = 7;
        display.fillRect(static_cast<int>(c * 9), 63 - val, 6, val + 1, SSD1306_WHITE);
    }

    display.display();
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN_NYALAKAN, INPUT_PULLUP);
    pinMode(PIN_BTN_MATIKAN, INPUT_PULLUP);
    pinMode(PIN_BTN_STATUS, INPUT_PULLUP);
    pinMode(PIN_RELAY_LOAD, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);

    digitalWrite(PIN_RELAY_LOAD, LOW);
    digitalWrite(PIN_STATUS_LED, LOW);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println(F("Initializing KWS..."));
        display.display();
    }

    kwsEngine.begin();
    i2sHardwareActive = initI2SMicrophone();

    Serial.println(F("=============================================================="));
    Serial.println(F(" Project 15: Edge Offline Voice Command (MFCC + INT8 1D-CNN)"));
    Serial.println(F(" Press Buttons (GPIO 12/14/27) or send NYALAKAN/MATIKAN/STATUS"));
    Serial.println(F("=============================================================="));
}

void loop() {
    pollSimulationTriggers();
    captureAudioFrame(pcmFrame);

    uint32_t t_dsp_start = micros();
    float rms_db = 0.0f;
    float centroid_hz = 0.0f;
    kwsEngine.pushAudioFrame(pcmFrame, rms_db, centroid_hz);
    uint32_t dsp_latency_us = micros() - t_dsp_start;

    EdgeKWS::KWSInferenceResult res = kwsEngine.runInference(
        rms_db, centroid_hz, dsp_latency_us
    );
    frameCounter++;

    uint32_t now = millis();
    float top_conf = res.smoothed_probabilities[res.smoothed_class];

    if (res.smoothed_class >= EdgeKWS::KW_NYALAKAN &&
        top_conf >= CONFIDENCE_THRESHOLD &&
        (now - lastCommandMs) > LOCKOUT_MS) {

        lastCommandMs = now;
        totalCommands++;

        if (res.smoothed_class == EdgeKWS::KW_NYALAKAN) {
            relayState = true;
            digitalWrite(PIN_RELAY_LOAD, HIGH);
            digitalWrite(PIN_STATUS_LED, HIGH);
            Serial.printf("[KWS ACTION] Voice Command 'NYALAKAN' (%.1f%%) -> Relay ON\n",
                          top_conf * 100.0f);
        } else if (res.smoothed_class == EdgeKWS::KW_MATIKAN) {
            relayState = false;
            digitalWrite(PIN_RELAY_LOAD, LOW);
            digitalWrite(PIN_STATUS_LED, LOW);
            Serial.printf("[KWS ACTION] Voice Command 'MATIKAN' (%.1f%%) -> Relay OFF\n",
                          top_conf * 100.0f);
        } else if (res.smoothed_class == EdgeKWS::KW_STATUS) {
            Serial.printf(
                "[KWS STATUS] Relay=%s | Commands=%lu | FreeHeap=%luB | DSP=%luus | NN=%luus\n",
                relayState ? "ON" : "OFF",
                static_cast<unsigned long>(totalCommands),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(res.dsp_latency_us),
                static_cast<unsigned long>(res.nn_latency_us)
            );
        }
    }

    if (frameCounter % 5 == 0) {
        updateOLED(res);
    }

    if (frameCounter % 25 == 0) {
        Serial.printf(
            "{\"frame\":%lu,\"kw\":\"%s\",\"conf\":%.3f,\"rms_db\":%.1f,"
            "\"centroid_hz\":%.0f,\"dsp_us\":%lu,\"nn_us\":%lu,\"relay\":%s}\n",
            static_cast<unsigned long>(frameCounter),
            EdgeKWS::KEYWORD_LABELS[res.smoothed_class],
            top_conf,
            res.frame_rms_db,
            res.spectral_centroid_hz,
            static_cast<unsigned long>(res.dsp_latency_us),
            static_cast<unsigned long>(res.nn_latency_us),
            relayState ? "true" : "false"
        );
    }

    delay(10);
}
