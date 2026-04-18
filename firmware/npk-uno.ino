#include <SoftwareSerial.h>

#define RX_PIN 2
#define TX_PIN 3
#define RE 6
#define DE 7

SoftwareSerial mod(RX_PIN, TX_PIN); // RX, TX

const byte nitro[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
const byte phos[]  = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
const byte pota[]  = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};

byte response[7];

void clearInput() {
  while (mod.available()) {
    mod.read();
  }
}

bool readResponse(byte *buf, byte expectedBytes, unsigned long timeoutMs) {
  unsigned long start = millis();
  byte index = 0;

  while (index < expectedBytes && (millis() - start) < timeoutMs) {
    if (mod.available()) {
      buf[index++] = mod.read();
    }
  }

  return (index == expectedBytes);
}

bool sendCommand(const byte *cmd, byte cmdLen, byte *buf, byte expectedBytes) {
  clearInput();

  digitalWrite(DE, HIGH);
  digitalWrite(RE, HIGH);
  delay(5);

  for (byte i = 0; i < cmdLen; i++) {
    mod.write(cmd[i]);
  }
  mod.flush();

  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);
  delay(20);

  return readResponse(buf, expectedBytes, 1000);
}

int parseValue(const byte *buf) {
  return (buf[3] << 8) | buf[4];
}

int getNitrogen() {
  if (!sendCommand(nitro, sizeof(nitro), response, 7)) {
    Serial.println("Nitrogen read timeout/no response");
    return -1;
  }

  Serial.print("Raw N response: ");
  for (byte i = 0; i < 7; i++) {
    if (response[i] < 0x10) Serial.print('0');
    Serial.print(response[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  return parseValue(response);
}

int getPhosphorous() {
  if (!sendCommand(phos, sizeof(phos), response, 7)) {
    Serial.println("Phosphorous read timeout/no response");
    return -1;
  }

  Serial.print("Raw P response: ");
  for (byte i = 0; i < 7; i++) {
    if (response[i] < 0x10) Serial.print('0');
    Serial.print(response[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  return parseValue(response);
}

int getPotassium() {
  if (!sendCommand(pota, sizeof(pota), response, 7)) {
    Serial.println("Potassium read timeout/no response");
    return -1;
  }

  Serial.print("Raw K response: ");
  for (byte i = 0; i < 7; i++) {
    if (response[i] < 0x10) Serial.print('0');
    Serial.print(response[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  return parseValue(response);
}

void setup() {
  Serial.begin(9600);
  mod.begin(4800);

  pinMode(RE, OUTPUT);
  pinMode(DE, OUTPUT);

  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);

  delay(1000);
  Serial.println("Soil NPK Sensor");
  Serial.println("Initializing...");
  delay(2000);
}

void loop() {
  int n, p, k;

  Serial.println("Reading NPK...");

  n = getNitrogen();
  if (n >= 0) {
    Serial.print("Nitrogen = ");
    Serial.print(n);
    Serial.println(" mg/kg");
  }

  delay(500);

  p = getPhosphorous();
  if (p >= 0) {
    Serial.print("Phosphorous = ");
    Serial.print(p);
    Serial.println(" mg/kg");
  }

  delay(500);

  k = getPotassium();
  if (k >= 0) {
    Serial.print("Potassium = ");
    Serial.print(k);
    Serial.println(" mg/kg");
  }

  Serial.println("------------------------------");
  delay(3000);
}
