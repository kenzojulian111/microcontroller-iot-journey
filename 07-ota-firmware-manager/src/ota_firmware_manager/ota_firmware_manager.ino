/**
 * ============================================================================
 * Proyek 07 : OTA Firmware Update & Fleet Manager
 * Repo      : IoT_lab
 * Board     : ESP32 & ESP8266 (Otomatis mendeteksi arsitektur board)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Dual-Mode Over-The-Air (OTA) Firmware Update:
 *    - Metode 1: Local Push OTA (ArduinoOTA.h) — Upload langsung dari Arduino IDE
 *      melalui jaringan Wi-Fi lokal dengan autentikasi password & progress bar.
 *    - Metode 2: Cloud Pull HTTP/HTTPS OTA (HTTPUpdate / ESPhttpUpdate) — Mengecek
 *      manifest `version.json` di server/cloud secara berkala atau saat tombol ditekan,
 *      membandingkan Semantic Version (`FIRMWARE_VERSION`), dan otomatis mengunduh
 *      serta mem-flash binary `.bin` jika tersedia versi yang lebih baru.
 * 2. Web Diagnostik & Fleet Dashboard (`GET /`) untuk melihat versi firmware,
 *    partisi flash aktif, sisa memori, dan tombol pemicu "Check for Update Now".
 * 3. Indikator LED Status OTA & Tombol Fisik pemicu pemeriksaan update manual.
 * ============================================================================
 */

#include <Arduino.h>
#include <ArduinoOTA.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #include <esp_ota_ops.h>
  WebServer server(80);
  #define BOARD_NAME "ESP32"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266httpUpdate.h>
  ESP8266WebServer server(80);
  #define BOARD_NAME "ESP8266"
#else
  #error "Board tidak didukung! Pilih board ESP32 atau ESP8266 di Arduino IDE."
#endif

// Gunakan secrets.h jika tersedia, atau fallback ke default simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID             "Wokwi-GUEST"
  #define WIFI_PASSWORD         ""
  #define OTA_HOSTNAME          "esp-ota-node-01"
  #define OTA_PASSWORD          "admin-ota-rahasia"
  #define VERSION_MANIFEST_URL  "http://192.168.1.100:8000/version.json"
  #define FIRMWARE_BIN_URL      "http://192.168.1.100:8000/ota_firmware_manager.ino.bin"
#endif

// ============================================================================
// Identitas & Versi Firmware Saat Ini (Semantic Versioning: MAJOR.MINOR.PATCH)
// ============================================================================
const char* FIRMWARE_VERSION = "1.0.0";
const char* BUILD_TIMESTAMP  = __DATE__ " " __TIME__;

// ============================================================================
// Konfigurasi Pin Hardware
// ============================================================================
#if defined(ESP32)
  const int PIN_OTA_BUTTON = 14; // Tombol fisik pemicu Cloud OTA Check (INPUT_PULLUP)
  const int PIN_STATUS_LED = 2;  // LED indikator status OTA & Heartbeat
  const int PIN_ALERT_LED  = 26; // LED indikator proses flashing / update berlangsung
#elif defined(ESP8266)
  const int PIN_OTA_BUTTON = 14; // D5 (GPIO14)
  const int PIN_STATUS_LED = LED_BUILTIN;
  const int PIN_ALERT_LED  = 5;  // D1 (GPIO5)
#endif

// ============================================================================
// Variabel Global & Timer
// ============================================================================
const unsigned long AUTO_CHECK_INTERVAL_MS = 3600000UL; // Cek otomatis tiap 1 jam
const unsigned long HEARTBEAT_INTERVAL_MS  = 1000UL;    // Kedip LED normal tiap 1 detik
const unsigned long DEBOUNCE_DELAY_MS      = 50UL;

unsigned long lastAutoCheckTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastDebounceTime  = 0;
bool lastButtonReading          = HIGH;
bool heartbeatLedState          = false;

