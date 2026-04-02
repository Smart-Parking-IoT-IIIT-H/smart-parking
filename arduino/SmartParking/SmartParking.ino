// ============================================================
//  SmartParking.ino — ESP32 Smart Parking Firmware
//  2 ACTIVE slots (S3/S4 disabled) | HC-SR04 + IR | RGB LED
//  OLED | Servo | MQTT TLS
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// ─────────────────────────────────────────
//  CONFIG
// ─────────────────────────────────────────
const char* WIFI_SSID   = "IQOO";
const char* WIFI_PASS   = "12345678";
const char* MQTT_HOST   = "706dd0796e994ac0bf5970d78b2f43b1.s1.eu.hivemq.cloud";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "esp32-parking";
const char* MQTT_PASS   = "IoTesp32Park";
const char* MQTT_CLIENT = "esp32-smartpark-01";

// ─────────────────────────────────────────
//  PIN MAP — only 2 slots, zero conflicts
// ─────────────────────────────────────────
//                        S1   S2
const int TRIG[]  = {  5,   4 };
const int ECHO[]  = { 18,  16 };
const int IR[]    = { 19,  17 };
const int LED_R[] = { 25,  14 };
const int LED_G[] = { 26,  12 };
const int LED_B[] = { 27,  13 };

#define OLED_SDA  21
#define OLED_SCL  22
#define OLED_ADDR 0x3C
#define OLED_W    128
#define OLED_H    64
#define SERVO_PIN 11

// ─────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────
#define NUM_SLOTS        2    // ← only 2 active
#define US_OCCUPIED_CM  20
#define US_MAX_CM      300
#define US_MIN_CM        2
#define DEBOUNCE_CONFIRM 3
#define MEDIAN_SAMPLES   3
#define HEARTBEAT_MS  10000
#define SERVO_OPEN_DEG  90
#define SERVO_CLOSE_DEG  0

// ─────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────
WiFiClientSecure  wifiClient;
PubSubClient      mqtt(wifiClient);
Adafruit_SSD1306  oled(OLED_W, OLED_H, &Wire, -1);
Servo             gate;

// ─────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────
struct SlotState {
  bool occupied;
  bool ir;
  bool us;
  bool error;
  int  debounceCount;
  bool pendingState;
};

SlotState         slots[NUM_SLOTS];
SlotState         sharedSlots[NUM_SLOTS];
SemaphoreHandle_t dataMutex;
unsigned long     lastHeartbeat = 0;

// ─────────────────────────────────────────
//  ULTRASONIC — MEDIAN FILTER
// ─────────────────────────────────────────
long readUltrasonicCm(int trigPin, int echoPin) {
  long readings[MEDIAN_SAMPLES];
  for (int i = 0; i < MEDIAN_SAMPLES; i++) {
    digitalWrite(trigPin, LOW);  delayMicroseconds(2);
    digitalWrite(trigPin, HIGH); delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long dur = pulseIn(echoPin, HIGH, 30000);
    readings[i] = dur * 0.034 / 2;
    delay(5);
  }
  for (int i = 0; i < MEDIAN_SAMPLES - 1; i++)
    for (int j = i+1; j < MEDIAN_SAMPLES; j++)
      if (readings[j] < readings[i]) { long t=readings[i]; readings[i]=readings[j]; readings[j]=t; }
  return readings[MEDIAN_SAMPLES / 2];
}

// ─────────────────────────────────────────
//  LED CONTROL
// ─────────────────────────────────────────
void setLED(int s, bool r, bool g, bool b) {
  digitalWrite(LED_R[s], r); digitalWrite(LED_G[s], g); digitalWrite(LED_B[s], b);
}
void setLEDGreen(int s)  { setLED(s,0,1,0); }
void setLEDRed(int s)    { setLED(s,1,0,0); }
void setLEDYellow(int s) { setLED(s,1,1,0); }
void setLEDOff(int s)    { setLED(s,0,0,0); }

// ─────────────────────────────────────────
//  OLED — shows active slots + S3/S4 as "waiting"
// ─────────────────────────────────────────
void updateOLED(int freeCount) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(8, 4);
  oled.print(freeCount); oled.print("/2 free");

  oled.setTextSize(1);
  oled.setCursor(8, 26);
  oled.print("S1:"); oled.print(sharedSlots[0].occupied ? "OCC" : "FREE");
  oled.setCursor(72, 26);
  oled.print("S2:"); oled.print(sharedSlots[1].occupied ? "OCC" : "FREE");

  // S3 and S4 shown as dashes (not active)
  oled.setCursor(8, 40);
  oled.print("S3:--- S4:---");

  oled.setCursor(8, 54);
  oled.print("SmartPark v1.0");
  oled.display();
}

// ─────────────────────────────────────────
//  SERVO
// ─────────────────────────────────────────
void updateServo(int freeCount) {
  gate.write(freeCount > 0 ? SERVO_OPEN_DEG : SERVO_CLOSE_DEG);
}

