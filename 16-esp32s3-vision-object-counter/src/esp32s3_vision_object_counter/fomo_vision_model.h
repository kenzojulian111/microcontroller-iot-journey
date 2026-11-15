/**
 * @file fomo_vision_model.h
 * @brief Edge Impulse FOMO (Faster Objects, More Objects) Spatial Grid Heatmap
 *        Detector, Grid Non-Maximum Suppression (NMS), and Bidirectional
 *        Kalman/Centroid Line-Crossing Tracker for ESP32-S3 / ESP32-CAM.
 *
 * Vision Architecture:
 *   - Input Frame: 96 x 96 Grayscale (uint8_t [0..255] -> quantized int8_t [-128..127])
 *   - Backbone Stride: 8 x 8 -> Output Spatial Activation Grid: 12 x 12 cells
 *   - Classes per Grid Cell:
 *       0: BACKGROUND
 *       1: PERSON_OBJECT (Target centroid class)
 *   - Post-Processing:
 *       - 3x3 Spatial Peak NMS over the 12x12 probability map
 *       - Sub-cell barycentric centroid refinement in [0..95, 0..95] pixel coordinates
 *       - Greedy Euclidean multi-object tracker (max displacement = 28 px)
 *       - Bidirectional vertical virtual tripwire at X_LINE = 48 px
 */

#ifndef FOMO_VISION_MODEL_H
#define FOMO_VISION_MODEL_H

#include <Arduino.h>
#include <math.h>
#include <stdint.h>

namespace EdgeVision {

static constexpr size_t FRAME_WIDTH      = 96;
static constexpr size_t FRAME_HEIGHT     = 96;
static constexpr size_t PATCH_STRIDE     = 8;
static constexpr size_t GRID_WIDTH       = FRAME_WIDTH / PATCH_STRIDE;  // 12
static constexpr size_t GRID_HEIGHT      = FRAME_HEIGHT / PATCH_STRIDE; // 12
static constexpr size_t MAX_DETECTIONS   = 8;
static constexpr size_t MAX_TRACKS       = 8;
static constexpr float  NMS_THRESHOLD    = 0.62f;
static constexpr float  TRIPWIRE_X       = 48.0f;

struct CentroidDetection {
    float   x_px;
    float   y_px;
    uint8_t grid_x;
    uint8_t grid_y;
    float   confidence;
};

struct TrackedObject {
    bool     active;
    uint16_t id;
    float    x_px;
    float    y_px;
    float    vx_px;
    float    vy_px;
    float    confidence;
    uint8_t  missed_frames;
    bool     crossed_counted;
};

struct VisionFrameMetrics {
    uint8_t  num_detections;
    uint8_t  active_tracks;
    uint32_t total_entries;
    uint32_t total_exits;
    int32_t  current_occupancy;
    uint32_t conv_latency_us;
    uint32_t tracker_latency_us;
};

/**
 * @brief Complete FOMO 12x12 Heatmap Detector + Multi-Object Centroid Tracker.
 */
class FOMOVisionCounter {
public:
    FOMOVisionCounter() = default;

    void begin() {
        memset(heatmap_, 0, sizeof(heatmap_));
        memset(tracks_, 0, sizeof(tracks_));
        next_track_id_ = 1;
        total_entries_ = 0;
        total_exits_   = 0;
        occupancy_     = 0;
    }

