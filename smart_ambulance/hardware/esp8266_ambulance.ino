#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================================================================
// 📺 0.96" I2C OLED DISPLAY SETUP (SSD1306)
// =========================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Free pins on NodeMCU so existing Button (D5), Buzzer (D2), and LED (D1) remain unchanged!
#define OLED_SDA_PIN D6 // Connect to OLED SDA
#define OLED_SCL_PIN D7 // Connect to OLED SCL

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledFound = false;

// =========================================================================
// 🚑 AMBULANCE HARDWARE PINS (UNCHANGED)
// =========================================================================
#define BUTTON_PIN D5
#define BUZZER_PIN D2
#define LED_PIN    D1

// REPLACE WITH MAC ADDRESS OF THE TRAFFIC ESP32
uint8_t broadcastAddress[] = {0xB0, 0xCB, 0xD8, 0x0C, 0xE2, 0xD4}; // Traffic ESP32 MAC Address

typedef struct struct_message {
    bool emergency;
} struct_message;

struct_message myData;
bool isEmergency = false;
String lastDeliveryStatus = "Ready";

// Draws the live ambulance status on the OLED
void updateOled(String title, String statusText, String detailText, String txStatus) {
  if (!oledFound) return;
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header / Title Banner
  display.setTextSize(1);
  display.setCursor(18, 2);
  display.print(title);
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

  // Status Value (Large Text)
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

  // Bottom Status Bar (Tx status)
  display.setCursor(0, 56);
  display.print("ESP-NOW: ");
  display.print(txStatus);

  display.display();
}

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    lastDeliveryStatus = "Sent OK";
    Serial.println("Delivery Success");
  } else {
    lastDeliveryStatus = "Tx Fail";
    Serial.println("Delivery Fail");
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // Initialize I2C for OLED on D6 (SDA) and D7 (SCL)
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

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    if (oledFound) updateOled("AMBULANCE UNIT", "ERROR", "ESP-NOW Failed", "Err");
    return;
  }

  // Set ESP8266 role and register callback
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer (The ESP32)
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  
  Serial.println("ESP8266 Ambulance Unit Ready.");
  updateOled("AMBULANCE UNIT", "STANDBY", "System Ready", "Listening");
}

void loop() {
  // Read physical button with debounce
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      isEmergency = !isEmergency; // Toggle state
      
      myData.emergency = isEmergency;
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

      // Update OLED screen immediately
      if (isEmergency) {
        updateOled("! EMERGENCY !", "ACTIVE", "Priority Request", lastDeliveryStatus);
      } else {
        updateOled("AMBULANCE UNIT", "STANDBY", "Normal Mode", lastDeliveryStatus);
      }
      
      while(digitalRead(BUTTON_PIN) == LOW); // Wait for release
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
    
    // Periodically resend emergency signal and update OLED
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    updateOled("! EMERGENCY !", "ACTIVE", "Priority Request", lastDeliveryStatus);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}