// ─────────────────────────────────────────
//  SENSOR TASK — Core 0
// ─────────────────────────────────────────
void sensorTask(void* param) {
  Serial.println("[SENSOR] Task started on Core 0");
  while (true) {
    for (int i = 0; i < NUM_SLOTS; i++) {
      long dist    = readUltrasonicCm(TRIG[i], ECHO[i]);
      bool irVal   = !digitalRead(IR[i]);
      bool usOcc   = (dist > US_MIN_CM && dist < US_OCCUPIED_CM);
      bool usErr   = (dist <= 0 || dist > US_MAX_CM);
      bool agree   = (irVal == usOcc);
      bool newOcc  = irVal && usOcc;

      if (!usErr && agree) {
        if (newOcc != slots[i].occupied) {
          if (newOcc == slots[i].pendingState) slots[i].debounceCount++;
          else { slots[i].pendingState = newOcc; slots[i].debounceCount = 1; }
          if (slots[i].debounceCount >= DEBOUNCE_CONFIRM) {
            slots[i].occupied = newOcc; slots[i].debounceCount = 0;
          }
        } else slots[i].debounceCount = 0;
        slots[i].error = false;
      } else if (usErr) slots[i].error = true;

      slots[i].ir = irVal;
      slots[i].us = usOcc;

      if      (slots[i].error)    setLEDYellow(i);
      else if (slots[i].occupied) setLEDRed(i);
      else                        setLEDGreen(i);
    }

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(sharedSlots, slots, sizeof(slots));
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ─────────────────────────────────────────
//  MQTT RECONNECT
// ─────────────────────────────────────────
void mqttReconnect() {
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS))
      Serial.println(" connected.");
    else { Serial.printf(" failed rc=%d\n", mqtt.state()); delay(3000); attempts++; }
  }
}

// ─────────────────────────────────────────
//  PUBLISH SLOT
// ─────────────────────────────────────────
void publishSlot(int i, SlotState& s) {
  StaticJsonDocument<128> doc;
  doc["occupied"]  = s.occupied;
  doc["slot"]      = i + 1;
  doc["ir"]        = s.ir;
  doc["us"]        = s.us;
  doc["error"]     = s.error;
  doc["timestamp"] = millis() / 1000;
  char buf[128]; serializeJson(doc, buf);
  char topic[32]; snprintf(topic, sizeof(topic), "parking/slot/%d/status", i+1);
  mqtt.publish(topic, buf, true);
  Serial.printf("[MQTT] Slot %d: %s\n", i+1, buf);
}

// ─────────────────────────────────────────
//  PUBLISH SLOTS 3 & 4 AS "WAITING" ONCE
// ─────────────────────────────────────────
void publishWaitingSlots() {
  for (int i = 2; i <= 3; i++) {
    StaticJsonDocument<128> doc;
    doc["occupied"]  = false;
    doc["slot"]      = i + 1;
    doc["ir"]        = false;
    doc["us"]        = false;
    doc["error"]     = false;
    doc["waiting"]   = true;   // ← dashboard sees this, shows grey
    doc["timestamp"] = millis() / 1000;
    char buf[128]; serializeJson(doc, buf);
    char topic[32]; snprintf(topic, sizeof(topic), "parking/slot/%d/status", i+1);
    mqtt.publish(topic, buf, true);
    Serial.printf("[MQTT] Slot %d published as WAITING\n", i+1);
  }
}

// ─────────────────────────────────────────
//  COMM TASK — Core 1
// ─────────────────────────────────────────
void commTask(void* param) {
  Serial.println("[COMM] Task started on Core 1");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  wifiClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(256);

  SlotState localSnap[NUM_SLOTS], prevSnap[NUM_SLOTS];
  memset(prevSnap, 0, sizeof(prevSnap));
  bool waitingPublished = false;

  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect(); vTaskDelay(pdMS_TO_TICKS(5000)); continue;
    }
    if (!mqtt.connected()) mqttReconnect();
    mqtt.loop();

    // Publish slots 3 & 4 as waiting once after first connect
    if (!waitingPublished && mqtt.connected()) {
      publishWaitingSlots();
      waitingPublished = true;
    }

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(localSnap, sharedSlots, sizeof(sharedSlots));
      xSemaphoreGive(dataMutex);
    }

    for (int i = 0; i < NUM_SLOTS; i++) {
      if (localSnap[i].occupied != prevSnap[i].occupied ||
          localSnap[i].error    != prevSnap[i].error) {
        publishSlot(i, localSnap[i]);
        prevSnap[i] = localSnap[i];
      }
    }

    int freeCount = 0;
    for (int i = 0; i < NUM_SLOTS; i++)
      if (!localSnap[i].occupied && !localSnap[i].error) freeCount++;

    updateOLED(freeCount);
    updateServo(freeCount);

    unsigned long now = millis();
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
      StaticJsonDocument<32> hb; hb["ts"] = now/1000;
      char hbBuf[32]; serializeJson(hb, hbBuf);
      mqtt.publish("parking/heartbeat", hbBuf);
      lastHeartbeat = now;
      Serial.println("[MQTT] Heartbeat sent");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== SmartParking v1.0 — 2 slots active ===");

  for (int i = 0; i < NUM_SLOTS; i++) {
    pinMode(TRIG[i], OUTPUT); pinMode(ECHO[i], INPUT);
    pinMode(IR[i],   INPUT);
    pinMode(LED_R[i], OUTPUT); pinMode(LED_G[i], OUTPUT); pinMode(LED_B[i], OUTPUT);
    setLEDOff(i);
  }

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] Init failed");
  } else {
    oled.clearDisplay();
    oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 28); oled.print("  SmartPark booting...");
    oled.display();
  }

  gate.attach(SERVO_PIN);
  gate.write(SERVO_OPEN_DEG);

  memset(slots,       0, sizeof(slots));
  memset(sharedSlots, 0, sizeof(sharedSlots));
  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(commTask,   "CommTask",   8192, NULL, 1, NULL, 1);

  Serial.println("[SETUP] Done.");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }