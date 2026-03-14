#include <Arduino.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN -1
#endif

const uint32_t SERIAL_BAUD = 115200;
const uint32_t HEARTBEAT_MS = 1000;

uint32_t lastBeatAt = 0;
uint32_t beatCount = 0;
bool ledState = false;

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);

  if (LED_BUILTIN >= 0)
  {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
  }

  Serial.println();
  Serial.println("=== NILA Module Test 00: Serial Smoke Test ===");
  Serial.printf("Build date: %s %s\n", __DATE__, __TIME__);
  Serial.printf("CPU freq (MHz): %lu\n", (unsigned long)(getCpuFrequencyMhz()));
  Serial.printf("LED_BUILTIN pin: %d\n", LED_BUILTIN);
  Serial.println("Expected: heartbeat line every 1s.");
  Serial.println("==============================================");
}

void loop()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastBeatAt) < HEARTBEAT_MS)
  {
    delay(10);
    return;
  }

  lastBeatAt = now;
  beatCount++;

  if (LED_BUILTIN >= 0)
  {
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }

  Serial.printf(
      "[heartbeat] count=%lu uptimeMs=%lu freeHeap=%lu\n",
      (unsigned long)beatCount,
      (unsigned long)now,
      (unsigned long)ESP.getFreeHeap());
}
