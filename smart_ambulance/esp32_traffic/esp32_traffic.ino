#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =========================================================================
// 📺 16x2 I2C LCD DISPLAY SETUP (Auto-detects address 0x27 or 0x3F)
// =========================================================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

LiquidCrystal_I2C lcd27(0x27, 16, 2);
LiquidCrystal_I2C lcd3F(0x3F, 16, 2);
LiquidCrystal_I2C* lcd = &lcd27; // Default pointer
bool lcdFound = false;

// =========================================================================
// 🚦 TRAFFIC LIGHT 1 (LANE 1: AMBULANCE CORRIDOR)
// =========================================================================
#define RED1_PIN    25
#define YELLOW1_PIN 26
#define GREEN1_PIN  27

// =========================================================================
// 🚦 TRAFFIC LIGHT 2 (LANE 2: CROSS TRAFFIC)
// ⚠️ NOTE: GPIO 35 is an INPUT-ONLY pin on ESP32!
// We use GPIO 18 for Green 2 so it can safely drive the LED!
// =========================================================================
#define RED2_PIN    32
#define YELLOW2_PIN 33
#define GREEN2_PIN  18  // Safe output pin right next to 19!

// =========================================================================
// 📏 ULTRASONIC SENSOR (HC-SR04: PHYSICAL PASSING VERIFICATION)
// =========================================================================
#define TRIG_PIN    14
#define ECHO_PIN    12
const int DETECTION_THRESHOLD_CM = 30; // Under 30cm confirms vehicle passage

// =========================================================================
// ESP-NOW DATA STRUCTURE
// =========================================================================
typedef struct struct_message {
    bool emergency;
} struct_message;

struct_message myData;

// State Machine Variables
bool emergencyMode = false;
bool vehiclePassedDetected = false;
unsigned long vehiclePassedTime = 0;
const unsigned long CLEARANCE_DURATION_MS = 3000; // 3 seconds clearance shutdown
unsigned long lastEmergencyPacketTime = 0;
const unsigned long OUT_OF_RANGE_TIMEOUT_MS = 5000; // 5 seconds disconnect timeout

// Coordinated Traffic Timers (5s Green/Red, 2s Yellow)
// Phase 0: TL1 RED    | TL2 GREEN   (5s)
// Phase 1: TL1 YELLOW | TL2 YELLOW  (2s)
// Phase 2: TL1 GREEN  | TL2 RED     (5s)
// Phase 3: TL1 YELLOW | TL2 YELLOW  (2s)
int trafficPhase = 0;
unsigned long lastPhaseChangeTime = 0;
const unsigned long PHASE_MAIN_DURATION_MS   = 5000; // 5 Seconds
const unsigned long PHASE_YELLOW_DURATION_MS = 2000; // 2 Seconds

// Sensor & Telemetry Timers
unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 250;
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_INTERVAL_MS = 150;
long currentDistanceCm = 999;

// LCD Caching to avoid flicker
String currentLcdLine1 = "";
String currentLcdLine2 = "";
unsigned long lastLcdToggleTime = 0;
bool emgBannerToggle = false;

// LCD Update Helper
void updateLcd(String line1, String line2) {
  if (!lcdFound) return;
  if (line1 != currentLcdLine1 || line2 != currentLcdLine2) {
    currentLcdLine1 = line1;
    currentLcdLine2 = line2;
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print(line1.substring(0, 16));
    lcd->setCursor(0, 1);
    lcd->print(line2.substring(0, 16));
  }
}

// Helper: Set Traffic Light 1 (Red, Yellow, Green)
void setTL1(bool r, bool y, bool g) {
  digitalWrite(RED1_PIN,    r ? HIGH : LOW);
  digitalWrite(YELLOW1_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN1_PIN,  g ? HIGH : LOW);
}

