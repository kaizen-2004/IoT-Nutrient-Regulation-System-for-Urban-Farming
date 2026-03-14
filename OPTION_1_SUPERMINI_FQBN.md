# Option 1: Fixed FQBN Workflow (ESP32-C3 SuperMini)

Use one board profile consistently for all firmware compile/upload steps.

## Fixed Board Profile

- Primary FQBN: `esp32:esp32:nologo_esp32c3_super_mini`
- USB CDC option: `CDCOnBoot=default`

If your board variant matches MakerGO instead, use:

- Alternate FQBN: `esp32:esp32:makergo_c3_supermini`

## 1) Go to Project Root

```bash
cd /home/steve/projects/thesis_nila_go
```

## 2) Confirm Board Port

```bash
arduino-cli board list
```

Example port: `/dev/ttyACM0`

## 3) Compile + Upload a Test Sketch (Smoke Test)

```bash
arduino-cli compile \
  --fqbn esp32:esp32:nologo_esp32c3_super_mini \
  --board-options CDCOnBoot=default \
  firmware/esp32-c3/module-tests/00_serial_smoke_test

arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn esp32:esp32:nologo_esp32c3_super_mini \
  firmware/esp32-c3/module-tests/00_serial_smoke_test
```

## 4) Open Serial Monitor

```bash
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200,dtr=off,rts=off
```

If needed, press the board `RESET` button once after monitor connects.

## 5) Compile + Upload Main Firmware

```bash
arduino-cli compile \
  --fqbn esp32:esp32:nologo_esp32c3_super_mini \
  --board-options CDCOnBoot=default \
  firmware/esp32-c3/esp32_c3_controller

arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn esp32:esp32:nologo_esp32c3_super_mini \
  firmware/esp32-c3/esp32_c3_controller
```

## 6) Module Test Upload Template

Replace `<TEST_FOLDER>` with one of:
- `01_i2c_lcd_test`
- `02_dht_dual_test`
- `03_moisture_adc_dual_test`
- `04_tank_float_test`
- `05_relay_valve_test`
- `06_rs485_npk_single_test`
- `07_rs485_npk_dual_test`
- `08_wifi_api_smoke_test`
- `09_full_integration_test`

```bash
arduino-cli compile \
  --fqbn esp32:esp32:nologo_esp32c3_super_mini \
  --board-options CDCOnBoot=default \
  firmware/esp32-c3/module-tests/<TEST_FOLDER>

arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn esp32:esp32:nologo_esp32c3_super_mini \
  firmware/esp32-c3/module-tests/<TEST_FOLDER>
```

## Notes

- Keep the same FQBN for all builds to avoid USB-CDC mismatches.
- If port changes after upload, run `arduino-cli board list` again and use the new port.
