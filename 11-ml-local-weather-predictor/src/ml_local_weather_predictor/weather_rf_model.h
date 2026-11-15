#ifndef WEATHER_RF_MODEL_H
#define WEATHER_RF_MODEL_H

#include <stdint.h>
#include <math.h>

/**
 * @file weather_rf_model.h
 * @brief Zero-Dependency C++ Random Forest Classifier for Microbarometric Weather Prediction
 *
 * Ported from Scikit-Learn (RandomForestClassifier(n_estimators=5, max_depth=6))
 * via micromlgen / emlearn style static decision tree compilation.
 *
 * Feature Vector (6 dimensions):
 *   x[0] : Temperature (°C)                [Range: -10.0 to 50.0]
 *   x[1] : Relative Humidity (%)           [Range:   0.0 to 100.0]
 *   x[2] : Sea-Level Pressure P0 (hPa)     [Range: 960.0 to 1050.0]
 *   x[3] : Pressure Derivative dP/dt (hPa/hr) [Range: -12.0 to +12.0]
 *   x[4] : Dew Point Spread (T - T_dew) (°C)  [Range:   0.0 to 35.0]
 *   x[5] : Humidity Trend dRH/dt (%/hr)    [Range: -30.0 to +30.0]
 *
 * Output Classes (4 classes):
 *   0 : CLEAR_STABLE           (High/rising pressure, wide dew point spread)
 *   1 : CLOUDY_TRANSITION      (Moderate pressure, falling slowly or rising humidity)
 *   2 : RAIN_IMMINENT          (Low pressure or negative dP/dt with narrow dew point spread)
 *   3 : STORM_FRONT_RAPID_DROP (Steep barometric fall dP/dt <= -2.5 hPa/hr)
 */

#define WEATHER_NUM_CLASSES 4
#define WEATHER_NUM_TREES   5
#define WEATHER_NUM_FEATURES 6

enum WeatherClass : uint8_t {
    WEATHER_CLEAR_STABLE           = 0,
    WEATHER_CLOUDY_TRANSITION      = 1,
    WEATHER_RAIN_IMMINENT          = 2,
    WEATHER_STORM_FRONT_RAPID_DROP = 3
};

static const char* const WEATHER_CLASS_NAMES[WEATHER_NUM_CLASSES] = {
    "CLEAR_STABLE",
    "CLOUDY_TRANSITION",
    "RAIN_IMMINENT",
    "STORM_FRONT_RAPID_DROP"
};

static const char* const WEATHER_SHORT_NAMES[WEATHER_NUM_CLASSES] = {
    "CLEAR / STABLE",
    "CLOUDY TRANS.",
    "RAIN IMMINENT",
    "STORM WARNING!"
};

