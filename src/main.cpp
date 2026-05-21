#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DevOxygen.h"
#include "DevTempHumidity.h"

// ========================================
// MQTT
// ========================================
const char* mqtt_server  = "broker.hivemq.com";
const int   mqtt_port    = 1883;
const char* topic_sensor = "sensor/oxygen";
const char* topic_relay1 = "control/relay/1";
const char* topic_relay2 = "control/relay/2";
const char* topic_relay3 = "control/relay/3";

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ========================================
// Relay GPIO (Active Low)
// ========================================
#define RLY1_PIN 17
#define RLY2_PIN 16
#define RLY3_PIN  4

bool relay1State = false;
bool relay2State = false;
bool relay3State = false;

// ========================================
// BOOT button (GPIO0) สำหรับ reset WiFi
// ========================================
#define BOOT_PIN 0

// ========================================
// OLED
// ========================================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool    oled_available = false;
uint8_t oledPage       = 0;

// ========================================
// Sensors
// ========================================
DevOxygen       o2Sensor(&Serial, 1);
DevTempHumidity thSensor(&Serial, 2);

// ========================================
// แสดงข้อความบน OLED (helper)
// ========================================
void oledPrint(const char* line1, const char* line2 = "", const char* line3 = "") {
  if (!oled_available) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);  display.println(line1);
  display.setCursor(0, 20); display.println(line2);
  display.setCursor(0, 36); display.println(line3);
  display.display();
}

// ========================================
// Relay
// ========================================
void setRelay(int pin, bool state) {
  digitalWrite(pin, state ? LOW : HIGH);
}

// ========================================
// Publish sensor + relay state
// ========================================
void publishStatus() {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"o2_pct\":%.2f,\"o2_mgl\":%.2f,\"temp_water\":%.1f,"
    "\"temp_air\":%.1f,\"humidity\":%.1f,"
    "\"relay1\":%s,\"relay2\":%s,\"relay3\":%s}",
    o2Sensor.getOxygenPct(),
    o2Sensor.getOxygenMgl(),
    o2Sensor.getTemperature(),
    thSensor.getTemperature(),
    thSensor.getHumidity(),
    relay1State ? "true" : "false",
    relay2State ? "true" : "false",
    relay3State ? "true" : "false"
  );
  mqttClient.publish(topic_sensor, payload);
}

// ========================================
// MQTT callback
// ========================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();
  bool state = (msg == "ON");

  if (String(topic) == topic_relay1) {
    relay1State = state; setRelay(RLY1_PIN, state);
  } else if (String(topic) == topic_relay2) {
    relay2State = state; setRelay(RLY2_PIN, state);
  } else if (String(topic) == topic_relay3) {
    relay3State = state; setRelay(RLY3_PIN, state);
  }
  publishStatus();
}

// ========================================
// Connect MQTT
// ========================================
void connectMQTT() {
  oledPrint("Connecting MQTT...", mqtt_server);
  while (!mqttClient.connected()) {
    String clientId = "ESP32-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      mqttClient.subscribe(topic_relay1);
      mqttClient.subscribe(topic_relay2);
      mqttClient.subscribe(topic_relay3);
    } else {
      delay(3000);
    }
  }
}

