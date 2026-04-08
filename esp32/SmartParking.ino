// ============================================================
//  SmartParking.ino — ESP32 Smart Parking Firmware
//  2 ACTIVE slots (S3/S4 disabled) | IR + US | RG LED
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
const char* WIFI_SSID   = "iQOO";
const char* WIFI_PASS   = "12345678";
const char* MQTT_HOST   = "706dd0796e994ac0bf5970d78b2f43b1.s1.eu.hivemq.cloud";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "esp32-parking";
const char* MQTT_PASS   = "IoTesp32Park";
const char* MQTT_CLIENT = "esp32-smartpark-01";

// ─────────────────────────────────────────
//  PIN MAP — IR + US, 2 active slots
// ─────────────────────────────────────────
//                        S1   S2
const int IR[]    = { 19,  35 };
const int LED_R[] = { 25,  14 };
const int LED_G[] = { 26,  12 };
// LED_B removed — no blue channel

//  US sensor pins — change here if your wiring differs
const int US_TRIG[] = {  4,  15 };
const int US_ECHO[] = {  5,  13 };

#define OLED_SDA  21
#define OLED_SCL  22
#define OLED_ADDR 0x3C
#define OLED_W    128
#define OLED_H    64
#define SERVO_PIN 11

// ─────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────
#define NUM_SLOTS           2
#define DEBOUNCE_CONFIRM    3
#define HEARTBEAT_MS     10000
#define SERVO_OPEN_DEG      90
#define SERVO_CLOSE_DEG      0

#define US_CALIB_PINGS      10     // pings per slot during boot calibration
#define US_CALIB_NOISE_CM   50.0f  // readings above this are treated as noise/timeout
#define US_CALIB_MIN_VALID   3     // need at least this many good pings to trust average
#define US_CALIB_MARGIN_CM   3.0f  // threshold = average − margin
#define US_CALIB_FALLBACK_CM 11.0f // used when calibration cannot get enough valid pings
#define US_TIMEOUT_US       30000  // pulseIn timeout (≈ 5 m round-trip)

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
  bool  occupied;
  bool  ir;
  float us;          // last measured distance in cm (0.0 if skipped)
  bool  error;
  int   debounceCount;
  bool  pendingState;
};

SlotState         slots[NUM_SLOTS];
SlotState         sharedSlots[NUM_SLOTS];
SemaphoreHandle_t dataMutex;
unsigned long     lastHeartbeat = 0;

float             usThreshold[NUM_SLOTS];  // set by calibrateUS() at boot

// ─────────────────────────────────────────
//  LED CONTROL — red/green only, no blue
// ─────────────────────────────────────────
void setLED(int s, bool r, bool g) {
  digitalWrite(LED_R[s], r);
  digitalWrite(LED_G[s], g);
}
void setLEDGreen(int s) { setLED(s, 0, 1); }
void setLEDRed(int s)   { setLED(s, 1, 0); }
void setLEDOff(int s)   { setLED(s, 0, 0); }

// ─────────────────────────────────────────
//  US — single ping, returns distance in cm
//  Returns -1.0 on timeout
// ─────────────────────────────────────────
float pingUS(int slot) {
  digitalWrite(US_TRIG[slot], LOW);
  delayMicroseconds(2);
  digitalWrite(US_TRIG[slot], HIGH);
  delayMicroseconds(10);
  digitalWrite(US_TRIG[slot], LOW);
  long duration = pulseIn(US_ECHO[slot], HIGH, US_TIMEOUT_US);
  if (duration == 0) return -1.0f;           // timeout
  return duration * 0.0343f / 2.0f;          // cm
}

// ─────────────────────────────────────────
//  BOOT CALIBRATION — runs synchronously in setup()
//  Slots MUST be empty when powering on.
// ─────────────────────────────────────────
void calibrateUS() {
  Serial.println("[CALIB] Starting US calibration — ensure slots are EMPTY");
  for (int i = 0; i < NUM_SLOTS; i++) {
    float sum   = 0.0f;
    int   valid = 0;
    for (int p = 0; p < US_CALIB_PINGS; p++) {
      float d = pingUS(i);
      if (d > 0.0f && d <= US_CALIB_NOISE_CM) {
        sum += d;
        valid++;
      }
      Serial.printf("[CALIB] S%d ping %d: %.1f cm\n", i + 1, p + 1, d);
      delay(60);   // allow echo to settle between pings
    }

    if (valid >= US_CALIB_MIN_VALID) {
      float avg = sum / valid;
      usThreshold[i] = avg - US_CALIB_MARGIN_CM;
      Serial.printf("[CALIB] S%d avg=%.1f cm  threshold=%.1f cm (%d/%d valid)\n",
                    i + 1, avg, usThreshold[i], valid, US_CALIB_PINGS);
    } else {
      usThreshold[i] = US_CALIB_FALLBACK_CM;
      Serial.printf("[CALIB] S%d FALLBACK threshold=%.1f cm (only %d/%d valid — check wiring)\n",
                    i + 1, usThreshold[i], valid, US_CALIB_PINGS);
    }
  }
  Serial.println("[CALIB] Done.");
}

