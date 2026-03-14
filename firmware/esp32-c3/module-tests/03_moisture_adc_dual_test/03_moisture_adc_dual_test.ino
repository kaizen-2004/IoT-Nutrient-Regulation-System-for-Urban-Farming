#include <Arduino.h>

const uint32_t SERIAL_BAUD = 115200;
const uint32_t SAMPLE_MS = 1500;

const uint8_t MOISTURE_PIN_Z1 = 0;
const uint8_t MOISTURE_PIN_Z2 = 1;

// Keep this aligned with current controller firmware calibration.
const int DRY_RAW_Z1 = 3000;
const int WET_RAW_Z1 = 1300;
const int DRY_RAW_Z2 = 3000;
const int WET_RAW_Z2 = 1300;

uint32_t lastSampleAt = 0;
uint32_t sampleCount = 0;

float moisturePercentFromRaw(int raw, int dryRaw, int wetRaw)
{
  int span = dryRaw - wetRaw;
  if (span == 0)
  {
    return 0.0f;
  }

  float pct = (float)(dryRaw - raw) * 100.0f / (float)span;
  if (pct < 0.0f)
  {
    pct = 0.0f;
  }
  if (pct > 100.0f)
  {
    pct = 100.0f;
  }
  return pct;
}

int readAveragedRaw(uint8_t pin)
{
  const uint8_t sampleN = 8;
  long sum = 0;
  for (uint8_t i = 0; i < sampleN; i++)
  {
    sum += analogRead(pin);
    delay(5);
  }
  return (int)(sum / sampleN);
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 03: Dual Moisture ADC ===");
  Serial.printf("Zone1 pin: GPIO%u | Zone2 pin: GPIO%u\n", MOISTURE_PIN_Z1, MOISTURE_PIN_Z2);
  Serial.printf("Calibration Z1 dry=%d wet=%d\n", DRY_RAW_Z1, WET_RAW_Z1);
  Serial.printf("Calibration Z2 dry=%d wet=%d\n", DRY_RAW_Z2, WET_RAW_Z2);
  Serial.println("Expected: raw + moisture% values every 1.5 seconds.");

  analogSetPinAttenuation(MOISTURE_PIN_Z1, ADC_11db);
  analogSetPinAttenuation(MOISTURE_PIN_Z2, ADC_11db);
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

  int rawZ1 = readAveragedRaw(MOISTURE_PIN_Z1);
  int rawZ2 = readAveragedRaw(MOISTURE_PIN_Z2);

  float pctZ1 = moisturePercentFromRaw(rawZ1, DRY_RAW_Z1, WET_RAW_Z1);
  float pctZ2 = moisturePercentFromRaw(rawZ2, DRY_RAW_Z2, WET_RAW_Z2);

  Serial.printf(
      "[sample %lu] uptimeMs=%lu | Z1 raw=%d pct=%.2f%% | Z2 raw=%d pct=%.2f%%\n",
      (unsigned long)sampleCount,
      (unsigned long)now,
      rawZ1,
      pctZ1,
      rawZ2,
      pctZ2);
}
