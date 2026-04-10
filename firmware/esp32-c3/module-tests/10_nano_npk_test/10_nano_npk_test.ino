#include <Arduino.h>
#include <SoftwareSerial.h>

#define RE 8
#define DE 7
#define RS485_RX_PIN 2 // MAX485 RO -> D2
#define RS485_TX_PIN 3 // MAX485 DI <- D3

const uint32_t RS485_BAUD = 4800;
const uint32_t RESP_TIMEOUT_MS = 250;

const uint16_t REG_N = 0x001E;
const uint16_t REG_P = 0x001F;
const uint16_t REG_K = 0x0020;
const uint16_t REG7_N = 0x0004;
const uint16_t REG7_P = 0x0005;
const uint16_t REG7_K = 0x0006;

SoftwareSerial rs485(RS485_RX_PIN, RS485_TX_PIN); // RX, TX

uint16_t crc16Modbus(const uint8_t *data, uint8_t len)
{
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
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

void setRs485Tx(bool txEnable)
{
  digitalWrite(DE, txEnable ? HIGH : LOW);
  digitalWrite(RE, txEnable ? HIGH : LOW);
}

void flushRs485Input()
{
  while (rs485.available())
  {
    rs485.read();
  }
}

bool readRegister(uint8_t addr, uint16_t reg, uint16_t &outValue)
{
  uint8_t req[8];
  req[0] = addr;
  req[1] = 0x03;
  req[2] = (uint8_t)((reg >> 8) & 0xFF);
  req[3] = (uint8_t)(reg & 0xFF);
  req[4] = 0x00;
  req[5] = 0x01;
  uint16_t crc = crc16Modbus(req, 6);
  req[6] = (uint8_t)(crc & 0xFF);
  req[7] = (uint8_t)((crc >> 8) & 0xFF);

  flushRs485Input();

  setRs485Tx(true);
  delay(2);
  rs485.write(req, sizeof(req));
  rs485.flush();
  delay(2);
  setRs485Tx(false);

  uint8_t resp[7];
  uint8_t idx = 0;
  unsigned long started = millis();
  while ((millis() - started) < RESP_TIMEOUT_MS && idx < sizeof(resp))
  {
    if (rs485.available())
    {
      int v = rs485.read();
      if (v >= 0)
      {
        resp[idx++] = (uint8_t)v;
      }
    }
  }

  if (idx != 7)
  {
    return false;
  }
  if (resp[0] != addr || resp[1] != 0x03 || resp[2] != 0x02)
  {
    return false;
  }

  uint16_t crcRx = (uint16_t)resp[5] | ((uint16_t)resp[6] << 8);
  uint16_t crcCalc = crc16Modbus(resp, 5);
  if (crcRx != crcCalc)
  {
    return false;
  }

  outValue = ((uint16_t)resp[3] << 8) | resp[4];
  return true;
}

bool readNpk3(uint8_t addr, uint16_t &n, uint16_t &p, uint16_t &k)
{
  bool okN = readRegister(addr, REG_N, n);
  delay(120);
  bool okP = readRegister(addr, REG_P, p);
  delay(120);
  bool okK = readRegister(addr, REG_K, k);
  return okN && okP && okK;
}

bool readNpk3_7in1Map(uint8_t addr, uint16_t &n, uint16_t &p, uint16_t &k)
{
  bool okN = readRegister(addr, REG7_N, n);
  delay(120);
  bool okP = readRegister(addr, REG7_P, p);
  delay(120);
  bool okK = readRegister(addr, REG7_K, k);
  return okN && okP && okK;
}

void setup()
{
  Serial.begin(115200);
  rs485.begin(RS485_BAUD);
  pinMode(RE, OUTPUT);
  pinMode(DE, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  setRs485Tx(false);
  delay(300);
  Serial.println("Nano + MAX485 NPK debug test");
  Serial.println("Wiring: RO->D2, DI<-D3, DE->D7, RE->D8");
  Serial.println("Scanning Modbus address 1..40");
}

void loop()
{
  static uint32_t scanRound = 0;
  scanRound++;

  uint8_t foundAddr = 0;
  uint16_t n = 0, p = 0, k = 0;

  Serial.print("[scan ");
  Serial.print(scanRound);
  Serial.print("] probing 1..40 ");

  for (uint8_t addr = 1; addr <= 40; addr++)
  {
    uint16_t probeN = 0;
    if (readRegister(addr, REG_N, probeN))
    {
      foundAddr = addr;
      n = probeN;
      break;
    }

    Serial.print('.');
    if ((addr % 10) == 0)
    {
      Serial.print(' ');
    }
    delay(40);
  }
  Serial.println();

  if (!foundAddr)
  {
    Serial.println("No valid sensor response in 1..40");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(600);
    digitalWrite(LED_BUILTIN, LOW);
    delay(600);
    return;
  }

  bool fullOk = readNpk3(foundAddr, n, p, k);
  if (!fullOk)
  {
    Serial.print("Found addr ");
    Serial.print(foundAddr);
    Serial.println(", but full N/P/K read failed (noise/timing/wiring).");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(300);
    digitalWrite(LED_BUILTIN, LOW);
    delay(300);
    return;
  }

  uint16_t n7 = 0, p7 = 0, k7 = 0;
  bool ok7 = readNpk3_7in1Map(foundAddr, n7, p7, k7);

  Serial.print("Found addr ");
  Serial.print(foundAddr);
  Serial.print(" | map001E N=");
  Serial.print(n);
  Serial.print(" P=");
  Serial.print(p);
  Serial.print(" K=");
  Serial.println(k);

  if (ok7)
  {
    Serial.print("Found addr ");
    Serial.print(foundAddr);
    Serial.print(" | map0004 N=");
    Serial.print(n7);
    Serial.print(" P=");
    Serial.print(p7);
    Serial.print(" K=");
    Serial.println(k7);
  }
  else
  {
    Serial.println("map0004 read failed");
  }

  digitalWrite(LED_BUILTIN, HIGH);
  delay(120);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
}