String lastOtaStatusMessage     = "Idle - Menunggu jadwal pemeriksaan atau trigger manual.";
String latestRemoteVersion      = "Belum diperiksa";
bool manualCheckRequested       = false;

// ============================================================================
// Helper: Ambil Nama Partisi Flash Aktif (Khusus ESP32)
// ============================================================================
String getActivePartitionLabel() {
#if defined(ESP32)
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr) {
    return String(running->label) + " (0x" + String(running->address, HEX) + ")";
  }
  return "Unknown";
#else
  return "ESP8266 Dual-Bank Flash";
#endif
}

// ============================================================================
// Helper: Bandingkan Semantic Version (contoh: "1.2.0" vs "1.0.0")
// Mengembalikan true jika remoteVer > localVer
// ============================================================================
bool isNewerVersion(const String& remoteVer, const String& localVer) {
  int rMajor = 0, rMinor = 0, rPatch = 0;
  int lMajor = 0, lMinor = 0, lPatch = 0;

  sscanf(remoteVer.c_str(), "%d.%d.%d", &rMajor, &rMinor, &rPatch);
  sscanf(localVer.c_str(),  "%d.%d.%d", &lMajor, &lMinor, &lPatch);

  if (rMajor != lMajor) return rMajor > lMajor;
  if (rMinor != lMinor) return rMinor > lMinor;
  return rPatch > lPatch;
}

// ============================================================================
// Helper: Ekstraksi Field String Sederhana dari JSON Manifest
// ============================================================================
String extractJsonStringField(const String& json, const String& key) {
  String searchKey = "\"" + key + "\"";
  int keyIdx = json.indexOf(searchKey);
  if (keyIdx < 0) return "";

  int colonIdx = json.indexOf(':', keyIdx + searchKey.length());
  if (colonIdx < 0) return "";

  int firstQuote = json.indexOf('"', colonIdx + 1);
  if (firstQuote < 0) return "";

  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";

  return json.substring(firstQuote + 1, secondQuote);
}

// ============================================================================
// Metode 1: Setup Local Push OTA (ArduinoOTA via IDE)
// ============================================================================
void setupArduinoOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "Firmware Sketch" : "Filesystem (SPIFFS/LittleFS)";
    Serial.println("\n============================================");
    Serial.printf("[LOCAL-OTA] Memulai pembaruan %s...\n", type.c_str());
    digitalWrite(PIN_ALERT_LED, HIGH);
  });

  ArduinoOTA.onEnd([]() {
    digitalWrite(PIN_ALERT_LED, LOW);
    Serial.println("\n[LOCAL-OTA] Pembaruan selesai 100%! Me-reboot perangkat...");
    Serial.println("============================================");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int pct = (progress * 100U) / total;
    static unsigned int lastPrintedPct = 999;
    if (pct % 10 == 0 && pct != lastPrintedPct) {
      lastPrintedPct = pct;
      Serial.printf("[LOCAL-OTA] Progress Upload: %u%% (%u / %u bytes)\n", pct, progress, total);
      digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    digitalWrite(PIN_ALERT_LED, LOW);
    Serial.printf("\n[LOCAL-OTA] ERROR [%u]: ", error);
    if (error == OTA_AUTH_ERROR)         Serial.println("Autentikasi Password Gagal!");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Gagal Memulai Partisi Flash!");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Koneksi Terputus dari Klien!");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Gagal Menerima Paket Data!");
    else if (error == OTA_END_ERROR)     Serial.println("Gagal Memvalidasi Image Akhir!");
  });

  ArduinoOTA.begin();
  Serial.printf("[LOCAL-OTA] Siap menerima Push OTA di Hostname: %s.local\n", OTA_HOSTNAME);
}

