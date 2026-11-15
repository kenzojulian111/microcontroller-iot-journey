/**
 * @file ml_energy_disaggregation_nilm.ino
 * @brief Project 17: Non-Intrusive Load Monitoring (NILM) & Harmonic Appliance Disaggregator
 *
 * Hardware Architecture:
 *   - MCU: ESP32
 *   - Current Sensor: YHDC SCT-013-000 Non-Invasive Split-Core Current Transformer
 *     connected to GPIO 34 (ADC1_CH6) via 33 Ohm burden resistor & 1.65V mid-rail DC bias.
 *   - Interactive Load Combination Switches (Wokwi & Bench Simulation):
 *     - GPIO 12 (INPUT_PULLUP): Toggle REFRIGERATOR_COMPRESSOR (145 W, Inductive PF=0.68)
 *     - GPIO 14 (INPUT_PULLUP): Toggle RESISTIVE_IRON_HEATER   (1100 W, Resistive PF=0.99)
 *     - GPIO 27 (INPUT_PULLUP): Toggle INVERTER_AC_SWITCHING   (780 W, PWM Harmonics H3+H5)
 *     - GPIO 26 (INPUT_PULLUP): Toggle LAPTOP_SMPS_CHARGER     (65 W, Rectifier Pulse THD>80%)
 *   - SSD1306 I2C OLED (SDA=GPIO 21, SCL=GPIO 22): Displays Aggregate Power, THD_I,
 *     and per-appliance disaggregated wattages & states.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "nilm_model.h"

static constexpr int PIN_SCT013_ADC     = 34;
static constexpr int PIN_BTN_FRIDGE     = 12;
static constexpr int PIN_BTN_IRON       = 14;
static constexpr int PIN_BTN_INVERTER   = 27;
static constexpr int PIN_BTN_SMPS       = 26;
static constexpr int PIN_LED_OVERLOAD   = 2;
static constexpr int PIN_I2C_SDA        = 21;
static constexpr int PIN_I2C_SCL        = 22;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
EdgeNILM::NILMDisaggregator nilmEngine;

float   currentWaveform[EdgeNILM::WINDOW_SAMPLES];
uint8_t simulatedLoadMask = (1U << EdgeNILM::APP_REFRIGERATOR) | (1U << EdgeNILM::APP_LAPTOP_SMPS);
bool    lastBtnState[4]   = {HIGH, HIGH, HIGH, HIGH};
uint32_t windowCounter    = 0;
uint32_t lastAutoCycleMs  = 0;
bool     manualOverride   = false;

/**
 * @brief Polls the 4 interactive appliance toggle buttons and Serial console commands.
 */
void pollLoadControls() {
    const int pins[4] = {PIN_BTN_FRIDGE, PIN_BTN_IRON, PIN_BTN_INVERTER, PIN_BTN_SMPS};
    for (uint8_t i = 0; i < 4; ++i) {
        bool cur = digitalRead(pins[i]);
        if (cur == LOW && lastBtnState[i] == HIGH) {
            simulatedLoadMask ^= (1U << i);
            manualOverride = true;
            Serial.printf("[LOAD SWITCH] Toggled %s -> %s (Active Mask: 0x%02X)\n",
                          EdgeNILM::APPLIANCE_NAMES[i],
                          (simulatedLoadMask & (1U << i)) ? "ON" : "OFF",
                          simulatedLoadMask);
        }
        lastBtnState[i] = cur;
    }

    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0 && cmd[0] >= '0' && cmd[0] <= '3') {
            uint8_t idx = static_cast<uint8_t>(cmd[0] - '0');
            simulatedLoadMask ^= (1U << idx);
            manualOverride = true;
        }
    }

    // Automatically cycle through realistic multi-appliance combinations every 6 seconds
    // if the user hasn't manually toggled a button yet
    if (!manualOverride && (millis() - lastAutoCycleMs) > 6000) {
        lastAutoCycleMs = millis();
        static const uint8_t DEMO_MASKS[] = {
            0x01, // Fridge only (145 W)
            0x09, // Fridge + Laptop SMPS (210 W)
            0x05, // Fridge + Inverter AC (925 W)
            0x07, // Fridge + Iron + Inverter AC (2025 W)
            0x0A, // Iron + Laptop SMPS (1165 W)
            0x0F  // All 4 appliances simultaneously active (2090 W)
        };
        static size_t demoIdx = 0;
        demoIdx = (demoIdx + 1) % (sizeof(DEMO_MASKS) / sizeof(DEMO_MASKS[0]));
        simulatedLoadMask = DEMO_MASKS[demoIdx];
    }
}

/**
 * @brief Samples 200 points at 2,000 Hz (500 us sample interval) from GPIO 34
 *        or synthesizes the composite multi-appliance harmonic waveform in Wokwi.
 */