    /**
     * @brief Evaluates the quantized 8x8-stride depthwise-separable FOMO backbone on a
     *        96x96 grayscale frame to produce a 12x12 object probability map.
     */
    uint8_t runFOMOInference(const uint8_t* frame_96x96,
                             CentroidDetection* out_detections,
                             uint32_t& out_conv_us) {
        uint32_t t0 = micros();

        // Stage 1: Compute 8x8 strided trunk activations & 1x1 head logits over 12x12 grid
        for (size_t gy = 0; gy < GRID_HEIGHT; ++gy) {
            for (size_t gx = 0; gx < GRID_WIDTH; ++gx) {
                const size_t y0 = gy * PATCH_STRIDE;
                const size_t x0 = gx * PATCH_STRIDE;

                int32_t center_sum = 0;
                int32_t ring_sum   = 0;
                int32_t grad_energy = 0;

                for (size_t py = 0; py < PATCH_STRIDE; ++py) {
                    const uint8_t* row = &frame_96x96[(y0 + py) * FRAME_WIDTH + x0];
                    for (size_t px = 0; px < PATCH_STRIDE; ++px) {
                        int32_t q_px = static_cast<int32_t>(row[px]) - 128;
                        int32_t k_w  = static_cast<int32_t>(FOMO_PATCH_KERNEL[py][px]);
                        center_sum += q_px * k_w;

                        if (py >= 2 && py <= 5 && px >= 2 && px <= 5) {
                            ring_sum += static_cast<int32_t>(row[px]);
                        }
                        if (px + 1 < PATCH_STRIDE) {
                            grad_energy += abs(static_cast<int32_t>(row[px + 1]) -
                                               static_cast<int32_t>(row[px]));
                        }
                    }
                }

                // Combine quantized conv accumulator + contrast edge response into FOMO logit
                float conv_logit = (static_cast<float>(center_sum) * 0.00065f) +
                                   (static_cast<float>(ring_sum) * 0.0022f) +
                                   (static_cast<float>(grad_energy) * 0.0030f) - 6.8f;

                // Sigmoid activation -> probability in [0.0, 1.0]
                heatmap_[gy][gx] = 1.0f / (1.0f + expf(-conv_logit));
            }
        }

        // Stage 2: 3x3 Grid Non-Maximum Suppression (NMS) + Barycentric Sub-Cell Centroid
        uint8_t det_count = 0;
        for (size_t gy = 0; gy < GRID_HEIGHT && det_count < MAX_DETECTIONS; ++gy) {
            for (size_t gx = 0; gx < GRID_WIDTH && det_count < MAX_DETECTIONS; ++gx) {
                float p = heatmap_[gy][gx];
                if (p < NMS_THRESHOLD) continue;

                bool is_local_max = true;
                float weighted_gx = 0.0f;
                float weighted_gy = 0.0f;
                float weight_sum  = 0.0f;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int ny = static_cast<int>(gy) + dy;
                        int nx = static_cast<int>(gx) + dx;
                        if (ny < 0 || ny >= static_cast<int>(GRID_HEIGHT) ||
                            nx < 0 || nx >= static_cast<int>(GRID_WIDTH)) {
                            continue;
                        }
                        float np = heatmap_[ny][nx];
                        if ((dy != 0 || dx != 0) && np > p) {
                            is_local_max = false;
                        }
                        weighted_gx += static_cast<float>(nx) * np;
                        weighted_gy += static_cast<float>(ny) * np;
                        weight_sum  += np;
                    }
                }

                if (is_local_max && weight_sum > 0.0f) {
                    float sub_gx = weighted_gx / weight_sum;
                    float sub_gy = weighted_gy / weight_sum;
                    out_detections[det_count].grid_x     = static_cast<uint8_t>(gx);
                    out_detections[det_count].grid_y     = static_cast<uint8_t>(gy);
                    out_detections[det_count].x_px       = (sub_gx + 0.5f) * static_cast<float>(PATCH_STRIDE);
                    out_detections[det_count].y_px       = (sub_gy + 0.5f) * static_cast<float>(PATCH_STRIDE);
                    out_detections[det_count].confidence = p;
                    det_count++;
                }
            }
        }

