#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, 16, 15, 4);

#define LORA_SCK  5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS   18
#define LORA_RST  23
#define LORA_DIO0 26
#define LED_PIN   25

HardwareSerial MySerial(1);

String lastTxMsg  = "";   // ข้อมูลเซนเซอร์ล่าสุดที่ส่งออกไป
String lastRxCmd  = "";   // คำสั่ง Relay ล่าสุดที่รับมาจาก LoRa
unsigned long lastRxTime = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // UART → ESP32 (RX=GPIO13, TX=GPIO12)
  MySerial.begin(9600, SERIAL_8N1, 13, 12);

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 32, "Initializing...");
  u8g2.sendBuffer();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(920E6)) {
    u8g2.clearBuffer();
    u8g2.drawStr(10, 32, "LoRa FAILED!");
    u8g2.sendBuffer();
    while (1);
  }

  LoRa.setSyncWord(0xF3);
  Serial.println("TTGO Node1 Ready (Bidirectional)");
}

void loop() {
  // ── ① รับข้อมูลเซนเซอร์จาก ESP32 → ส่งออก LoRa ──────────────────────
  if (MySerial.available()) {
    String msg = MySerial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      lastTxMsg = msg;
      LoRa.beginPacket();
      LoRa.print(msg);
      LoRa.endPacket();
      Serial.println("[LoRa TX] " + msg);

      // กระพริบ LED สั้น ๆ
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      digitalWrite(LED_PIN, LOW);
    }
  }

  // ── ② รับคำสั่ง Relay จาก LoRa → ส่งต่อให้ ESP32 ────────────────────
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    incoming.trim();

    // ตรวจว่าเป็นคำสั่ง Relay (R1:ON / R1:OFF / R2:ON / ...)
    if (incoming.length() > 0 &&
        incoming.charAt(0) == 'R' &&
        incoming.indexOf(':') != -1) {

      lastRxCmd  = incoming;
      lastRxTime = millis();

      // ส่งต่อให้ ESP32 ผ่าน UART พร้อม newline
      MySerial.println(incoming);
      Serial.println("[LoRa RX → UART] " + incoming);

      // กระพริบ LED ยาวขึ้นเพื่อแยกจาก TX
      digitalWrite(LED_PIN, HIGH);
      delay(150);
      digitalWrite(LED_PIN, LOW);
    }
  }

  // ── ③ แสดงผล OLED ─────────────────────────────────────────────────────
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  // Header
  u8g2.drawStr(0, 10, "TTGO #1 [Pool]");
  u8g2.drawHLine(0, 13, 128);

  // TX: ข้อมูลเซนเซอร์
  if (lastTxMsg == "") {
    u8g2.drawStr(0, 28, "TX: Waiting ESP32...");
  } else {
    u8g2.drawStr(0, 26, "TX:");
    u8g2.drawStr(12, 26, lastTxMsg.substring(0, 18).c_str());
    u8g2.drawStr(0, 38, lastTxMsg.substring(18, 36).c_str());
  }

  // RX: คำสั่ง Relay
  u8g2.drawHLine(0, 42, 128);
  if (lastRxCmd == "") {
    u8g2.drawStr(0, 55, "RX: No cmd yet");
  } else {
    // แสดงคำสั่งล่าสุด + เวลาผ่านมากี่วินาที
    char rxLine[32];
    unsigned long secAgo = (millis() - lastRxTime) / 1000;
    snprintf(rxLine, sizeof(rxLine), "RX: %s (%lus ago)", lastRxCmd.c_str(), secAgo);
    u8g2.drawStr(0, 55, rxLine);
  }

  u8g2.sendBuffer();
  delay(10);
}
