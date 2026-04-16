// ============================================================
//  GateController.ino — ESP32 Entry/Exit Gate Controller
//  Subscribes to parking/count + parking/gate/exit/open
//  Controls 2 servo barriers + 2 IR sensors
//  Optional OLED status display
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ─────────────────────────────────────────
//  CONFIG — match these to your SmartParking ESP32
// ─────────────────────────────────────────
const char* WIFI_SSID   = "iQOO";
const char* WIFI_PASS   = "12345678";
const char* MQTT_HOST   = "706dd0796e994ac0bf5970d78b2f43b1.s1.eu.hivemq.cloud";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "esp32-parking";
const char* MQTT_PASS   = "IoTesp32Park";
const char* MQTT_CLIENT = "esp32-gatecontrol-01";

// ─────────────────────────────────────────
//  PIN MAP
// ─────────────────────────────────────────
#define ENTRY_IR_PIN     19
#define EXIT_IR_PIN      18
#define ENTRY_SERVO_PIN   5
#define EXIT_SERVO_PIN    4

// ─────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────
#define SERVO_OPEN_DEG    90
#define SERVO_CLOSE_DEG    0
#define GATE_HOLD_MS    5000   // keep gate open for 5 seconds
#define IR_DEBOUNCE_MS   300   // debounce for IR sensors

// ─────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────
WiFiClientSecure  wifiClient;
PubSubClient      mqtt(wifiClient);
Servo             entryGate;
Servo             exitGate;

// ─────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────
volatile int  occupiedCount = 0;
volatile int  totalSlots    = 2;
volatile bool exitOpenCmd   = false;   // set by MQTT callback

bool          entryGateOpen = false;
bool          exitGateOpen  = false;
unsigned long entryOpenTime = 0;
unsigned long exitOpenTime  = 0;
unsigned long lastEntryIR   = 0;       // debounce timestamp
unsigned long lastExitIR    = 0;

// ─────────────────────────────────────────
//  MQTT CALLBACK — runs on incoming messages
// ─────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Null-terminate payload for parsing
  char msg[256];
  unsigned int copyLen = (length < 255) ? length : 255;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  Serial.printf("[MQTT-IN] %s: %s\n", topic, msg);

  // ── parking/count ──
  if (strcmp(topic, "parking/count") == 0) {
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      occupiedCount = doc["occupied"] | 0;
      totalSlots    = doc["total"]    | 2;
      Serial.printf("[GATE] Count updated: %d/%d\n", occupiedCount, totalSlots);
    }
  }

  // ── parking/gate/exit/open ──
  if (strcmp(topic, "parking/gate/exit/open") == 0) {
    exitOpenCmd = true;
    Serial.println("[GATE] Exit open command received");
  }
}

// ─────────────────────────────────────────
//  MQTT CONNECT + SUBSCRIBE
// ─────────────────────────────────────────
void mqttReconnect() {
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS)) {
      Serial.println(" connected.");
      // Subscribe to topics
      mqtt.subscribe("parking/count");
      mqtt.subscribe("parking/gate/exit/open");
      Serial.println("[MQTT] Subscribed to parking/count, parking/gate/exit/open");
    } else {
      Serial.printf(" failed rc=%d\n", mqtt.state());
      delay(3000);
      attempts++;
    }
  }
}

// ─────────────────────────────────────────
//  OPEN / CLOSE GATE HELPERS
// ─────────────────────────────────────────
void openEntryGate() {
  if (entryGateOpen) return;
  entryGate.write(SERVO_OPEN_DEG);
  entryGateOpen = true;
  entryOpenTime = millis();
  Serial.println("[GATE] Entry OPEN");
}

void closeEntryGate() {
  if (!entryGateOpen) return;
  entryGate.write(SERVO_CLOSE_DEG);
  entryGateOpen = false;
  Serial.println("[GATE] Entry CLOSED");
}

void openExitGate() {
  if (exitGateOpen) return;
  exitGate.write(SERVO_OPEN_DEG);
  exitGateOpen = true;
  exitOpenTime = millis();
  Serial.println("[GATE] Exit OPEN");
}

void closeExitGate() {
  if (!exitGateOpen) return;
  exitGate.write(SERVO_CLOSE_DEG);
  exitGateOpen = false;
  Serial.println("[GATE] Exit CLOSED");
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== GateController v1.0 — Entry/Exit ===");

  // IR sensors
  pinMode(ENTRY_IR_PIN, INPUT);
  pinMode(EXIT_IR_PIN,  INPUT);

  // Servos
  entryGate.attach(ENTRY_SERVO_PIN);
  exitGate.attach(EXIT_SERVO_PIN);
  entryGate.write(SERVO_CLOSE_DEG);
  exitGate.write(SERVO_CLOSE_DEG);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // MQTT
  wifiClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(256);
  mqtt.setCallback(mqttCallback);

  mqttReconnect();

  Serial.println("[SETUP] Done.");
}

// ─────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────
void loop() {
  // ── WiFi / MQTT housekeeping ──
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(5000);
    return;
  }
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  unsigned long now = millis();

  // ══════════════════════════════════════
  //  ENTRY GATE LOGIC
  // ══════════════════════════════════════
  bool entryIR = !digitalRead(ENTRY_IR_PIN);   // active-low IR

  if (entryIR && !entryGateOpen && (now - lastEntryIR > IR_DEBOUNCE_MS)) {
    lastEntryIR = now;

    if (occupiedCount < totalSlots) {
      // Parking has space → open entry gate
      openEntryGate();
      Serial.printf("[ENTRY] Car detected, spots available (%d/%d) → opening\n",
                    occupiedCount, totalSlots);
    } else {
      // Parking full → don't open
      Serial.printf("[ENTRY] Car detected but FULL (%d/%d) → denied\n",
                    occupiedCount, totalSlots);
    }
  }

  // Auto-close entry gate after hold time
  if (entryGateOpen && (now - entryOpenTime >= GATE_HOLD_MS)) {
    closeEntryGate();
  }

  // ══════════════════════════════════════
  //  EXIT GATE LOGIC
  // ══════════════════════════════════════
  // Exit gate opens only when dashboard sends MQTT command
  // (after QR display + payment timeout)
  if (exitOpenCmd) {
    exitOpenCmd = false;
    openExitGate();
    Serial.println("[EXIT] Gate opening via MQTT command");
  }

  // Auto-close exit gate after hold time
  if (exitGateOpen && (now - exitOpenTime >= GATE_HOLD_MS)) {
    closeExitGate();
  }

  delay(50);   // small loop delay
}
