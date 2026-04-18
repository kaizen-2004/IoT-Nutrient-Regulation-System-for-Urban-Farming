ESP32-C3 (controller side)
- GPIO6 -> Pump relay z1a
- GPIO7 -> Pump relay z1b
- GPIO4 -> Pump relay z2a
- GPIO3 -> Pump relay z2b
- GPIO21 (TX) -> Uno D10 (RX)
- GPIO20 (RX) <- Uno D11 (TX) via voltage divider
- GPIO8 / GPIO9 -> I2C LCD (SDA / SCL)  
- GPIO5 -> Ultrasonic TRIG
- GPIO10 <- Ultrasonic ECHO via voltage divider if sensor is 5V
Why this is safest:
- Avoids GPIO2 for externally-driven ECHO (boot/strap-sensitive risk).
- Keeps I2C (8/9) isolated from relays.
- Keeps UART link (20/21) isolated from sensors/relays.
---
Arduino Uno (sensor bridge side)
- D10 (RX) <- ESP32 GPIO21 (TX)
- D11 (TX) -> ESP32 GPIO20 (RX) via divider
- A0 -> moisture Z1
- A1 -> moisture Z2
- D2 -> DS18B20 data
- D4 (RX), D5 (TX), D6 (DE+RE) -> MAX485 #1 (NPK sensor #1, addr 1)
- D7 (RX), D8 (TX), D9 (DE+RE) -> MAX485 #2 (NPK sensor #2, addr 1)
This gives each address-1 NPK sensor its own transceiver/channel, so no Modbus address collision.
---
Voltage-divider rule (must-have)
- Required on:
  1) Uno D11 TX -> ESP32 GPIO20 RX
  2) HC-SR04 ECHO -> ESP32 GPIO10 (if sensor powered at 5V)
- Not required on:
  - ESP32 GPIO21 TX -> Uno D10 RX
  - Ultrasonic TRIG output from ESP32
---
