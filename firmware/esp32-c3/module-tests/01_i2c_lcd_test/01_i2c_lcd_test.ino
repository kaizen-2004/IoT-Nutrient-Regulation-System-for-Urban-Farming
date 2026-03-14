#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const uint32_t SERIAL_BAUD = 115200;
const uint8_t LCD_ADDR = 0x27;
const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;
const uint32_t LCD_ROTATE_MS = 1500;

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

uint32_t lastSwapAt = 0;
uint32_t page = 0;

void scanI2CBus()
{
  Serial.println("Scanning I2C bus...");
  uint8_t foundCount = 0;

  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0)
    {
      Serial.printf("I2C device found at 0x%02X\n", addr);
      foundCount++;
    }
  }

  if (foundCount == 0)
  {
    Serial.println("No I2C devices found.");
  }
  else
  {
    Serial.printf("I2C scan complete. Devices found: %u\n", foundCount);
  }
}

void drawPage0()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NILA LCD TEST PAGE 1");
  lcd.setCursor(0, 1);
  lcd.print("Addr: 0x27  Size:20x4");
  lcd.setCursor(0, 2);
  lcd.print("Uptime(s): ");
  lcd.print((unsigned long)(millis() / 1000UL));
  lcd.setCursor(0, 3);
  lcd.print("If visible: LCD OK");
}

void drawPage1()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NILA LCD TEST PAGE 2");
  lcd.setCursor(0, 1);
  lcd.print("ABCDEFGHIJKLMNOPQRST");
  lcd.setCursor(0, 2);
  lcd.print("12345678901234567890");
  lcd.setCursor(0, 3);
  lcd.print("Rotate every 1.5 sec");
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 01: I2C LCD ===");

  Wire.begin();
  scanI2CBus();

  lcd.init();
  lcd.backlight();
  drawPage0();

  Serial.println("LCD initialized at 0x27.");
  Serial.println("If screen is blank, check:");
  Serial.println("1) Address (often 0x27 or 0x3F)");
  Serial.println("2) SDA/SCL wiring");
  Serial.println("3) VCC/GND and contrast potentiometer");
}

void loop()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastSwapAt) < LCD_ROTATE_MS)
  {
    delay(20);
    return;
  }

  lastSwapAt = now;
  page = (page + 1) % 2;

  if (page == 0)
  {
    drawPage0();
  }
  else
  {
    drawPage1();
  }

  Serial.printf("[lcd] page=%lu uptimeMs=%lu\n", (unsigned long)page, (unsigned long)now);
}
