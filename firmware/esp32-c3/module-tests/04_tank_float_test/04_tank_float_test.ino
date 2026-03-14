#include <Arduino.h>

const uint32_t SERIAL_BAUD = 115200;
const uint32_t SAMPLE_MS = 1000;

const uint8_t TANK_LEVEL_PIN = 3;
const bool TANK_LEVEL_ACTIVE_LOW = true;

uint32_t lastSampleAt = 0;
uint32_t sampleCount = 0;

bool isTankLevelLow()
{
  const uint8_t sampleN = 12;
  uint8_t lowVotes = 0;

  for (uint8_t i = 0; i < sampleN; i++)
  {
    int raw = digitalRead(TANK_LEVEL_PIN);
    bool low = TANK_LEVEL_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
    if (low)
    {
      lowVotes++;
    }
    delay(2);
  }

  return lowVotes > (sampleN / 2);
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 04: Tank Float Switch ===");
  Serial.printf("Tank pin: GPIO%u | Active-low: %s\n", TANK_LEVEL_PIN, TANK_LEVEL_ACTIVE_LOW ? "true" : "false");
  Serial.println("Expected: LOW status when tank is low.");

  pinMode(TANK_LEVEL_PIN, INPUT_PULLUP);
}

void loop()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastSampleAt) < SAMPLE_MS)
  {
    delay(20);
    return;
  }

  lastSampleAt = now;
  sampleCount++;

  int raw = digitalRead(TANK_LEVEL_PIN);
  bool tankLow = isTankLevelLow();

  Serial.printf(
      "[sample %lu] uptimeMs=%lu raw=%d tankLow=%s\n",
      (unsigned long)sampleCount,
      (unsigned long)now,
      raw,
      tankLow ? "true" : "false");
}