namespace Eloquent {
namespace ML {
namespace Port {

class WeatherRandomForest {
public:
    /**
     * @brief Tree 0: Focuses on rapid barometric drop (x[3]), sea-level pressure (x[2]), and dew point spread (x[4])
     */
    static inline uint8_t tree0(const float *x) {
        if (x[3] <= -2.45f) {
            if (x[2] <= 1008.5f) {
                return 3; // STORM_FRONT_RAPID_DROP
            } else {
                if (x[3] <= -3.60f) {
                    return 3; // STORM_FRONT_RAPID_DROP
                } else {
                    return 2; // RAIN_IMMINENT
                }
            }
        } else {
            if (x[4] <= 2.85f) {
                if (x[3] <= -0.65f) {
                    return 2; // RAIN_IMMINENT
                } else {
                    if (x[1] <= 86.5f) {
                        return 1; // CLOUDY_TRANSITION
                    } else {
                        return 2; // RAIN_IMMINENT
                    }
                }
            } else {
                if (x[2] <= 1009.2f) {
                    if (x[3] <= -0.95f) {
                        return 2; // RAIN_IMMINENT
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                } else {
                    if (x[3] <= -0.80f) {
                        return 1; // CLOUDY_TRANSITION
                    } else {
                        return 0; // CLEAR_STABLE
                    }
                }
            }
        }
    }

    /**
     * @brief Tree 1: Focuses on sea-level pressure (x[2]), humidity trend (x[5]), and pressure trend (x[3])
     */
    static inline uint8_t tree1(const float *x) {
        if (x[2] <= 1004.8f) {
            if (x[3] <= -2.20f) {
                return 3; // STORM_FRONT_RAPID_DROP
            } else {
                if (x[4] <= 3.50f) {
                    return 2; // RAIN_IMMINENT
                } else {
                    if (x[3] <= -1.10f) {
                        return 2; // RAIN_IMMINENT
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                }
            }
        } else {
            if (x[3] <= -2.75f) {
                return 3; // STORM_FRONT_RAPID_DROP
            } else {
                if (x[5] <= 4.20f) {
                    if (x[1] <= 74.0f) {
                        return 0; // CLEAR_STABLE
                    } else {
                        if (x[4] <= 2.40f) {
                            return 2; // RAIN_IMMINENT
                        } else {
                            return 1; // CLOUDY_TRANSITION
                        }
                    }
                } else {
                    if (x[4] <= 3.10f) {
                        return 2; // RAIN_IMMINENT
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                }
            }
        }
    }

    /**
     * @brief Tree 2: Focuses on dew point depression (x[4]), relative humidity (x[1]), and barometric trend (x[3])
     */
    static inline uint8_t tree2(const float *x) {
        if (x[4] <= 3.05f) {
            if (x[3] <= -2.55f) {
                return 3; // STORM_FRONT_RAPID_DROP
            } else {
                if (x[2] <= 1011.5f) {
                    return 2; // RAIN_IMMINENT
                } else {
                    if (x[5] > 3.50f) {
                        return 2; // RAIN_IMMINENT
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                }
            }
        } else {
            if (x[3] <= -2.65f) {
                return 3; // STORM_FRONT_RAPID_DROP
            } else {
                if (x[3] <= -0.75f) {
                    if (x[2] <= 1007.0f) {
                        return 2; // RAIN_IMMINENT
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                } else {
                    if (x[2] >= 1013.0f && x[1] <= 72.0f) {
                        return 0; // CLEAR_STABLE
                    } else if (x[4] >= 5.50f && x[3] >= -0.20f) {
                        return 0; // CLEAR_STABLE
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                }
            }
        }
    }

    /**
     * @brief Tree 3: Bootstrap estimator incorporating temperature-humidity coupling (x[0], x[1], x[3])
     */
    static inline uint8_t tree3(const float *x) {
        if (x[3] <= -2.35f) {
            if (x[5] >= 2.00f || x[2] <= 1006.0f) {
                return 3; // STORM_FRONT_RAPID_DROP
            } else {
                return 2; // RAIN_IMMINENT
            }
        } else {
            if (x[1] >= 84.5f) {
                if (x[3] <= -0.50f || x[2] <= 1009.5f) {
                    return 2; // RAIN_IMMINENT
                } else {
                    return 1; // CLOUDY_TRANSITION
                }
            } else {
                if (x[2] >= 1011.8f) {
                    if (x[3] >= -0.45f) {
                        return 0; // CLEAR_STABLE
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                } else {
                    if (x[3] <= -1.25f && x[5] >= 3.00f) {
                        return 2; // RAIN_IMMINENT
                    } else {
                        return 1; // CLOUDY_TRANSITION
                    }
                }
            }
        }
    }

    /**
     * @brief Tree 4: Bootstrap estimator balancing barometric tendency (x[3]), spread (x[4]), and pressure (x[2])
     */
    static inline uint8_t tree4(const float *x) {
        if (x[3] <= -2.50f) {
            return 3; // STORM_FRONT_RAPID_DROP
        }
        if (x[2] <= 1005.5f && x[3] <= -1.00f) {
            return 2; // RAIN_IMMINENT
        }
        if (x[4] <= 2.65f && x[1] >= 82.0f) {
            return 2; // RAIN_IMMINENT
        }
        if (x[2] >= 1011.0f && x[3] >= -0.35f && x[4] >= 4.20f) {
            return 0; // CLEAR_STABLE
        }
        if (x[3] >= 0.40f && x[1] <= 75.0f) {
            return 0; // CLEAR_STABLE
        }
        return 1; // CLOUDY_TRANSITION
    }

    /**
     * @brief Run ensemble inference across all 5 decision trees
     * @param features 6-element float array [T, RH, P0, dP/dt, T-Tdew, dRH/dt]
     * @param confidence Output pointer to winning class vote ratio (0.20 to 1.00)
     * @param votesOut Optional 4-element array to inspect individual tree votes
     * @return Predicted WeatherClass index (0..3)
     */
    static uint8_t predict(const float features[WEATHER_NUM_FEATURES], float *confidence, uint8_t *votesOut = nullptr) {
        uint8_t votes[WEATHER_NUM_CLASSES] = {0, 0, 0, 0};

        votes[tree0(features)]++;
        votes[tree1(features)]++;
        votes[tree2(features)]++;
        votes[tree3(features)]++;
        votes[tree4(features)]++;

        uint8_t bestClass = 0;
        uint8_t maxVotes = votes[0];

        for (uint8_t c = 1; c < WEATHER_NUM_CLASSES; c++) {
            if (votes[c] > maxVotes) {
                maxVotes = votes[c];
                bestClass = c;
            }
        }

        if (confidence != nullptr) {
            *confidence = static_cast<float>(maxVotes) / static_cast<float>(WEATHER_NUM_TREES);
        }

        if (votesOut != nullptr) {
            for (uint8_t c = 0; c < WEATHER_NUM_CLASSES; c++) {
                votesOut[c] = votes[c];
            }
        }

        return bestClass;
    }
};

} // namespace Port
} // namespace ML
} // namespace Eloquent

/**
 * @brief Top-level C-style wrapper matching the specification signature
 */
inline uint8_t predictWeatherClass(const float features[WEATHER_NUM_FEATURES], float* confidence) {
    return Eloquent::ML::Port::WeatherRandomForest::predict(features, confidence, nullptr);
}

#endif // WEATHER_RF_MODEL_H