// ============================================================================
// Metode 2: Cloud Pull HTTP OTA (Cek version.json & unduh binary .bin)
// ============================================================================
void performCloudOtaCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    lastOtaStatusMessage = "Gagal: Wi-Fi tidak terhubung.";
    Serial.println("[CLOUD-OTA] Batal mengecek update karena Wi-Fi terputus.");
    return;
  }

  Serial.println("\n--------------------------------------------");
  Serial.println("[CLOUD-OTA] Memeriksa manifest versi terbaru...");
  Serial.printf("[CLOUD-OTA] Manifest URL : %s\n", VERSION_MANIFEST_URL);
  Serial.printf("[CLOUD-OTA] Versi Lokal  : v%s\n", FIRMWARE_VERSION);
  digitalWrite(PIN_ALERT_LED, HIGH);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);

  if (!http.begin(client, VERSION_MANIFEST_URL)) {
    lastOtaStatusMessage = "Gagal menginisialisasi koneksi HTTP ke server manifest.";
    Serial.println("[CLOUD-OTA] Error: Gagal membuka koneksi HTTP.");
    digitalWrite(PIN_ALERT_LED, LOW);
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    lastOtaStatusMessage = "Manifest tidak dapat diakses (HTTP " + String(httpCode) + ").";
    Serial.printf("[CLOUD-OTA] Gagal mengunduh version.json (HTTP Code: %d)\n", httpCode);
    http.end();
    digitalWrite(PIN_ALERT_LED, LOW);
    Serial.println("--------------------------------------------");
    return;
  }

  String payload = http.getString();
  http.end();

  String remoteVersion = extractJsonStringField(payload, "version");
  String remoteBinUrl  = extractJsonStringField(payload, "url");
  String releaseNotes  = extractJsonStringField(payload, "notes");

  if (remoteVersion.length() == 0) {
    lastOtaStatusMessage = "Format version.json tidak valid (field 'version' kosong).";
    Serial.println("[CLOUD-OTA] Error: JSON tidak memiliki field 'version'.");
    digitalWrite(PIN_ALERT_LED, LOW);
    return;
  }

  if (remoteBinUrl.length() == 0) {
    remoteBinUrl = FIRMWARE_BIN_URL;
  }

  latestRemoteVersion = remoteVersion;
  Serial.printf("[CLOUD-OTA] Versi Server : v%s\n", remoteVersion.c_str());
  if (releaseNotes.length() > 0) {
    Serial.printf("[CLOUD-OTA] Catatan Rilis: %s\n", releaseNotes.c_str());
  }

  if (!isNewerVersion(remoteVersion, FIRMWARE_VERSION)) {
    lastOtaStatusMessage = "Firmware sudah versi terbaru (v" + String(FIRMWARE_VERSION) + ").";
    Serial.println("[CLOUD-OTA] Firmware saat ini sudah paling mutakhir. Tidak perlu update.");
    digitalWrite(PIN_ALERT_LED, LOW);
    Serial.println("--------------------------------------------");
    return;
  }

  // Versi baru ditemukan -> mulai proses unduh & flash
  Serial.printf("[CLOUD-OTA] UPDATE TERSEDIA! Mengunduh binary dari: %s\n", remoteBinUrl.c_str());
  lastOtaStatusMessage = "Mengunduh & mem-flash v" + remoteVersion + "...";

#if defined(ESP32)
  httpUpdate.setLedPin(PIN_STATUS_LED, HIGH);
  httpUpdate.onStart([]() {
    Serial.println("[CLOUD-OTA] Memulai penulisan ke partisi OTA berikutnya...");
  });
  httpUpdate.onEnd([]() {
    Serial.println("[CLOUD-OTA] Penulisan Flash selesai! Perangkat akan segera reboot...");
  });
  httpUpdate.onProgress([](int cur, int total) {
    int pct = (total > 0) ? ((cur * 100) / total) : 0;
    static int lastPct = -1;
    if (pct % 10 == 0 && pct != lastPct) {
      lastPct = pct;
      Serial.printf("[CLOUD-OTA] Mengunduh Firmware: %d%% (%d / %d bytes)\n", pct, cur, total);
    }
  });
  httpUpdate.onError([](int err) {
    Serial.printf("[CLOUD-OTA] HTTPUpdate Error Kode (%d): %s\n", err, httpUpdate.getLastErrorString().c_str());
  });

  t_httpUpdate_return ret = httpUpdate.update(client, remoteBinUrl);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      lastOtaStatusMessage = "Gagal Flash: " + httpUpdate.getLastErrorString();
      Serial.printf("[CLOUD-OTA] GAGAL (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      lastOtaStatusMessage = "Server melaporkan tidak ada pembaruan.";
      Serial.println("[CLOUD-OTA] Tidak ada pembaruan binary.");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("[CLOUD-OTA] Sukses! Rebooting...");
      break;
  }
