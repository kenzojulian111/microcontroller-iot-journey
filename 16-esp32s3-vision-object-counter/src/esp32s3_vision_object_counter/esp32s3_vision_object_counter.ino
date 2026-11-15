/**
 * @file esp32s3_vision_object_counter.ino
 * @brief Project 16: ESP32-S3 Embedded Vision Object Counter (Edge Impulse FOMO + Centroid Tracker)
 *
 * Features:
 *   - Supports OV2640 DVP Camera (`esp_camera.h`, 96x96 Grayscale) on XIAO ESP32S3 Sense / ESP32-CAM
 *   - Includes a deterministic 96x96 Moving-Blob Optical Frame Synthesizer so the full FOMO
 *     inference, 12x12 heatmap, NMS, and bidirectional line-crossing tracker run on standard
 *     ESP32 in Wokwi Simulator!
 *   - Interactive Hardware Controls (Wokwi & Bench):
 *       - GPIO 12 (BTN_ENTRY): Spawns a person blob walking Left -> Right across X=48 px (ENTRY)
 *       - GPIO 14 (BTN_EXIT):  Spawns a person blob walking Right -> Left across X=48 px (EXIT)
 *       - GPIO 27 (BTN_RESET): Resets occupancy counters
 *   - SSD1306 OLED split-screen view: Live 12x12 FOMO Heatmap + Tripwire + Occupancy Metrics
 *   - Local HTTP Server (Port 80): JSON telemetry (`/api/metrics`) & live HTML/SVG FOMO grid dashboard (`/`)
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "secrets.example.h"
#include "fomo_vision_model.h"

#if ENABLE_HW_OV2640
#include "esp_camera.h"
// XIAO ESP32S3 Sense OV2640 Pin Map
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13
#endif

static constexpr int PIN_BTN_SPAWN_ENTRY = 12;
static constexpr int PIN_BTN_SPAWN_EXIT  = 14;
static constexpr int PIN_BTN_RESET       = 27;
static constexpr int PIN_LED_OCCUPIED    = 2;
static constexpr int PIN_I2C_SDA         = 21;
static constexpr int PIN_I2C_SCL         = 22;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
WebServer httpServer(HTTP_SERVER_PORT);
EdgeVision::FOMOVisionCounter visionEngine;

uint8_t frameBuffer[EdgeVision::FRAME_WIDTH * EdgeVision::FRAME_HEIGHT];
EdgeVision::CentroidDetection detections[EdgeVision::MAX_DETECTIONS];
EdgeVision::VisionFrameMetrics latestMetrics{};

struct SyntheticBlob {
    bool  active;
    float x;
    float y;
    float vx;
    float vy;
    float radius;
};

SyntheticBlob simBlobs[2] = {
    {true,  14.0f, 32.0f,  4.2f, 0.2f, 8.5f},
    {false, 82.0f, 68.0f, -4.2f, 0.0f, 8.5f}
};

uint32_t frameIdx = 0;
bool     cameraInitialized = false;

#if ENABLE_HW_OV2640
bool initOV2640Camera() {
    camera_config_t config{};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size   = FRAMESIZE_96X96;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;
    return (esp_camera_init(&config) == ESP_OK);
}
#endif

/**
 * @brief Renders synthetic 96x96 grayscale optical frame with Gaussian person blobs
 *        traversing the scene and subtle sensor shot noise.
 */
