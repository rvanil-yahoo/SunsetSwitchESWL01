#include <Arduino.h>

#define BLINK_PIN 5        // GPIO5
#define BLINK_INTERVAL 500 // milliseconds

void setup() {
    Serial.begin(115200);
    pinMode(BLINK_PIN, OUTPUT);
    Serial.println("Blink started on GPIO5");
}

void loop() {
    digitalWrite(BLINK_PIN, HIGH);
    delay(BLINK_INTERVAL);
    digitalWrite(BLINK_PIN, LOW);
    delay(BLINK_INTERVAL);
}