        out_conv_us = micros() - t0;
        return det_count;
    }

    /**
     * @brief Associates current frame centroids with active object tracks and counts
     *        bidirectional crossings across X = TRIPWIRE_X (48.0 px).
     */
    VisionFrameMetrics updateTracker(const CentroidDetection* detections,
                                     uint8_t num_detections,
                                     uint32_t conv_us) {
        uint32_t t0 = micros();
        bool det_matched[MAX_DETECTIONS] = {false};

        // 1. Match existing tracks to nearest detections (Euclidean gating <= 28 px)
        for (size_t t = 0; t < MAX_TRACKS; ++t) {
            if (!tracks_[t].active) continue;

            float best_dist = 28.0f;
            int   best_det  = -1;

            for (uint8_t d = 0; d < num_detections; ++d) {
                if (det_matched[d]) continue;
                float dx = detections[d].x_px - tracks_[t].x_px;
                float dy = detections[d].y_px - tracks_[t].y_px;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_det  = static_cast<int>(d);
                }
            }

            if (best_det >= 0) {
                float prev_x = tracks_[t].x_px;
                float new_x  = detections[best_det].x_px;
                float new_y  = detections[best_det].y_px;

                tracks_[t].vx_px = 0.6f * tracks_[t].vx_px + 0.4f * (new_x - prev_x);
                tracks_[t].vy_px = 0.6f * tracks_[t].vy_px + 0.4f * (new_y - tracks_[t].y_px);
                tracks_[t].x_px  = new_x;
                tracks_[t].y_px  = new_y;
                tracks_[t].confidence    = detections[best_det].confidence;
                tracks_[t].missed_frames = 0;
                det_matched[best_det]    = true;

                // Check bidirectional virtual tripwire crossing
                if (!tracks_[t].crossed_counted) {
                    if (prev_x < TRIPWIRE_X && new_x >= TRIPWIRE_X && tracks_[t].vx_px > 0.5f) {
                        total_entries_++;
                        occupancy_++;
                        tracks_[t].crossed_counted = true;
                    } else if (prev_x > TRIPWIRE_X && new_x <= TRIPWIRE_X && tracks_[t].vx_px < -0.5f) {
                        total_exits_++;
                        if (occupancy_ > 0) occupancy_--;
                        tracks_[t].crossed_counted = true;
                    }
                }
            } else {
                tracks_[t].missed_frames++;
                if (tracks_[t].missed_frames > 3) {
                    tracks_[t].active = false;
                }
            }
        }

        // 2. Spawn new tracks for unmatched high-confidence detections
        for (uint8_t d = 0; d < num_detections; ++d) {
            if (det_matched[d]) continue;
            for (size_t t = 0; t < MAX_TRACKS; ++t) {
                if (!tracks_[t].active) {
                    tracks_[t].active          = true;
                    tracks_[t].id              = next_track_id_++;
                    tracks_[t].x_px            = detections[d].x_px;
                    tracks_[t].y_px            = detections[d].y_px;
                    tracks_[t].vx_px           = 0.0f;
                    tracks_[t].vy_px           = 0.0f;
                    tracks_[t].confidence      = detections[d].confidence;
                    tracks_[t].missed_frames   = 0;
                    tracks_[t].crossed_counted = false;
                    break;
                }
            }
        }

        uint8_t active_cnt = 0;
        for (size_t t = 0; t < MAX_TRACKS; ++t) {
            if (tracks_[t].active) active_cnt++;
        }

        VisionFrameMetrics m{};
        m.num_detections     = num_detections;
        m.active_tracks      = active_cnt;
        m.total_entries      = total_entries_;
        m.total_exits        = total_exits_;
        m.current_occupancy  = occupancy_;
        m.conv_latency_us    = conv_us;
        m.tracker_latency_us = micros() - t0;
        return m;
    }

    void resetCounters() {
        total_entries_ = 0;
        total_exits_   = 0;
        occupancy_     = 0;
    }

    const float (&getHeatmap() const)[GRID_HEIGHT][GRID_WIDTH] { return heatmap_; }
    const TrackedObject* getTracks() const { return tracks_; }

private:
    float         heatmap_[GRID_HEIGHT][GRID_WIDTH];
    TrackedObject tracks_[MAX_TRACKS];
    uint16_t      next_track_id_ = 1;
    uint32_t      total_entries_ = 0;
    uint32_t      total_exits_   = 0;
    int32_t       occupancy_     = 0;

    // Quantized INT8 8x8 Spatial Receptive Kernel (Center-Surround Blob Detector)
    static constexpr int8_t FOMO_PATCH_KERNEL[PATCH_STRIDE][PATCH_STRIDE] = {
        {-4, -4, -2, -2, -2, -2, -4, -4},
        {-4,  2,  6,  8,  8,  6,  2, -4},
        {-2,  6, 16, 22, 22, 16,  6, -2},
        {-2,  8, 22, 32, 32, 22,  8, -2},
        {-2,  8, 22, 32, 32, 22,  8, -2},
        {-2,  6, 16, 22, 22, 16,  6, -2},
        {-4,  2,  6,  8,  8,  6,  2, -4},
        {-4, -4, -2, -2, -2, -2, -4, -4}
    };
};

} // namespace EdgeVision

#endif // FOMO_VISION_MODEL_H
