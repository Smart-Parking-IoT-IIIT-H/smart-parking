// ============================================================
//  GateController.ino — ESP32 Exit Gate Controller
//  Subscribes to parking/gate/exit/open
//  Publishes to parking/exit/car/detected
//  Controls 1 exit servo only
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <ArduinoOTA.h>

// ─────────────────────────────────────────
//  CONFIG
// ─────────────────────────────────────────
const char* WIFI_SSID   = "iQOO";
const char* WIFI_PASS   = "12345678";
const char* MQTT_HOST   = "6bf52feab0aa462a94eda4f44fdf671c.s1.eu.hivemq.cloud";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "esp32-park";
const char* MQTT_PASS   = "IoTesp32-Park";
const char* MQTT_CLIENT = "esp32-gatecontrol-01";

// ─────────────────────────────────────────
//  PIN MAP
// ─────────────────────────────────────────
#define EXIT_SERVO_PIN    18
#define EXIT_IR_PIN       5    // <-- Added Exit IR Pin (Update if wired differently)

// ─────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────
#define SERVO_OPEN_DEG      0
#define SERVO_CLOSE_DEG    90
#define GATE_HOLD_MS    5000   // keep gate open for 5 seconds

// ─────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────
WiFiClientSecure  wifiClient;
PubSubClient      mqtt(wifiClient);
Servo             exitGate;

// ─────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────
volatile bool exitOpenCmd = false;

bool          exitGateOpen = false;
unsigned long exitOpenTime = 0;

int           lastIrState  = HIGH; // <-- Tracks IR state to prevent MQTT spam

// ─────────────────────────────────────────
//  MQTT CALLBACK
// ─────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[256];
  unsigned int copyLen = (length < 255) ? length : 255;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  Serial.printf("[MQTT-IN] %s: %s\n", topic, msg);

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
      mqtt.subscribe("parking/gate/exit/open");
      Serial.println("[MQTT] Subscribed to parking/gate/exit/open");
    } else {
      Serial.printf(" failed rc=%d\n", mqtt.state());
      delay(3000);
      attempts++;
    }
  }
}

// ─────────────────────────────────────────
//  GATE HELPERS
// ─────────────────────────────────────────
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
//  OTA SETUP — called after WiFi connects
// ─────────────────────────────────────────
void setupOTA() {
  ArduinoOTA.setHostname("SmartPark-Gate");
  ArduinoOTA.setPassword("smartpark");

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Update starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update complete! Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR)    Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)     Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.printf("[OTA] Ready. Hostname: SmartPark-Gate  IP: %s\n",
                WiFi.localIP().toString().c_str());
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== GateController v2.0-OTA — Exit Only ===");

  exitGate.attach(EXIT_SERVO_PIN);
  exitGate.write(SERVO_CLOSE_DEG);
  Serial.println("[SETUP] Servo attached to pin " + String(EXIT_SERVO_PIN) + ", closed.");
  
  pinMode(EXIT_IR_PIN, INPUT);
  Serial.println("[SETUP] IR sensor on pin " + String(EXIT_IR_PIN) + " ready.");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  // Force Google DNS to fix ENOTFOUND on restricted networks
  IPAddress dns(8, 8, 8, 8);
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns);
  delay(500); // let DNS settle

  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] DNS: %s\n", WiFi.dnsIP().toString().c_str());

  wifiClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(256);
  mqtt.setCallback(mqttCallback);

  mqttReconnect();

  // Initialize OTA after WiFi is connected
  setupOTA();

  Serial.println("[SETUP] Done.");
}

// ─────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected! Attempting reconnect...");
    WiFi.reconnect();
    delay(5000);
    return;
  }
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();
  ArduinoOTA.handle();   // check for OTA updates each iteration

  unsigned long now = millis();

  // ─────────────────────────────────────────
  // EXIT IR SENSOR LOGIC
  // ─────────────────────────────────────────
  // Assuming standard LM393 IR module where LOW = object detected
  int currentIrState = digitalRead(EXIT_IR_PIN);
  
  if (currentIrState == LOW && lastIrState == HIGH) {
    Serial.println("[IR] >>> Car DETECTED at exit (LOW edge)");
    bool sent = mqtt.publish("parking/exit/car/detected", "true");
    Serial.println(sent ? "[IR] ✓ Published parking/exit/car/detected" : "[IR] ✗ FAILED to publish!");
  }
  if (currentIrState == HIGH && lastIrState == LOW) {
    Serial.println("[IR] Car left exit sensor (HIGH edge)");
  }
  lastIrState = currentIrState;

  // ─────────────────────────────────────────
  // EXIT GATE LOGIC
  // ─────────────────────────────────────────
  // Opens via MQTT command from dashboard
  if (exitOpenCmd) {
    exitOpenCmd = false;
    openExitGate();
    Serial.println("[EXIT] Gate opening via MQTT command");
  }

  // Auto-close after hold time
  if (exitGateOpen && (now - exitOpenTime >= GATE_HOLD_MS)) {
    Serial.printf("[GATE] Hold time %dms elapsed — auto-closing\n", GATE_HOLD_MS);
    closeExitGate();
  }

  delay(50);
}