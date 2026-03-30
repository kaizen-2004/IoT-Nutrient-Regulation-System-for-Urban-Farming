# Automated Solar-Powered IoT Monitoring and Nutrient Regulation

This repository now contains:

- `ESP32-C3 firmware` for sensing, control logic, LCD output, and local API
- `Mobile dashboard app` for local-only monitoring over the same Wi‑Fi network

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
- Local network features:
  - Local API served by ESP32-C3 (`/api/info`, `/api/status`, `/healthz`)
  - Email alerts (SMTP) for tank-low events

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
- `apps/mobile-dashboard/`: Capacitor-based local dashboard app
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
   - Wi-Fi (`WIFI_SSID`, `WIFI_PASSWORD`)
   - Local API flag (`ENABLE_LOCAL_API_SERVER`)
   - Email SMTP (`SMTP_HOST`, `SMTP_USERNAME`, `SMTP_PASSWORD`, etc.)
5. Upload to ESP32-C3.
6. Open serial monitor for logs.
7. Find device IP and test:
   - `http://<esp32-ip>/healthz`
   - `http://<esp32-ip>/api/info`
   - `http://<esp32-ip>/api/status`
8. Reserve a fixed DHCP IP for the ESP32-C3 in your router, then reconnect device Wi-Fi.
9. Open `apps/mobile-dashboard/` and install the Capacitor app dependencies.
10. Run the mobile dashboard and connect it to the fixed ESP32 IP (example: `http://192.168.1.50`).

Detailed local fixed-address instructions are in `firmware/esp32-c3/README.md`, and app setup steps are in `apps/mobile-dashboard/README.md`.

## Notes

- NPK reads use RS485/Modbus (`GPIO21` TX, `GPIO20` RX, `GPIO10` DE/RE by default).
- If your relay board is active-low, set `VALVE_ACTIVE_HIGH=false` in firmware.
- Tank sensor defaults to active-low (`TANK_LEVEL_ACTIVE_LOW=true`).
- Email alerts are off by default (`ENABLE_EMAIL_ALERTS=false`) until SMTP settings are configured.
