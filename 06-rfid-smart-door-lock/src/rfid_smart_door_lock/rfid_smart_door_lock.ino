/**
 * ============================================================================
 * Proyek 06 : RFID Smart Door Lock with Access Logs & Offline Cache
 * Repo      : IoT_lab
 * Board     : ESP32 (SPI MFRC522 + NTP Time Sync + HTTP REST API Client)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Membaca UID Kartu / Keyfob RFID 13.56 MHz menggunakan modul MFRC522 (SPI).
 * 2. Sinkronisasi waktu otomatis dengan Server NTP (`pool.ntp.org`, WIB UTC+7)
 *    untuk mencatat timestamp format ISO-8601 (`YYYY-MM-DDTHH:MM:SS+07:00`).
 * 3. Validasi kartu ke Server REST API (HTTP POST JSON) sekaligus mengirimkan
 *    log akses (`uid`, `device_id`, `timestamp`, `status`, `user_name`).
 * 4. Dilengkapi Daftar Kartu Lokal Fallback (Offline Cache) di memori Flash/RAM
 *    sehingga pintu tetap dapat dibuka oleh kartu terdaftar meskipun koneksi
 *    Wi-Fi atau Server Backend sedang terputus!
 * 5. Mengendalikan Relay Solenoid Door Lock (otomatis mengunci kembali setelah
 *    4 detik secara non-blocking), LED Hijau (Akses Diterima), LED Merah
 *    (Akses Ditolak), dan pola nada Buzzer aktif/pasif.
 * 6. Menyediakan 2 Tombol Simulasi Tap Kartu pada GPIO32 (Kartu Master Valid)
 *    dan GPIO33 (Kartu Asing Ditolak) untuk kemudahan pengujian di Wokwi.
 *
 * Library yang dibutuhkan di Arduino IDE:
 * - MFRC522 (oleh GithubCommunity / Miguel Balboa)
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// Gunakan secrets.h jika tersedia, atau fallback ke konfigurasi simulasi Wokwi
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID        "Wokwi-GUEST"
  #define WIFI_PASSWORD    ""
  #define API_ENDPOINT_URL "http://192.168.1.100:8000/api/v1/access-log"
  #define API_SECRET_KEY   "iotlab-doorlock-secret-key-2026"
  #define DEVICE_ID        "door_lab_utama"
#endif

// ============================================================================
// Konfigurasi Pin Hardware ESP32
// ============================================================================
// Pin SPI Modul RFID RC522 (Menggunakan VSPI Default ESP32: SCK=18, MISO=19, MOSI=23)
const int PIN_RFID_SS         = 5;   // GPIO5  - SDA / SS / CS pada RC522
const int PIN_RFID_RST        = 22;  // GPIO22 - RST pada RC522

// Pin Aktuator & Indikator
const int PIN_RELAY_LOCK      = 26;  // GPIO26 - Relay Penggerak Solenoid Door Lock 12V
const int PIN_LED_GREEN       = 25;  // GPIO25 - LED Hijau (Indikator Akses Diterima)
const int PIN_LED_RED         = 27;  // GPIO27 - LED Merah (Indikator Akses Ditolak)
const int PIN_BUZZER          = 14;  // GPIO14 - Buzzer Feedback Audio

// Pin Tombol Simulasi Wokwi (Menguji Tap Kartu Valid vs Kartu Asing tanpa modul fisik)
const int PIN_BTN_SIM_VALID   = 32;  // GPIO32 - Simulasi Tap Kartu Master ("A1:B2:C3:D4")
const int PIN_BTN_SIM_DENIED  = 33;  // GPIO33 - Simulasi Tap Kartu Asing ("DE:AD:BE:EF")

// Ubah ke `true` jika modul relay bersifat Active-LOW
const bool RELAY_ACTIVE_LOW   = false;

// Durasi pintu terbuka sebelum Solenoid mengunci kembali secara otomatis
const unsigned long UNLOCK_DURATION_MS = 4000;

// Konfigurasi Waktu NTP (WIB = UTC+7 = 7 * 3600 detik)
const char* NTP_SERVER_1      = "pool.ntp.org";
const char* NTP_SERVER_2      = "time.nist.gov";
const long  GMT_OFFSET_SEC    = 7 * 3600;
const int   DAYLIGHT_OFFSET   = 0;

// ============================================================================
// Database Kartu Lokal Fallback (Offline Cache)
// ============================================================================
struct AuthorizedCard {
  const char* uid;
  const char* ownerName;
  const char* role;
};

// Tambahkan UID kartu RFID kamu di sini (format HEX huruf besar dipisah titik dua)
const AuthorizedCard LOCAL_CARD_CACHE[] = {
  { "A1:B2:C3:D4", "Budi Santoso (Admin Lab)", "ADMIN"   },
  { "04:5A:8E:92", "Siti Aminah (Asisten)",    "STAFF"   },
  { "73:1F:44:0C", "Rizky Pratama (Teknisi)",  "MEMBER"  }
};
const size_t LOCAL_CARD_COUNT = sizeof(LOCAL_CARD_CACHE) / sizeof(LOCAL_CARD_CACHE[0]);

// ============================================================================
// Objek Global & State Sistem
// ============================================================================
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);

bool doorUnlocked          = false;
unsigned long unlockStartMs = 0;

bool lastBtnValidState     = HIGH;
bool lastBtnDeniedState    = HIGH;
unsigned long lastBtnMs    = 0;

// ============================================================================
// Fungsi Helper Kontrol Solenoid Relay & Audio Buzzer
// ============================================================================
void setSolenoidLock(bool unlock) {
  doorUnlocked = unlock;
  bool pinLevel = RELAY_ACTIVE_LOW ? !unlock : unlock;
  digitalWrite(PIN_RELAY_LOCK, pinLevel ? HIGH : LOW);

  if (unlock) {
    unlockStartMs = millis();
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
    Serial.println("[SOLENOID] Pintu TERBUKA (Solenoid Energized).");
  } else {
    digitalWrite(PIN_LED_GREEN, LOW);
    Serial.println("[SOLENOID] Pintu TERKUNCI kembali (Auto-Lock).");
  }
}

void playBeepGranted() {
  // Nada 2-beep pendek frekuensi tinggi (Akses Diterima)
  tone(PIN_BUZZER, 2400, 110);
  delay(140);
  tone(PIN_BUZZER, 3200, 160);
  delay(170);
  noTone(PIN_BUZZER);
}

void playBeepDenied() {
  // Nada panjang rendah 2 kali (Akses Ditolak / Alarm Peringatan)
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_LED_RED, HIGH);
    tone(PIN_BUZZER, 450, 280);
    delay(320);
    digitalWrite(PIN_LED_RED, LOW);
    delay(80);
  }
  noTone(PIN_BUZZER);
}

// ============================================================================
// Pembacaan Waktu NTP (Format ISO-8601)
// ============================================================================
String getIso8601Timestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    // Fallback jika NTP belum sinkron: gunakan uptime detik
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "UPTIME-%lus", millis() / 1000UL);
    return String(fallback);
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+07:00", &timeinfo);
  return String(buf);
}

// ============================================================================
// Pencarian Kartu di Offline Cache Lokal
// ============================================================================
const AuthorizedCard* findCardInLocalCache(const String& uid) {
  for (size_t i = 0; i < LOCAL_CARD_COUNT; i++) {
    if (uid.equalsIgnoreCase(LOCAL_CARD_CACHE[i].uid)) {
      return &LOCAL_CARD_CACHE[i];
    }
  }
  return nullptr;
}

// ============================================================================
// Validasi & Pencatatan Log Akses ke Server REST API
// ============================================================================
bool sendAccessLogAndValidate(const String& uid,
                              const String& timestamp,
                              bool localGranted,
                              const char* localOwner,
                              String& outServerMessage) {
  if (WiFi.status() != WL_CONNECTED) {
    outServerMessage = "OFFLINE_CACHE (Wi-Fi Terputus)";
    return localGranted;
  }

  // Bangun payload JSON untuk dikirim ke REST API Backend
  char jsonPayload[320];
  snprintf(
    jsonPayload,
    sizeof(jsonPayload),
    "{\"uid\":\"%s\",\"device_id\":\"%s\",\"timestamp\":\"%s\","
    "\"local_decision\":\"%s\",\"user_name\":\"%s\"}",
    uid.c_str(),
    DEVICE_ID,
    timestamp.c_str(),
    localGranted ? "GRANTED" : "DENIED",
    localOwner ? localOwner : "Unknown"
  );

  Serial.printf("[API TX] POST %s\n", API_ENDPOINT_URL);
  Serial.printf("[API TX] Payload: %s\n", jsonPayload);

  // Jika menggunakan IP contoh default di simulator Wokwi, langsung gunakan hasil Offline Cache
  if (String(API_ENDPOINT_URL).indexOf("192.168.1.100") >= 0) {
    outServerMessage = "LOCAL_CACHE (Mode Demo / Server Lokal Belum Diatur)";
    return localGranted;
  }

  HTTPClient http;
  http.begin(API_ENDPOINT_URL);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_SECRET_KEY);

  int httpCode = http.POST(jsonPayload);
  if (httpCode >= 200 && httpCode < 300) {
    String resp = http.getString();
    resp.trim();
    Serial.printf("[API RX] HTTP %d: %s\n", httpCode, resp.c_str());
    http.end();

    // Jika server mengembalikan {"authorized":true} atau {"authorized":false},
    // keputusan server dapat meng-override atau memperkuat cache lokal
    if (resp.indexOf("\"authorized\":true") >= 0 || resp.indexOf("\"authorized\": true") >= 0) {
      outServerMessage = "SERVER_AUTHORIZED";
      return true;
    } else if (resp.indexOf("\"authorized\":false") >= 0 || resp.indexOf("\"authorized\": false") >= 0) {
      outServerMessage = "SERVER_DENIED";
      return false;
    }

    outServerMessage = "LOGGED_OK";
    return localGranted;
  } else {
    Serial.printf("[API WARN] Gagal menghubungi REST API (HTTP %d). Menggunakan Fallback Offline Cache.\n", httpCode);
    http.end();
    outServerMessage = "OFFLINE_CACHE_FALLBACK";
    return localGranted;
  }
}

// ============================================================================
// Proses Utama Saat Kartu RFID Terdeteksi (Fisik maupun Simulasi)
// ============================================================================
void processCardTap(const String& uid, const char* sourceLabel) {
  String timestamp = getIso8601Timestamp();
  const AuthorizedCard* matchedCard = findCardInLocalCache(uid);
  bool localGranted = (matchedCard != nullptr);
  const char* ownerName = localGranted ? matchedCard->ownerName : "Kartu Tidak Dikenal";

  Serial.println("\n------------------------------------------------------------");
  Serial.printf("[RFID TAP] Sumber    : %s\n", sourceLabel);
  Serial.printf("[RFID TAP] UID Kartu : %s\n", uid.c_str());
  Serial.printf("[RFID TAP] Waktu NTP : %s\n", timestamp.c_str());
  Serial.printf("[CACHE]    Pemilik   : %s (%s)\n",
                ownerName, localGranted ? "TERDAFTAR" : "TIDAK TERDAFTAR");

  String authSource;
  bool finalGranted = sendAccessLogAndValidate(uid, timestamp, localGranted, ownerName, authSource);

  if (finalGranted) {
    Serial.printf("[ACCESS]   DITERIMA (%s) -> Membuka kunci pintu selama %lu detik.\n",
                  authSource.c_str(), UNLOCK_DURATION_MS / 1000UL);
    setSolenoidLock(true);
    playBeepGranted();
  } else {
    Serial.printf("[ACCESS]   DITOLAK (%s) -> Pintu tetap terkunci!\n", authSource.c_str());
    setSolenoidLock(false);
    playBeepDenied();
  }
  Serial.println("------------------------------------------------------------");
}

// ============================================================================
// Pembacaan Modul Fisik RFID RC522 (SPI) & Tombol Simulasi Wokwi
// ============================================================================
void checkRfidReader() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Konversi byte array UID menjadi format string "AA:BB:CC:DD"
  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (i > 0) uidStr += ":";
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  // Hentikan enkripsi & komunikasi pada kartu agar tidak terbaca berulang-ulang
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  processCardTap(uidStr, "Sensor Fisik RC522 (SPI)");
}

void checkSimulationButtons() {
  unsigned long now = millis();
  if (now - lastBtnMs < 60) return;

  bool validBtn  = digitalRead(PIN_BTN_SIM_VALID);
  bool deniedBtn = digitalRead(PIN_BTN_SIM_DENIED);

  if (validBtn == LOW && lastBtnValidState == HIGH) {
    lastBtnMs = now;
    processCardTap("A1:B2:C3:D4", "Tombol Simulasi (Kartu Master Valid)");
  }
  if (deniedBtn == LOW && lastBtnDeniedState == HIGH) {
    lastBtnMs = now;
    processCardTap("DE:AD:BE:EF", "Tombol Simulasi (Kartu Asing / Ditolak)");
  }

  lastBtnValidState  = validBtn;
  lastBtnDeniedState = deniedBtn;
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n============================================================");
  Serial.println("  IoT Lab - 06 RFID Smart Door Lock with Access Logs        ");
  Serial.printf("  Device ID : %s\n", DEVICE_ID);
  Serial.println("============================================================");

  // Inisialisasi Pin Output & Input
  pinMode(PIN_RELAY_LOCK, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_SIM_VALID, INPUT_PULLUP);
  pinMode(PIN_BTN_SIM_DENIED, INPUT_PULLUP);

  // Pastikan pintu dalam kondisi terkunci saat pertama kali menyala
  setSolenoidLock(false);

  // Inisialisasi Bus SPI & Modul MFRC522
  SPI.begin();
  mfrc522.PCD_Init();
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("[RC522] Info: Modul fisik RC522 tidak terdeteksi di bus SPI.");
    Serial.println("[RC522] Gunakan tombol hijau (GPIO32) / merah (GPIO33) untuk simulasi tap kartu di Wokwi.");
  } else {
    Serial.printf("[RC522] Modul MFRC522 siap! (Firmware Version: 0x%02X)\n", version);
  }

  // Koneksi ke Jaringan Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 10000) {
    delay(350);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Terhubung!");
    Serial.print("[WIFI] Alamat IP : ");
    Serial.println(WiFi.localIP());

    // Sinkronisasi Waktu NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER_1, NTP_SERVER_2);
    Serial.print("[NTP]  Menyinkronkan waktu dengan server NTP...");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 4000)) {
      Serial.printf(" Berhasil! Waktu sekarang: %s\n", getIso8601Timestamp().c_str());
    } else {
      Serial.println(" Belum sinkron (akan disinkronkan di background).");
    }
  } else {
    Serial.println("\n[WIFI] Offline! Sistem berjalan menggunakan Offline Card Cache.");
  }

  Serial.println("[READY] Silakan tempelkan kartu RFID ke reader atau tekan tombol simulasi.");
}

void loop() {
  // 1. Cek pembacaan kartu dari sensor RFID RC522 (SPI) maupun tombol simulasi Wokwi
  checkRfidReader();
  checkSimulationButtons();

  // 2. Auto-Lock Non-Blocking: Kunci kembali pintu setelah UNLOCK_DURATION_MS berlalu
  if (doorUnlocked && (millis() - unlockStartMs >= UNLOCK_DURATION_MS)) {
    setSolenoidLock(false);
  }
}
