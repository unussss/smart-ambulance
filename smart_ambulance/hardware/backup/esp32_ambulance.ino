#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================================================================
// 📺 0.96" I2C OLED DISPLAY SETUP (SSD1306 on ESP32 Hardware I2C)
// =========================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Standard ESP32 Hardware I2C Pins (Labels match silkscreen!)
#define OLED_SDA_PIN 21 // Pin labeled 'D21' on ESP32
#define OLED_SCL_PIN 22 // Pin labeled 'D22' on ESP32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledFound = false;

// =========================================================================
// 🚑 AMBULANCE HARDWARE PINS (ESP32 - MATCHING LABELS!)
// =========================================================================
#define BUTTON_PIN 5   // Pin labeled 'D5' on ESP32 (Emergency Toggle Push Button)
#define BUZZER_PIN 2   // Pin labeled 'D2' on ESP32 (Siren Buzzer)
#define LED_PIN    4   // Pin labeled 'D4' on ESP32 (Red Emergency Beacon LED)

// TARGET: Traffic ESP32 MAC Address
uint8_t broadcastAddress[] = {0xB0, 0xCB, 0xD8, 0x0C, 0xE2, 0xD4};

// ESP-NOW Data Structure (Matches Traffic Unit Exactly!)
typedef struct struct_message {
    bool emergency;
} struct_message;

struct_message myData;
bool isEmergency = false;
String lastDeliveryStatus = "Ready";

// Draws the live ambulance status on the OLED screen
void updateOled(String title, String statusText, String detailText, String txStatus) {
  if (!oledFound) return;
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title Header Banner
  display.setTextSize(1);
  display.setCursor(18, 2);
  display.print(title);
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

  // Status Label & Large Value
  display.setTextSize(1);
  display.setCursor(0, 18);
  display.print("STATUS:");
  
  display.setTextSize(2);
  display.setCursor(0, 28);
  display.print(statusText);

  // Detail / Info
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print(detailText);

  // Bottom Status Bar (ESP-NOW Tx Delivery Status)
  display.setCursor(0, 56);
  display.print("ESP-NOW: ");
  display.print(txStatus);

  display.display();
}

// Callback when data is sent over ESP-NOW (Compatible with ESP32 Core v2 and v3)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  if (status == ESP_NOW_SEND_SUCCESS) {
    lastDeliveryStatus = "Sent OK";
    Serial.println("ESP-NOW: Delivery Success");
  } else {
    lastDeliveryStatus = "Tx Fail";
    Serial.println("ESP-NOW: Delivery Fail");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // Initialize Hardware I2C for OLED on D21 (SDA) and D22 (SCL)
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  delay(100);

  // Auto-detect OLED address 0x3C (default) or 0x3D
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledFound = true;
  } else if (display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    oledFound = true;
  } else {
    Serial.println("Warning: OLED display not found on I2C bus.");
  }

  if (oledFound) {
    display.clearDisplay();
    updateOled("AMBULANCE UNIT", "STANDBY", "System Ready", "Idle");
  }

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    if (oledFound) updateOled("AMBULANCE UNIT", "ERROR", "ESP-NOW Failed", "Err");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  // Register Peer (Traffic ESP32)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add Traffic ESP32 as peer");
    if (oledFound) updateOled("AMBULANCE UNIT", "ERROR", "Peer Add Fail", "Err");
    return;
  }

  Serial.println("ESP32 Ambulance Unit Ready.");
  updateOled("AMBULANCE UNIT", "STANDBY", "System Ready", "Listening");
}

void loop() {
  // Read physical emergency push button with debounce
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      isEmergency = !isEmergency; // Toggle state

      myData.emergency = isEmergency;
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

      // Update OLED screen immediately
      if (isEmergency) {
        updateOled("! EMERGENCY !", "ACTIVE", "Priority Request", lastDeliveryStatus);
        Serial.println(">> Emergency Mode ACTIVATED!");
      } else {
        updateOled("AMBULANCE UNIT", "STANDBY", "Normal Mode", lastDeliveryStatus);
        Serial.println(">> Emergency Mode CANCELLED (Resuming Normal Traffic)");
      }

      while (digitalRead(BUTTON_PIN) == LOW); // Wait for release
    }
  }

  // Emergency Siren, Beacon & Periodic Broadcast
  if (isEmergency) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    delay(200);

    // Periodically re-transmit emergency packet
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    updateOled("! EMERGENCY !", "ACTIVE", "Priority Request", lastDeliveryStatus);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}
