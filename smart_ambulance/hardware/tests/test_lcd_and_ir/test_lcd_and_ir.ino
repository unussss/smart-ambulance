/*
 =========================================================================
 🔬 30-SECOND HARDWARE TEST: 16x2 I2C LCD & IR SENSOR (ESP32)
 =========================================================================
 Upload this sketch to test:
   1. The 16x2 I2C LCD Display (Auto-tries address 0x27 & 0x3F).
   2. The IR Sensor module connected to GPIO 14.

 LCD Wiring:
   - GND -> ESP32 GND
   - VCC -> ESP32 VIN (5V)
   - SDA -> ESP32 GPIO 21
   - SCL -> ESP32 GPIO 22

 IR Sensor Wiring:
   - GND -> ESP32 GND
   - VCC -> ESP32 3.3V (or 5V VIN)
   - OUT -> ESP32 GPIO 14
 =========================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define IR_SENSOR_PIN 14

LiquidCrystal_I2C lcd27(0x27, 16, 2);
LiquidCrystal_I2C lcd3F(0x3F, 16, 2);
LiquidCrystal_I2C* lcd = &lcd27;
bool lcdFound = false;
byte detectedAddr = 0x27;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========================================================");
  Serial.println("🔬 STARTING 16x2 LCD & IR SENSOR TEST");
  Serial.println("========================================================");

  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  // Scan I2C
  Serial.println("Scanning I2C on SDA(GPIO 21) & SCL(GPIO 22)...");
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    lcd = &lcd27;
    lcdFound = true;
    detectedAddr = 0x27;
    Serial.println("✅ I2C Device Found at 0x27!");
  } else {
    Wire.beginTransmission(0x3F);
    if (Wire.endTransmission() == 0) {
      lcd = &lcd3F;
      lcdFound = true;
      detectedAddr = 0x3F;
      Serial.println("✅ I2C Device Found at 0x3F!");
    } else {
      Serial.println("⚠️ Scan did not acknowledge 0x27/0x3F. Trying default 0x27...");
      lcd = &lcd27;
      lcdFound = true;
    }
  }

  // Initialize LCD
  lcd->init();
  lcd->backlight();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("LCD WORKING! OK");
  lcd->setCursor(0, 1);
  lcd->print("IR: WAITING...");

  Serial.println("✅ Text sent to LCD!");
  Serial.println("👉 IMPORTANT: If screen is blue with NO letters, turn the");
  Serial.println("   small BLUE POTENTIOMETER screw on the back with a screwdriver!");
}

void loop() {
  // Read IR Sensor (Active LOW: LOW when obstacle in front, HIGH when clear)
  int irState = digitalRead(IR_SENSOR_PIN);
  bool obstacleDetected = (irState == LOW);

  if (obstacleDetected) {
    Serial.println("🔍 IR SENSOR: [DETECTED] Obstacle in front of sensor!");
    lcd->setCursor(0, 1);
    lcd->print("IR: DETECTED!   ");
  } else {
    Serial.println("🔍 IR SENSOR: [CLEAR] Path is clear");
    lcd->setCursor(0, 1);
    lcd->print("IR: CLEAR       ");
  }

  delay(300);
}
