#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =========================================================================
// 📺 16x2 I2C LCD DISPLAY SETUP (Dual Address 0x27 & 0x3F Auto-Detection)
// =========================================================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

LiquidCrystal_I2C lcd27(0x27, 16, 2);
LiquidCrystal_I2C lcd3F(0x3F, 16, 2);
LiquidCrystal_I2C* activeLcd = &lcd27;
bool lcdFound = false;

// =========================================================================
// 🚦 TRAFFIC LIGHT 1 (LANE 1: AMBULANCE CORRIDOR)
// =========================================================================
#define RED1_PIN    25
#define YELLOW1_PIN 26
#define GREEN1_PIN  27

// =========================================================================
// 🚦 TRAFFIC LIGHT 2 (LANE 2: CROSS TRAFFIC)
// =========================================================================
#define RED2_PIN    32
#define YELLOW2_PIN 33
#define GREEN2_PIN  18

// =========================================================================
// 🍓 RASPBERRY PI HARDWARE UART LINK (GPIO PINS)
// =========================================================================
// ESP32 D17 (TX2) connects to Raspberry Pi Pin 10 (GPIO 15 - RXD)
// ESP32 D16 (RX2) connects to Raspberry Pi Pin 8  (GPIO 14 - TXD)
#define PI_RX2_PIN 16
#define PI_TX2_PIN 17

// =========================================================================
// 🔍 IR OBSTACLE SENSOR PIN
// =========================================================================
#define IR_SENSOR_PIN 14

// =========================================================================
// 📡 WIRELESS COMMUNICATION: FULL MAXIMUM NATIVE RANGE (100m+)
// =========================================================================
// No distance restriction: All valid emergency packets accepted at maximum range!

// =========================================================================
// ESP-NOW DATA STRUCTURE
// =========================================================================
typedef struct struct_message {
    bool emergency;
} struct_message;

struct_message myData;

// State Machine Variables
bool emergencyMode = false;
unsigned long emergencyStartTime = 0;
bool vehiclePassedDetected = false;
unsigned long vehiclePassedTime = 0;
unsigned long lastEmergencyPacketTime = 0;

// Smart IR Sensor State Tracking
bool irArmed = false;
int irInitialState = HIGH;
bool irSawVehicle = false;

// Lockout flag: Prevents ambulance on desk from re-triggering after it passes
bool ambulancePassedLockout = false;

// ⏱️ TIMERS AS REQUESTED:
const unsigned long CLEARANCE_DURATION_MS   = 5000; // Exact 5 seconds delay after IR triggers
const unsigned long OUT_OF_RANGE_TIMEOUT_MS = 5000; // 5 seconds out-of-range recovery
// Permanent Incident Lockout: Lockout will NOT auto-reset while ambulance is still transmitting on desk!

// Normal 2-Way Traffic Signal Timers
int trafficPhase = 0;
unsigned long lastPhaseChangeTime = 0;
const unsigned long PHASE_MAIN_DURATION_MS   = 5000; // 5 Seconds Green/Red
const unsigned long PHASE_YELLOW_DURATION_MS = 2000; // 2 Seconds Yellow

// Telemetry & Display Timers
unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 250;
bool currentIrStatus = false;
String currentLcdLine1 = "";
String currentLcdLine2 = "";
unsigned long lastLcdToggleTime = 0;
bool emgBannerToggle = false;

// Streams messages simultaneously to Laptop USB Serial AND Raspberry Pi GPIO Serial2
void streamPrintln(String msg) {
  Serial.println(msg);
  Serial2.println(msg);
}

// Safe LCD updater (only writes when text changes to prevent screen flicker)
void updateLcd(String line1, String line2) {
  if (!lcdFound || activeLcd == nullptr) return;
  if (line1 != currentLcdLine1 || line2 != currentLcdLine2) {
    currentLcdLine1 = line1;
    currentLcdLine2 = line2;
    activeLcd->clear();
    activeLcd->setCursor(0, 0);
    activeLcd->print(line1.substring(0, 16));
    activeLcd->setCursor(0, 1);
    activeLcd->print(line2.substring(0, 16));
  }
}

