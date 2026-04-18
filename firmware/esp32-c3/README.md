# ESP32-C3 Firmware (ESP32 + Uno Split)

This firmware now targets a split architecture: **ESP32-C3 controller + Arduino Uno sensor bridge**.

## Features

- 4x pump relay outputs on ESP32-C3 (`z1a`, `z1b`, `z2a`, `z2b`)
- 20x4 LCD (I2C) driven by ESP32-C3
- JSON API for Flutter app monitoring/provisioning/manual control
- SoftAP setup mode for first-time Wi-Fi onboarding and recovery
- Arduino Uno telemetry bridge over UART (CSV + CRC)
- UNO-side sensing:
  - 2x capacitive soil moisture sensors
  - HC-SR04 tank distance
  - DS18B20 temperature probes (1-wire bus)
  - RS485 NPK acquisition

## Control Logic

- 30 minutes idle, then 10 minutes active window.
- Water valve opens when moisture is low OR temperature is below threshold.
- Nutrient valve opens when N, P, or K is below threshold.
- Water valve is blocked when tank level is low.
- Inter-valve delay is applied to avoid excessive flow.

## File

- `esp32_c3_controller.ino`

## Default Pin Map (ESP32-C3)

- `GPIO6`: Pump relay `z1a`
- `GPIO7`: Pump relay `z1b`
- `GPIO4`: Pump relay `z2a`
- `GPIO3`: Pump relay `z2b`
- `GPIO5`: HC-SR04 `TRIG` (tank sensor)
- `GPIO10`: HC-SR04 `ECHO` (tank sensor)
- `GPIO21`: UART TX to Arduino Uno RX (serial telemetry link)
- `GPIO20`: UART RX from Arduino Uno TX (serial telemetry link)
- LCD: I2C (`0x27`) via board SDA/SCL

## Default Pin Map (Arduino Uno Sensor Bridge)

- `D11` (SoftwareSerial TX) -> ESP32 `GPIO20` (RX)
- `D10` (SoftwareSerial RX) <- ESP32 `GPIO21` (TX)
- `A0`: moisture zone 1 analog
- `A1`: moisture zone 2 analog
- `D2`: DS18B20 one-wire bus
- `D5`/`D4` + `D6`: SoftwareSerial TX/RX/DE-RE for MAX485 #1
- `D8`/`D7` + `D9`: SoftwareSerial TX/RX/DE-RE for MAX485 #2

Adjust pins as needed for your exact board.

## Updated Wiring Table (ESP32-C3 + Uno)

| Link | From | To | Notes |
|---|---|---|---|
| Serial TX | ESP32 `GPIO21` | Uno `D10` (RX) | 3.3V -> 5V logic is generally OK |
| Serial RX | Uno `D11` (TX) | ESP32 `GPIO20` | **Use level shifter or divider** (5V -> 3.3V) |
| Ground | ESP32 `GND` | Uno `GND` | Mandatory shared reference |

| ESP32-C3 output | Connects to |
|---|---|
| `GPIO6` | Relay IN for Pump `z1a` |
| `GPIO7` | Relay IN for Pump `z1b` |
| `GPIO4` | Relay IN for Pump `z2a` |
| `GPIO3` | Relay IN for Pump `z2b` |
| `GPIO5` | HC-SR04 `TRIG` |
| `GPIO10` | HC-SR04 `ECHO` (direct if 3.3V module; level shift if 5V ECHO) |
| I2C `SDA/SCL` | 20x4 LCD backpack (`0x27`) |

| Uno sensor input | Connects to |
|---|---|
| `A0` | Capacitive moisture zone 1 analog out |
| `A1` | Capacitive moisture zone 2 analog out |
| `D2` | DS18B20 one-wire data (with pull-up) |

| Uno RS485 channel | Connects to |
|---|---|
| MAX485 #1: `D5` (TX), `D4` (RX), `D6` (DE+RE) | NPK sensor #1 (address 1) |
| MAX485 #2: `D8` (TX), `D7` (RX), `D9` (DE+RE) | NPK sensor #2 (address 1) |

### Shared Ground Rule

