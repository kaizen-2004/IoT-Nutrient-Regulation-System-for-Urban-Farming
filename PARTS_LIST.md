# Specific Components and Parts (BOM)

This BOM matches the current **ESP32-C3 standalone firmware** design.

| # | Component | Specific Part / Model | Qty | Notes |
|---|---|---|---:|---|
| 1 | Microcontroller | Espressif `ESP32-C3-DevKitM-1` | 1 | Main controller board |
| 2 | Soil Moisture Sensor | DFRobot Capacitive Soil Moisture Sensor `SEN0193` | 2 | One per grow zone |
| 3 | Air Temp/Humidity Sensor | DHT22 / AM2302 module | 2 | One per grow zone |
| 4 | Soil NPK Sensor | JXCT RS485 Soil NPK 3-in-1 Sensor (N/P/K) | 2 | One per grow zone, unique Modbus addresses |
| 5 | RS485 Transceiver | MAX3485 TTL-RS485 module (3.3V logic) | 1 | ESP32-C3 UART to NPK RS485 bus |
| 6 | Tank Level Sensor | Vertical float switch `PP NO/NC` type (FS-01 style) | 1 | Low-water safety signal |
| 7 | Water Solenoid Valve | 12V DC Normally Closed Solenoid Valve, 1/2 inch (e.g., U.S. Solid `USS2-00015`) | 1 | Water line control |
| 8 | Nutrient Solenoid Valve | 12V DC Normally Closed Solenoid Valve, 1/2 inch (same class as water valve) | 1 | Nutrient line control |
| 9 | Relay Driver | 2-channel opto-isolated relay module, 5V coil, 3.3V logic compatible | 1 | Relay CH1 water, CH2 nutrient |
| 10 | LCD Display | 2004 (20x4) LCD + I2C backpack (PCF8574, `0x27`) | 1 | Real-time local monitoring |
| 11 | Flyback Protection | `1N5408` diode | 2 | Across each solenoid coil if not integrated |
| 12 | Relay + Valve Power | 12V DC power supply (>= 5A recommended) | 1 | Dedicated actuator supply |
| 13 | Buck Converter | LM2596 DC-DC step-down module | 1 | 12V to 5V rail where needed |
| 14 | ESP32 Supply (if separate) | 5V USB supply or regulated 5V rail | 1 | Board power input |
| 15 | Solar Panel | 20W 12V nominal solar panel | 1 | For solar operation |
| 16 | Battery | 12V LiFePO4 battery pack (e.g., 6Ah to 12Ah) | 1 | Energy storage |
| 17 | Solar Charge Controller | 12V solar charge controller (LiFePO4 compatible) | 1 | Panel-to-battery management |
| 18 | Plumbing Fittings | 1/2 inch tubing + fittings + hose clamps | set | For valve and tank plumbing |
| 19 | Electrical Hardware | Screw terminal blocks, Dupont/JST leads, enclosure | set | Assembly and reliability |

## Network/Service Requirements

- Wi-Fi network for dashboard access.
- SMTP-capable email account (app password if provider requires it).

## Pin Mapping Used by Firmware

- `GPIO6`: water valve relay input
- `GPIO7`: nutrient valve relay input
- `GPIO3`: tank level sensor input
- `GPIO4`: DHT22 zone 1
- `GPIO5`: DHT22 zone 2
- `GPIO0`: moisture ADC zone 1
- `GPIO1`: moisture ADC zone 2
- LCD: I2C bus (`SDA/SCL` defaults on board)
- RS485 NPK bus:
  - Use `1x` TTL-to-RS485 module for both NPK sensors on shared `A/B` lines.
  - Assign unique Modbus addresses (example: sensor 1 = address `1`, sensor 2 = address `2`).
  - Fixed firmware pins:
    - `GPIO21` -> TTL-RS485 `DI` (TX)
    - `GPIO20` -> TTL-RS485 `RO` (RX)
    - `GPIO10` -> TTL-RS485 `DE` + `RE` (direction control)