#elif defined(ESP8266)
  ESPhttpUpdate.setLedPin(PIN_STATUS_LED, LOW);
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, remoteBinUrl);
  if (ret == HTTP_UPDATE_FAILED) {
    lastOtaStatusMessage = "Gagal Flash: " + ESPhttpUpdate.getLastErrorString();
    Serial.printf("[CLOUD-OTA] GAGAL (%d): %s\n",
                  ESPhttpUpdate.getLastError(),
                  ESPhttpUpdate.getLastErrorString().c_str());
  }
#endif

  digitalWrite(PIN_ALERT_LED, LOW);
  Serial.println("--------------------------------------------");
}

// ============================================================================
// Web Dashboard Diagnostik & Endpoint REST API
// ============================================================================
void handleRootDashboard() {
  String html = "<!DOCTYPE html><html lang='id'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>IoT Fleet & OTA Firmware Manager</title>";
  html += "<style>";
  html += "body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px;}";
  html += ".card{max-width:640px;margin:0 auto;background:#1e293b;border-radius:14px;padding:24px;box-shadow:0 10px 25px rgba(0,0,0,0.4);border:1px solid #334155;}";
  html += "h1{margin-top:0;font-size:1.4rem;color:#38bdf8;}";
  html += ".badge{display:inline-block;padding:4px 10px;border-radius:999px;background:#0284c7;color:#fff;font-weight:600;font-size:0.85rem;}";
  html += "table{width:100%;border-collapse:collapse;margin:18px 0;}";
  html += "td{padding:10px 8px;border-bottom:1px solid #334155;font-size:0.95rem;}";
  html += "td:first-child{color:#94a3b8;width:42%;}";
  html += ".btn{display:inline-block;background:#22c55e;color:#052e16;font-weight:700;border:none;padding:12px 20px;border-radius:8px;cursor:pointer;text-decoration:none;font-size:0.95rem;}";
  html += ".btn:hover{background:#4ade80;}";
  html += ".status{background:#0f172a;padding:12px;border-radius:8px;border-left:4px solid #38bdf8;margin:16px 0;font-family:monospace;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>IoT Lab 07 — OTA Firmware & Fleet Manager</h1>";
  html += "<p>Node Hostname: <strong>" + String(OTA_HOSTNAME) + "</strong> &nbsp; <span class='badge'>v" + String(FIRMWARE_VERSION) + "</span></p>";
  html += "<table>";
  html += "<tr><td>Target Arsitektur</td><td>" + String(BOARD_NAME) + "</td></tr>";
  html += "<tr><td>Versi Firmware Aktif</td><td><strong>v" + String(FIRMWARE_VERSION) + "</strong> (" + String(BUILD_TIMESTAMP) + ")</td></tr>";
  html += "<tr><td>Partisi Flash Aktif</td><td><code>" + getActivePartitionLabel() + "</code></td></tr>";
  html += "<tr><td>Versi Server Terakhir</td><td>" + latestRemoteVersion + "</td></tr>";
  html += "<tr><td>Alamat IP & RSSI</td><td>" + WiFi.localIP().toString() + " (" + String(WiFi.RSSI()) + " dBm)</td></tr>";
  html += "<tr><td>Free Heap RAM</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
  html += "<tr><td>Uptime Perangkat</td><td>" + String(millis() / 1000) + " detik</td></tr>";
  html += "<tr><td>Manifest Endpoint</td><td><code>" + String(VERSION_MANIFEST_URL) + "</code></td></tr>";
  html += "</table>";
  html += "<div class='status'><strong>Status OTA:</strong> " + lastOtaStatusMessage + "</div>";
  html += "<form action='/api/check-update' method='POST'>";
  html += "<button class='btn' type='submit'>Check for Update Now (Cloud Pull OTA)</button>";
  html += "</form>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleApiStatus() {
  String json = "{";
  json += "\"hostname\":\"" + String(OTA_HOSTNAME) + "\",";
  json += "\"board\":\"" + String(BOARD_NAME) + "\",";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"remote_version\":\"" + latestRemoteVersion + "\",";
  json += "\"active_partition\":\"" + getActivePartitionLabel() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"uptime_s\":" + String(millis() / 1000) + ",";
  json += "\"ota_status\":\"" + lastOtaStatusMessage + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiCheckUpdate() {
  manualCheckRequested = true;
  lastOtaStatusMessage = "Pemeriksaan OTA dijadwalkan dari Web Dashboard...";
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Memeriksa pembaruan firmware...");
}

// ============================================================================
// Pembacaan Tombol Fisik Pemicu Check OTA Manual
// ============================================================================
void checkPhysicalOtaButton() {
  int reading = digitalRead(PIN_OTA_BUTTON);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    static bool stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        Serial.println("[BUTTON] Tombol OTA ditekan! Memicu pemeriksaan Cloud OTA...");
        manualCheckRequested = true;
      }
    }
  }
  lastButtonReading = reading;
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(PIN_OTA_BUTTON, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_ALERT_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  digitalWrite(PIN_ALERT_LED, LOW);

  Serial.println("\n============================================");
  Serial.println("  IoT Lab - 07 OTA Firmware & Fleet Manager ");
  Serial.printf("  Board           : %s\n", BOARD_NAME);
  Serial.printf("  Firmware Aktif  : v%s (%s)\n", FIRMWARE_VERSION, BUILD_TIMESTAMP);
  Serial.printf("  Partisi Berjalan: %s\n", getActivePartitionLabel().c_str());
  Serial.println("============================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    delay(350);
    Serial.print(".");
  }

  digitalWrite(PIN_STATUS_LED, HIGH);
  Serial.println("\n[WIFI] Terhubung!");
  Serial.printf("[WIFI] Alamat IP Diagnostik : http://%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WIFI] Kekuatan Sinyal      : %d dBm\n", WiFi.RSSI());

  // Aktifkan Local Push OTA (ArduinoOTA)
  setupArduinoOTA();

  // Aktifkan Web Server Diagnostik & Trigger Manual
  server.on("/", HTTP_GET, handleRootDashboard);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/check-update", HTTP_POST, handleApiCheckUpdate);
  server.on("/api/check-update", HTTP_GET, handleApiCheckUpdate);
  server.begin();
  Serial.println("[HTTP] Web Fleet Diagnostik aktif di port 80.");
  Serial.println("[INFO] Tekan tombol fisik pada GPIO14 atau klik tombol di Web untuk cek Cloud OTA.");
}

void loop() {
  // 1. Layani paket Local Push OTA dari Arduino IDE
  ArduinoOTA.handle();

  // 2. Layani request HTTP Dashboard
  server.handleClient();

  // 3. Baca tombol fisik pemicu manual OTA
  checkPhysicalOtaButton();

  // 4. Jalankan Cloud Pull OTA jika diminta manual atau interval waktu tercapai
  unsigned long now = millis();
  if (manualCheckRequested || (now - lastAutoCheckTime >= AUTO_CHECK_INTERVAL_MS)) {
    manualCheckRequested = false;
    lastAutoCheckTime = now;
    performCloudOtaCheck();
  }

  // 5. Heartbeat indikator LED agar operator tahu firmware berjalan normal
  if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatTime = now;
    heartbeatLedState = !heartbeatLedState;
    digitalWrite(PIN_STATUS_LED, heartbeatLedState ? HIGH : LOW);
  }
}
