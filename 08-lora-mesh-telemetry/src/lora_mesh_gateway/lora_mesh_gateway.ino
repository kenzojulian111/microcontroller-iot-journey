/**
 * ============================================================================
 * Proyek 08 : LoRa Mesh Environmental Telemetry — Gateway Utama (LoRa-to-Cloud)
 * Repo      : IoT_lab
 * Board     : ESP32 (Modul SPI LoRa SX1278/RFM95 + Wi-Fi Cloud Uplink)
 * Framework : Arduino IDE
 * ============================================================================
 * Fitur Utama:
 * 1. Menerima paket telemetri multi-hop dari seluruh Node Sensor & Repeater
 *    pada jaringan LoRa Mesh (`DST_ID = 0x00`).
 * 2. Mencatat kualitas sinyal radio fisik (`RSSI` dalam dBm dan `SNR` dalam dB)
 *    serta jumlah loncatan (`hopCount`) yang ditempuh paket.
 * 3. Deduplication Ring Buffer untuk mencegah duplikasi pengiriman ke Cloud
 *    ketika sebuah paket diterima dari beberapa Repeater berbeda.
 * 4. Meneruskan payload JSON ke Cloud / HTTP Webhook serta menyajikan
 *    tabel monitoring langsung di layar OLED SSD1306 & Serial Monitor.
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LoRa.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID           "Wokwi-GUEST"
  #define WIFI_PASSWORD       ""
  #define CLOUD_WEBHOOK_URL   ""
  #define GATEWAY_API_TOKEN   "token-rahasia-gateway-01"
#endif

// ============================================================================
// Konfigurasi Identitas Gateway & Parameter Radio LoRa
// (Harus identik dengan pengaturan di lora_mesh_node.ino)
// ============================================================================
const uint8_t GATEWAY_ID          = 0x00;
const long    LORA_FREQUENCY      = 915E6;
const int     LORA_SPREADING_FACT = 9;
const long    LORA_BANDWIDTH      = 125E3;
const int     LORA_CODING_RATE    = 5;
const uint8_t LORA_SYNC_WORD      = 0x34;

// Pin SPI LoRa SX1278 / RFM95 pada ESP32
const int PIN_LORA_SCK   = 18;
const int PIN_LORA_MISO  = 19;
const int PIN_LORA_MOSI  = 23;
const int PIN_LORA_CS    = 5;
const int PIN_LORA_RST   = 14;
const int PIN_LORA_DIO0  = 26;
const int PIN_STATUS_LED = 2;

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

const size_t DEDUP_BUFFER_SIZE = 32;
SeenEntry seenBuffer[DEDUP_BUFFER_SIZE];
size_t seenWriteIndex = 0;
uint32_t totalRxUniquePackets = 0;

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
// Penerusan Paket ke Cloud / HTTP Webhook
// ============================================================================
void forwardTelemetryToCloud(const MeshPacket& pkt, int rssi, float snr) {
  String envelopeJson = "{";
  envelopeJson += "\"gateway_id\":\"0x00\",";
  envelopeJson += "\"src_node\":" + String(pkt.srcId) + ",";
  envelopeJson += "\"via_node\":" + String(pkt.viaId) + ",";
  envelopeJson += "\"msg_id\":" + String(pkt.msgId) + ",";
  envelopeJson += "\"hops\":" + String(pkt.hopCount) + ",";
  envelopeJson += "\"rssi_dbm\":" + String(rssi) + ",";
  envelopeJson += "\"snr_db\":" + String(snr, 1) + ",";
  envelopeJson += "\"data\":" + pkt.payload;
  envelopeJson += "}";

  Serial.printf("[CLOUD-UPLINK] JSON Envelope: %s\n", envelopeJson.c_str());

  if (String(CLOUD_WEBHOOK_URL).length() == 0 || WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  if (http.begin(client, CLOUD_WEBHOOK_URL)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(GATEWAY_API_TOKEN));
    int code = http.POST(envelopeJson);
    Serial.printf("[CLOUD-UPLINK] HTTP POST Status: %d\n", code);
    http.end();
  }
}

// ============================================================================
// Tampilan OLED Gateway
// ============================================================================
void updateGatewayOled(const MeshPacket& pkt, int rssi, float snr) {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("LORA MESH GATEWAY");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.printf("From: 0x%02X Via: 0x%02X", pkt.srcId, pkt.viaId);

  display.setCursor(0, 26);
  display.printf("Msg : #%u (Hop %u/%u)", pkt.msgId, pkt.hopCount, pkt.maxHops);

  display.setCursor(0, 38);
  display.printf("RSSI: %d dBm SNR:%.1f", rssi, snr);

  display.setCursor(0, 52);
  display.printf("Total RX: %lu pkts", (unsigned long)totalRxUniquePackets);

  display.display();
}

// ============================================================================
// Memproses Paket Masuk (Dari Radio LoRa atau Simulasi Serial)
// ============================================================================
void processIncomingFrame(const String& rawFrame, int rssi, float snr) {
  MeshPacket pkt;
  if (!parseMeshPacket(rawFrame, pkt)) {
    Serial.printf("[GATEWAY-RX] Format paket tidak dikenali: %s\n", rawFrame.c_str());
    return;
  }

  if (isPacketAlreadySeen(pkt.srcId, pkt.msgId)) {
    Serial.printf("[GATEWAY-DEDUP] Paket duplikat (SRC:0x%02X, MSG:#%u via 0x%02X) diabaikan.\n",
                  pkt.srcId, pkt.msgId, pkt.viaId);
    return;
  }

  markPacketAsSeen(pkt.srcId, pkt.msgId);
  totalRxUniquePackets++;

  digitalWrite(PIN_STATUS_LED, HIGH);
  Serial.println("\n============================================================");
  Serial.printf("[GATEWAY-RX] Paket #%u Diterima!\n", pkt.msgId);
  Serial.printf("  Node Asal (SRC) : 0x%02X\n", pkt.srcId);
  Serial.printf("  Node Relay (VIA): 0x%02X (Jumlah Hop: %u dari maks %u)\n",
                pkt.viaId, pkt.hopCount, pkt.maxHops);
  Serial.printf("  Kualitas Radio  : RSSI = %d dBm | SNR = %.1f dB\n", rssi, snr);
  Serial.printf("  Payload Sensor  : %s\n", pkt.payload.c_str());

  updateGatewayOled(pkt, rssi, snr);
  forwardTelemetryToCloud(pkt, rssi, snr);
  Serial.println("============================================================");
  digitalWrite(PIN_STATUS_LED, LOW);
}

// ============================================================================
// Setup & Loop Utama
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  Wire.begin(21, 22);

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LORA MESH GATEWAY");
    display.println("Menghubungkan WiFi..");
    display.display();
  }

  Serial.println("\n============================================================");
  Serial.println("  IoT Lab - 08 LoRa Mesh Environmental Telemetry (Gateway)  ");
  Serial.println("============================================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Menghubungkan ke SSID: %s ", WIFI_SSID);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < 8000)) {
    delay(350);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Terhubung! IP Gateway: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WIFI] Berjalan dalam mode Offline Serial Gateway.");
  }

  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);

  if (LoRa.begin(LORA_FREQUENCY)) {
    loraHardwareOk = true;
    LoRa.setSpreadingFactor(LORA_SPREADING_FACT);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();
    Serial.println("[LORA] Radio Gateway siap mendengarkan paket pada 915 MHz (SF9)!");
  } else {
    loraHardwareOk = false;
    Serial.println("[LORA] Modul SPI SX127x tidak terdeteksi. Anda dapat menyimulasikan paket masuk");
    Serial.println("       dengan mengirim string frame ke Serial Monitor, contoh:");
    Serial.println("       101|1|0|2|1|3|{\"t\":26.8,\"h\":81.0,\"dist\":85.0,\"water\":215.0,\"alert\":true}");
  }
}

void loop() {
  // 1. Baca paket dari modul fisik LoRa SX127x
  if (loraHardwareOk) {
    int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
      String rawFrame = "";
      while (LoRa.available()) {
        rawFrame += (char)LoRa.read();
      }
      processIncomingFrame(rawFrame, LoRa.packetRssi(), LoRa.packetSnr());
    }
  }

  // 2. Dukungan injeksi frame via Serial Monitor untuk pengujian protokol Mesh tanpa RF
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      processIncomingFrame(line, -78, 8.5f);
    }
  }
}
