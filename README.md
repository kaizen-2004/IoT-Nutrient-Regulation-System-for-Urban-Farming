# Automated Solar-Powered IoT Monitoring and Nutrient Regulation

This repository now contains:

- `ESP32-C3 controller firmware` for Wi-Fi, LCD, API, and pump control
- `Arduino Uno sensor bridge firmware` for soil/tank/temp/NPK telemetry
- `Cloud dashboard direction` (Supabase + Render) on branch `feature/cloud-dashboard`

## Included Capabilities

- Soil monitoring via Uno bridge:
  - 2x capacitive soil moisture sensors
  - NPK telemetry over RS485/Modbus
- Ambient monitoring via Uno bridge:
  - DS18B20 probes on 1-wire bus
- Tank safety:
  - 1x water tank distance sensor blocks watering when tank is low
- Actuation:
  - 4 relay channels driving 4 pumps (`z1a`, `z1b`, `z2a`, `z2b`)
- Local display:
  - 20x4 LCD (I2C) real-time monitoring pages
- Network features:
  - Email alerts (SMTP) for tank-low events
  - App-oriented JSON API for provisioning and monitoring
  - SoftAP fallback provisioning for first setup and Wi-Fi recovery

## Control Process

- `30 minutes idle -> 10 minutes active -> repeat`
- During active window:
  - Read sensors
  - Decide valve durations based on thresholds
  - Open water/nutrient valves with delay spacing
  - Enforce tank-low safety lockout for water valve

## Project Structure

- `firmware/esp32-c3/esp32_c3_controller/esp32_c3_controller.ino`: ESP32-C3 controller firmware
- `firmware/arduino-uno/uno_sensor_bridge/uno_sensor_bridge.ino`: Arduino Uno sensor bridge firmware
- `firmware/esp32-c3/README.md`: firmware setup, pins, and feature guide
- `PARTS_LIST.md`: specific components and parts list
- `HARDWARE_BRINGUP_STATUS.md`: running checklist of module validation progress
- `AGENT.md`: thesis/project description source

## Getting Started

1. Open `firmware/esp32-c3/esp32_c3_controller/esp32_c3_controller.ino` in Arduino IDE.
2. Install board support: **esp32 by Espressif Systems**.
3. Install Arduino libraries:
   - `DHT sensor library`
   - `LiquidCrystal_I2C`
4. Configure constants at top of sketch:
   - Email SMTP (`SMTP_HOST`, `SMTP_USERNAME`, `SMTP_PASSWORD`, etc.)
5. Upload ESP32-C3 firmware.
6. Open and upload `firmware/arduino-uno/uno_sensor_bridge/uno_sensor_bridge.ino` to Arduino Uno.
7. Wire ESP32/Uno UART and shared GND as listed in `firmware/esp32-c3/README.md`.
8. Open serial monitor for logs.
9. On first boot, connect the Flutter app to the device setup AP shown on the LCD.
10. Scan the printed device QR sticker in the app, then send your home `2.4 GHz` Wi-Fi credentials.
11. Reserve a fixed DHCP IP for the ESP32-C3 in your router after provisioning completes.
12. Use the app dashboard or serial logs as device-side verification.

## App Provisioning Flow

- Device boots with saved Wi-Fi credentials and tries station mode first.
- If no credentials exist, or Wi-Fi stays unavailable for about `30 seconds`, the ESP32-C3 starts a setup AP.
- The Flutter app scans the printed QR sticker, connects to the setup AP, and calls:
  - `GET /api/provisioning/info`
  - `POST /api/provisioning/configure`
  - `GET /api/provisioning/result`
- After provisioning, the app monitors the controller over the normal JSON endpoints:
  - `GET /api/info`
  - `GET /api/status`
  - `GET /healthz`
  - `POST /api/device/reset-wifi`

## QR Sticker Payload

- Recommended QR payload format:

```json
{"v":1,"model":"NRS-C3","deviceId":"plantcare-a1b2c3","setupAp":"NutrientReg-Setup-a1b2c3","setupIp":"192.168.4.1"}
```

Branch-specific firmware details are in `firmware/esp32-c3/README.md`.

## Notes

- Serial link between Uno and ESP32 runs at `19200` baud by default.
- Protect ESP32 RX with level shifting (Uno TX is 5V).
- If your relay board is active-low, set `VALVE_ACTIVE_HIGH=false` in firmware.
- Email alerts are off by default (`ENABLE_EMAIL_ALERTS=false`) until SMTP settings are configured.
