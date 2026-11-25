/*
 =========================================================================
 🔬 HARDWARE DIAGNOSTIC TEST: 16x2 I2C LCD & HC-SR04 ULTRASONIC SENSOR
 =========================================================================
 Upload this sketch to immediately find:
   1. The EXACT I2C address of your LCD (0x27, 0x3F, or others).
   2. Whether your HC-SR04 ultrasonic sensor is receiving 5V power and returning echo pulses.
 =========================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

#define TRIG_PIN 14
#define ECHO_PIN 12

LiquidCrystal_I2C* lcd = nullptr;
byte detectedAddress = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n" + String('=', 65));
  Serial.println("🔍 STARTING LCD & ULTRASONIC HARDWARE DIAGNOSTIC SCAN");
  Serial.println(String('=', 65));

  // 1. Configure Ultrasonic Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // 2. Initialize I2C Bus on GPIO 21 & 22
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  // 3. Scan all 127 I2C Addresses
  Serial.println("\n[1/2] Scanning I2C Bus on SDA(GPIO 21) & SCL(GPIO 22)...");
  int devicesFound = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("  ✅ I2C DEVICE FOUND at address: 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);

      if (address == 0x27 || address == 0x3F) {
        Serial.println(" <-- Standard 16x2 LCD backpack!");
        detectedAddress = address;
      } else {
        Serial.println();
      }
      devicesFound++;
    }
  }

  if (devicesFound == 0) {
    Serial.println("  ❌ NO I2C DEVICES DETECTED!");
    Serial.println("  --> Check 1: Is LCD VCC connected to VIN (5V)? (Not 3.3V!)");
    Serial.println("  --> Check 2: Is LCD GND connected to ESP32 GND?");
    Serial.println("  --> Check 3: Is SDA connected to GPIO 21?");
    Serial.println("  --> Check 4: Is SCL connected to GPIO 22?");
  } else if (detectedAddress != 0) {
    Serial.print("\n[SUCCESS] Initializing LCD at address 0x");
    Serial.println(detectedAddress, HEX);
    
    lcd = new LiquidCrystal_I2C(detectedAddress, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print("LCD WORKING! OK");
    lcd->setCursor(0, 1);
    lcd->print("ADDR: 0x" + String(detectedAddress, HEX));

    Serial.println("  ✅ Text sent to LCD!");
    Serial.println("  💡 TIP: If screen has light but no letters, turn the BLUE CONTRAST screw on the back!");
  }

  Serial.println("\n[2/2] Starting Continuous Ultrasonic Distance Reading...\n");
}

void loop() {
  // Trigger HC-SR04 pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure Echo pulse (40ms timeout)
  long duration = pulseIn(ECHO_PIN, HIGH, 40000);
  
  if (duration == 0) {
    Serial.println("📏 ULTRASONIC: TIMEOUT / NO ECHO");
    Serial.println("   --> Check 1: Is HC-SR04 VCC connected to VIN (5V)? (3.3V will NOT work!)");
    Serial.println("   --> Check 2: Is TRIG wired to GPIO 14?");
    Serial.println("   --> Check 3: Is ECHO wired to GPIO 12?");
    
    if (lcd && detectedAddress != 0) {
      lcd->setCursor(0, 1);
      lcd->print("DIST: NO ECHO   ");
    }
  } else {
    long distance = duration * 0.034 / 2;
    Serial.print("📏 ULTRASONIC DISTANCE: ");
    Serial.print(distance);
    Serial.print(" cm  (Pulse Duration: ");
    Serial.print(duration);
    Serial.println(" us)");

    if (lcd && detectedAddress != 0) {
      lcd->setCursor(0, 1);
      lcd->print("DIST: " + String(distance) + " cm     ");
    }
  }

  delay(600);
}
