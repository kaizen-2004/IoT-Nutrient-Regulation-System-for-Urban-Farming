# Automated Solar-Powered IoT Monitoring and Nutrient Regulation

This repository now contains:

- `ESP32-C3 firmware` for sensing, control logic, LCD output, and safety automation
- `Cloud dashboard direction` (Supabase + Render) on branch `feature/cloud-dashboard`

## Included Capabilities

- Soil monitoring:
  - 2x capacitive soil moisture sensors
  - 2x NPK soil sensors via RS485/Modbus (addresses 1 and 2 by default)
- Ambient monitoring:
  - 2x DHT22 sensors (temperature/humidity)
- Tank safety:
  - 1x water tank low-level sensor blocks water valve when tank is low
- Actuation:
  - 2 relay channels driving 2 solenoid valves (water, nutrient)
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

- `firmware/esp32-c3/esp32_c3_controller.ino`: main firmware
- `firmware/esp32-c3/README.md`: firmware setup, pins, and feature guide
- `PARTS_LIST.md`: specific components and parts list
- `HARDWARE_BRINGUP_STATUS.md`: running checklist of module validation progress
- `AGENT.md`: thesis/project description source

## Getting Started

1. Open `firmware/esp32-c3/esp32_c3_controller.ino` in Arduino IDE.
2. Install board support: **esp32 by Espressif Systems**.
3. Install Arduino libraries:
   - `DHT sensor library`
   - `LiquidCrystal_I2C`
4. Configure constants at top of sketch:
   - Email SMTP (`SMTP_HOST`, `SMTP_USERNAME`, `SMTP_PASSWORD`, etc.)
5. Upload to ESP32-C3.
6. Open serial monitor for logs.
7. On first boot, connect the Flutter app to the device setup AP shown on the LCD.
8. Scan the printed device QR sticker in the app, then send your home `2.4 GHz` Wi-Fi credentials.
9. Reserve a fixed DHCP IP for the ESP32-C3 in your router after provisioning completes.
10. Use the app dashboard or serial logs as device-side verification.

## App Provisioning Flow

- Device boots with saved Wi-Fi credentials and tries station mode first.
- If no credentials exist, or Wi-Fi stays unavailable for `5 minutes`, the ESP32-C3 starts a setup AP.
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

- NPK reads use RS485/Modbus (`GPIO21` TX, `GPIO20` RX, `GPIO10` DE/RE by default).
- If your relay board is active-low, set `VALVE_ACTIVE_HIGH=false` in firmware.
- Tank sensor defaults to active-low (`TANK_LEVEL_ACTIVE_LOW=true`).
- Email alerts are off by default (`ENABLE_EMAIL_ALERTS=false`) until SMTP settings are configured.
