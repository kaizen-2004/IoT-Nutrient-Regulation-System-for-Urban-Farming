#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

// ----------------------------
// Core configuration
// ----------------------------
const uint32_t SERIAL_BAUD = 115200;

// Set true only when you want to test Wi-Fi/API in integrated mode.
const bool ENABLE_WIFI_API_TEST = false;
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const uint32_t WIFI_RETRY_INTERVAL_MS = 15000UL;

// ----------------------------
// Pin mapping (ESP32-C3 SuperMini)
// ----------------------------
const uint8_t WATER_RELAY_PIN = 6;
const uint8_t NUTRIENT_RELAY_PIN = 7;
const uint8_t TANK_LEVEL_PIN = 3;
const bool TANK_LEVEL_ACTIVE_LOW = true;

const uint8_t DHT_PIN_Z1 = 4;
const uint8_t DHT_PIN_Z2 = 5;

const uint8_t MOISTURE_PIN_Z1 = 0;
const uint8_t MOISTURE_PIN_Z2 = 1;
const int MOISTURE_DRY_RAW_Z1 = 3000;
const int MOISTURE_WET_RAW_Z1 = 1300;
const int MOISTURE_DRY_RAW_Z2 = 3000;
const int MOISTURE_WET_RAW_Z2 = 1300;

const uint8_t RS485_TX_PIN = 21;
const uint8_t RS485_RX_PIN = 20;
const uint8_t RS485_DE_RE_PIN = 10;
const bool RS485_DE_RE_TX_HIGH = true;
const uint32_t RS485_BAUD = 9600;
const uint8_t NPK_ADDR_Z1 = 1;
const uint8_t NPK_ADDR_Z2 = 2;
const uint32_t RS485_TIMEOUT_MS = 500;

const uint8_t LCD_ADDR = 0x27;
const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

// Relay logic polarity (set false if active-low relay board).
const bool RELAY_ACTIVE_HIGH = true;

// ----------------------------
// Timing
// ----------------------------
const uint32_t SAMPLE_MS = 3000;
const uint32_t LCD_UPDATE_MS = 1000;
const uint32_t STATUS_PRINT_MS = 3000;

// ----------------------------
// Objects
// ----------------------------
DHT dhtZ1(DHT_PIN_Z1, DHT22);
DHT dhtZ2(DHT_PIN_Z2, DHT22);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
HardwareSerial RS485Serial(1);
WebServer server(80);

// ----------------------------
// Runtime state
// ----------------------------
struct NPK
{
  uint16_t n;
  uint16_t p;
  uint16_t k;
  bool ok;
};

struct Telemetry
{
  float moisturePctZ1;
  float moisturePctZ2;
  float tempCZ1;
  float tempCZ2;
  float humidityPctZ1;
  float humidityPctZ2;
  bool tankLow;
  NPK npkZ1;
  NPK npkZ2;
};

Telemetry latest = {};
bool waterRelayOn = false;
bool nutrientRelayOn = false;

uint32_t lastSampleAt = 0;
uint32_t lastLCDAt = 0;
uint32_t lastStatusPrintAt = 0;
uint32_t sampleCount = 0;

bool serverStarted = false;
uint32_t lastWiFiRetryAt = 0;

// ----------------------------
// Utilities
// ----------------------------
bool isConfiguredValue(const char *value)
{
  if (value == nullptr || strlen(value) == 0)
  {
    return false;
  }
  return strncmp(value, "YOUR_", 5) != 0;
}

bool hasWiFiConfig()
{
  return isConfiguredValue(WIFI_SSID) && isConfiguredValue(WIFI_PASSWORD);
}

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

void setRelay(uint8_t pin, bool on)
{
  bool level = on ? RELAY_ACTIVE_HIGH : !RELAY_ACTIVE_HIGH;
  digitalWrite(pin, level ? HIGH : LOW);

  if (pin == WATER_RELAY_PIN)
  {
    waterRelayOn = on;
  }
  else if (pin == NUTRIENT_RELAY_PIN)
  {
    nutrientRelayOn = on;
  }
}

void allRelaysOff()
{
  setRelay(WATER_RELAY_PIN, false);
  setRelay(NUTRIENT_RELAY_PIN, false);
}