void setTL1(bool r, bool y, bool g) {
  digitalWrite(RED1_PIN, r ? HIGH : LOW);
  digitalWrite(YELLOW1_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN1_PIN, g ? HIGH : LOW);
}

void setTL2(bool r, bool y, bool g) {
  digitalWrite(RED2_PIN, r ? HIGH : LOW);
  digitalWrite(YELLOW2_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN2_PIN, g ? HIGH : LOW);
}

String getTL1Color() {
  if (digitalRead(GREEN1_PIN) == HIGH) return "GREEN";
  if (digitalRead(YELLOW1_PIN) == HIGH) return "YELLOW";
  return "RED";
}

String getTL2Color() {
  if (digitalRead(GREEN2_PIN) == HIGH) return "GREEN";
  if (digitalRead(YELLOW2_PIN) == HIGH) return "YELLOW";
  return "RED";
}

// ESP-NOW Wireless Packet Receiver (ESP32 Core v3 compatible)
void OnDataRecv(const esp_now_recv_info * mac_info, const uint8_t *incomingData, int len) {
  // 1. Log Signal Strength (RSSI) for Live Telemetry / Audit
  if (mac_info != nullptr && mac_info->rx_ctrl != nullptr) {
    int rssi = mac_info->rx_ctrl->rssi;
    Serial.print("Ambulance Signal RSSI: ");
    Serial.print(rssi);
    Serial.println(" dBm (Full Range Mode - Packet Accepted)");
  }

  memcpy(&myData, incomingData, sizeof(myData));
  
  if (myData.emergency) {
    // If ambulance already passed IR sensor, lock it out so it doesn't re-trigger while on desk
    if (ambulancePassedLockout) {
      return;
    }
    
    if (!emergencyMode && !vehiclePassedDetected) {
      streamPrintln("EVENT: EMERGENCY_REQUEST_RECEIVED (Full Range)");
      emergencyMode = true;
      emergencyStartTime = millis();
      vehiclePassedDetected = false;
      irArmed = false;
      irSawVehicle = false;
    }
    
    if (!vehiclePassedDetected) {
      lastEmergencyPacketTime = millis();
    }
  } else {
    // Ambulance toggled button OFF: Reset lockout ready for next demonstration!
    ambulancePassedLockout = false;
    
    if (emergencyMode && !vehiclePassedDetected) {
      emergencyMode = false;
      irArmed = false;
      streamPrintln("EVENT: EMERGENCY_CANCELLED_BY_AMBULANCE");
      updateLcd("AMBULANCE CANCEL", "RESUMING NORMAL");
      delay(600);
      lastPhaseChangeTime = millis();
      trafficPhase = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  // Hardware UART2 for Raspberry Pi (D16=RX, D17=TX)
  Serial2.begin(115200, SERIAL_8N1, PI_RX2_PIN, PI_TX2_PIN);
  delay(200);

  // Traffic Light LED Pin Configurations
  pinMode(RED1_PIN, OUTPUT); pinMode(YELLOW1_PIN, OUTPUT); pinMode(GREEN1_PIN, OUTPUT);
  pinMode(RED2_PIN, OUTPUT); pinMode(YELLOW2_PIN, OUTPUT); pinMode(GREEN2_PIN, OUTPUT);
  
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);

  // Initialize I2C and LCD (Auto-detect address 0x27 or 0x3F)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    activeLcd = &lcd27; lcdFound = true;
  } else {
    Wire.beginTransmission(0x3F);
    if (Wire.endTransmission() == 0) {
      activeLcd = &lcd3F; lcdFound = true;
    } else {
      activeLcd = &lcd27; lcdFound = true;
    }
  }

  activeLcd->init();
  activeLcd->backlight();
  activeLcd->clear();
  activeLcd->setCursor(0, 0); activeLcd->print("SMART TRAFFIC");
  activeLcd->setCursor(0, 1); activeLcd->print("SYSTEM READY");

  // Initial State: Lane 1 RED, Lane 2 GREEN
  setTL1(true, false, false);
  setTL2(false, false, true);
  delay(1000);

  // Initialize Wi-Fi in Station Mode for ESP-NOW (Maximum 19.5 dBm Output Power)
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.disconnect();
  esp_now_init();
  esp_now_register_recv_cb(OnDataRecv);

  lastPhaseChangeTime = millis();
  trafficPhase = 0;
  streamPrintln("EVENT: TRAFFIC_CONTROLLER_READY");
}

