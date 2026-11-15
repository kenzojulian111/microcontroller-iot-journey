/**
 * @file ml_local_weather_predictor.ino
 * @brief Project 11: Edge TinyML Microbarometric Weather Predictor (Random Forest on MCU)
 *
 * Target Hardware: ESP32 / ESP8266
 * Sensors:
 *   - Bosch BME280 (I2C 0x76/0x77) for Temperature (°C), Relative Humidity (%), and Pressure (hPa)
 *   - Fallback Wokwi Simulation Mode: DHT22 (GPIO 4) + Analog Barometric Potentiometer (GPIO 34)
 * Display:
 *   - SSD1306 128x64 I2C OLED (0x3C)
 *
 * Feature Engineering Pipeline:
 *   1. Magnus-Tetens Dew Point Approximation (T_dew) & Dew Point Spread (T - T_dew)
 *   2. Hypsometric Sea-Level Pressure Reduction (P_0)
 *   3. Circular Ring Buffer (N = 12 slots) for Ordinary Least Squares (OLS) linear regression
 *      to extract smoothed Barometric Tendency (dP/dt in hPa/hr) and Humidity Trend (dRH/dt in %/hr)
 *   4. Zero-dependency 5-tree Random Forest Classifier (`weather_rf_model.h`)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "weather_rf_model.h"

// ============================================================================
// Hardware Pin Definitions & Constants
// ============================================================================
#define I2C_SDA_PIN          21
#define I2C_SCL_PIN          22
#define DHT_PIN              4
#define DHT_TYPE             DHT22
#define BARO_SIM_ADC_PIN     34   // Potentiometer simulating barometric pressure (975..1035 hPa)
#define STORM_INJECT_BTN_PIN 15   // Pushbutton to inject synthetic rapid cold-front pressure drop
#define STATUS_LED_PIN       2    // Onboard LED for storm alert

#define OLED_WIDTH           128
#define OLED_HEIGHT          64
#define OLED_RESET           -1
#define OLED_I2C_ADDR        0x3C

// Station altitude in meters above Mean Sea Level (adjust for local deployment)
#define STATION_ALTITUDE_M   120.0f

// Circular history buffer configuration
// In production: 1 sample every 5 minutes -> 12 samples = 1 hour sliding window.
// In interactive lab / Wokwi mode: 1 sample every 2 seconds with time-scaling factor
// so each sample represents a 5-minute synoptic interval (1/12th of an hour).
#define HISTORY_BUFFER_SIZE  12
#define SAMPLE_PERIOD_MS     2000UL
#define SYNOPTIC_STEP_HOURS  (5.0f / 60.0f) // Each ring-buffer step corresponds to 5 minutes (0.08333 hr)

// ============================================================================
// Global Peripheral Instances & State
// ============================================================================
Adafruit_BME280 bme;
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

bool hasBme280 = false;
bool hasOled   = false;

struct MeteoSample {
    float temperatureC;
    float humidityPct;
    float pressureHpa;
};

MeteoSample historyBuffer[HISTORY_BUFFER_SIZE];
uint8_t bufferHead  = 0;
uint8_t bufferCount = 0;
uint32_t lastSampleMs = 0;
uint32_t sampleIndex  = 0;

// Synthetic front injection state (for testing without waiting hours)
bool syntheticStormActive = false;
float syntheticPressureDropOffset = 0.0f;

// ============================================================================
// Mathematical & Meteorological Helper Functions
// ============================================================================

/**
 * @brief Compute Dew Point (°C) using the Magnus-Tetens formula
 *        gamma(T, RH) = (a * T / (b + T)) + ln(RH / 100.0)
 *        T_dew = (b * gamma) / (a - gamma)
 *        where a = 17.625, b = 243.04 °C
 */
