#include <ESP8266WiFi.h>
#include <espnow.h>

// ESP8266 NodeMCU Pins
#define BUTTON_PIN D5
#define BUZZER_PIN D2
#define LED_PIN D1

// REPLACE WITH MAC ADDRESS OF THE TRAFFIC ESP32
uint8_t broadcastAddress[] = {0xB0, 0xCB, 0xD8, 0x0C, 0xE2, 0xD4}; // Traffic ESP32 MAC Address

typedef struct struct_message {
    bool emergency;
} struct_message;

struct_message myData;
bool isEmergency = false;

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0){
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // Set device as a Wi-Fi Station with Maximum RF Output Power (20.5 dBm)
  WiFi.mode(WIFI_STA);
  WiFi.setOutputPower(20.5);
  WiFi.disconnect();

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Set ESP8266 role and register callback
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer (The ESP32)
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  
  Serial.println("ESP8266 Ambulance Unit Ready.");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      isEmergency = !isEmergency; // Toggle state
      
      myData.emergency = isEmergency;
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
      
      while(digitalRead(BUTTON_PIN) == LOW); // Wait for release
    }
  }

  if (isEmergency) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    delay(200);
    
    // Periodically resend emergency signal
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}