// ─────────────────────────────────────────
//  OLED — shows slot status + live IR/US
// ─────────────────────────────────────────
void updateOLED(int freeCount) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  // Row 1: free count summary
  oled.setTextSize(2);
  oled.setCursor(8, 2);
  oled.print(freeCount); oled.print("/2 free");

  // Row 2: S1 and S2 occupancy status
  oled.setTextSize(1);
  oled.setCursor(8, 24);
  oled.print("S1:"); oled.print(sharedSlots[0].occupied ? "OCC " : "FREE");
  oled.setCursor(72, 24);
  oled.print("S2:"); oled.print(sharedSlots[1].occupied ? "OCC " : "FREE");

  // Row 3: live IR raw readings
  oled.setCursor(8, 36);
  oled.print("IR1:"); oled.print(sharedSlots[0].ir ? "1" : "0");
  oled.print("  ");
  oled.print("IR2:"); oled.print(sharedSlots[1].ir ? "1" : "0");

  // Row 4: inactive slots
  oled.setCursor(8, 48);
  oled.print("S3:--- S4:---");

  // Row 5: firmware label
  oled.setCursor(8, 58);
  oled.print("SmartPark v1.1");

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
//  IR gates the US read; debounce is unchanged.
// ─────────────────────────────────────────
void sensorTask(void* param) {
  Serial.println("[SENSOR] Task started on Core 0");
  while (true) {
    for (int i = 0; i < NUM_SLOTS; i++) {
      bool irVal = !digitalRead(IR[i]);   // true = car detected (reflection-based IR)

      bool newOcc;
      float dist = 0.0f;

      if (irVal) {
        // IR triggered — confirm with US
        dist   = pingUS(i);
        newOcc = (dist > 0.0f && dist <= usThreshold[i]);
        // dist <= 0 means US timed out — treat as free (false positive from IR)
      } else {
        // IR clear — no need to ping US
        newOcc = false;
      }

      // Debounce: require DEBOUNCE_CONFIRM consecutive reads of a new state
      if (newOcc != slots[i].occupied) {
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
        slots[i].debounceCount = 0;   // stable — reset counter
      }

      slots[i].ir    = irVal;
      slots[i].us    = dist;
      slots[i].error = false;

      // LED: green = free, red = occupied
      if (slots[i].occupied) setLEDRed(i);
      else                   setLEDGreen(i);
    }

    // Push to shared state for the comm task
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
//  PUBLISH SLOT — us field is now real data
// ─────────────────────────────────────────
void publishSlot(int i, SlotState& s) {
  StaticJsonDocument<160> doc;
  doc["slot"]      = i + 1;
  doc["occupied"]  = s.occupied;
  doc["ir"]        = s.ir;
  doc["us"]        = s.us;       // real measured distance in cm (0.0 if IR was clear)
  doc["error"]     = s.error;
  doc["timestamp"] = millis() / 1000;
  char buf[160]; serializeJson(doc, buf);
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
    doc["slot"]      = i + 1;
    doc["occupied"]  = false;
    doc["ir"]        = false;
    doc["us"]        = 0.0f;
    doc["error"]     = false;
    doc["waiting"]   = true;
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

    if (!waitingPublished && mqtt.connected()) {
      publishWaitingSlots();
      waitingPublished = true;
    }

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(localSnap, sharedSlots, sizeof(sharedSlots));
      xSemaphoreGive(dataMutex);
    }

    // Publish only on state change
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
  Serial.println("\n=== SmartParking v1.1 — IR + US, 2 slots active ===");

  for (int i = 0; i < NUM_SLOTS; i++) {
    pinMode(IR[i],          INPUT);
    pinMode(LED_R[i],       OUTPUT);
    pinMode(LED_G[i],       OUTPUT);
    pinMode(US_TRIG[i],     OUTPUT);
    pinMode(US_ECHO[i],     INPUT);
    digitalWrite(US_TRIG[i], LOW);
    setLEDOff(i);
  }

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] Init failed");
  } else {
    oled.clearDisplay();
    oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 24); oled.print("  Calibrating US...");
    oled.display();
  }

  gate.attach(SERVO_PIN);
  gate.write(SERVO_OPEN_DEG);

  memset(slots,       0, sizeof(slots));
  memset(sharedSlots, 0, sizeof(sharedSlots));
  dataMutex = xSemaphoreCreateMutex();

  // Boot calibration — must run before tasks start; slots must be empty
  calibrateUS();

  // Update OLED to show boot complete
  oled.clearDisplay();
  oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 28); oled.print("  SmartPark booting...");
  oled.display();

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(commTask,   "CommTask",   8192, NULL, 1, NULL, 1);

  Serial.println("[SETUP] Done.");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
