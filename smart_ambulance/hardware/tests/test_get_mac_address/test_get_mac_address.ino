// Phase B: ESP-NOW GET MAC ADDRESS (Upload to Traffic ESP32)
#include "WiFi.h"

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("THIS ESP32 MAC ADDRESS: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