// Helper: Set Traffic Light 2 (Red, Yellow, Green)
void setTL2(bool r, bool y, bool g) {
  digitalWrite(RED2_PIN,    r ? HIGH : LOW);
  digitalWrite(YELLOW2_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN2_PIN,  g ? HIGH : LOW);
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

// ESP-NOW Receive Callback
void OnDataRecv(const esp_now_recv_info * mac_info, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  
  if (myData.emergency) {
    if (!emergencyMode && !vehiclePassedDetected) {
      Serial.println("EVENT: EMERGENCY_REQUEST_RECEIVED");
      vehiclePassedDetected = false;
    }
    if (!vehiclePassedDetected) {
      emergencyMode = true;
      lastEmergencyPacketTime = millis();
    }
  } else {
    if (emergencyMode && !vehiclePassedDetected) {
      emergencyMode = false;
      Serial.println("EVENT: EMERGENCY_CANCELLED_BY_AMBULANCE");
      updateLcd("AMBULANCE CANCEL", "RESUMING NORMAL");
      delay(600);
      lastPhaseChangeTime = millis();
      trafficPhase = 0;
    }
  }
}

// Robust HC-SR04 ultrasonic distance reading
long readUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout (~5 meters)
  if (duration == 0) return 999; // No echo
  return duration * 0.034 / 2;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  
  // 1. Configure LED outputs
  pinMode(RED1_PIN,    OUTPUT);
  pinMode(YELLOW1_PIN, OUTPUT);
  pinMode(GREEN1_PIN,  OUTPUT);
  
  pinMode(RED2_PIN,    OUTPUT);
  pinMode(YELLOW2_PIN, OUTPUT);
  pinMode(GREEN2_PIN,  OUTPUT);
  
  // 2. Configure Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // 3. Auto-detect I2C LCD on address 0x27 or 0x3F
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    lcd = &lcd27;
    lcdFound = true;
    Serial.println("EVENT: I2C_LCD_CONNECTED_AT_0x27");
  } else {
    Wire.beginTransmission(0x3F);
    if (Wire.endTransmission() == 0) {
      lcd = &lcd3F;
      lcdFound = true;
      Serial.println("EVENT: I2C_LCD_CONNECTED_AT_0x3F");
    } else {
      Serial.println("EVENT: I2C_LCD_NOT_FOUND_CHECK_WIRING");
      lcdFound = false;
    }
  }

  if (lcdFound) {
    lcd->init();
    lcd->backlight();
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print("SMART TRAFFIC");
    lcd->setCursor(0, 1);
    lcd->print("COORDINATED V2");
  }

  // Initial Safe Baseline:
  // TL1 starts RED | TL2 starts GREEN
  setTL1(true, false, false);
  setTL2(false, false, true);
  delay(1000);

  // 4. Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("EVENT: ERROR_ESP_NOW_INIT");
    updateLcd("ESP-NOW ERROR", "CHECK SETTINGS");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);

  lastPhaseChangeTime = millis();
  trafficPhase = 0; // Starts at Phase 0: TL1 RED | TL2 GREEN
  
  Serial.println("EVENT: SYSTEM_READY");
  Serial.println("EVENT: COORDINATED_TRAFFIC_ACTIVE");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Process Dashboard Demo Commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "CMD:DEMO_EMERGENCY") {
      emergencyMode = true;
      vehiclePassedDetected = false;
      lastEmergencyPacketTime = currentMillis;
      Serial.println("EVENT: MANUAL_EMERGENCY_TRIGGERED");
    } else if (cmd == "CMD:DEMO_VEHICLE_PASSED") {
      if (emergencyMode) {
        vehiclePassedDetected = true;
        vehiclePassedTime = currentMillis;
        Serial.println("EVENT: DEMO_VEHICLE_PASSED_JUNCTION");
      }
    } else if (cmd == "CMD:DEMO_NORMAL") {
      emergencyMode = false;
      vehiclePassedDetected = false;
      Serial.println("EVENT: MANUAL_NORMAL_RESTORED");
      lastPhaseChangeTime = currentMillis;
      trafficPhase = 0;
    }
  }

  // 2. Read Ultrasonic Sensor (Physical Verification Only)
  if (currentMillis - lastSensorReadTime >= SENSOR_INTERVAL_MS) {
    lastSensorReadTime = currentMillis;
    currentDistanceCm = readUltrasonicDistance();
  }

  // 3. Traffic Lights & Coordination Logic
  if (emergencyMode) {
    // 🚨 EMERGENCY OVERRIDE:
    // Lane 1 (Ambulance Lane) = GREEN
    // Lane 2 (Cross Traffic)  = RED
    setTL1(false, false, true);
    setTL2(true, false, false);

    // CASE A: VEHICLE HAS PHYSICALLY PASSED THE SENSOR (< 30cm)
    if (vehiclePassedDetected) {
      unsigned long elapsedSincePassed = currentMillis - vehiclePassedTime;
      
      if (elapsedSincePassed >= CLEARANCE_DURATION_MS) {
        // 3 Seconds finished! Return smoothly to coordinated cycle
        emergencyMode = false;
        vehiclePassedDetected = false;
        Serial.println("EVENT: AMBULANCE_PASSED_TURNING_TO_NORMAL");
        updateLcd("AMBULANCE PASSED", "TURNING TO NORMAL");
        delay(800);
        lastPhaseChangeTime = millis();
        trafficPhase = 0; // Re-enter at TL1 RED | TL2 GREEN
        return;
      } else {
        int secRemaining = (CLEARANCE_DURATION_MS - elapsedSincePassed) / 1000 + 1;
        updateLcd("AMBULANCE PASSED", "NORMAL IN " + String(secRemaining) + " SEC");
      }
    } 
    // CASE B: VEHICLE APPROACHING (AWAITING PHYSICAL CROSSING OR TIMEOUT)
    else {
      // Check Ultrasonic Sensor
      if (currentDistanceCm > 0 && currentDistanceCm <= DETECTION_THRESHOLD_CM) {
        vehiclePassedDetected = true;
        vehiclePassedTime = currentMillis;
        Serial.print("EVENT: ULTRASONIC_VERIFIED_AMBULANCE_PASSED (Distance: ");
        Serial.print(currentDistanceCm);
        Serial.println(" cm)");
        Serial.println("EVENT: CLEARANCE_COUNTDOWN_3_SEC_STARTED");
        updateLcd("AMBULANCE PASSED", "NORMAL IN 3 SEC");
      } 
      // Check Out of Range / Disconnect Timeout (No radio packets for > 5s)
      else if (currentMillis - lastEmergencyPacketTime > OUT_OF_RANGE_TIMEOUT_MS) {
        emergencyMode = false;
        vehiclePassedDetected = false;
        Serial.println("EVENT: OUT_OF_RANGE_DISCONNECT");
        Serial.println("EVENT: RESUMING_NORMAL_MODE");
        updateLcd("OUT OF RANGE", "TURNING TO NORMAL");
        delay(1000);
        lastPhaseChangeTime = millis();
        trafficPhase = 0;
        return;
      } 
      // Approaching Warning Flash
      else {
        if (currentMillis - lastLcdToggleTime >= 1000) {
          lastLcdToggleTime = currentMillis;
          emgBannerToggle = !emgBannerToggle;
        }

        if (emgBannerToggle) {
          updateLcd("!AMBULANCE NEAR!", "CLEAR LANE FAST!");
        } else {
          updateLcd("PRIORITY ACTIVE", "L1:GRN  L2:RED");
        }
      }
    }
  } 
  // 🚦 COORDINATED 2-WAY TRAFFIC CYCLE:
  // TL1: RED (5s)   ➔ YELLOW (2s) ➔ GREEN (5s) ➔ YELLOW (2s) ➔ RED
  // TL2: GREEN (5s) ➔ YELLOW (2s) ➔ RED (5s)   ➔ YELLOW (2s) ➔ GREEN
  else {
    switch (trafficPhase) {
      case 0:
        // PHASE 0: TL1 RED (5s) | TL2 GREEN (5s)
        setTL1(true, false, false);
        setTL2(false, false, true);
        {
          int remainingSec = (PHASE_MAIN_DURATION_MS - (currentMillis - lastPhaseChangeTime)) / 1000 + 1;
          updateLcd("TL1:RED   TL2:GRN", "NEXT IN " + String(remainingSec) + "s");
        }
        if (currentMillis - lastPhaseChangeTime >= PHASE_MAIN_DURATION_MS) {
          trafficPhase = 1;
          lastPhaseChangeTime = currentMillis;
        }
        break;

      case 1:
        // PHASE 1: TL1 YELLOW (2s) | TL2 YELLOW (2s)
        // (TL1 preparing to turn GREEN, TL2 preparing to turn RED!)
        setTL1(false, true, false);
        setTL2(false, true, false);
        updateLcd("TL1:YEL   TL2:YEL", "CAUTION / PREPARE");
        if (currentMillis - lastPhaseChangeTime >= PHASE_YELLOW_DURATION_MS) {
          trafficPhase = 2;
          lastPhaseChangeTime = currentMillis;
        }
        break;

      case 2:
        // PHASE 2: TL1 GREEN (5s) | TL2 RED (5s)
        setTL1(false, false, true);
        setTL2(true, false, false);
        {
          int remainingSec = (PHASE_MAIN_DURATION_MS - (currentMillis - lastPhaseChangeTime)) / 1000 + 1;
          updateLcd("TL1:GRN   TL2:RED", "NEXT IN " + String(remainingSec) + "s");
        }
        if (currentMillis - lastPhaseChangeTime >= PHASE_MAIN_DURATION_MS) {
          trafficPhase = 3;
          lastPhaseChangeTime = currentMillis;
        }
        break;

      case 3:
        // PHASE 3: TL1 YELLOW (2s) | TL2 YELLOW (2s)
        // (TL1 preparing to turn RED, TL2 preparing to turn GREEN!)
        setTL1(false, true, false);
        setTL2(false, true, false);
        updateLcd("TL1:YEL   TL2:YEL", "CAUTION / PREPARE");
        if (currentMillis - lastPhaseChangeTime >= PHASE_YELLOW_DURATION_MS) {
          trafficPhase = 0; // Loops back to Phase 0!
          lastPhaseChangeTime = currentMillis;
        }
        break;
    }
  }

  // 4. Send Real-Time Telemetry to Dashboard (Every 250ms)
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = currentMillis;
    
    Serial.print("STATUS:MODE=");
    if (emergencyMode) {
      if (vehiclePassedDetected) Serial.print("PASSED_3S");
      else Serial.print("EMERGENCY");
    } else {
      Serial.print("NORMAL");
    }
    Serial.print(",L1=");
    Serial.print(getTL1Color());
    Serial.print(",L2=");
    Serial.print(getTL2Color());
    Serial.print(",DIST=");
    Serial.print(currentDistanceCm);
    Serial.print(",LCD1=");
    Serial.print(currentLcdLine1);
    Serial.print(",LCD2=");
    Serial.println(currentLcdLine2);
  }

  delay(20);
}
