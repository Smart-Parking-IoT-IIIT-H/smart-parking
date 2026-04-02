// ============================================================
//  SmartParking.ino — ESP32 Smart Parking Firmware
//  4 slots | HC-SR04 + IR | RGB LED | OLED | Servo | MQTT TLS
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
//  CONFIG — EDIT THESE
// ─────────────────────────────────────────
const char* WIFI_SSID     = "Pixel_9183";
const char* WIFI_PASS     = "11223344";

const char* MQTT_HOST     = "706dd0796e994ac0bf5970d78b2f43b1.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;
const char* MQTT_USER     = "esp32-parking";
const char* MQTT_PASS     = "IoTesp32Park";
const char* MQTT_CLIENT   = "esp32-smartpark-01";

// ─────────────────────────────────────────
//  PIN MAP
// ─────────────────────────────────────────
// Slot:          S1    S2    S3    S4
const int TRIG[] = { 5,   4,   2,   0  };
const int ECHO[] = { 18,  16,  15,  36 };   // D36 = input only ✓
const int IR[]   = { 19,  17,  32,  39 };   // D39 = input only ✓
const int LED_R[] = { 25,  14,  33,  21 };
const int LED_G[] = { 26,  12,  26,  22 };  // NOTE: S1 & S3 share D26 — set carefully
const int LED_B[] = { 27,  13,  25,  23 };  // NOTE: S1 & S3 share D25 — set carefully

// OLED
#define OLED_SDA  21
#define OLED_SCL  22
#define OLED_ADDR 0x3C
#define OLED_W    128
#define OLED_H    64

// Servo
#define SERVO_PIN 13

// ─────────────────────────────────────────
//  TUNING CONSTANTS
// ─────────────────────────────────────────
#define NUM_SLOTS         4
#define US_OCCUPIED_CM    20     // < this distance = car present
#define US_MAX_CM         300    // > this = sensor error
#define US_MIN_CM         2      // < this = sensor error
#define DEBOUNCE_CONFIRM  3      // consecutive agreements before state change
#define MEDIAN_SAMPLES    3      // ultrasonic median filter readings
#define HEARTBEAT_MS      10000  // 10s heartbeat interval
#define SERVO_OPEN_DEG    90
#define SERVO_CLOSE_DEG   0

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
  int  debounceCount;   // positive = confirming OCCUPIED, negative = confirming FREE
  bool pendingState;    // what we're debouncing toward
};

SlotState slots[NUM_SLOTS];
unsigned long lastHeartbeat = 0;
unsigned long lastPublish[NUM_SLOTS] = {0};

// FreeRTOS task handles
TaskHandle_t sensorTaskHandle;
TaskHandle_t commTaskHandle;

// Shared data protected by mutex
SemaphoreHandle_t dataMutex;
SlotState sharedSlots[NUM_SLOTS];

// ─────────────────────────────────────────
//  ULTRASONIC — MEDIAN FILTER
// ─────────────────────────────────────────
long readUltrasonicCm(int trigPin, int echoPin) {
  long readings[MEDIAN_SAMPLES];

  for (int i = 0; i < MEDIAN_SAMPLES; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout
    readings[i] = duration * 0.034 / 2;
    delay(5);
  }

  // Sort 3 values and return middle
  for (int i = 0; i < MEDIAN_SAMPLES - 1; i++) {
    for (int j = i + 1; j < MEDIAN_SAMPLES; j++) {
      if (readings[j] < readings[i]) {
        long tmp = readings[i];
        readings[i] = readings[j];
        readings[j] = tmp;
      }
    }
  }
  return readings[MEDIAN_SAMPLES / 2];
}

