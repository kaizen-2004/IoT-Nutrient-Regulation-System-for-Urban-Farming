#include <Arduino.h>
#include <HardwareSerial.h>

const uint32_t SERIAL_BAUD = 115200;
const uint32_t RS485_BAUD = 9600;
const uint8_t RS485_TX_PIN = 21;
const uint8_t RS485_RX_PIN = 20;
const uint8_t RS485_DE_RE_PIN = 10;
const bool RS485_DE_RE_TX_HIGH = true;

const uint8_t SENSOR_ADDR_Z1 = 1;
const uint8_t SENSOR_ADDR_Z2 = 2;
const uint32_t POLL_MS = 2500;
const uint32_t RESPONSE_TIMEOUT_MS = 500;

HardwareSerial RS485Serial(1);

uint32_t lastPollAt = 0;
uint32_t pollCount = 0;

uint16_t modbusCRC16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFF;
  for (uint16_t pos = 0; pos < len; pos++)
  {
    crc ^= (uint16_t)data[pos];
    for (uint8_t i = 0; i < 8; i++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void setRS485DirectionTx(bool txEnable)
{
  bool level = txEnable ? RS485_DE_RE_TX_HIGH : !RS485_DE_RE_TX_HIGH;
  digitalWrite(RS485_DE_RE_PIN, level ? HIGH : LOW);
}

void flushRS485Input()
{
  while (RS485Serial.available())
  {
    RS485Serial.read();
  }
}

bool readNPKRegisters(uint8_t addr, uint16_t &n, uint16_t &p, uint16_t &k)
{
  uint8_t req[8];
  req[0] = addr;
  req[1] = 0x03;
  req[2] = 0x00;
  req[3] = 0x1E;
  req[4] = 0x00;
  req[5] = 0x03;
  uint16_t crc = modbusCRC16(req, 6);
  req[6] = (uint8_t)(crc & 0xFF);
  req[7] = (uint8_t)((crc >> 8) & 0xFF);

  flushRS485Input();
  setRS485DirectionTx(true);
  delay(2);
  RS485Serial.write(req, sizeof(req));
  RS485Serial.flush();
  delay(2);
  setRS485DirectionTx(false);

  uint8_t resp[32];
  uint8_t len = 0;
  uint32_t startedAt = millis();

  while ((uint32_t)(millis() - startedAt) < RESPONSE_TIMEOUT_MS)
  {
    while (RS485Serial.available() && len < sizeof(resp))
    {
      resp[len++] = (uint8_t)RS485Serial.read();
    }
    if (len >= 11)
    {
      break;
    }
    delay(1);
  }

  if (len < 11 || resp[0] != addr || resp[1] != 0x03 || resp[2] != 0x06)
  {
    return false;
  }

  uint16_t crcRx = (uint16_t)resp[9] | ((uint16_t)resp[10] << 8);
  uint16_t crcCalc = modbusCRC16(resp, 9);
  if (crcRx != crcCalc)
  {
    return false;
  }

  n = ((uint16_t)resp[3] << 8) | resp[4];
  p = ((uint16_t)resp[5] << 8) | resp[6];
  k = ((uint16_t)resp[7] << 8) | resp[8];
  return true;
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 07: RS485 NPK Dual Sensor ===");
  Serial.printf("RS485 TX=%u RX=%u DE/RE=%u\n", RS485_TX_PIN, RS485_RX_PIN, RS485_DE_RE_PIN);
  Serial.printf("Zone1 addr=%u | Zone2 addr=%u | baud=%lu\n", SENSOR_ADDR_Z1, SENSOR_ADDR_Z2, (unsigned long)RS485_BAUD);
  Serial.println("Both sensors share the same A/B RS485 bus.");

  pinMode(RS485_DE_RE_PIN, OUTPUT);
  setRS485DirectionTx(false);

  RS485Serial.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
}

void loop()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastPollAt) < POLL_MS)
  {
    delay(20);
    return;
  }

  lastPollAt = now;
  pollCount++;

  uint16_t n1 = 0, p1 = 0, k1 = 0;
  uint16_t n2 = 0, p2 = 0, k2 = 0;

  bool ok1 = readNPKRegisters(SENSOR_ADDR_Z1, n1, p1, k1);
  delay(30);
  bool ok2 = readNPKRegisters(SENSOR_ADDR_Z2, n2, p2, k2);

  Serial.printf("[poll %lu] ", (unsigned long)pollCount);
  if (ok1)
  {
    Serial.printf("Z1(N=%u P=%u K=%u) ", n1, p1, k1);
  }
  else
  {
    Serial.print("Z1(read failed) ");
  }

  if (ok2)
  {
    Serial.printf("Z2(N=%u P=%u K=%u)", n2, p2, k2);
  }
  else
  {
    Serial.print("Z2(read failed)");
  }
  Serial.println();
}
