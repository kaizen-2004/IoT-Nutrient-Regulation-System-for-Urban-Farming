ESP32-C3 (controller side)
- GPIO6  -> Water pump relay #1
- GPIO4  -> Water pump relay #2
- GPIO3  -> Water pump relay #3
- GPIO5  -> Water pump relay #4
- GPIO7  -> Nutrient pump relay #1
- GPIO10 -> Nutrient pump relay #2
- GPIO1  -> Reset/setup button (hold ~4s to force setup mode)
- GPIO8 / GPIO9 -> I2C LCD (SDA / SCL)
- GPIO21 (TX) -> Uno D10 (RX)
- GPIO20 (RX) <- Uno D11 (TX) via voltage divider

Why this is safest:
- ESP32 pins are dedicated to relays, LCD, reset button, and Uno link only.
- Tank ultrasonic is fully moved to Uno, avoiding ESP32 pin conflicts.
- UART link (20/21) remains isolated from relay switching pins.

---
Arduino Uno (sensor bridge side)
- D10 (RX) <- ESP32 GPIO21 (TX)
- D11 (TX) -> ESP32 GPIO20 (RX) via divider
- A0 -> moisture Z1
- A1 -> moisture Z2
- D2 -> DS18B20 data
- D12 -> Ultrasonic TRIG
- A2  -> Ultrasonic ECHO

RS485 channels (zone mapping):
- Channel 1 (MAX3485, 3.3V logic, 4-pin module: VCC/TXD/RXD/GND):
  - D4 (RX) <- MAX3485 TXD (direct)
  - D5 (TX) -> MAX3485 RXD via voltage divider
  - D6 unused (no DE/RE pin exposed on this module)
  - NPK probe mapped: Zone 1 / NPK sensor #1 (addr 1)
  - MAX3485 VCC -> 3.3V

- Channel 2 (MAX485, 5V logic):
  - D7 (RX), D8 (TX), D9 (DE+RE) -> MAX485 #2 (NPK sensor #2, addr 1)
  - NPK probe mapped: Zone 2 / NPK sensor #2 (addr 1)
  - MAX485 VCC -> 5V

---
Voltage-divider rule (must-have)
- Required on:
  1) Uno D11 TX -> ESP32 GPIO20 RX
  2) Uno D5 TX  -> MAX3485 RXD

- Divider values (for each required line):
  - R1 = 10k ohm (from TX pin to divider node)
  - R2 = 20k ohm (from divider node to GND)
  - Divider node -> target RX input (ESP32 GPIO20 or MAX3485 RXD)

- Not required on:
  - ESP32 GPIO21 TX -> Uno D10 RX
  - MAX3485 TXD -> Uno D4 RX
  - Uno ultrasonic connections (D12/A2)
---
