// Phase A: Ambulance Hardware Test
#define BUTTON_PIN 27
#define BUZZER_PIN 26
#define LED_PIN 25

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("Ambulance Hardware Test Ready. Press the button!");
}

void loop() {
  // If button is pressed (LOW due to INPUT_PULLUP)
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Button PRESSED -> Buzzer ON, LED ON");
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
  delay(100);
}