// ─────────────────────────────────────────
//  RGB LED CONTROL
// ─────────────────────────────────────────
// Common cathode: HIGH = LED on
void setLED(int slot, bool r, bool g, bool b) {
  // S1 and S3 share pins D25/D26 — only one can be shown at a time.
  // In a real build, use separate pins. For demo, last write wins.
  digitalWrite(LED_R[slot], r ? HIGH : LOW);
  digitalWrite(LED_G[slot], g ? HIGH : LOW);
  digitalWrite(LED_B[slot], b ? HIGH : LOW);
}

void setLEDGreen(int slot)  { setLED(slot, false, true,  false); }
void setLEDRed(int slot)    { setLED(slot, true,  false, false); }
void setLEDYellow(int slot) { setLED(slot, true,  true,  false); }
void setLEDOff(int slot)    { setLED(slot, false, false, false); }

// ─────────────────────────────────────────
//  OLED UPDATE
// ─────────────────────────────────────────
void updateOLED(int freeCount) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  // Large free count
  oled.setTextSize(3);
  oled.setCursor(10, 8);
  oled.print(freeCount);
  oled.print("/4");

  oled.setTextSize(1);
  oled.setCursor(10, 42);
  oled.print("slots free");

  // Bottom row: individual slot dots
  for (int i = 0; i < NUM_SLOTS; i++) {
    int x = 10 + i * 28;
    if (sharedSlots[i].error) {
      oled.drawChar(x, 54, '?', SSD1306_WHITE, SSD1306_BLACK, 1);
    } else if (sharedSlots[i].occupied) {
      oled.fillRect(x, 54, 10, 8, SSD1306_WHITE);
    } else {
      oled.drawRect(x, 54, 10, 8, SSD1306_WHITE);
    }
  }

  oled.display();
}

// ─────────────────────────────────────────
//  SERVO GATE
// ─────────────────────────────────────────
void updateServo(int freeCount) {
  if (freeCount > 0) {
    gate.write(SERVO_OPEN_DEG);   // any slot free → barrier open
  } else {
    gate.write(SERVO_CLOSE_DEG);  // all full → barrier closed
  }
}

// ─────────────────────────────────────────
//  SENSOR TASK — runs on Core 0
// ─────────────────────────────────────────
void sensorTask(void* param) {
  Serial.println("[SENSOR] Task started on Core 0");

  while (true) {
    for (int i = 0; i < NUM_SLOTS; i++) {
      // Read sensors
      long dist  = readUltrasonicCm(TRIG[i], ECHO[i]);
      bool irVal = !digitalRead(IR[i]);  // IR: LOW when object detected (active-low)

      bool usOccupied = (dist > US_MIN_CM && dist < US_OCCUPIED_CM);
      bool usError    = (dist <= 0 || dist > US_MAX_CM);

      // Dual sensor cross-validation
      bool agree  = (irVal == usOccupied);
      bool newOcc = irVal && usOccupied;   // both must agree car is there

      // Debounce state machine
      if (!usError && agree) {
        if (newOcc != slots[i].occupied) {
          // Accumulate toward pending state
          if (newOcc == slots[i].pendingState) {
            slots[i].debounceCount++;
          } else {
            slots[i].pendingState  = newOcc;
            slots[i].debounceCount = 1;
          }

          if (slots[i].debounceCount >= DEBOUNCE_CONFIRM) {
            slots[i].occupied      = newOcc;
            slots[i].debounceCount = 0;
          }
        } else {
          slots[i].debounceCount = 0;  // already in stable state
        }
        slots[i].error = false;
      } else if (usError) {
        slots[i].error = true;
      }
      // If sensors disagree but no error: keep previous state (noise rejection)

      slots[i].ir = irVal;
      slots[i].us = usOccupied;

      // Update LED immediately on Core 0
      if (slots[i].error) {
        setLEDYellow(i);
      } else if (slots[i].occupied) {
        setLEDRed(i);
      } else {
        setLEDGreen(i);
      }
    }

    // Push a copy to shared state for Comm task
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(sharedSlots, slots, sizeof(slots));
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz sensor polling
  }
}