float computeDewPointC(float tempC, float rhPct) {
    const float a = 17.625f;
    const float b = 243.04f;
    float clampedRh = constrain(rhPct, 1.0f, 100.0f);
    float gamma = ((a * tempC) / (b + tempC)) + logf(clampedRh / 100.0f);
    return (b * gamma) / (a - gamma);
}

/**
 * @brief Reduce station pressure (P_station) to Mean Sea Level Pressure (P_0)
 *        using the international barometric formula:
 *        P_0 = P_station * (1 - (0.0065 * h) / (T + 0.0065 * h + 273.15)) ^ (-5.257)
 */
float computeSeaLevelPressure(float stationPressureHpa, float tempC, float altitudeM) {
    float denominator = tempC + (0.0065f * altitudeM) + 273.15f;
    float base = 1.0f - ((0.0065f * altitudeM) / denominator);
    return stationPressureHpa * powf(base, -5.257f);
}

/**
 * @brief Push a new meteorological observation into the circular ring buffer
 */
void pushSample(float tempC, float rhPct, float seaLevelPressureHpa) {
    historyBuffer[bufferHead].temperatureC = tempC;
    historyBuffer[bufferHead].humidityPct  = rhPct;
    historyBuffer[bufferHead].pressureHpa  = seaLevelPressureHpa;

    bufferHead = (bufferHead + 1) % HISTORY_BUFFER_SIZE;
    if (bufferCount < HISTORY_BUFFER_SIZE) {
        bufferCount++;
    }
}

/**
 * @brief Compute Ordinary Least Squares (OLS) linear regression slope over the ring buffer
 *        Returns temporal derivative in units per hour (hPa/hr and %/hr).
 */
void computeTemporalDerivatives(float *dP_dt, float *dRH_dt) {
    if (bufferCount < 2) {
        *dP_dt  = 0.0f;
        *dRH_dt = 0.0f;
        return;
    }

    // Oldest sample in the ring buffer
    uint8_t oldestIdx = (bufferHead + HISTORY_BUFFER_SIZE - bufferCount) % HISTORY_BUFFER_SIZE;

    float sumT  = 0.0f;
    float sumP  = 0.0f;
    float sumRH = 0.0f;
    float sumTT = 0.0f;
    float sumTP = 0.0f;
    float sumTRH = 0.0f;

    for (uint8_t i = 0; i < bufferCount; i++) {
        uint8_t idx = (oldestIdx + i) % HISTORY_BUFFER_SIZE;
        float tHours = i * SYNOPTIC_STEP_HOURS;
        float p  = historyBuffer[idx].pressureHpa;
        float rh = historyBuffer[idx].humidityPct;

        sumT   += tHours;
        sumP   += p;
        sumRH  += rh;
        sumTT  += tHours * tHours;
        sumTP  += tHours * p;
        sumTRH += tHours * rh;
    }

    float n = static_cast<float>(bufferCount);
    float denom = (n * sumTT) - (sumT * sumT);

    if (fabsf(denom) < 1e-6f) {
        *dP_dt  = 0.0f;
        *dRH_dt = 0.0f;
        return;
    }

    *dP_dt  = ((n * sumTP)  - (sumT * sumP))  / denom;
    *dRH_dt = ((n * sumTRH) - (sumT * sumRH)) / denom;
}

/**
 * @brief Read physical BME280 or fallback to Wokwi DHT22 + Analog Barometer simulator
 */
