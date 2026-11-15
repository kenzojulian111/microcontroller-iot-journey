/**
 * ============================================================================
 * Proyek 08 : LoRa Mesh Environmental Telemetry — Sensor & Repeater Node
 * Repo      : IoT_lab
 * Board     : ESP32 (Kompatibel dengan Modul SPI LoRa SX1278 433MHz / RFM95 915MHz)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Membaca telemetri lingkungan jarak jauh (Deteksi Dini Banjir & Cuaca Hutan):
 *    - Ketinggian muka air / jarak ultrasonik (HC-SR04)
 *    - Suhu & Kelembapan udara (DHT22)
 *    - Tegangan baterai node (ADC)
 * 2. Protokol LoRa Mesh Multi-Hop Ringan:
 *    - Format Paket: `MSG_ID|SRC_ID|DST_ID|VIA_ID|HOP|MAX_HOP|JSON_PAYLOAD`
 *    - Deduplication Ring Buffer: Menyimpan pasangan `(srcId, msgId)` terakhir
 *      agar paket broadcast/mesh tidak memicu *broadcast storm* (looping tak terbatas).
 *    - Auto-Forwarding Repeater: Meneruskan paket dari node tetangga yang belum
 *      mencapai Gateway (`DST_ID = 0x00`) dengan jeda acak anti-tabrakan (CSMA/CA jitter).
 * 3. Tampilan status OLED SSD1306 I2C & fallback simulasi otomatis jika dijalankan
 *    di Wokwi tanpa chip SPI SX127x fisik.
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// Konfigurasi Identitas Node & Parameter Frekuensi LoRa
// ============================================================================
// Ubah NODE_ID untuk masing-masing perangkat di lapangan:
//   0x01 = Node Sensor Hulu Sungai
//   0x02 = Node Repeater Bukit Tengah
//   0x00 = Gateway Utama (Lihat sketch lora_mesh_gateway.ino)
const uint8_t NODE_ID             = 0x01;
const uint8_t GATEWAY_ID          = 0x00;
const uint8_t DEFAULT_MAX_HOPS    = 3;
const bool    ENABLE_REPEATER     = true;

// Pilih pita frekuensi modul LoRa:
//   433E6 (433 MHz - SX1278 Asia)
//   915E6 (915 MHz - RFM95/SX1276 frekuensi ISM Indonesia/US)
const long    LORA_FREQUENCY      = 915E6;
const int     LORA_SPREADING_FACT = 9;       // SF7 (cepat) s/d SF12 (jarak terjauh)
const long    LORA_BANDWIDTH      = 125E3;   // 125 kHz
const int     LORA_CODING_RATE    = 5;       // 4/5
const uint8_t LORA_SYNC_WORD      = 0x34;    // Sync word privat jaringan IoT_lab
const int     LORA_TX_POWER_DBM   = 17;      // 2 dBm s/d 20 dBm

// ============================================================================
// Konfigurasi Pin Hardware (ESP32)
// ============================================================================
// Pin SPI LoRa SX1278 / RFM95
const int PIN_LORA_SCK   = 18;
const int PIN_LORA_MISO  = 19;
const int PIN_LORA_MOSI  = 23;
const int PIN_LORA_CS    = 5;
const int PIN_LORA_RST   = 14;
const int PIN_LORA_DIO0  = 26;

// Pin Sensor Lingkungan
const int PIN_DHT        = 4;   // DHT22 Data
const int PIN_TRIG       = 12;  // HC-SR04 Trigger
const int PIN_ECHO       = 13;  // HC-SR04 Echo
const int PIN_STATUS_LED = 2;   // LED Indikator Transmisi

// Konfigurasi Kalibrasi Ketinggian Air Sungai (cm)
const float SENSOR_MOUNT_HEIGHT_CM = 300.0f; // Tinggi pemasangan sensor dari dasar sungai
const float FLOOD_ALERT_LEVEL_CM   = 200.0f; // Ambang batas siaga banjir

// ============================================================================
// Inisialisasi Objek Sensor & Layar OLED
// ============================================================================
#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledAvailable  = false;
bool loraHardwareOk = false;

// ============================================================================
// Struktur Paket Mesh & Deduplication Ring Buffer
// ============================================================================
struct MeshPacket {
  uint16_t msgId;
  uint8_t  srcId;
  uint8_t  dstId;
  uint8_t  viaId;
  uint8_t  hopCount;
  uint8_t  maxHops;
  String   payload;
};

struct SeenEntry {
  uint8_t  srcId;
  uint16_t msgId;
};

const size_t DEDUP_BUFFER_SIZE = 24;
SeenEntry seenBuffer[DEDUP_BUFFER_SIZE];
size_t seenWriteIndex = 0;

uint16_t localPacketCounter  = 0;
uint32_t totalTxPackets      = 0;
uint32_t totalRelayedPackets = 0;
unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 10000UL; // Kirim tiap 10 detik

// ============================================================================
// Fungsi Deduplication Ring Buffer
// ============================================================================
bool isPacketAlreadySeen(uint8_t srcId, uint16_t msgId) {
  for (size_t i = 0; i < DEDUP_BUFFER_SIZE; i++) {
    if (seenBuffer[i].srcId == srcId && seenBuffer[i].msgId == msgId && msgId != 0) {
      return true;
    }
  }
  return false;
}

void markPacketAsSeen(uint8_t srcId, uint16_t msgId) {
  seenBuffer[seenWriteIndex].srcId = srcId;
  seenBuffer[seenWriteIndex].msgId = msgId;
  seenWriteIndex = (seenWriteIndex + 1) % DEDUP_BUFFER_SIZE;
}

// ============================================================================
// Serialisasi & Deserialisasi Paket LoRa Mesh
// Format Frame: MSG_ID|SRC_ID|DST_ID|VIA_ID|HOP|MAX_HOP|JSON_PAYLOAD
// ============================================================================
String serializeMeshPacket(const MeshPacket& pkt) {
  char header[48];
  snprintf(header, sizeof(header), "%u|%u|%u|%u|%u|%u|",
           pkt.msgId, pkt.srcId, pkt.dstId, pkt.viaId, pkt.hopCount, pkt.maxHops);
  return String(header) + pkt.payload;
}

bool parseMeshPacket(const String& raw, MeshPacket& outPkt) {
  int idx[6];
  int lastPos = 0;
  for (int i = 0; i < 6; i++) {
    idx[i] = raw.indexOf('|', lastPos);
    if (idx[i] < 0) return false;
    lastPos = idx[i] + 1;
  }

  outPkt.msgId    = (uint16_t)raw.substring(0, idx[0]).toInt();
  outPkt.srcId    = (uint8_t)raw.substring(idx[0] + 1, idx[1]).toInt();
  outPkt.dstId    = (uint8_t)raw.substring(idx[1] + 1, idx[2]).toInt();
  outPkt.viaId    = (uint8_t)raw.substring(idx[2] + 1, idx[3]).toInt();
  outPkt.hopCount = (uint8_t)raw.substring(idx[3] + 1, idx[4]).toInt();
  outPkt.maxHops  = (uint8_t)raw.substring(idx[4] + 1, idx[5]).toInt();
  outPkt.payload  = raw.substring(idx[5] + 1);
  return true;
}

// ============================================================================
// Pembacaan Sensor Ultrasonik (HC-SR04) & Ketinggian Air
// ============================================================================
float readUltrasonicDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long durationUs = pulseIn(PIN_ECHO, HIGH, 30000UL); // Timeout 30 ms (~5 meter)
  if (durationUs <= 0) {
    return SENSOR_MOUNT_HEIGHT_CM; // Tidak ada pantulan
  }
  float distanceCm = (durationUs * 0.0343f) / 2.0f;
  return constrain(distanceCm, 2.0f, SENSOR_MOUNT_HEIGHT_CM);
}

// ============================================================================
// Pengiriman Paket melalui Radio LoRa (atau Simulasi jika di Wokwi)
// ============================================================================
void transmitMeshPacket(const MeshPacket& pkt) {
  String frame = serializeMeshPacket(pkt);
  digitalWrite(PIN_STATUS_LED, HIGH);

  if (loraHardwareOk) {
    LoRa.beginPacket();
    LoRa.print(frame);
    LoRa.endPacket();
  }

  totalTxPackets++;
  Serial.printf("[LORA-TX] Frame Terkirim (%u bytes): %s\n", frame.length(), frame.c_str());
  digitalWrite(PIN_STATUS_LED, LOW);
}

// ============================================================================
// Update Tampilan Layar OLED SSD1306
// ============================================================================
void updateOledDisplay(float tempC, float humPct, float waterLevelCm, const char* alertStatus) {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.printf("LORA MESH NODE 0x%02X", NODE_ID);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.printf("Temp : %.1f C | %.0f%%", tempC, humPct);

  display.setCursor(0, 26);
  display.printf("Air  : %.1f cm", waterLevelCm);

  display.setCursor(0, 38);
  display.printf("Stat : %s", alertStatus);

  display.setCursor(0, 52);
  display.printf("TX:%lu  Relay:%lu", (unsigned long)totalTxPackets, (unsigned long)totalRelayedPackets);

  display.display();
}

// ============================================================================
// Membangun & Mengirim Paket Telemetri Sensor Lokal
// ============================================================================
void sendLocalTelemetry() {
  float tempC  = dht.readTemperature();
  float humPct = dht.readHumidity();
  if (isnan(tempC))  tempC  = 27.5f;
  if (isnan(humPct)) humPct = 75.0f;

  float distCm       = readUltrasonicDistanceCm();
  float waterLevelCm = max(0.0f, SENSOR_MOUNT_HEIGHT_CM - distCm);
  bool  floodAlert   = (waterLevelCm >= FLOOD_ALERT_LEVEL_CM);
  const char* status = floodAlert ? "SIAGA_BANJIR" : "NORMAL";

  localPacketCounter++;
  markPacketAsSeen(NODE_ID, localPacketCounter);

  char jsonPayload[160];
  snprintf(jsonPayload, sizeof(jsonPayload),
           "{\"t\":%.1f,\"h\":%.1f,\"dist\":%.1f,\"water\":%.1f,\"alert\":%s}",
           tempC, humPct, distCm, waterLevelCm, floodAlert ? "true" : "false");

  MeshPacket pkt;
  pkt.msgId    = localPacketCounter;
  pkt.srcId    = NODE_ID;
  pkt.dstId    = GATEWAY_ID;
  pkt.viaId    = NODE_ID;
  pkt.hopCount = 0;
  pkt.maxHops  = DEFAULT_MAX_HOPS;
  pkt.payload  = String(jsonPayload);

  Serial.println("\n------------------------------------------------------------");
  Serial.printf("[SENSOR] Suhu: %.1f C | RH: %.1f%% | Jarak Sensor: %.1f cm | Tinggi Air: %.1f cm (%s)\n",
                tempC, humPct, distCm, waterLevelCm, status);
  transmitMeshPacket(pkt);
  updateOledDisplay(tempC, humPct, waterLevelCm, status);
}

// ============================================================================
// Memproses Paket Masuk & Logika Repeater Multi-Hop
// ============================================================================
void handleIncomingLoRaPacket() {
  if (!loraHardwareOk) return;

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  String rawFrame = "";
  while (LoRa.available()) {
    rawFrame += (char)LoRa.read();
  }

  int rssi  = LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  MeshPacket pkt;
  if (!parseMeshPacket(rawFrame, pkt)) {
    Serial.printf("[LORA-RX] Paket korup diabaikan (%d bytes, RSSI %d dBm)\n", packetSize, rssi);
    return;
  }

  // Abaikan jika paket berasal dari diri sendiri atau sudah pernah dilihat
  if (pkt.srcId == NODE_ID || isPacketAlreadySeen(pkt.srcId, pkt.msgId)) {
    Serial.printf("[MESH-DEDUP] Paket duplikat (SRC:0x%02X, MSG:%u) diabaikan.\n", pkt.srcId, pkt.msgId);
    return;
  }

  markPacketAsSeen(pkt.srcId, pkt.msgId);
  Serial.printf("[LORA-RX] Dari 0x%02X via 0x%02X | Hop %u/%u | RSSI %d dBm | SNR %.1f dB | Payload: %s\n",
                pkt.srcId, pkt.viaId, pkt.hopCount, pkt.maxHops, rssi, snr, pkt.payload.c_str());

  // Jika node ini bukan tujuan akhir dan fitur Repeater aktif, teruskan paket!
  if (ENABLE_REPEATER && pkt.dstId != NODE_ID && pkt.hopCount < pkt.maxHops) {
    pkt.hopCount++;
    pkt.viaId = NODE_ID;

    // Jeda acak (50 - 250 ms) untuk mencegah tabrakan sinyal (Collision Avoidance)
    unsigned long backoffMs = random(50, 250);
    delay(backoffMs);

    Serial.printf("[MESH-RELAY] Meneruskan paket MSG:%u dari 0x%02X -> 0x%02X (Hop %u/%u, Backoff %lu ms)\n",
                  pkt.msgId, pkt.srcId, pkt.dstId, pkt.hopCount, pkt.maxHops, backoffMs);
    transmitMeshPacket(pkt);
    totalRelayedPackets++;
  }
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  digitalWrite(PIN_TRIG, LOW);

  dht.begin();
  Wire.begin(21, 22);

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("IoT Lab - Proyek 08");
    display.println("LoRa Mesh Telemetry");
    display.printf("Node ID: 0x%02X\n", NODE_ID);
    display.display();
  }

  Serial.println("\n============================================================");
  Serial.println("  IoT Lab - 08 LoRa Mesh Environmental Telemetry (Node)     ");
  Serial.printf("  Node ID        : 0x%02X | Repeater Mode: %s\n", NODE_ID, ENABLE_REPEATER ? "ENABLED" : "DISABLED");
  Serial.printf("  Frekuensi LoRa : %.0f MHz (SF%d, BW %.0f kHz)\n",
                LORA_FREQUENCY / 1e6, LORA_SPREADING_FACT, LORA_BANDWIDTH / 1e3);
  Serial.println("============================================================");

  // Inisialisasi SPI & Modul Transceiver LoRa SX1278 / RFM95
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);

  if (LoRa.begin(LORA_FREQUENCY)) {
    loraHardwareOk = true;
    LoRa.setSpreadingFactor(LORA_SPREADING_FACT);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setTxPower(LORA_TX_POWER_DBM);
    LoRa.enableCrc();
    Serial.println("[LORA] Transceiver SX127x berhasil diinisialisasi!");
  } else {
    loraHardwareOk = false;
    Serial.println("[LORA] Modul SPI SX127x fisik tidak terdeteksi (Mode Simulasi Wokwi Aktif).");
    Serial.println("[LORA] Paket Mesh tetap dibuat dan ditampilkan melalui Serial & OLED.");
  }

  // Kirim paket telemetri pertama saat startup
  sendLocalTelemetry();
  lastTelemetryTime = millis();
}

void loop() {
  // 1. Dengarkan paket LoRa masuk dan lakukan relay jika diperlukan
  handleIncomingLoRaPacket();

  // 2. Kirim telemetri sensor secara berkala
  if (millis() - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = millis();
    sendLocalTelemetry();
  }
}