// ----------------------------
// RS485 / Modbus helpers
// ----------------------------
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

NPK readNPK(uint8_t addr)
{
  NPK out = {0, 0, 0, false};

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
  while ((uint32_t)(millis() - startedAt) < RS485_TIMEOUT_MS)
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
    return out;
  }

  uint16_t crcRx = (uint16_t)resp[9] | ((uint16_t)resp[10] << 8);
  uint16_t crcCalc = modbusCRC16(resp, 9);
  if (crcRx != crcCalc)
  {
    return out;
  }

  out.n = ((uint16_t)resp[3] << 8) | resp[4];
  out.p = ((uint16_t)resp[5] << 8) | resp[6];
  out.k = ((uint16_t)resp[7] << 8) | resp[8];
  out.ok = true;
  return out;
}

// ----------------------------
// Wi-Fi API
// ----------------------------
void sendCommonHeaders()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
}

void handleOptions()
{
  sendCommonHeaders();
  server.send(204, "text/plain", "");
}

void handleRoot()
{
  sendCommonHeaders();
  server.send(200, "text/plain", "NILA full integration test online.");
}

void handleHealthz()
{
  sendCommonHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleStatus()
{
  String json = "{";
  json += "\"sampleCount\":";
  json += String(sampleCount);
  json += ",\"uptimeMs\":";
  json += String(millis());
  json += ",\"wifiConnected\":";
  json += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",\"tankLow\":";
  json += (latest.tankLow ? "true" : "false");
  json += ",\"waterRelayOn\":";
  json += (waterRelayOn ? "true" : "false");
  json += ",\"nutrientRelayOn\":";
  json += (nutrientRelayOn ? "true" : "false");
  json += ",\"zones\":[";
  json += "{";
  json += "\"zone\":1";
  json += ",\"moisturePct\":";
  json += String(latest.moisturePctZ1, 2);
  json += ",\"tempC\":";
  json += String(latest.tempCZ1, 2);
  json += ",\"humidityPct\":";
  json += String(latest.humidityPctZ1, 2);
  json += ",\"npkOk\":";
  json += (latest.npkZ1.ok ? "true" : "false");
  json += ",\"npk\":{\"n\":";
  json += String(latest.npkZ1.n);
  json += ",\"p\":";
  json += String(latest.npkZ1.p);
  json += ",\"k\":";
  json += String(latest.npkZ1.k);
  json += "}";
  json += "},";
  json += "{";
  json += "\"zone\":2";
  json += ",\"moisturePct\":";
  json += String(latest.moisturePctZ2, 2);
  json += ",\"tempC\":";
  json += String(latest.tempCZ2, 2);
  json += ",\"humidityPct\":";
  json += String(latest.humidityPctZ2, 2);
  json += ",\"npkOk\":";
  json += (latest.npkZ2.ok ? "true" : "false");
  json += ",\"npk\":{\"n\":";
  json += String(latest.npkZ2.n);
  json += ",\"p\":";
  json += String(latest.npkZ2.p);
  json += ",\"k\":";
  json += String(latest.npkZ2.k);
  json += "}";
  json += "}";
  json += "]";
  json += "}";

  sendCommonHeaders();
  server.send(200, "application/json", json);
}

void beginWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectWiFiBlocking()
{
  if (!ENABLE_WIFI_API_TEST || !hasWiFiConfig())
  {
    return;
  }

  beginWiFi();
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - startedAt) < WIFI_CONNECT_TIMEOUT_MS)
  {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("Wi-Fi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  }
  else
  {
    Serial.println("Wi-Fi initial connection failed.");
  }
}

void maintainWiFi()
{
  if (!ENABLE_WIFI_API_TEST || !hasWiFiConfig())
  {
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - lastWiFiRetryAt) < WIFI_RETRY_INTERVAL_MS)
  {
    return;
  }

  lastWiFiRetryAt = now;
  Serial.println("Wi-Fi reconnect attempt...");
  WiFi.disconnect();
  delay(20);
  beginWiFi();
}