void acquireSensorReadings(float *tempC, float *rhPct, float *seaLevelPressureHpa) {
    if (digitalRead(STORM_INJECT_BTN_PIN) == LOW) {
        syntheticStormActive = true;
    }

    if (syntheticStormActive) {
        syntheticPressureDropOffset -= 0.45f; // Rapidly drop pressure each step (~ -5.4 hPa/hr)
        if (syntheticPressureDropOffset < -14.0f) {
            syntheticStormActive = false;
            syntheticPressureDropOffset = 0.0f;
        }
    }

    if (hasBme280) {
        float rawT = bme.readTemperature();
        float rawRH = bme.readHumidity();
        float rawStationP = bme.readPressure() / 100.0f; // Pa -> hPa

        *tempC = rawT;
        *rhPct = constrain(rawRH, 0.0f, 100.0f);
        *seaLevelPressureHpa = computeSeaLevelPressure(rawStationP, rawT, STATION_ALTITUDE_M) + syntheticPressureDropOffset;
    } else {
        // Fallback Wokwi simulation mode using DHT22 + Potentiometer on GPIO 34
        float dhtT = dht.readTemperature();
        float dhtRH = dht.readHumidity();

        if (isnan(dhtT) || isnan(dhtRH)) {
            dhtT  = 23.5f;
            dhtRH = 62.0f;
        }

        int adcRaw = analogRead(BARO_SIM_ADC_PIN);
        // Map 12-bit ADC (0..4095) to 980.0 .. 1030.0 hPa
        float simStationP = 980.0f + (static_cast<float>(adcRaw) / 4095.0f) * 50.0f;

        *tempC = dhtT;
        *rhPct = constrain(dhtRH, 0.0f, 100.0f);
        *seaLevelPressureHpa = simStationP + syntheticPressureDropOffset;
    }
}

/**
 * @brief Render real-time telemetry and Random Forest inference results on SSD1306 OLED
 */
void updateOledDisplay(const float features[WEATHER_NUM_FEATURES], uint8_t predClass, float confidence, const uint8_t votes[WEATHER_NUM_CLASSES]) {
    if (!hasOled) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Header Banner
    display.setCursor(0, 0);
    display.print(F("TINYML BARO FORECAST"));
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // Row 1: Temperature, Humidity, Pressure
    display.setCursor(0, 12);
    display.printf("T:%.1fC RH:%.0f%%", features[0], features[1]);

    display.setCursor(0, 22);
    display.printf("P0:%.1fhPa dP:%+.1f", features[2], features[3]);

    // Row 2: Dew Point Spread & Tree Votes
    display.setCursor(0, 32);
    display.printf("Spread:%.1fC V:%u%u%u%u", features[4], votes[0], votes[1], votes[2], votes[3]);

    // Prediction Box
    display.drawRect(0, 43, 128, 21, SSD1306_WHITE);
    display.setCursor(4, 46);
    display.print(WEATHER_SHORT_NAMES[predClass]);
    display.setCursor(4, 55);
    display.printf("Conf: %.0f%% (RF-5)", confidence * 100.0f);

    display.display();
}

/**
 * @brief Handle Serial commands (`STORM`, `RESET`, `STATUS`) for interactive testing
 */
void handleSerialCommands() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "STORM") {
        syntheticStormActive = true;
        syntheticPressureDropOffset = 0.0f;
        Serial.println(F("[CMD] Synthetic rapid cold-front pressure drop activated!"));
    } else if (cmd == "RESET") {
        syntheticStormActive = false;
        syntheticPressureDropOffset = 0.0f;
        bufferCount = 0;
        bufferHead  = 0;
        Serial.println(F("[CMD] History ring buffer and synthetic offset reset."));
    }
}

