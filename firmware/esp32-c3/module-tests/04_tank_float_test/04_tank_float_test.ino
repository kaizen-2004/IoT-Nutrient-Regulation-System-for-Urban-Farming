#include <Arduino.h>

const uint32_t SERIAL_BAUD = 115200;
const uint32_t SAMPLE_MS = 1000;

const uint8_t TANK_LEVEL_TRIG_PIN = 2;
const uint8_t TANK_LEVEL_ECHO_PIN = 3;
const uint8_t TANK_LEVEL_SAMPLE_COUNT = 5;
const uint32_t TANK_ULTRASONIC_TIMEOUT_US = 30000UL;
const float TANK_LOW_DISTANCE_CM = 24.0f;
const float TANK_MIN_VALID_DISTANCE_CM = 2.0f;
const float TANK_MAX_VALID_DISTANCE_CM = 300.0f;
const bool TANK_FAILSAFE_LOW_ON_SENSOR_ERROR = true;

uint32_t lastSampleAt = 0;
uint32_t sampleCount = 0;
float latestTankDistanceCm = NAN;
uint8_t latestValidSamples = 0;

float readTankDistanceCm()
{
  digitalWrite(TANK_LEVEL_TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TANK_LEVEL_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TANK_LEVEL_TRIG_PIN, LOW);

  unsigned long pulseWidthUs = pulseIn(TANK_LEVEL_ECHO_PIN, HIGH, TANK_ULTRASONIC_TIMEOUT_US);
  if (pulseWidthUs == 0)
  {
    return NAN;
  }

  return ((float)pulseWidthUs * 0.0343f) * 0.5f;
}

bool isTankLevelLow()
{
  uint8_t validSamples = 0;
  uint8_t lowVotes = 0;
  float distanceSum = 0.0f;

  for (uint8_t i = 0; i < TANK_LEVEL_SAMPLE_COUNT; i++)
  {
    float distanceCm = readTankDistanceCm();
    if (!isnan(distanceCm) &&
        distanceCm >= TANK_MIN_VALID_DISTANCE_CM &&
        distanceCm <= TANK_MAX_VALID_DISTANCE_CM)
    {
      validSamples++;
      distanceSum += distanceCm;
      if (distanceCm >= TANK_LOW_DISTANCE_CM)
      {
        lowVotes++;
      }
    }
    delay(25);
  }

  latestValidSamples = validSamples;

  if (validSamples == 0)
  {
    latestTankDistanceCm = NAN;
    return TANK_FAILSAFE_LOW_ON_SENSOR_ERROR;
  }

  latestTankDistanceCm = distanceSum / (float)validSamples;
  return lowVotes > (validSamples / 2);
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 04: HC-SR04 Tank Level ===");
  Serial.printf("TRIG: GPIO%u | ECHO: GPIO%u\n", TANK_LEVEL_TRIG_PIN, TANK_LEVEL_ECHO_PIN);
  Serial.printf("Low threshold: %.1f cm | Valid range: %.1f..%.1f cm\n",
                TANK_LOW_DISTANCE_CM,
                TANK_MIN_VALID_DISTANCE_CM,
                TANK_MAX_VALID_DISTANCE_CM);
  Serial.println("Expected: larger distance means lower tank level.");

  pinMode(TANK_LEVEL_TRIG_PIN, OUTPUT);
  pinMode(TANK_LEVEL_ECHO_PIN, INPUT);
  digitalWrite(TANK_LEVEL_TRIG_PIN, LOW);
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

  bool tankLow = isTankLevelLow();
  bool sensorError = (latestValidSamples == 0);

  if (sensorError)
  {
    Serial.printf(
        "[sample %lu] uptimeMs=%lu distanceCm=nan validSamples=%u tankLow=%s sensorError=true\n",
        (unsigned long)sampleCount,
        (unsigned long)now,
        latestValidSamples,
        tankLow ? "true" : "false");
    return;
  }

  Serial.printf(
      "[sample %lu] uptimeMs=%lu distanceCm=%.2f validSamples=%u tankLow=%s sensorError=false\n",
      (unsigned long)sampleCount,
      (unsigned long)now,
      latestTankDistanceCm,
      latestValidSamples,
      tankLow ? "true" : "false");
}
