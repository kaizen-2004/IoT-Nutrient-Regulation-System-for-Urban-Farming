#include <Arduino.h>
#include <HardwareSerial.h>

const uint32_t SERIAL_BAUD = 4800;
const uint32_t RS485_BAUD = 9600;
const uint8_t RS485_TX_PIN = 21;
const uint8_t RS485_RX_PIN = 20;
const uint8_t RS485_DE_RE_PIN = 10;
const bool RS485_DE_RE_TX_HIGH = false;

const uint8_t SENSOR_ADDR = 1; // Used when address scan is disabled.
const uint32_t POLL_MS = 2000;
const uint32_t SINGLE_RESPONSE_TIMEOUT_MS = 500;
const uint32_t SCAN_RESPONSE_TIMEOUT_MS = 120;
const bool ENABLE_ADDRESS_SCAN = true;
const uint8_t SCAN_ADDR_START = 1;
const uint8_t SCAN_ADDR_END = 40;

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

bool readNPKRegisters(uint8_t addr, uint16_t &n, uint16_t &p, uint16_t &k, uint32_t timeoutMs)
{
  // Common JXCT/SN-3002 register block:
  // Function 0x03, start 0x001E, quantity 0x0003 => N, P, K
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

  while ((uint32_t)(millis() - startedAt) < timeoutMs)
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

  if (len < 5)
  {
    Serial.println("NPK read failed: no/short response.");
    return false;
  }

  if (resp[0] != addr)
  {
    Serial.printf("NPK read failed: unexpected addr %u (expected %u)\n", resp[0], addr);
    return false;
  }

  if (resp[1] & 0x80)
  {
    Serial.printf("NPK Modbus exception: func=0x%02X code=0x%02X\n", resp[1], resp[2]);
    return false;
  }

  if (len < 11 || resp[1] != 0x03 || resp[2] != 0x06)
  {
    Serial.printf("NPK read failed: unexpected frame (len=%u)\n", len);
    Serial.print("Raw: ");
    for (uint8_t i = 0; i < len; i++)
    {
      Serial.printf("%02X ", resp[i]);
    }
    Serial.println();
    return false;
  }

  uint16_t crcRx = (uint16_t)resp[9] | ((uint16_t)resp[10] << 8);
  uint16_t crcCalc = modbusCRC16(resp, 9);
  if (crcRx != crcCalc)
  {
    Serial.printf("NPK CRC mismatch: rx=0x%04X calc=0x%04X\n", crcRx, crcCalc);
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
  Serial.println("=== NILA Module Test 06: RS485 NPK Single Sensor ===");
  Serial.printf("RS485 TX=%u RX=%u DE/RE=%u\n", RS485_TX_PIN, RS485_RX_PIN, RS485_DE_RE_PIN);
  if (ENABLE_ADDRESS_SCAN)
  {
    Serial.printf("Address scan enabled: %u..%u, baud=%lu\n",
                  SCAN_ADDR_START,
                  SCAN_ADDR_END,
                  (unsigned long)RS485_BAUD);
  }
  else
  {
    Serial.printf("Sensor address=%u, baud=%lu\n", SENSOR_ADDR, (unsigned long)RS485_BAUD);
  }
  Serial.println("Polling register block: 0x001E..0x0020 (N,P,K).");

  pinMode(RS485_DE_RE_PIN, OUTPUT);
  setRS485DirectionTx(false);
  pinMode(RS485_RX_PIN, INPUT_PULLUP);

  RS485Serial.begin(RS485_BAUD, SERIAL_8E1, RS485_RX_PIN,
  RS485_TX_PIN);
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

  if (ENABLE_ADDRESS_SCAN)
  {
    Serial.printf("[scan %lu] probing addresses %u..%u\n",
                  (unsigned long)pollCount,
                  SCAN_ADDR_START,
                  SCAN_ADDR_END);
    bool foundAny = false;
    for (uint8_t addr = SCAN_ADDR_START; addr <= SCAN_ADDR_END; addr++)
    {
      uint16_t n = 0;
      uint16_t p = 0;
      uint16_t k = 0;
      bool ok = readNPKRegisters(addr, n, p, k, SCAN_RESPONSE_TIMEOUT_MS);
      if (ok)
      {
        foundAny = true;
        Serial.printf("[scan %lu] addr=%u N=%u P=%u K=%u\n",
                      (unsigned long)pollCount,
                      addr,
                      n,
                      p,
                      k);
      }
      delay(25);
    }

    if (!foundAny)
    {
      Serial.printf("[scan %lu] no sensor found in range.\n", (unsigned long)pollCount);
    }
    return;
  }

  uint16_t n = 0;
  uint16_t p = 0;
  uint16_t k = 0;
  bool ok = readNPKRegisters(SENSOR_ADDR, n, p, k, SINGLE_RESPONSE_TIMEOUT_MS);

  if (ok)
  {
    Serial.printf("[poll %lu] N=%u P=%u K=%u\n", (unsigned long)pollCount, n, p, k);
  }
  else
  {
    Serial.printf("[poll %lu] read failed.\n", (unsigned long)pollCount);
  }
}
