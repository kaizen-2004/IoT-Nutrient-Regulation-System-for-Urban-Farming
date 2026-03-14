#include <Arduino.h>

const uint32_t SERIAL_BAUD = 115200;

const uint8_t WATER_RELAY_PIN = 6;
const uint8_t NUTRIENT_RELAY_PIN = 7;

// Match this with your relay hardware behavior.
// true  = HIGH energizes relay
// false = LOW energizes relay
const bool RELAY_ACTIVE_HIGH = true;

const uint32_t PRE_DELAY_MS = 2000;
const uint32_t PULSE_MS = 2500;
const uint32_t GAP_MS = 2000;

void setRelay(uint8_t pin, bool on)
{
  bool level = on ? RELAY_ACTIVE_HIGH : !RELAY_ACTIVE_HIGH;
  digitalWrite(pin, level ? HIGH : LOW);
}

void allRelaysOff()
{
  setRelay(WATER_RELAY_PIN, false);
  setRelay(NUTRIENT_RELAY_PIN, false);
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 05: Relay / Valve Test ===");
  Serial.printf("Water relay pin: GPIO%u\n", WATER_RELAY_PIN);
  Serial.printf("Nutrient relay pin: GPIO%u\n", NUTRIENT_RELAY_PIN);
  Serial.printf("RELAY_ACTIVE_HIGH: %s\n", RELAY_ACTIVE_HIGH ? "true" : "false");
  Serial.println("WARNING: Keep hydraulic path safe before testing.");

  pinMode(WATER_RELAY_PIN, OUTPUT);
  pinMode(NUTRIENT_RELAY_PIN, OUTPUT);
  allRelaysOff();
}

void loop()
{
  Serial.println("\nCycle start in 2s...");
  delay(PRE_DELAY_MS);

  Serial.println("Water relay ON");
  setRelay(WATER_RELAY_PIN, true);
  delay(PULSE_MS);
  Serial.println("Water relay OFF");
  setRelay(WATER_RELAY_PIN, false);

  delay(GAP_MS);

  Serial.println("Nutrient relay ON");
  setRelay(NUTRIENT_RELAY_PIN, true);
  delay(PULSE_MS);
  Serial.println("Nutrient relay OFF");
  setRelay(NUTRIENT_RELAY_PIN, false);

  delay(GAP_MS);
}
