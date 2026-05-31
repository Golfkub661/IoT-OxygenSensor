#include <U8g2lib.h>
#include <Wire.h>
#include <LoRa.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// ─── OLED ──────────────────────────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, 16, 15, 4);

// ─── LoRa Pins ─────────────────────────────────────────────────────────────
#define LORA_SCK  5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS   18
#define LORA_RST  14
#define LORA_DIO0 26
#define LED_PIN   25

// ─── MQTT ──────────────────────────────────────────────────────────────────
const char* mqtt_server  = "broker.hivemq.com";
const int   mqtt_port    = 1883;
const char* topic_sensor = "sensor/oxygen";
const char* topic_relay1 = "control/relay/1";
const char* topic_relay2 = "control/relay/2";
const char* topic_relay3 = "control/relay/3";

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ─── State ─────────────────────────────────────────────────────────────────
String lastLoraMsg  = "";
String lastMqttCmd  = "";
bool   mqttOK       = false;
unsigned long lastMqttCmdTime = 0;

// ─── OLED Helper ───────────────────────────────────────────────────────────
void oledMsg(const char* line1, const char* line2 = "", const char* line3 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, line1);
  u8g2.drawStr(0, 26, line2);
  u8g2.drawStr(0, 42, line3);
  u8g2.sendBuffer();
}

// ─── ส่งคำสั่ง Relay ออก LoRa ─────────────────────────────────────────────
// รูปแบบ: "R1:ON" / "R1:OFF" / "R2:ON" ...
void sendRelayViaLora(const char* relayCmd) {
  LoRa.beginPacket();
  LoRa.print(relayCmd);
  LoRa.endPacket();
  Serial.println("[LoRa TX Relay] " + String(relayCmd));
  digitalWrite(LED_PIN, HIGH);
  delay(150);
  digitalWrite(LED_PIN, LOW);
}

// ─── MQTT Callback: รับคำสั่ง Relay จาก Dashboard ─────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();  // "ON" หรือ "OFF"

  String topicStr = String(topic);
  String relayNum = "";

  if      (topicStr == topic_relay1) relayNum = "R1";
  else if (topicStr == topic_relay2) relayNum = "R2";
  else if (topicStr == topic_relay3) relayNum = "R3";

  if (relayNum != "") {
    String cmd = relayNum + ":" + msg;   // เช่น "R1:ON"
    lastMqttCmd     = cmd;
    lastMqttCmdTime = millis();
    sendRelayViaLora(cmd.c_str());
    Serial.println("[MQTT RX] " + topicStr + " = " + msg);
  }
}

// ─── MQTT Connect/Reconnect ────────────────────────────────────────────────
void connectMQTT() {
  oledMsg("Connecting MQTT...", mqtt_server);
  int retries = 0;
  while (!mqttClient.connected() && retries < 5) {
    String clientId = "TTGO2-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      mqttClient.subscribe(topic_relay1);
      mqttClient.subscribe(topic_relay2);
      mqttClient.subscribe(topic_relay3);
      mqttOK = true;
      Serial.println("MQTT Connected");
    } else {
      retries++;
      Serial.printf("MQTT failed rc=%d retry %d/5\n", mqttClient.state(), retries);
      delay(3000);
    }
  }
  if (!mqttClient.connected()) {
    mqttOK = false;
    Serial.println("MQTT unavailable, continuing offline");
  }
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // OLED
  u8g2.begin();
  oledMsg("TTGO #2 Boot...");

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(920E6)) {
    oledMsg("LoRa FAILED!", "Check wiring");
    while (1);
  }
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa OK");

  // WiFiManager
  oledMsg("WiFi Connecting...", "AP: OxygenSensor2", "Pass: 12345678");
  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setAPCallback([](WiFiManager* wm) {
    oledMsg("WiFi Config Mode", "Connect to:", "OxygenSensor2 AP");
  });
  if (!wm.autoConnect("OxygenSensor2", "12345678")) {
    oledMsg("WiFi Failed!", "Restarting...");
    delay(2000);
    ESP.restart();
  }
  Serial.println("WiFi Connected: " + WiFi.localIP().toString());
  oledMsg("WiFi OK!", WiFi.localIP().toString().c_str());
  delay(1000);

  // MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();

  oledMsg("TTGO #2 Ready", "Waiting LoRa...");
}

// ─── Loop ──────────────────────────────────────────────────────────────────
void loop() {
  // ── MQTT keepalive / reconnect ─────────────────────────────────────────
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      mqttOK = false;
      connectMQTT();
    }
    mqttClient.loop();
    mqttOK = mqttClient.connected();
  } else {
    mqttOK = false;
  }

  // ── ① รับ LoRa packet จาก TTGO ตัวที่ 1 ─────────────────────────────
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String msg = "";
    while (LoRa.available()) msg += (char)LoRa.read();
    int rssi = LoRa.packetRssi();
    msg.trim();
    Serial.println("[LoRa RX] " + msg);

    // Parse JSON แล้วส่งต่อ MQTT
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, msg);

    if (!err) {
      lastLoraMsg = msg;

      // สร้าง MQTT payload (ขยาย key ให้ Django เข้าใจ)
      char payload[256];
      snprintf(payload, sizeof(payload),
        "{\"o2_pct\":%.2f,\"o2_mgl\":%.2f,\"temp_water\":%.1f,"
        "\"temp_air\":%.1f,\"humidity\":%.1f,"
        "\"relay1\":%s,\"relay2\":%s,\"relay3\":%s}",
        (float)doc["o2p"],
        (float)doc["o2m"],
        (float)doc["tw"],
        (float)doc["ta"],
        (float)doc["hum"],
        (int)doc["r1"] ? "true" : "false",
        (int)doc["r2"] ? "true" : "false",
        (int)doc["r3"] ? "true" : "false"
      );

      if (mqttOK) {
        mqttClient.publish(topic_sensor, payload);
        Serial.println("[MQTT TX] " + String(payload));
      }

      // แสดง OLED: ข้อมูลเซนเซอร์
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);

      u8g2.drawStr(0, 10, "TTGO #2 [Home]");
      u8g2.drawHLine(0, 13, 128);

      char buf[32];
      snprintf(buf, sizeof(buf), "O2 %.2f%% %.2fmg/L", (float)doc["o2p"], (float)doc["o2m"]);
      u8g2.drawStr(0, 26, buf);

      snprintf(buf, sizeof(buf), "Tw:%.1fC Ta:%.1fC", (float)doc["tw"], (float)doc["ta"]);
      u8g2.drawStr(0, 38, buf);

      snprintf(buf, sizeof(buf), "Hum:%.1f%% RSSI:%d", (float)doc["hum"], rssi);
      u8g2.drawStr(0, 50, buf);

      // MQTT status แถวล่างสุด
      snprintf(buf, sizeof(buf), "%s  MQTT:%s",
        WiFi.status() == WL_CONNECTED ? "WiFi:OK" : "WiFi:--",
        mqttOK ? "OK" : "--"
      );
      u8g2.drawStr(0, 62, buf);

      u8g2.sendBuffer();

    } else {
      Serial.println("[LoRa RX] JSON Error: " + msg);
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(0, 10, "TTGO #2 [Home]");
      u8g2.drawHLine(0, 13, 128);
      u8g2.drawStr(0, 30, "JSON Error!");
      u8g2.drawStr(0, 44, msg.substring(0, 20).c_str());
      u8g2.sendBuffer();
    }

    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }

  delay(10);
}