void setupServerIfNeeded()
{
  if (!ENABLE_WIFI_API_TEST || serverStarted || WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/healthz", HTTP_GET, handleHealthz);
  server.on("/healthz", HTTP_OPTIONS, handleOptions);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.onNotFound([]()
                    {
    sendCommonHeaders();
    server.send(404, "application/json", "{\"error\":\"not_found\"}"); });

  server.begin();
  serverStarted = true;
  Serial.printf("API online: http://%s/api/status\n", WiFi.localIP().toString().c_str());
}

// ----------------------------
// LCD / serial integration
// ----------------------------
void scanI2CBus()
{
  Serial.println("I2C scan start...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
    {
      Serial.printf("I2C device @ 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("I2C scan done. Found=%u\n", found);
}

void updateLCD()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastLCDAt) < LCD_UPDATE_MS)
  {
    return;
  }
  lastLCDAt = now;

  char line1[21];
  char line2[21];
  char line3[21];
  char line4[21];

  const char *wifiText = (ENABLE_WIFI_API_TEST && WiFi.status() == WL_CONNECTED) ? "OK" : "NO";
  const char *tankText = latest.tankLow ? "LOW" : "OK";

  snprintf(line1, sizeof(line1), "INTTEST Wi:%s C:%lu", wifiText, (unsigned long)sampleCount);
  snprintf(line2, sizeof(line2), "M1:%2.0f M2:%2.0f", latest.moisturePctZ1, latest.moisturePctZ2);
  snprintf(line3, sizeof(line3), "T1:%2.0f H1:%2.0f", latest.tempCZ1, latest.humidityPctZ1);
  snprintf(line4, sizeof(line4), "Tnk:%s W:%s N:%s", tankText, waterRelayOn ? "ON" : "OFF", nutrientRelayOn ? "ON" : "OFF");

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

void printStatus()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastStatusPrintAt) < STATUS_PRINT_MS)
  {
    return;
  }
  lastStatusPrintAt = now;

  Serial.printf(
      "[status] sample=%lu tankLow=%s W=%s N=%s | Z1 M=%.2f T=%.2f H=%.2f NPK(%u,%u,%u,%s) | Z2 M=%.2f T=%.2f H=%.2f NPK(%u,%u,%u,%s)\n",
      (unsigned long)sampleCount,
      latest.tankLow ? "true" : "false",
      waterRelayOn ? "on" : "off",
      nutrientRelayOn ? "on" : "off",
      latest.moisturePctZ1,
      latest.tempCZ1,
      latest.humidityPctZ1,
      latest.npkZ1.n,
      latest.npkZ1.p,
      latest.npkZ1.k,
      latest.npkZ1.ok ? "ok" : "fail",
      latest.moisturePctZ2,
      latest.tempCZ2,
      latest.humidityPctZ2,
      latest.npkZ2.n,
      latest.npkZ2.p,
      latest.npkZ2.k,
      latest.npkZ2.ok ? "ok" : "fail");
}

void sampleAllModules()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastSampleAt) < SAMPLE_MS)
  {
    return;
  }
  lastSampleAt = now;
  sampleCount++;

  int rawZ1 = readAveragedRaw(MOISTURE_PIN_Z1);
  int rawZ2 = readAveragedRaw(MOISTURE_PIN_Z2);
  latest.moisturePctZ1 = moisturePercentFromRaw(rawZ1, MOISTURE_DRY_RAW_Z1, MOISTURE_WET_RAW_Z1);
  latest.moisturePctZ2 = moisturePercentFromRaw(rawZ2, MOISTURE_DRY_RAW_Z2, MOISTURE_WET_RAW_Z2);

  float t1 = dhtZ1.readTemperature();
  float h1 = dhtZ1.readHumidity();
  float t2 = dhtZ2.readTemperature();
  float h2 = dhtZ2.readHumidity();
  if (!isnan(t1))
  {
    latest.tempCZ1 = t1;
  }
  if (!isnan(h1))
  {
    latest.humidityPctZ1 = h1;
  }
  if (!isnan(t2))
  {
    latest.tempCZ2 = t2;
  }
  if (!isnan(h2))
  {
    latest.humidityPctZ2 = h2;
  }

  latest.tankLow = isTankLevelLow();
  latest.npkZ1 = readNPK(NPK_ADDR_Z1);
  delay(20);
  latest.npkZ2 = readNPK(NPK_ADDR_Z2);
}