// ============================================================================
// Arduino Setup & Main Loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(STORM_INJECT_BTN_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Initialize SSD1306 OLED
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        hasOled = true;
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(8, 20);
        display.println(F("TinyML Weather RF"));
        display.setCursor(8, 34);
        display.println(F("Initializing..."));
        display.display();
    } else {
        Serial.println(F("[WARN] SSD1306 OLED not detected on I2C 0x3C."));
    }

    // Initialize Bosch BME280 (check 0x76 then 0x77)
    if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
        hasBme280 = true;
        bme.setSampling(
            Adafruit_BME280::MODE_NORMAL,
            Adafruit_BME280::SAMPLING_X2,  // Temperature oversampling x2
            Adafruit_BME280::SAMPLING_X16, // Pressure oversampling x16 (low noise for dP/dt)
            Adafruit_BME280::SAMPLING_X4,  // Humidity oversampling x4
            Adafruit_BME280::FILTER_X16,   // IIR filter coefficient 16 against wind gusts
            Adafruit_BME280::STANDBY_MS_500
        );
        Serial.println(F("[INIT] Bosch BME280 initialized with IIR X16 barometric filtering."));
    } else {
        dht.begin();
        analogReadResolution(12);
        Serial.println(F("[INIT] BME280 not found -> Using Wokwi Fallback (DHT22 + GPIO34 Baro Potentiometer)."));
    }

    Serial.println(F("================================================================================"));
    Serial.println(F(" Project 11: Edge TinyML Local Weather Predictor (5-Tree Random Forest Ensemble)"));
    Serial.println(F(" Commands: Send 'STORM' to simulate a rapid cold front, or 'RESET' to clear."));
    Serial.println(F("================================================================================"));
}

void loop() {
    handleSerialCommands();

    uint32_t now = millis();
    if (now - lastSampleMs < SAMPLE_PERIOD_MS) {
        return;
    }
    lastSampleMs = now;
    sampleIndex++;

    // 1. Acquire raw measurements
    float tempC = 0.0f, rhPct = 0.0f, seaLevelPressureHpa = 0.0f;
    acquireSensorReadings(&tempC, &rhPct, &seaLevelPressureHpa);

    // 2. Push to circular ring buffer & extract OLS temporal derivatives
    pushSample(tempC, rhPct, seaLevelPressureHpa);

    float dP_dt = 0.0f, dRH_dt = 0.0f;
    computeTemporalDerivatives(&dP_dt, &dRH_dt);

    // 3. Compute thermodynamic Dew Point Spread (T - T_dew)
    float dewPointC = computeDewPointC(tempC, rhPct);
    float dewPointSpread = tempC - dewPointC;

    // 4. Assemble 6-element feature vector for Random Forest inference
    float features[WEATHER_NUM_FEATURES] = {
        tempC,
        rhPct,
        seaLevelPressureHpa,
        dP_dt,
        dewPointSpread,
        dRH_dt
    };

    // 5. Measure inference latency and classify weather regime
    uint8_t treeVotes[WEATHER_NUM_CLASSES] = {0};
    float confidence = 0.0f;

    uint32_t tStartUs = micros();
    uint8_t predictedClass = Eloquent::ML::Port::WeatherRandomForest::predict(features, &confidence, treeVotes);
    uint32_t inferenceUs = micros() - tStartUs;

    // 6. Visual/LED storm alert when class >= RAIN_IMMINENT
    if (predictedClass == WEATHER_STORM_FRONT_RAPID_DROP) {
        digitalWrite(STATUS_LED_PIN, HIGH);
    } else if (predictedClass == WEATHER_RAIN_IMMINENT) {
        digitalWrite(STATUS_LED_PIN, (sampleIndex % 2 == 0) ? HIGH : LOW);
    } else {
        digitalWrite(STATUS_LED_PIN, LOW);
    }

    // 7. Update OLED & emit structured telemetry log
    updateOledDisplay(features, predictedClass, confidence, treeVotes);

    Serial.printf(
        "[#%04lu] T=%5.2fC | RH=%5.1f%% | P0=%7.2fhPa | dP/dt=%+6.2fhPa/h | Spread=%4.2fC | dRH/dt=%+5.1f%%/h | "
        "Pred=%u (%s) | Conf=%.0f%% [%u,%u,%u,%u] | %luus\n",
        sampleIndex,
        features[0],
        features[1],
        features[2],
        features[3],
        features[4],
        features[5],
        predictedClass,
        WEATHER_CLASS_NAMES[predictedClass],
        confidence * 100.0f,
        treeVotes[0], treeVotes[1], treeVotes[2], treeVotes[3],
        inferenceUs
    );
}