void renderSyntheticFrame(uint8_t* out_frame) {
    // Poll hardware pushbuttons for interactive spawning
    if (digitalRead(PIN_BTN_SPAWN_ENTRY) == LOW && !simBlobs[0].active) {
        simBlobs[0] = {true, 10.0f, 28.0f + static_cast<float>((frameIdx * 7) % 40), 4.5f, 0.0f, 8.5f};
    }
    if (digitalRead(PIN_BTN_SPAWN_EXIT) == LOW && !simBlobs[1].active) {
        simBlobs[1] = {true, 86.0f, 64.0f - static_cast<float>((frameIdx * 5) % 36), -4.5f, 0.0f, 8.5f};
    }
    if (digitalRead(PIN_BTN_RESET) == LOW) {
        visionEngine.resetCounters();
    }

    // Periodic autonomous pedestrian spawn every 45 frames for continuous demo
    if (frameIdx % 45 == 0 && !simBlobs[0].active) {
        simBlobs[0] = {true, 10.0f, 26.0f, 4.4f, 0.15f, 8.5f};
    } else if (frameIdx % 75 == 35 && !simBlobs[1].active) {
        simBlobs[1] = {true, 86.0f, 68.0f, -4.4f, -0.10f, 8.5f};
    }

    // Update blob kinematics
    for (auto& b : simBlobs) {
        if (!b.active) continue;
        b.x += b.vx;
        b.y += b.vy;
        if (b.x < 4.0f || b.x > 92.0f || b.y < 4.0f || b.y > 92.0f) {
            b.active = false;
        }
    }

    // Render 96x96 grayscale pixel buffer
    for (size_t y = 0; y < EdgeVision::FRAME_HEIGHT; ++y) {
        for (size_t x = 0; x < EdgeVision::FRAME_WIDTH; ++x) {
            int32_t pix = 22 + ((x * 13 + y * 7 + frameIdx) & 0x07); // Ambient floor
            for (const auto& b : simBlobs) {
                if (!b.active) continue;
                float dx = static_cast<float>(x) - b.x;
                float dy = static_cast<float>(y) - b.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < (b.radius * b.radius * 2.2f)) {
                    float intensity = 225.0f * expf(-d2 / (2.0f * 16.0f));
                    pix += static_cast<int32_t>(intensity);
                }
            }
            if (pix > 255) pix = 255;
            out_frame[y * EdgeVision::FRAME_WIDTH + x] = static_cast<uint8_t>(pix);
        }
    }
}

void acquireFrame(uint8_t* buffer) {
#if ENABLE_HW_OV2640
    if (cameraInitialized) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb && fb->len >= EdgeVision::FRAME_WIDTH * EdgeVision::FRAME_HEIGHT) {
            memcpy(buffer, fb->buf, EdgeVision::FRAME_WIDTH * EdgeVision::FRAME_HEIGHT);
            esp_camera_fb_return(fb);
            return;
        }
        if (fb) esp_camera_fb_return(fb);
    }
#endif
    renderSyntheticFrame(buffer);
}

void renderOLED(const EdgeVision::VisionFrameMetrics& m) {
    display.clearDisplay();

    // Left Pane (52x52 px): 12x12 FOMO Heatmap Grid (4x4 px per cell) + Tripwire
    display.drawRect(0, 8, 50, 50, SSD1306_WHITE);
    display.drawFastVLine(1 + 6 * 4, 8, 50, SSD1306_WHITE); // Virtual Tripwire at X=48 (grid col 6)

    const auto& heatmap = visionEngine.getHeatmap();
    for (size_t gy = 0; gy < EdgeVision::GRID_HEIGHT; ++gy) {
        for (size_t gx = 0; gx < EdgeVision::GRID_WIDTH; ++gx) {
            if (heatmap[gy][gx] >= EdgeVision::NMS_THRESHOLD) {
                display.fillRect(static_cast<int>(1 + gx * 4),
                                 static_cast<int>(9 + gy * 4),
                                 3, 3, SSD1306_WHITE);
            } else if (heatmap[gy][gx] >= 0.30f) {
                display.drawPixel(static_cast<int>(2 + gx * 4),
                                  static_cast<int>(10 + gy * 4),
                                  SSD1306_WHITE);
            }
        }
    }

    // Right Pane: Telemetry & Counters
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(F("FOMO 96x96->12x12 NMS"));

    display.setCursor(54, 12);
    display.printf("IN : %4lu", static_cast<unsigned long>(m.total_entries));
    display.setCursor(54, 23);
    display.printf("OUT: %4lu", static_cast<unsigned long>(m.total_exits));
    display.setCursor(54, 34);
    display.printf("OCC: %4ld", static_cast<long>(m.current_occupancy));
    display.setCursor(54, 45);
    display.printf("TRK:%u DET:%u", m.active_tracks, m.num_detections);
    display.setCursor(54, 55);
    display.printf("CNN:%4luus", static_cast<unsigned long>(m.conv_latency_us));

    display.display();
}

