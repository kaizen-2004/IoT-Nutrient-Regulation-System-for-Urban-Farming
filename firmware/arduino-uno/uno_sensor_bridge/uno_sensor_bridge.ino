#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DallasTemperature.h>
#include <OneWire.h>

const uint32_t USB_BAUD = 115200;
const uint32_t ESP_LINK_BAUD = 19200;
const uint32_t TELEMETRY_INTERVAL_MS = 1000UL;

const uint8_t ESP_LINK_RX_PIN = 10;
const uint8_t ESP_LINK_TX_PIN = 11;

const uint8_t MOISTURE_1_PIN = A0;
const uint8_t MOISTURE_2_PIN = A1;
const int MOISTURE_DRY_RAW = 850;
const int MOISTURE_WET_RAW = 420;

const uint8_t DS18B20_PIN = 2;
const float DEFAULT_TEMP_C = 25.0f;
const bool USE_FIXED_DS18B20_ADDRS = true;

const uint8_t TANK_TRIG_PIN = 12;
const uint8_t TANK_ECHO_PIN = 6;
const uint8_t TANK_SAMPLE_COUNT = 5;
const uint32_t TANK_PULSE_TIMEOUT_US = 30000UL;
const float TANK_MIN_VALID_CM = 2.0f;
const float TANK_MAX_VALID_CM = 400.0f;

const uint8_t UNASSIGNED_PIN = 255;
const uint8_t RS485_1_RX_PIN = 4;
const uint8_t RS485_1_TX_PIN = 5;
const uint8_t RS485_1_DE_RE_PIN = UNASSIGNED_PIN;
const uint8_t RS485_2_RX_PIN = 7;
const uint8_t RS485_2_TX_PIN = 8;
const uint8_t RS485_2_DE_RE_PIN = 9;
const bool RS485_DE_RE_TX_HIGH = true;
const uint32_t RS485_1_BAUD_DEFAULT = 4800;
const uint32_t RS485_2_BAUD_DEFAULT = 4800;
const uint8_t RS485_1_SENSOR_ADDR = 1;
const uint8_t RS485_2_SENSOR_ADDR = 1;
const uint32_t RS485_RESPONSE_TIMEOUT_MS = 1000;
const uint8_t RS485_READ_RETRIES = 2;
const uint32_t RS485_INTER_REQUEST_DELAY_MS = 60;
const bool ENABLE_RS485_DEBUG = false;
const bool ENABLE_RS485_1_AUTODETECT = false;
const bool ENABLE_RS485_CH1_READ = true;
const bool ENABLE_RS485_CH2_READ = true;
const uint16_t RS485_AUTODIR_TURNAROUND_DELAY_MS = 4;
const uint32_t RS485_SCAN_BAUDS[] = {4800, 9600, 2400, 19200};
const uint8_t RS485_ADDR_SCAN_MIN = 1;
const uint8_t RS485_ADDR_SCAN_MAX = 8;

const bool ENABLE_DS18B20 = true;
const bool ENABLE_RS485_NPK = true;

SoftwareSerial espLink(ESP_LINK_RX_PIN, ESP_LINK_TX_PIN);
SoftwareSerial rs485Serial1(RS485_1_RX_PIN, RS485_1_TX_PIN);
SoftwareSerial rs485Serial2(RS485_2_RX_PIN, RS485_2_TX_PIN);

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DeviceAddress dsAddr1 = {0x28, 0x61, 0x66, 0x36, 0xF3, 0x61, 0x51, 0x54};
DeviceAddress dsAddr2 = {0x28, 0x21, 0x66, 0x36, 0xF3, 0x47, 0xC9, 0x99};
bool dsHave1 = false;
bool dsHave2 = false;

uint32_t seqCounter = 0;
uint32_t lastTelemetryAt = 0;
float lastTemp1 = DEFAULT_TEMP_C;
float lastTemp2 = DEFAULT_TEMP_C;
float lastN[2] = {0.0f, 0.0f};
float lastP[2] = {0.0f, 0.0f};
float lastK[2] = {0.0f, 0.0f};
uint32_t rs485BaudCh1 = RS485_1_BAUD_DEFAULT;
uint32_t rs485BaudCh2 = RS485_2_BAUD_DEFAULT;
uint8_t rs485AddrCh1 = RS485_1_SENSOR_ADDR;
uint8_t rs485AddrCh2 = RS485_2_SENSOR_ADDR;

uint8_t xorCrc(const char *text, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint8_t)text[i];
  }
  return crc;
}

uint16_t modbusCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void rs485SetTx(uint8_t deRePin, bool tx) {
  if (deRePin == UNASSIGNED_PIN) {
    return;
  }
  bool level = tx ? RS485_DE_RE_TX_HIGH : !RS485_DE_RE_TX_HIGH;
  digitalWrite(deRePin, level ? HIGH : LOW);
}

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printHexFrame(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printHexByte(data[i]);
    if (i + 1 < len) {
      Serial.print(' ');
    }
  }
}

float moisturePercentFromRaw(int raw) {
  int bounded = constrain(raw, MOISTURE_WET_RAW, MOISTURE_DRY_RAW);
  float ratio = (float)(MOISTURE_DRY_RAW - bounded) /
                (float)(MOISTURE_DRY_RAW - MOISTURE_WET_RAW);
  if (ratio < 0.0f) {
    ratio = 0.0f;
  }
  if (ratio > 1.0f) {
    ratio = 1.0f;
  }
  return ratio * 100.0f;
}

float readUltrasonicDistanceCm() {
  digitalWrite(TANK_TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TANK_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TANK_TRIG_PIN, LOW);

  unsigned long pulseUs = pulseIn(TANK_ECHO_PIN, HIGH, TANK_PULSE_TIMEOUT_US);
  if (pulseUs == 0) {
    return NAN;
  }

  return ((float)pulseUs * 0.0343f) * 0.5f;
}

float readTankDistanceCm() {
  float sum = 0.0f;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < TANK_SAMPLE_COUNT; i++) {
    float cm = readUltrasonicDistanceCm();
    if (isfinite(cm) && cm >= TANK_MIN_VALID_CM && cm <= TANK_MAX_VALID_CM) {
      sum += cm;
      valid++;
    }
    delay(10);
  }

  if (valid == 0) {
    return NAN;
  }
  return sum / (float)valid;
}

void initDs18b20() {
  ds18b20.begin();
  if (USE_FIXED_DS18B20_ADDRS) {
    dsHave1 = ds18b20.isConnected(dsAddr1);
    dsHave2 = ds18b20.isConnected(dsAddr2);
  } else {
    int count = ds18b20.getDeviceCount();
    if (count > 0) {
      dsHave1 = ds18b20.getAddress(dsAddr1, 0);
    }
    if (count > 1) {
      dsHave2 = ds18b20.getAddress(dsAddr2, 1);
    }
  }
  ds18b20.setWaitForConversion(true);

  Serial.print("[uno] ds18b20 z1=");
  Serial.println(dsHave1 ? "ok" : "missing");
  Serial.print("[uno] ds18b20 z2=");
  Serial.println(dsHave2 ? "ok" : "missing");
}

void readDs18b20Temps(float &temp1, float &temp2) {
  temp1 = lastTemp1;
  temp2 = lastTemp2;

  if (!ENABLE_DS18B20 || (!dsHave1 && !dsHave2)) {
    return;
  }

  ds18b20.requestTemperatures();
  if (dsHave1) {
    float t = ds18b20.getTempC(dsAddr1);
    if (t > -50.0f && t < 125.0f) {
      temp1 = t;
      lastTemp1 = t;
    }
  }
  if (dsHave2) {
    float t = ds18b20.getTempC(dsAddr2);
    if (t > -50.0f && t < 125.0f) {
      temp2 = t;
      lastTemp2 = t;
    }
  } else {
    temp2 = temp1;
    lastTemp2 = temp2;
  }
}

bool readHoldingRegister(SoftwareSerial &bus,
                         uint8_t deRePin,
                         const char *channelName,
                         uint8_t addr,
                         uint16_t reg,
                         uint16_t &out) {
  uint8_t req[8];
  req[0] = addr;
  req[1] = 0x03;
  req[2] = (uint8_t)((reg >> 8) & 0xFF);
  req[3] = (uint8_t)(reg & 0xFF);
  req[4] = 0x00;
  req[5] = 0x01;
  uint16_t crc = modbusCrc16(req, 6);
  req[6] = (uint8_t)(crc & 0xFF);
  req[7] = (uint8_t)((crc >> 8) & 0xFF);

  for (uint8_t attempt = 0; attempt <= RS485_READ_RETRIES; attempt++) {
    bus.listen();
    while (bus.available()) {
      bus.read();
    }

    rs485SetTx(deRePin, true);
    bus.write(req, sizeof(req));
    bus.flush();
    rs485SetTx(deRePin, false);
    if (deRePin == UNASSIGNED_PIN) {
      delay(RS485_AUTODIR_TURNAROUND_DELAY_MS);
    }

    uint8_t resp[16];
    size_t len = 0;
    uint32_t startedAt = millis();
    while ((uint32_t)(millis() - startedAt) < RS485_RESPONSE_TIMEOUT_MS) {
      while (bus.available() && len < sizeof(resp)) {
        resp[len++] = (uint8_t)bus.read();
      }
      if (len >= 7) {
        break;
      }
    }

    espLink.listen();

    bool ok = true;
    if (len < 7) {
      ok = false;
      if (ENABLE_RS485_DEBUG) {
        Serial.print("[rs485]");
        Serial.print(channelName);
        Serial.print(" timeout attempt=");
        Serial.print(attempt + 1);
        Serial.print(" addr=");
        Serial.print(addr);
        Serial.print(" reg=0x");
        Serial.println(reg, HEX);
      }
    }

    if (ok && (resp[0] != addr || resp[1] != 0x03 || resp[2] != 0x02)) {
      ok = false;
      if (ENABLE_RS485_DEBUG) {
        Serial.print("[rs485]");
        Serial.print(channelName);
        Serial.print(" bad-header attempt=");
        Serial.print(attempt + 1);
        Serial.print(" resp=");
        printHexFrame(resp, len);
        Serial.println();
      }
    }

    if (ok) {
      uint16_t crcResp = (uint16_t)resp[len - 2] | ((uint16_t)resp[len - 1] << 8);
      uint16_t crcCalc = modbusCrc16(resp, len - 2);
      if (crcResp != crcCalc) {
        ok = false;
        if (ENABLE_RS485_DEBUG) {
          Serial.print("[rs485]");
          Serial.print(channelName);
          Serial.print(" crc-fail attempt=");
          Serial.print(attempt + 1);
          Serial.print(" calc=0x");
          Serial.print(crcCalc, HEX);
          Serial.print(" resp=0x");
          Serial.print(crcResp, HEX);
          Serial.print(" frame=");
          printHexFrame(resp, len);
          Serial.println();
        }
      }
    }

    if (ok) {
      out = ((uint16_t)resp[3] << 8) | (uint16_t)resp[4];
      return true;
    }

    delay(RS485_INTER_REQUEST_DELAY_MS);
  }

  return false;
}

bool autodetectRs485Channel(SoftwareSerial &bus,
                            uint8_t deRePin,
                            const char *channelName,
                            uint32_t &detectedBaud,
                            uint8_t &detectedAddr) {
  for (size_t b = 0; b < (sizeof(RS485_SCAN_BAUDS) / sizeof(RS485_SCAN_BAUDS[0])); b++) {
    uint32_t baud = RS485_SCAN_BAUDS[b];
    bus.begin(baud);
    delay(80);

    if (ENABLE_RS485_DEBUG) {
      Serial.print("[rs485]");
      Serial.print(channelName);
      Serial.print(" scan baud=");
      Serial.println(baud);
    }

    for (uint8_t addr = RS485_ADDR_SCAN_MIN; addr <= RS485_ADDR_SCAN_MAX; addr++) {
      uint16_t val = 0;
      if (readHoldingRegister(bus, deRePin, channelName, addr, 0x001E, val)) {
        detectedBaud = baud;
        detectedAddr = addr;
        if (ENABLE_RS485_DEBUG) {
          Serial.print("[rs485]");
          Serial.print(channelName);
          Serial.print(" detected addr=");
          Serial.print(addr);
          Serial.print(" baud=");
          Serial.print(baud);
          Serial.print(" nReg=");
          Serial.println(val);
        }
        return true;
      }
    }
  }

  if (ENABLE_RS485_DEBUG) {
    Serial.print("[rs485]");
    Serial.print(channelName);
    Serial.println(" autodetect failed");
  }
  return false;
}

void readNpkFromChannel(uint8_t channelIndex,
                        SoftwareSerial &bus,
                        uint8_t deRePin,
                        const char *channelName,
                        uint8_t addr,
                        float &n,
                        float &p,
                        float &k) {
  n = lastN[channelIndex];
  p = lastP[channelIndex];
  k = lastK[channelIndex];

  if (!ENABLE_RS485_NPK) {
    return;
  }

  uint16_t nReg = 0;
  uint16_t pReg = 0;
  uint16_t kReg = 0;
  bool okN = readHoldingRegister(bus, deRePin, channelName, addr, 0x001E, nReg);
  bool okP = readHoldingRegister(bus, deRePin, channelName, addr, 0x001F, pReg);
  bool okK = readHoldingRegister(bus, deRePin, channelName, addr, 0x0020, kReg);
  if (okN && okP && okK) {
    n = (float)nReg;
    p = (float)pReg;
    k = (float)kReg;
    lastN[channelIndex] = n;
    lastP[channelIndex] = p;
    lastK[channelIndex] = k;
  } else if (ENABLE_RS485_DEBUG) {
    Serial.print("[rs485]");
    Serial.print(channelName);
    Serial.print(" npk-read-failed addr=");
    Serial.println(addr);
  }
}

void sendTelemetryFrame() {
  seqCounter++;

  int raw1 = analogRead(MOISTURE_1_PIN);
  int raw2 = analogRead(MOISTURE_2_PIN);
  float m1 = moisturePercentFromRaw(raw1);
  float m2 = moisturePercentFromRaw(raw2);
  float tankCm = readTankDistanceCm();

  float t1 = lastTemp1;
  float t2 = lastTemp2;
  readDs18b20Temps(t1, t2);

  float n1 = lastN[0];
  float p1 = lastP[0];
  float k1 = lastK[0];
  float n2 = lastN[1];
  float p2 = lastP[1];
  float k2 = lastK[1];
  if (ENABLE_RS485_CH1_READ) {
    readNpkFromChannel(0,
                       rs485Serial1,
                       RS485_1_DE_RE_PIN,
                       "ch1",
                       rs485AddrCh1,
                       n1,
                       p1,
                       k1);
  }
  if (ENABLE_RS485_CH2_READ) {
    readNpkFromChannel(1,
                       rs485Serial2,
                       RS485_2_DE_RE_PIN,
                       "ch2",
                       rs485AddrCh2,
                       n2,
                       p2,
                       k2);
  }

  // AVR printf does not include float formatting by default.
  // Convert to scaled integers for stable CSV output.
  int16_t m1x10 = (int16_t)roundf(m1 * 10.0f);
  int16_t m2x10 = (int16_t)roundf(m2 * 10.0f);
  int16_t tankx10 = isfinite(tankCm) ? (int16_t)roundf(tankCm * 10.0f) : -10;
  int16_t n1x10 = (int16_t)roundf(n1 * 10.0f);
  int16_t p1x10 = (int16_t)roundf(p1 * 10.0f);
  int16_t k1x10 = (int16_t)roundf(k1 * 10.0f);
  int16_t n2x10 = (int16_t)roundf(n2 * 10.0f);
  int16_t p2x10 = (int16_t)roundf(p2 * 10.0f);
  int16_t k2x10 = (int16_t)roundf(k2 * 10.0f);
  int16_t t1x10 = (int16_t)roundf(t1 * 10.0f);
  int16_t t2x10 = (int16_t)roundf(t2 * 10.0f);

  char payload[128];
  snprintf(payload,
           sizeof(payload),
           "T,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
           (unsigned long)seqCounter,
           (unsigned long)millis(),
           (int)m1x10,
           (int)m2x10,
           (int)tankx10,
           (int)n1x10,
           (int)p1x10,
           (int)k1x10,
           (int)n2x10,
           (int)p2x10,
           (int)k2x10,
           (int)t1x10,
           (int)t2x10);

  uint8_t crc = xorCrc(payload, strlen(payload));

  char frame[160];
  snprintf(frame, sizeof(frame), "%s,%02X", payload, crc);
  espLink.println(frame);
  Serial.println(frame);
}

bool parsePingFrame(const char *line, uint32_t &seq) {
  char copy[120];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  char *ctx = nullptr;
  char *type = strtok_r(copy, ",", &ctx);
  char *seqTok = strtok_r(nullptr, ",", &ctx);
  char *cmd = strtok_r(nullptr, ",", &ctx);
  char *crcTok = strtok_r(nullptr, ",", &ctx);
  if (type == nullptr || seqTok == nullptr || cmd == nullptr || crcTok == nullptr) {
    return false;
  }
  if (strcmp(type, "C") != 0 || strcmp(cmd, "PING") != 0) {
    return false;
  }

  const char *lastComma = strrchr(line, ',');
  if (lastComma == nullptr) {
    return false;
  }

  uint8_t expected = (uint8_t)strtoul(crcTok, nullptr, 16);
  uint8_t actual = xorCrc(line, (size_t)(lastComma - line));
  if (expected != actual) {
    return false;
  }

  seq = strtoul(seqTok, nullptr, 10);
  return true;
}

void sendAck(uint32_t seq) {
  char payload[48];
  snprintf(payload, sizeof(payload), "A,%lu,PONG", (unsigned long)seq);
  uint8_t crc = xorCrc(payload, strlen(payload));

  char frame[64];
  snprintf(frame, sizeof(frame), "%s,%02X", payload, crc);
  espLink.println(frame);
  Serial.println(frame);
}

void serviceEspLink() {
  static char line[120];
  static uint8_t len = 0;

  espLink.listen();
  while (espLink.available()) {
    char c = (char)espLink.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (len > 0) {
        line[len] = '\0';
        uint32_t seq = 0;
        if (parsePingFrame(line, seq)) {
          sendAck(seq);
        }
      }
      len = 0;
      continue;
    }

    if (len >= sizeof(line) - 1) {
      len = 0;
      continue;
    }
    line[len++] = c;
  }
}

void setup() {
  Serial.begin(USB_BAUD);
  espLink.begin(ESP_LINK_BAUD);
  rs485Serial1.begin(rs485BaudCh1);
  rs485Serial2.begin(rs485BaudCh2);

  pinMode(MOISTURE_1_PIN, INPUT);
  pinMode(MOISTURE_2_PIN, INPUT);
  pinMode(TANK_TRIG_PIN, OUTPUT);
  pinMode(TANK_ECHO_PIN, INPUT);
  digitalWrite(TANK_TRIG_PIN, LOW);

  if (RS485_1_DE_RE_PIN != UNASSIGNED_PIN) {
    pinMode(RS485_1_DE_RE_PIN, OUTPUT);
    rs485SetTx(RS485_1_DE_RE_PIN, false);
  }
  if (RS485_2_DE_RE_PIN != UNASSIGNED_PIN) {
    pinMode(RS485_2_DE_RE_PIN, OUTPUT);
    rs485SetTx(RS485_2_DE_RE_PIN, false);
  }

  initDs18b20();

  if (ENABLE_RS485_1_AUTODETECT) {
    if (autodetectRs485Channel(rs485Serial1,
                               RS485_1_DE_RE_PIN,
                               "ch1",
                               rs485BaudCh1,
                               rs485AddrCh1)) {
      rs485Serial1.begin(rs485BaudCh1);
    } else {
      rs485BaudCh1 = RS485_1_BAUD_DEFAULT;
      rs485AddrCh1 = RS485_1_SENSOR_ADDR;
      rs485Serial1.begin(rs485BaudCh1);
    }
  }

  rs485Serial2.begin(rs485BaudCh2);

  delay(200);
  Serial.print("[uno] rs485 ch1 addr=");
  Serial.print(rs485AddrCh1);
  Serial.print(" baud=");
  Serial.println(rs485BaudCh1);
  Serial.print("[uno] rs485 ch2 addr=");
  Serial.print(rs485AddrCh2);
  Serial.print(" baud=");
  Serial.println(rs485BaudCh2);
  Serial.println("[uno] sensor bridge ready");
}

void loop() {
  serviceEspLink();

  uint32_t now = millis();
  if ((uint32_t)(now - lastTelemetryAt) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryAt = now;
    sendTelemetryFrame();
  }
}
