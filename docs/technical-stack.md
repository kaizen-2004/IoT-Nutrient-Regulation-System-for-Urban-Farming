# Nutrient Regulation System: Technical Stack

## 1) Application stack (mobile app)

- **Framework:** Flutter (Dart)
- **Platforms:** Android (APK build enabled; Flutter project also includes iOS/Linux/macOS/web/windows folders)
- **Key packages:**
  - `http` for REST API calls to ESP32
  - `mobile_scanner` for QR onboarding
  - `shared_preferences` for local device/app settings
  - `flutter_local_notifications` for tank/offline notifications
- **App entrypoint:** `app/lib/main.dart`

## 2) Controller firmware stack (ESP32-C3)

- **Platform:** Arduino framework on ESP32-C3
- **Core libraries used:**
  - `WebServer` (local REST API)
  - `WiFi`, `Preferences` (Wi-Fi provisioning and saved credentials)
  - `HardwareSerial` (UART link to Uno bridge)
  - `Wire`, `LiquidCrystal_I2C` (20x4 LCD)
- **Primary responsibilities:**
  - Exposes app APIs (`/api/info`, `/api/status`, provisioning endpoints, manual pump endpoint)
  - Runs watering/nutrient automation cycle
  - Executes manual pump commands from app
  - Receives telemetry from Uno bridge
- **Firmware entrypoint:**
  - `firmware/esp32-c3/esp32_c3_controller/esp32_c3_controller.ino`

## 3) Sensor bridge firmware stack (Arduino Uno)

- **Platform:** Arduino AVR (Uno)
- **Core libraries used:**
  - `SoftwareSerial` (ESP link + RS485 channels)
  - `OneWire`, `DallasTemperature` (DS18B20)
- **Primary responsibilities:**
  - Reads moisture, temperature, tank ultrasonic distance
  - Polls NPK sensors via RS485/Modbus registers
  - Sends telemetry frames to ESP32 over UART
- **Firmware entrypoint:**
  - `firmware/arduino-uno/uno_sensor_bridge/uno_sensor_bridge.ino`

## 4) Communication and API

- **App <-> ESP32:** HTTP JSON over local network
- **ESP32 <-> Uno:** UART serial link (ESP32 RX/TX to Uno TX/RX)
- **Uno <-> NPK sensors:** RS485 (MAX3485/MAX485 channels)
- **Manual control API:** `POST /api/manual/pump`
- **Status API:** `GET /api/status` (includes manual pump states)

## 5) Hardware stack (main components)

- ESP32-C3 controller board
- Arduino Uno sensor bridge board
- Relay modules for water and nutrient pump channels
- Water and nutrient pumps/valves (per relay channel)
- Soil moisture sensors (Zone 1 and Zone 2)
- DS18B20 temperature sensor(s)
- Ultrasonic level sensor (tank level via Uno)
- RS485 transceivers/modules (MAX3485 and MAX485)
- NPK sensor probes
- I2C 20x4 LCD
- Setup/reset push button

## 6) Current firmware pin mapping snapshot (ESP32-C3)

From the current ESP32 firmware constants:

- **Water pump pins:** GPIO6, GPIO4, GPIO3, GPIO7
- **Nutrient pump pins:** GPIO5, GPIO10
- **Setup/reset button:** GPIO1
- **LCD I2C:** SDA GPIO8, SCL GPIO9
- **UART to Uno:** TX GPIO21, RX GPIO20

> Note: If wiring docs and firmware differ, treat firmware constants as the runtime source of truth.