void acquireCurrentWindow(float* out_samples) {
    // Check if physical AC waveform is present on GPIO 34 by measuring peak-to-peak ADC swing
    int adc_min = 4095;
    int adc_max = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = analogRead(PIN_SCT013_ADC);
        if (raw < adc_min) adc_min = raw;
        if (raw > adc_max) adc_max = raw;
    }

    // If GPIO 34 has an AC signal riding on the 1.65V mid-rail bias (swing > 80 ADC counts)
    // and no potentiometer DC-only flatline, capture real-time 2000 Hz samples:
    if ((adc_max - adc_min) > 120) {
        const float mid_adc = 2048.0f;
        const float amps_per_lsb = (3.3f / 4095.0f) * 30.3f; // 100A:50mA CT with 33 Ohm burden
        uint32_t next_sample_us = micros();
        for (size_t i = 0; i < EdgeNILM::WINDOW_SAMPLES; ++i) {
            while (static_cast<int32_t>(micros() - next_sample_us) < 0) {
                // Tight 500 us hardware sample timing loop
            }
            next_sample_us += 500;
            int raw = analogRead(PIN_SCT013_ADC);
            out_samples[i] = (static_cast<float>(raw) - mid_adc) * amps_per_lsb;
        }
        return;
    }

    // In Wokwi simulation, use the composite harmonic waveform generator
    EdgeNILM::NILMDisaggregator::synthesizeCompositeWaveform(simulatedLoadMask, out_samples, 1.0f);
}

void updateOLED(const EdgeNILM::NILMDisaggregationResult& res) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Header: Aggregate Power, Power Factor & Total Harmonic Distortion
    display.setCursor(0, 0);
    display.printf("P:%4.0fW PF:%.2f THD:%2.0f%%",
                   res.features.active_power_w,
                   res.features.displacement_pf,
                   fminf(99.0f, res.features.thd_percent));

    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    // 4 Appliance Disaggregation Rows
    for (uint8_t a = 0; a < EdgeNILM::NUM_APPLIANCES; ++a) {
        int y = 14 + a * 12;
        display.setCursor(0, y);
        display.printf("%s:%s %4.0fW(%2.0f%%)",
                       EdgeNILM::APPLIANCE_SHORT_NAMES[a],
                       res.active_states[a] ? "ON " : "OFF",
                       res.estimated_watts[a],
                       fminf(99.0f, res.probabilities[a] * 100.0f));
    }

    display.display();
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN_FRIDGE, INPUT_PULLUP);
    pinMode(PIN_BTN_IRON, INPUT_PULLUP);
    pinMode(PIN_BTN_INVERTER, INPUT_PULLUP);
    pinMode(PIN_BTN_SMPS, INPUT_PULLUP);
    pinMode(PIN_LED_OVERLOAD, OUTPUT);
    analogReadResolution(12);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.display();

    nilmEngine.begin();

    Serial.println(F("======================================================================"));
    Serial.println(F(" Project 17: ML Energy Disaggregation (NILM Goertzel 50/150/250/350Hz)"));
    Serial.println(F(" Toggle Loads via Buttons GPIO 12 / 14 / 27 / 26 or Serial '0'..'3'"));
    Serial.println(F("======================================================================"));
}

void loop() {
    pollLoadControls();
    acquireCurrentWindow(currentWaveform);

    EdgeNILM::NILMDisaggregationResult res = nilmEngine.disaggregate(currentWaveform);
    windowCounter++;

    digitalWrite(PIN_LED_OVERLOAD, (res.features.active_power_w > 1500.0f) ? HIGH : LOW);
    updateOLED(res);

    if (windowCounter % 5 == 0) {
        Serial.printf(
            "{\"win\":%lu,\"p_total_w\":%.1f,\"i_rms_a\":%.2f,\"pf\":%.2f,\"thd_pct\":%.1f,"
            "\"h1_a\":%.2f,\"h3_a\":%.2f,\"h5_a\":%.2f,\"h7_a\":%.2f,"
            "\"loads\":{\"fridge\":%s,\"iron\":%s,\"inverter_ac\":%s,\"smps\":%s},"
            "\"dsp_us\":%lu,\"nn_us\":%lu}\n",
            static_cast<unsigned long>(windowCounter),
            res.features.active_power_w,
            res.features.i_rms_amps,
            res.features.displacement_pf,
            res.features.thd_percent,
            res.features.h1_50hz_amps,
            res.features.h3_150hz_amps,
            res.features.h5_250hz_amps,
            res.features.h7_350hz_amps,
            res.active_states[0] ? "true" : "false",
            res.active_states[1] ? "true" : "false",
            res.active_states[2] ? "true" : "false",
            res.active_states[3] ? "true" : "false",
            static_cast<unsigned long>(res.dsp_latency_us),
            static_cast<unsigned long>(res.nn_latency_us)
        );
    }

    delay(150);
}
