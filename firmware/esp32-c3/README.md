# ESP32-C3 Firmware (Standalone)

This project now runs fully on **ESP32-C3** (no PC/Go runtime required).

## Features

- 2x capacitive soil moisture sensors
- 2x DHT22 sensors
- 2x NPK sensors via RS485/Modbus (shared bus, unique addresses)
- 1x tank level sensor for low-water safety
- 2x relay outputs for water/nutrient solenoid valves
- 20x4 LCD (I2C) for real-time local monitoring
- JSON API for Flutter app monitoring and provisioning
- SoftAP setup mode for first-time Wi-Fi onboarding and recovery
- Email alert notification for tank-low events (SMTP)

## Control Logic

- 30 minutes idle, then 10 minutes active window.
- Water valve opens when moisture is low OR temperature is below threshold.
- Nutrient valve opens when N, P, or K is below threshold.
- Water valve is blocked when tank level is low.
- Inter-valve delay is applied to avoid excessive flow.

## File

- `esp32_c3_controller.ino`

## Default Pin Map (ESP32-C3)

- `GPIO6`: water valve relay input
- `GPIO7`: nutrient valve relay input
- `GPIO2`: HC-SR04 `TRIG` (tank level)
- `GPIO3`: HC-SR04 `ECHO` (tank level)
- `GPIO4`: DHT22 zone 1
- `GPIO5`: DHT22 zone 2
- `GPIO0`: moisture ADC zone 1
- `GPIO1`: moisture ADC zone 2
- `GPIO21`: RS485 UART TX -> TTL-RS485 `DI`
- `GPIO20`: RS485 UART RX <- TTL-RS485 `RO`
- `GPIO10`: RS485 direction -> TTL-RS485 `DE` + `RE` (tied)
- LCD: I2C (`0x27`) via default SDA/SCL pins
- RS485 (NPK): use 1x TTL-to-RS485 module, bus shared by both NPK sensors (unique Modbus addresses)

Adjust pins as needed for your exact board.

## Full Wiring Table (Entire Circuit)

Use this as the reference wiring table for the full thesis prototype.

| Component | Qty | Pin / Wire | Connects To | Notes |
|---|---:|---|---|---|
| ESP32-C3 Dev Board | 1 | `GPIO6` | Relay CH1 IN (Water) | Water control output |
| ESP32-C3 Dev Board | 1 | `GPIO7` | Relay CH2 IN (Nutrient) | Nutrient control output |
| ESP32-C3 Dev Board | 1 | `GPIO2` | HC-SR04 `TRIG` | Tank level pulse trigger output |
| ESP32-C3 Dev Board | 1 | `GPIO3` | HC-SR04 `ECHO` | Tank level pulse input |
| ESP32-C3 Dev Board | 1 | `GPIO4` | DHT22 Zone 1 data | Add `10k` pull-up to sensor VCC |
| ESP32-C3 Dev Board | 1 | `GPIO5` | DHT22 Zone 2 data | Add `10k` pull-up to sensor VCC |
| ESP32-C3 Dev Board | 1 | `GPIO0` | Moisture sensor Z1 analog out | ADC input |
| ESP32-C3 Dev Board | 1 | `GPIO1` | Moisture sensor Z2 analog out | ADC input |
| ESP32-C3 Dev Board | 1 | `SDA` | LCD I2C `SDA` | Use board default I2C pin |
| ESP32-C3 Dev Board | 1 | `SCL` | LCD I2C `SCL` | Use board default I2C pin |
| ESP32-C3 Dev Board | 1 | `GPIO21` | TTL-RS485 `DI` | RS485 UART TX |
| ESP32-C3 Dev Board | 1 | `GPIO20` | TTL-RS485 `RO` | RS485 UART RX |
| ESP32-C3 Dev Board | 1 | `GPIO10` | TTL-RS485 `DE` + `RE` (tied) | High = TX, Low = RX |
| Capacitive Moisture Sensor Z1 | 1 | `VCC`, `GND`, `AO` | 3.3V/5V rail, GND, `GPIO0` | Keep output within ESP32 ADC range |
| Capacitive Moisture Sensor Z2 | 1 | `VCC`, `GND`, `AO` | 3.3V/5V rail, GND, `GPIO1` | Keep output within ESP32 ADC range |
| DHT22 Z1 | 1 | `VCC`, `GND`, `DATA` | 3.3V/5V rail, GND, `GPIO4` | `10k` pull-up DATA->VCC |
| DHT22 Z2 | 1 | `VCC`, `GND`, `DATA` | 3.3V/5V rail, GND, `GPIO5` | `10k` pull-up DATA->VCC |
| SN-3002-TR-NPK-N01 #1 | 1 | `A`, `B` | TTL-RS485 `A`, `B` | Assign unique Modbus address (example: `1`) |
| SN-3002-TR-NPK-N01 #2 | 1 | `A`, `B` | Same shared RS485 bus `A`, `B` | Assign unique Modbus address (example: `2`) |
| SN-3002-TR-NPK-N01 #1/#2 | 2 | `V+`, `GND` | Sensor supply rail + common GND | Use sensor-rated DC voltage from datasheet |
| TTL-to-RS485 Module | 1 | `A`, `B` | Both NPK sensors in parallel bus | One module is enough for two sensors |
| TTL-to-RS485 Module | 1 | `VCC`, `GND` | ESP logic rail + GND | Use 3.3V logic-compatible module |
| 2-Channel Relay Module | 1 | `IN1`, `IN2`, `VCC`, `GND` | `GPIO6`, `GPIO7`, relay rail, GND | Confirm active-high/active-low behavior |
| Water Solenoid Valve | 1 | +/− coil | 12V supply via Relay CH1 contacts | Add flyback diode if valve has no internal protection |
| Nutrient Solenoid Valve | 1 | +/− coil | 12V supply via Relay CH2 contacts | Add flyback diode if valve has no internal protection |
| HC-SR04 (3.3V) | 1 | `VCC`, `GND`, `TRIG`, `ECHO` | 3.3V, GND, `GPIO2`, `GPIO3` | Mount above tank; keep sensor head dry |
| 20x4 LCD + I2C Backpack | 1 | `VCC`, `GND`, `SDA`, `SCL` | LCD rail, GND, I2C pins | If backpack pull-ups are 5V, use I2C level shifter |
| Solar Charge Controller | 1 | PV/BAT/LOAD terminals | Solar panel, battery, load rails | Follow controller polarity exactly |
| 12V Battery | 1 | +/− | Charge controller BAT terminals | Main energy storage |
| Solar Panel | 1 | +/− | Charge controller PV terminals | Primary renewable source |
| Buck Converter | 1 | IN+/IN−, OUT+/OUT− | 12V rail to 5V/logic rails | Power ESP/sensors/relay logic as needed |

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
4. DHT22 sensors:
   - Read both sensors and verify stable values.
5. Moisture sensors:
   - Read raw ADC and validate dry/wet response.
6. HC-SR04 tank sensor:
   - Verify stable distance readings and tune low-distance threshold in firmware.
7. Relay + valves:
   - Test relay channels with valves (or safe dummy load) and flyback protection.
8. RS485 + NPK sensor #1:
   - Bring up one sensor first, verify Modbus response.
9. RS485 + NPK sensor #2:
   - Add second sensor, set unique address, poll both on same bus.
10. Full integrated firmware:
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
- If no credentials exist, or connection fails for `5 minutes`, the device starts a setup AP.
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