- All low-voltage electronics must share a common reference ground:
  - ESP32 GND
  - Sensor GND
  - RS485 module GND
  - Relay logic GND
  - Tank sensor GND

## Bring-Up Strategy (Recommended)

Do **not** wire everything and debug all modules at once.

Use staged bring-up:

1. Power stage only:
   - Verify solar/charge/battery/buck voltages first.
2. MCU + serial:
   - Flash a minimal blink/serial sketch.
3. I2C LCD:
   - Run I2C scan + LCD hello test.
4. ESP32 <-> Uno telemetry link:
   - Confirm serial frames are arriving before full cycle tests.
5. Moisture sensors:
   - Read raw ADC and validate dry/wet response.
6. HC-SR04 tank sensor:
   - Verify stable distance readings and tune low-distance threshold in firmware.
7. Relay + valves:
   - Test relay channels with valves (or safe dummy load) and flyback protection.
8. RS485 + NPK sensor #1:
   - Bring up one sensor first, verify Modbus response.
9. Full integrated firmware:
   - Enable all modules together and run cycle tests.

### Same Firmware or Different Firmware?

- Best practice: use **small module-specific test sketches first**, then switch to full integrated firmware.
- This shortens debug time and prevents mixed-fault confusion.
- After all modules pass individually, run one integrated firmware for system validation.

## App API And Provisioning

- The firmware now exposes a lightweight JSON API for the Flutter app.
- The large embedded browser dashboard remains disabled by default with `#define ENABLE_LOCAL_API_SERVER 0`.
- The app should use these endpoints instead:
  - `GET /api/info`
  - `GET /api/status`
  - `GET /healthz`
  - `POST /api/device/reset-wifi`

### Provisioning Flow

- If saved Wi-Fi credentials exist, the ESP32-C3 tries to join them on boot.
- If no credentials exist, or connection fails for about `30 seconds`, the device starts a setup AP.
- The setup AP SSID format is `NutrientReg-Setup-<device-suffix>`.
- In setup mode the Flutter app should call:
  - `GET /api/provisioning/info`
  - `POST /api/provisioning/configure`
  - `GET /api/provisioning/result`
- After a successful join, the AP stays up briefly so the app can read the result, then the device returns to normal station mode.

### QR Sticker Payload

- Recommended QR payload format:

```json
{"v":1,"model":"NRS-C3","deviceId":"plantcare-a1b2c3","setupAp":"NutrientReg-Setup-a1b2c3","setupIp":"192.168.4.1"}
```

## LCD Pages (20x4)

- Page 1: system status with phase, cycle count, Wi-Fi state, time left, and tank status
- Page 2: zone moisture for both zones plus water/nutrient valve states
- Low-tank alert: shows a dedicated warning page with refill guidance and watering pause status

## Wi-Fi and Email Setup

Edit constants at top of `esp32_c3_controller.ino`:

- `ENABLE_LOCAL_API_SERVER` (preprocessor define at top of sketch)
- `TELEMETRY_REFRESH_INTERVAL_MS`

`WIFI_SSID` and `WIFI_PASSWORD` are now left blank by default because the Flutter app provisions them into ESP32 `Preferences` storage.

For email alerts:

- `ENABLE_EMAIL_ALERTS`
- `SMTP_HOST`
- `SMTP_PORT` (default `465`)
- `SMTP_USERNAME`
- `SMTP_PASSWORD` (app password if needed)
- `EMAIL_FROM`
- `EMAIL_TO`
- `EMAIL_ALERT_COOLDOWN_MS`

## Required Arduino Libraries

- `DHT sensor library`
- `LiquidCrystal_I2C`

Built-in with ESP32 core:

- `WiFi.h`
- `WebServer.h` (only if `ENABLE_LOCAL_API_SERVER` is set to `1`)
- `WiFiClientSecure.h`

## Upload

1. Open `esp32_c3_controller.ino` in Arduino IDE.
2. Install board support: **esp32 by Espressif Systems**.
3. Select ESP32-C3 board and COM port.
4. Install required libraries.
5. Update Wi-Fi/SMTP constants.
6. Upload.
