#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const uint32_t SERIAL_BAUD = 115200;
const uint32_t RS485_BAUD = 9600;
const uint8_t RS485_TX_PIN = 21;
const uint8_t RS485_RX_PIN = 20;
const uint8_t RS485_DE_RE_PIN = 10;
const bool RS485_DE_RE_TX_HIGH = true;
const uint8_t LCD_ADDR = 0x27;
const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

const uint8_t SENSOR_ADDR = 1; // Used when address scan is disabled.
const uint32_t POLL_MS = 2000;
const uint32_t SINGLE_RESPONSE_TIMEOUT_MS = 500;
const uint32_t SCAN_RESPONSE_TIMEOUT_MS = 120;
const bool ENABLE_ADDRESS_SCAN = true;
const uint8_t SCAN_ADDR_START = 1;
const uint8_t SCAN_ADDR_END = 40;

struct RS485Profile
{
  const char *label;
  uint32_t baud;
  uint32_t config;
};

const RS485Profile SCAN_PROFILES[] = {
    {"9600 8N1", 9600, SERIAL_8N1},
    {"4800 8N1", 4800, SERIAL_8N1},
    {"2400 8N1", 2400, SERIAL_8N1},
    {"9600 8E1", 9600, SERIAL_8E1},
    {"4800 8E1", 4800, SERIAL_8E1},
    {"2400 8E1", 2400, SERIAL_8E1},
};

HardwareSerial RS485Serial(1);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

uint32_t lastPollAt = 0;
uint32_t pollCount = 0;

void showLCDMessage(const char *line1, const char *line2, const char *line3, const char *line4)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  lcd.setCursor(0, 2);
  lcd.print(line3);
  lcd.setCursor(0, 3);
  lcd.print(line4);
}

void beginRS485(const RS485Profile &profile)
{
  RS485Serial.end();
  delay(10);
  RS485Serial.begin(profile.baud, profile.config, RS485_RX_PIN, RS485_TX_PIN);
}

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
  Serial.printf("LCD addr=0x%02X size=%ux%u\n", LCD_ADDR, LCD_COLS, LCD_ROWS);
  if (ENABLE_ADDRESS_SCAN)
  {
    Serial.printf("Address scan enabled: %u..%u, default baud=%lu\n",
                  SCAN_ADDR_START,
                  SCAN_ADDR_END,
                  (unsigned long)RS485_BAUD);
    Serial.println("Protocol scan profiles: 9600/4800/2400 with 8N1 and 8E1.");
  }
  else
  {
    Serial.printf("Sensor address=%u, baud=%lu\n", SENSOR_ADDR, (unsigned long)RS485_BAUD);
  }
  Serial.println("Polling register block: 0x001E..0x0020 (N,P,K).");

  pinMode(RS485_DE_RE_PIN, OUTPUT);
  setRS485DirectionTx(false);
  pinMode(RS485_RX_PIN, INPUT_PULLUP);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  showLCDMessage("NILA NPK TEST", "LCD + RS485 READY", "Scanning sensor...", "Wait serial output");

  beginRS485(SCAN_PROFILES[0]);
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
    Serial.printf("[scan %lu] probing addresses %u..%u across %u profiles\n",
                  (unsigned long)pollCount,
                  SCAN_ADDR_START,
                  SCAN_ADDR_END,
                  (unsigned)(sizeof(SCAN_PROFILES) / sizeof(SCAN_PROFILES[0])));
    bool foundAny = false;
    for (uint8_t profileIndex = 0; profileIndex < (sizeof(SCAN_PROFILES) / sizeof(SCAN_PROFILES[0])); profileIndex++)
    {
      const RS485Profile &profile = SCAN_PROFILES[profileIndex];
      beginRS485(profile);
      Serial.printf("[scan %lu] profile=%s\n", (unsigned long)pollCount, profile.label);

      char line3[21];
      snprintf(line3, sizeof(line3), "%s", profile.label);
      showLCDMessage("NILA NPK TEST", "Scanning profile", line3, "Check serial log");

      for (uint8_t addr = SCAN_ADDR_START; addr <= SCAN_ADDR_END; addr++)
      {
        uint16_t n = 0;
        uint16_t p = 0;
        uint16_t k = 0;
        bool ok = readNPKRegisters(addr, n, p, k, SCAN_RESPONSE_TIMEOUT_MS);
        if (ok)
        {
          foundAny = true;
          Serial.printf("[scan %lu] profile=%s addr=%u N=%u P=%u K=%u\n",
                        (unsigned long)pollCount,
                        profile.label,
                        addr,
                        n,
                        p,
                        k);
          char line2[21];
          char line3Found[21];
          char line4[21];
          snprintf(line2, sizeof(line2), "%s addr:%u", profile.label, addr);
          snprintf(line3Found, sizeof(line3Found), "N:%u P:%u", n, p);
          snprintf(line4, sizeof(line4), "K:%u", k);
          showLCDMessage("NPK VALUES", line2, line3Found, line4);
        }
        delay(25);
      }
    }

    if (!foundAny)
    {
      Serial.printf("[scan %lu] no sensor found in range.\n", (unsigned long)pollCount);
      showLCDMessage("NILA NPK TEST", "Scan result:", "No sensor found", "Check RS485 path");
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
    char line2[21];
    char line3[21];
    char line4[21];
    snprintf(line2, sizeof(line2), "Poll:%lu Addr:%u", (unsigned long)pollCount, SENSOR_ADDR);
    snprintf(line3, sizeof(line3), "N:%u P:%u", n, p);
    snprintf(line4, sizeof(line4), "K:%u", k);
    showLCDMessage("NPK VALUES", line2, line3, line4);
  }
  else
  {
    Serial.printf("[poll %lu] read failed.\n", (unsigned long)pollCount);
    char line4[21];
    snprintf(line4, sizeof(line4), "Poll:%lu", (unsigned long)pollCount);
    showLCDMessage("NPK VALUES", "Read failed", "Check addr/wiring", line4);
  }
}