// ----------------------------
// Manual serial command handler
// ----------------------------
void printCommandHelp()
{
  Serial.println("Commands:");
  Serial.println("  status");
  Serial.println("  w on | w off | w pulse");
  Serial.println("  n on | n off | n pulse");
  Serial.println("  all off");
  Serial.println("  help");
}

void runRelayPulse(uint8_t pin, uint32_t pulseMs)
{
  setRelay(pin, true);
  delay(pulseMs);
  setRelay(pin, false);
}

void handleCommand(const String &line)
{
  String cmd = line;
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0)
  {
    return;
  }

  if (cmd == "help")
  {
    printCommandHelp();
    return;
  }
  if (cmd == "status")
  {
    printStatus();
    return;
  }
  if (cmd == "all off")
  {
    allRelaysOff();
    Serial.println("All relays OFF.");
    return;
  }

  if (cmd == "w on")
  {
    if (latest.tankLow)
    {
      Serial.println("Blocked: tank low.");
      return;
    }
    setRelay(WATER_RELAY_PIN, true);
    Serial.println("Water relay ON.");
    return;
  }
  if (cmd == "w off")
  {
    setRelay(WATER_RELAY_PIN, false);
    Serial.println("Water relay OFF.");
    return;
  }
  if (cmd == "w pulse")
  {
    if (latest.tankLow)
    {
      Serial.println("Blocked: tank low.");
      return;
    }
    runRelayPulse(WATER_RELAY_PIN, 1200);
    Serial.println("Water relay pulse done.");
    return;
  }

  if (cmd == "n on")
  {
    setRelay(NUTRIENT_RELAY_PIN, true);
    Serial.println("Nutrient relay ON.");
    return;
  }
  if (cmd == "n off")
  {
    setRelay(NUTRIENT_RELAY_PIN, false);
    Serial.println("Nutrient relay OFF.");
    return;
  }
  if (cmd == "n pulse")
  {
    runRelayPulse(NUTRIENT_RELAY_PIN, 1200);
    Serial.println("Nutrient relay pulse done.");
    return;
  }

  Serial.printf("Unknown command: %s\n", cmd.c_str());
  printCommandHelp();
}

void serviceSerialCommands()
{
  if (!Serial.available())
  {
    return;
  }
  String line = Serial.readStringUntil('\n');
  handleCommand(line);
}

// ----------------------------
// Setup / loop
// ----------------------------
void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(500);
  Serial.println();
  Serial.println("=== NILA Module Test 09: Full Integration Test ===");
  Serial.println("Relays start OFF. Use serial commands for manual relay tests.");
  Serial.println("This sketch is for hardware integration validation, not production automation.");

  pinMode(WATER_RELAY_PIN, OUTPUT);
  pinMode(NUTRIENT_RELAY_PIN, OUTPUT);
  allRelaysOff();

  pinMode(TANK_LEVEL_PIN, INPUT_PULLUP);

  analogSetPinAttenuation(MOISTURE_PIN_Z1, ADC_11db);
  analogSetPinAttenuation(MOISTURE_PIN_Z2, ADC_11db);

  dhtZ1.begin();
  dhtZ2.begin();

  pinMode(RS485_DE_RE_PIN, OUTPUT);
  setRS485DirectionTx(false);
  RS485Serial.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  Wire.begin();
  scanI2CBus();
  lcd.init();
  lcd.backlight();

  if (ENABLE_WIFI_API_TEST)
  {
    if (!hasWiFiConfig())
    {
      Serial.println("Wi-Fi test enabled but credentials are placeholders.");
    }
    connectWiFiBlocking();
    setupServerIfNeeded();
  }

  printCommandHelp();
}

void loop()
{
  maintainWiFi();
  setupServerIfNeeded();
  if (serverStarted)
  {
    server.handleClient();
  }

  sampleAllModules();
  updateLCD();
  printStatus();
  serviceSerialCommands();

  delay(20);
}