// ─────────────────────────────────────────
//  MQTT CONNECT / RECONNECT
// ─────────────────────────────────────────
void mqttReconnect() {
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS)) {
      Serial.println(" connected.");
    } else {
      Serial.printf(" failed (rc=%d), retry in 3s\n", mqtt.state());
      delay(3000);
      attempts++;
    }
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

  char buf[128];
  serializeJson(doc, buf);

  char topic[32];
  snprintf(topic, sizeof(topic), "parking/slot/%d/status", i + 1);

  mqtt.publish(topic, buf, true);  // retained=true so dashboard shows state on reconnect
  Serial.printf("[MQTT] Published slot %d: %s\n", i + 1, buf);
}

// ─────────────────────────────────────────
//  COMM TASK — runs on Core 1
// ─────────────────────────────────────────
void commTask(void* param) {
  Serial.println("[COMM] Task started on Core 1");

  // WiFi connect
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // TLS — skip certificate verification for demo
  wifiClient.setInsecure();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(256);

  SlotState localSnapshot[NUM_SLOTS];
  SlotState prevSnapshot[NUM_SLOTS];
  memset(prevSnapshot, 0, sizeof(prevSnapshot));

  while (true) {
    // WiFi watchdog
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnecting...");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (!mqtt.connected()) {
      mqttReconnect();
    }
    mqtt.loop();

    // Grab latest sensor data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(localSnapshot, sharedSlots, sizeof(sharedSlots));
      xSemaphoreGive(dataMutex);
    }

    // Publish only on state change
    for (int i = 0; i < NUM_SLOTS; i++) {
      bool changed = (localSnapshot[i].occupied != prevSnapshot[i].occupied ||
                      localSnapshot[i].error    != prevSnapshot[i].error);
      if (changed) {
        publishSlot(i, localSnapshot[i]);
        prevSnapshot[i] = localSnapshot[i];
      }
    }

    // Count free slots for OLED + servo (both safe to call from Core 1)
    int freeCount = 0;
    for (int i = 0; i < NUM_SLOTS; i++) {
      if (!localSnapshot[i].occupied && !localSnapshot[i].error) freeCount++;
    }
    updateOLED(freeCount);
    updateServo(freeCount);

    // Heartbeat every 10s
    unsigned long now = millis();
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
      StaticJsonDocument<32> hb;
      hb["ts"] = now / 1000;
      char hbBuf[32];
      serializeJson(hb, hbBuf);
      mqtt.publish("parking/heartbeat", hbBuf);
      lastHeartbeat = now;
      Serial.println("[MQTT] Heartbeat sent");
    }

    vTaskDelay(pdMS_TO_TICKS(200));  // 5Hz comm loop
  }
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== SmartParking v1.0 ===");

  // Pin setup
  for (int i = 0; i < NUM_SLOTS; i++) {
    pinMode(TRIG[i],  OUTPUT);
    pinMode(ECHO[i],  INPUT);
    pinMode(IR[i],    INPUT);
    pinMode(LED_R[i], OUTPUT);
    pinMode(LED_G[i], OUTPUT);
    pinMode(LED_B[i], OUTPUT);
    setLEDOff(i);
  }

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] Init failed — check wiring");
  } else {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 28);
    oled.print("  SmartPark booting...");
    oled.display();
  }

  // Servo
  gate.attach(SERVO_PIN);
  gate.write(SERVO_OPEN_DEG);  // start open

  // Init state
  memset(slots,       0, sizeof(slots));
  memset(sharedSlots, 0, sizeof(sharedSlots));

  // Mutex
  dataMutex = xSemaphoreCreateMutex();

  // Launch FreeRTOS tasks
  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 0);
  xTaskCreatePinnedToCore(commTask,   "CommTask",   8192, NULL, 1, &commTaskHandle,   1);

  Serial.println("[SETUP] Both tasks launched.");
}

// ─────────────────────────────────────────
//  LOOP — empty (FreeRTOS handles everything)
// ─────────────────────────────────────────
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
