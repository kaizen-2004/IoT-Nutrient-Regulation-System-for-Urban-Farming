#include <Arduino.h>
#include <DHT.h>

const uint32_t SERIAL_BAUD = 115200;
const uint32_t SAMPLE_MS = 2000;

const uint8_t DHT_PIN_Z1 = 4;
const uint8_t DHT_PIN_Z2 = 5;

DHT dhtZ1(DHT_PIN_Z1, DHT22);
DHT dhtZ2(DHT_PIN_Z2, DHT22);

uint32_t lastSampleAt = 0;
uint32_t sampleCount = 0;

void printZone(uint8_t zone, float tempC, float humidityPct)
{
  bool ok = !isnan(tempC) && !isnan(humidityPct);
  if (!ok)
  {
    Serial.printf("Zone %u -> ERROR: failed reading DHT22\n", zone);
    return;
  }

  Serial.printf(
      "Zone %u -> Temp: %.2f C | Humidity: %.2f %%\n",
      zone,
      tempC,
      humidityPct);
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 02: Dual DHT22 ===");
  Serial.printf("Zone1 pin: GPIO%u | Zone2 pin: GPIO%u\n", DHT_PIN_Z1, DHT_PIN_Z2);
  Serial.println("Expected: stable readings every 2 seconds.");
  Serial.println("If ERROR appears repeatedly: check VCC/GND/DATA and pull-up resistor.");

  dhtZ1.begin();
  dhtZ2.begin();
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

  float z1TempC = dhtZ1.readTemperature();
  float z1HumidityPct = dhtZ1.readHumidity();
  float z2TempC = dhtZ2.readTemperature();
  float z2HumidityPct = dhtZ2.readHumidity();

  Serial.printf("\n[sample %lu] uptimeMs=%lu\n", (unsigned long)sampleCount, (unsigned long)now);
  printZone(1, z1TempC, z1HumidityPct);
  printZone(2, z2TempC, z2HumidityPct);
}