// ========================================
// WiFiManager — เริ่ม config portal
// ========================================
void startConfigPortal() {
  oledPrint("Config Mode", "Connect to:", "OxygenSensor_AP");
  delay(500);

  WiFiManager wm;
  wm.resetSettings();  // ล้าง WiFi เดิม

  // callback ตอนเข้า AP mode
  wm.setAPCallback([](WiFiManager* wm) {
    oledPrint("Config Mode", "192.168.4.1", "OxygenSensor_AP");
  });

  // เปิด portal ค้างไว้จนกว่าจะ config สำเร็จ
  if (!wm.startConfigPortal("OxygenSensor_AP", "12345678")) {
    oledPrint("Config Failed", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  oledPrint("WiFi Connected!", WiFi.localIP().toString().c_str());
  delay(1500);
}

// ========================================
// SETUP
// ========================================
void setup() {
  // Relay pins
  pinMode(RLY1_PIN, OUTPUT);
  pinMode(RLY2_PIN, OUTPUT);
  pinMode(RLY3_PIN, OUTPUT);
  setRelay(RLY1_PIN, false);
  setRelay(RLY2_PIN, false);
  setRelay(RLY3_PIN, false);

  // BOOT button
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    oled_available = false;
  } else {
    oled_available = true;
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Oxygen");
    display.setTextSize(1);
    display.println("");
    display.println("Sensor Ready");
    display.display();
    delay(2000);
  }

  // ตรวจสอบว่ากด BOOT ค้างไว้ตอน boot ไหม
  // ถ้ากดค้าง → เข้า config portal ทันที
  unsigned long pressStart = millis();
  oledPrint("Hold BOOT 3s", "to reset WiFi...");
  while (digitalRead(BOOT_PIN) == LOW) {
    if (millis() - pressStart >= 3000) {
      startConfigPortal();
      break;
    }
  }

  // WiFiManager — เชื่อมต่อ WiFi เดิม หรือเปิด portal ถ้าไม่มี
  WiFiManager wm;
  wm.setConnectTimeout(20);  // timeout 20 วินาที

  wm.setAPCallback([](WiFiManager* wm) {
    oledPrint("No WiFi saved", "Config Portal:", "192.168.4.1");
  });

  oledPrint("Connecting WiFi...");

  if (!wm.autoConnect("OxygenSensor_AP", "12345678")) {
    oledPrint("WiFi Failed", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  oledPrint("WiFi Connected!", WiFi.localIP().toString().c_str());
  delay(1500);

  // MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();

  // Sensors
  o2Sensor.begin(9600);
  thSensor.begin();
}

// ========================================
// LOOP
// ========================================
void loop() {
  // ตรวจ BOOT button ค้าง 3 วินาที → reset WiFi
  static unsigned long bootPressTime = 0;
  static bool          bootHeld      = false;

  if (digitalRead(BOOT_PIN) == LOW) {
    if (!bootHeld) {
      bootHeld      = true;
      bootPressTime = millis();
    } else if (millis() - bootPressTime >= 3000) {
      startConfigPortal();
      bootHeld = false;
      ESP.restart();  // restart หลัง config เสร็จ
    }
  } else {
    bootHeld = false;
  }

  // MQTT
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  static unsigned long lastRead       = 0;
  static unsigned long lastPageSwitch = 0;
  static unsigned long lastDisp       = 0;
  static bool o2OK = false;
  static bool thOK = false;

  // อ่านเซนเซอร์ทุก 3 วินาที
  if (millis() - lastRead >= 3000) {
    lastRead = millis();
    o2OK = o2Sensor.update();
    delay(200);
    thOK = thSensor.update();
    if (o2OK) publishStatus();
  }

  // สลับหน้า OLED ทุก 3 วินาที
  if (millis() - lastPageSwitch >= 3000) {
    lastPageSwitch = millis();
    oledPage = (oledPage + 1) % 2;
  }

  // อัปเดต OLED ทุก 500ms
  if (millis() - lastDisp >= 500) {
    lastDisp = millis();

    if (oled_available) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);

      if (oledPage == 0) {
        display.println("=== Oxygen ===");
        display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
        if (o2OK) {
          display.setCursor(0, 14);
          display.printf("O2 : %.2f %%", o2Sensor.getOxygenPct());
          display.setCursor(0, 26);
          display.printf("O2 : %.2f mg/L", o2Sensor.getOxygenMgl());
          display.setCursor(0, 38);
          display.printf("Temp: %.1f C", o2Sensor.getTemperature());
        } else {
          display.setCursor(0, 20);
          display.println("Sensor Error");
          display.println("- RS485 mode?");
          display.println("- Slave ID=1?");
        }
        display.setCursor(0, 50);
        display.printf("R:%s %s %s",
          relay1State ? "1" : "-",
          relay2State ? "2" : "-",
          relay3State ? "3" : "-"
        );
        display.setCursor(72, 50);
        display.print(mqttClient.connected() ? "MQTT:OK" : "MQTT:--");
        display.setCursor(100, 56);
        display.print("[1/2]");

      } else {
        display.println("=== Air Sensor ===");
        display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
        if (thOK) {
          display.setCursor(0, 18);
          display.printf("Temp: %.1f C", thSensor.getTemperature());
          display.setCursor(0, 34);
          display.printf("Humi: %.1f %%RH", thSensor.getHumidity());
        } else {
          display.setCursor(0, 20);
          display.println("Sensor Error");
          display.println("- RS485 mode?");
          display.println("- Slave ID=2?");
        }
        display.setCursor(100, 56);
        display.print("[2/2]");
      }

      display.display();
    }
  }

  delay(10);
}