void loop() {
  unsigned long currentMillis = millis();

  // Read current IR Pin state (0 or 1)
  int rawIr = digitalRead(IR_SENSOR_PIN);
  currentIrStatus = (rawIr == LOW); // True when obstacle blocks sensor

  // Permanent Lockout Active: While ambulance sits on desk, normal traffic runs continuously!
  // Lockout resets only when ambulance button is toggled OFF (emergency = false)

  // =========================================================================
  // 🚨 EMERGENCY PRIORITY MODE
  // =========================================================================
  if (emergencyMode) {
    // Keep Lane 1 GREEN (Ambulance corridor) and Lane 2 RED (Cross traffic)
    setTL1(false, false, true); 
    setTL2(true, false, false); 

    // --- CASE A: VEHICLE PASSAGE CONFIRMED BY IR SENSOR ---
    if (vehiclePassedDetected) {
      unsigned long elapsed = currentMillis - vehiclePassedTime;
      
      // Wait for exact 5 SECONDS delay before returning to normal!
      if (elapsed >= CLEARANCE_DURATION_MS) {
        emergencyMode = false;
        vehiclePassedDetected = false;
        irArmed = false;
        ambulancePassedLockout = true; // Lock out until next ambulance run

        streamPrintln("EVENT: AMBULANCE_PASSED_TURNING_TO_NORMAL");
        
        // Immediate Transition to Normal Phase 0 (Lane 1 RED, Lane 2 GREEN)
        trafficPhase = 0;
        lastPhaseChangeTime = millis();
        setTL1(true, false, false);
        setTL2(false, false, true);
        
        updateLcd("AMBULANCE PASSED", "NORMAL RESUMED");
        delay(800);
        return;
      } else {
        int secRemaining = (CLEARANCE_DURATION_MS - elapsed) / 1000 + 1;
        updateLcd("AMBULANCE PASSED", "NORMAL IN " + String(secRemaining) + " SEC");
      }
    } 
    // --- CASE B: EMERGENCY ACTIVE, WAITING FOR IR SENSOR PASS OR OUT-OF-RANGE ---
    else {
      // 1. Arm the IR sensor after 500ms of entering emergency to prevent false instant triggers
      if (!irArmed) {
        if (currentMillis - emergencyStartTime > 500) {
          irInitialState = rawIr;
          irArmed = true;
          irSawVehicle = false;
        }
      } 
      else {
        // 2. Detect actual vehicle arrival and departure across IR sensor:
        // Baseline HIGH (standard IR sensor): vehicle makes it LOW, then clears to HIGH!
        if (irInitialState == HIGH) {
          if (rawIr == LOW) {
            irSawVehicle = true; // Vehicle has reached the sensor
          }
          if (irSawVehicle && rawIr == HIGH) {
            // Vehicle cleared and sensor BECOMES HIGH again -> Start 5-second countdown!
            vehiclePassedDetected = true;
            vehiclePassedTime = currentMillis;
            streamPrintln("EVENT: IR_SENSOR_VERIFIED_AMBULANCE_PASSED (BECOMES HIGH)");
            updateLcd("AMBULANCE PASSED", "NORMAL IN 5 SEC");
          }
        } 
        // Baseline LOW (Active HIGH sensor): vehicle makes it HIGH!
        else {
          if (rawIr == HIGH) {
            vehiclePassedDetected = true;
            vehiclePassedTime = currentMillis;
            streamPrintln("EVENT: IR_SENSOR_VERIFIED_AMBULANCE_PASSED (BECOMES HIGH)");
            updateLcd("AMBULANCE PASSED", "NORMAL IN 5 SEC");
          }
        }
      }

      // 3. Fail-Safe: If ambulance drives away or powers off without triggering IR sensor
      if (!vehiclePassedDetected && (currentMillis - lastEmergencyPacketTime > OUT_OF_RANGE_TIMEOUT_MS)) {
        emergencyMode = false;
        vehiclePassedDetected = false;
        irArmed = false;
        ambulancePassedLockout = true;

        streamPrintln("EVENT: OUT_OF_RANGE_NORMAL_RESTORED");
        
        trafficPhase = 0;
        lastPhaseChangeTime = millis();
        setTL1(true, false, false);
        setTL2(false, false, true);
        
        updateLcd("OUT OF RANGE", "NORMAL RESUMED");
        delay(1000);
        return;
      } 
      // 4. While waiting for vehicle, display alternating emergency warning
      else if (!vehiclePassedDetected) {
        if (currentMillis - lastLcdToggleTime >= 1000) {
          lastLcdToggleTime = currentMillis;
          emgBannerToggle = !emgBannerToggle;
        }
        if (emgBannerToggle) updateLcd("!AMBULANCE NEAR!", "CLEAR LANE FAST!");
        else updateLcd("PRIORITY ACTIVE", "L1:GRN  L2:RED");
      }
    }
  } 
  // =========================================================================
  // 🟢 NORMAL 2-WAY TRAFFIC CYCLE (5s Green, 2s Yellow)
  // =========================================================================
  else {
    switch (trafficPhase) {
      case 0: // Lane 1 RED (5s) | Lane 2 GREEN
        setTL1(true, false, false); setTL2(false, false, true);
        { 
          int sec = (PHASE_MAIN_DURATION_MS - (currentMillis - lastPhaseChangeTime)) / 1000 + 1;
          updateLcd("TL1:RED   TL2:GRN", "NEXT IN " + String(sec) + "s"); 
        }
        if (currentMillis - lastPhaseChangeTime >= PHASE_MAIN_DURATION_MS) {
          trafficPhase = 1; lastPhaseChangeTime = currentMillis;
        }
        break;
        
      case 1: // Lane 1 YELLOW (2s) | Lane 2 YELLOW
        setTL1(false, true, false); setTL2(false, true, false);
        updateLcd("TL1:YEL   TL2:YEL", "CAUTION / PREPARE");
        if (currentMillis - lastPhaseChangeTime >= PHASE_YELLOW_DURATION_MS) {
          trafficPhase = 2; lastPhaseChangeTime = currentMillis;
        }
        break;
        
      case 2: // Lane 1 GREEN (5s) | Lane 2 RED
        setTL1(false, false, true); setTL2(true, false, false);
        { 
          int sec = (PHASE_MAIN_DURATION_MS - (currentMillis - lastPhaseChangeTime)) / 1000 + 1;
          updateLcd("TL1:GRN   TL2:RED", "NEXT IN " + String(sec) + "s"); 
        }
        if (currentMillis - lastPhaseChangeTime >= PHASE_MAIN_DURATION_MS) {
          trafficPhase = 3; lastPhaseChangeTime = currentMillis;
        }
        break;
        
      case 3: // Lane 1 YELLOW (2s) | Lane 2 YELLOW
        setTL1(false, true, false); setTL2(false, true, false);
        updateLcd("TL1:YEL   TL2:YEL", "CAUTION / PREPARE");
        if (currentMillis - lastPhaseChangeTime >= PHASE_YELLOW_DURATION_MS) {
          trafficPhase = 0; lastPhaseChangeTime = currentMillis;
        }
        break;
    }
  }

  // Real-time telemetry stream (every 250ms) to USB & Pi GPIO
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = currentMillis;
    String t = "STATUS:MODE=" + String(emergencyMode ? (vehiclePassedDetected ? "PASSED_5S" : "EMERGENCY") : "NORMAL");
    t += ",L1=" + getTL1Color() + ",L2=" + getTL2Color();
    t += ",IR=" + String((rawIr == LOW) ? "DETECTED" : "CLEAR");
    t += ",LCD1=" + currentLcdLine1 + ",LCD2=" + currentLcdLine2;
    streamPrintln(t);
  }
  
  delay(20);
}
