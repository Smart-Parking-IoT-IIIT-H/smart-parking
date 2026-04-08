# SmartPark — IoT Live Monitoring System

SmartPark is an end-to-end IoT-based smart parking system that monitors parking slot occupancy in real-time. The project consists of two primary components: an ESP32-based hardware node with sensors to detect vehicle presence, and a responsive web dashboard deployed on Firebase to visualize live slot data.

## 🚀 Features

- **Real-Time Occupancy Detection:** Uses Infrared (IR) and Ultrasonic (US) sensors to accurately detect whether a parking spot is free or occupied. Dual-sensor confirmation prevents false positives.
- **Automated Gate Control:** Controls a servo motor acting as a parking gate that opens when parking spots are available and closes when the lot is full.
- **Local Status Display:** Features an onboard OLED screen showing live parking availability directly at the parking entrance.
- **Cloud Connectivity (MQTT):** The ESP32 node publishes sensor data over a secure MQTT connection (TLS via HiveMQ).
- **Live Web Dashboard:** A highly responsive vanilla HTML/CSS/JS frontend application connecting to Firebase Realtime Database to display current slot capacity, utilization percentage, history charts, and uptime.
- **Hardware Watchdog & Debouncing:** Includes IR to gate US sensor reading, debouncing algorithms for stable readings, and a heartbeat system for connection monitoring.

## 📁 Repository Structure

- `esp32/SmartParking.ino` — C++ Firmware for the ESP32 microcontroller.
- `public/index.html` — The main frontend application file (HTML/CSS/JS).
- `requirements.txt` — Lists the Arduino library dependencies for the firmware.
- `firebase.json` — Configuration for deploying the frontend application to Firebase Hosting.

## 🛠️ Hardware Requirements

- **ESP32 Development Board**
- **2× IR Obstacle Avoidance Sensors**
- **2× HC-SR04 Ultrasonic Sensors**
- **Servo Motor** (for the entrance gate)
- **SSD1306 OLED Display** (I2C)
- **Status LEDs** (Red & Green per slot)

## 💻 Tech Stack

### Firmware (IoT Node)
- **Platform:** ESP32 (Arduino core)
- **Sensors:** IR, Ultrasonic (Ping)
- **Networking:** WiFi, MQTT over TLS (`PubSubClient`, `WiFiClientSecure`)
- **Display & Actuators:** `Adafruit SSD1306`, `ESP32Servo`

### Frontend (Dashboard)
- **UI:** Custom modern CSS design system with "glassmorphism" alerts, scanline background effects, and highly responsive breakpoint handling (Banner vs Phone modes).
- **Backend Sync:** Firebase Realtime Database (`firebase-database-compat`) synchronizing slot values pushed from the MQTT broker.
- **Charts:** Chart.js for real-time occupancy graphs.
- **Hosting:** Firebase Hosting.

## 🔧 Installation & Setup

### 1. ESP32 Firmware Setup
1. Open the `/esp32/SmartParking.ino` file in the Arduino IDE.
2. Install the required Arduino libraries listed in the `requirements.txt` file (e.g., `PubSubClient`, `ArduinoJson`, `Adafruit SSD1306`, `ESP32Servo`).
3. Update the Wi-Fi credentials and MQTT broker details in the `CONFIG` section of `SmartParking.ino`:
   ```cpp
   const char* WIFI_SSID   = "YOUR_WIFI_SSID";
   const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";
   ```
4. Flash the code to your ESP32 board. Ensure all parking slots are vacant during power-on for automatic ultrasonic range calibration.

### 2. Dashboard Deployment
1. Install Firebase CLI tools globally via npm:
   ```bash
   npm install -g firebase-tools
   ```
2. Initialize Firebase in the root directory (ensure you connect your project matching `smart-parking-1df76` or update `firebaseConfig` inside `public/index.html` with your own Firebase project credentials).
   ```bash
   firebase login
   firebase init hosting
   ```
3. Run or deploy the application:
   ```bash
   firebase serve     # To view locally
   firebase deploy    # To deploy to Firebase
   ```
*Note: The frontend currently connects to Firebase RTDB. An external bridge (not included in this repository codebase) is responsible for routing the ESP32 HiveMQ MQTT messages into the Firebase Realtime Database slots.*

## 🎨 System Internals

- **Calibration Routine:** The ESP32 takes rapid pings on boot to figure out the ambient baseline distances for the ultrasonic sensors. 
- **Debounced Feedback Loop:** A vehicle is only marked as occupying a slot when the IR sensor detects a reflection *and* the US sensor registers a valid distance below the threshold for multiple consecutive reads (debouncing).
- **Multi-tasking Application:** Makes use of `xTaskCreatePinnedToCore` on the ESP32. **Core 0** polls hardware sensors rapidly without interruption, while **Core 1** handles Wi-Fi connectivity, MQTT formatting (`ArduinoJson`), and display updates. 

## 📝 License
This project is open-source. Please see the primary author/maintainer for details on code reproduction and commercial use rights.