void setupHTTPServer() {
    httpServer.on("/api/metrics", HTTP_GET, []() {
        String json = "{";
        json += "\"frame\":" + String(frameIdx) + ",";
        json += "\"entries\":" + String(latestMetrics.total_entries) + ",";
        json += "\"exits\":" + String(latestMetrics.total_exits) + ",";
        json += "\"occupancy\":" + String(latestMetrics.current_occupancy) + ",";
        json += "\"detections\":" + String(latestMetrics.num_detections) + ",";
        json += "\"tracks\":" + String(latestMetrics.active_tracks) + ",";
        json += "\"conv_us\":" + String(latestMetrics.conv_latency_us) + ",";
        json += "\"heatmap\":[";
        const auto& hm = visionEngine.getHeatmap();
        for (size_t y = 0; y < EdgeVision::GRID_HEIGHT; ++y) {
            json += "[";
            for (size_t x = 0; x < EdgeVision::GRID_WIDTH; ++x) {
                json += String(hm[y][x], 2);
                if (x + 1 < EdgeVision::GRID_WIDTH) json += ",";
            }
            json += (y + 1 < EdgeVision::GRID_HEIGHT) ? "]," : "]";
        }
        json += "]}";
        httpServer.send(200, "application/json", json);
    });

    httpServer.on("/", HTTP_GET, []() {
        String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                        "<meta http-equiv='refresh' content='2'>"
                        "<title>ESP32-S3 FOMO Vision Object Counter</title>"
                        "<style>body{font-family:monospace;background:#111;color:#eee;padding:20px;}"
                        "table{border-collapse:collapse;}td{width:22px;height:22px;text-align:center;"
                        "border:1px solid #333;font-size:10px;}</style></head><body>"
                        "<h2>ESP32-S3 FOMO Vision Object Counter (12x12 Heatmap)</h2>");
        html += "<p>Entries: <b>" + String(latestMetrics.total_entries) +
                "</b> | Exits: <b>" + String(latestMetrics.total_exits) +
                "</b> | Occupancy: <b>" + String(latestMetrics.current_occupancy) +
                "</b> | FOMO Latency: <b>" + String(latestMetrics.conv_latency_us) + " us</b></p><table>";
        const auto& hm = visionEngine.getHeatmap();
        for (size_t y = 0; y < EdgeVision::GRID_HEIGHT; ++y) {
            html += "<tr>";
            for (size_t x = 0; x < EdgeVision::GRID_WIDTH; ++x) {
                int green = static_cast<int>(hm[y][x] * 220.0f);
                String border = (x == 6) ? "border-left:2px solid #f44;" : "";
                html += "<td style='background:rgb(0," + String(green) + ",40);" + border + "'>" +
                        String(static_cast<int>(hm[y][x] * 99.0f)) + "</td>";
            }
            html += "</tr>";
        }
        html += F("</table></body></html>");
        httpServer.send(200, "text/html", html);
    });

    httpServer.begin();
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN_SPAWN_ENTRY, INPUT_PULLUP);
    pinMode(PIN_BTN_SPAWN_EXIT, INPUT_PULLUP);
    pinMode(PIN_BTN_RESET, INPUT_PULLUP);
    pinMode(PIN_LED_OCCUPIED, OUTPUT);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.display();

    visionEngine.begin();

#if ENABLE_HW_OV2640
    cameraInitialized = initOV2640Camera();
#endif

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 3500) {
        delay(100);
    }
    setupHTTPServer();

    Serial.println(F("=================================================================="));
    Serial.println(F(" Project 16: ESP32-S3 Vision Object Counter (FOMO 12x12 + Tracker)"));
    Serial.println(F(" GPIO12=Spawn Entry | GPIO14=Spawn Exit | GPIO27=Reset Counters"));
    Serial.println(F("=================================================================="));
}

void loop() {
    httpServer.handleClient();

    acquireFrame(frameBuffer);

    uint32_t conv_us = 0;
    uint8_t num_det = visionEngine.runFOMOInference(frameBuffer, detections, conv_us);
    latestMetrics = visionEngine.updateTracker(detections, num_det, conv_us);
    frameIdx++;

    digitalWrite(PIN_LED_OCCUPIED, (latestMetrics.current_occupancy > 0) ? HIGH : LOW);
    renderOLED(latestMetrics);

    if (frameIdx % 10 == 0) {
        Serial.printf(
            "{\"frame\":%lu,\"det\":%u,\"tracks\":%u,\"entries\":%lu,\"exits\":%lu,"
            "\"occupancy\":%ld,\"fomo_us\":%lu,\"track_us\":%lu}\n",
            static_cast<unsigned long>(frameIdx),
            latestMetrics.num_detections,
            latestMetrics.active_tracks,
            static_cast<unsigned long>(latestMetrics.total_entries),
            static_cast<unsigned long>(latestMetrics.total_exits),
            static_cast<long>(latestMetrics.current_occupancy),
            static_cast<unsigned long>(latestMetrics.conv_latency_us),
            static_cast<unsigned long>(latestMetrics.tracker_latency_us)
        );
    }

    delay(80);
}
