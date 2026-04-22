# Agent Notes (Nutrient-Regulation-System)

Use this as repo-specific guardrails. Prefer executable sources over docs when they conflict.

## Canonical Sources

- OpenCode loads instructions from:
  - `AGENTS.md`
  - `docs/decision-support.md`
  - `docs/execution-rules.md`
  - `docs/debugging.md`
  (see `opencode.json`)
- Firmware sketch entrypoints:
  - ESP32-C3: `firmware/esp32-c3/esp32_c3_controller/esp32_c3_controller.ino`
  - Uno bridge: `firmware/arduino-uno/uno_sensor_bridge/uno_sensor_bridge.ino`
- App entrypoint: `app/lib/main.dart`

## Repo Reality (important)

- There is no CI workflow or task runner config in this repo.
- No root build system; use per-target commands.
- `updated_wiring.md` is the latest wiring summary and may be newer than older prose in README files.

## Verified Commands

### Flutter app (`app/`)
- Install deps: `flutter pub get`
- Run app: `flutter run`
- Analyze: `flutter analyze`
- Tests: `flutter test` (current suite includes `app/test/widget_test.dart`)

### ESP32 firmware
- Compile:
  `arduino-cli compile --fqbn esp32:esp32:esp32c3 "firmware/esp32-c3/esp32_c3_controller"`

### Uno firmware
- Compile:
  `arduino-cli compile --fqbn arduino:avr:uno "firmware/arduino-uno/uno_sensor_bridge"`

## API + App Coupling Gotcha

- App currently calls manual pump APIs from `app/lib/main.dart`:
  - `POST /api/manual/pump`
  - expects `status.manual.pumps` data shape
- If firmware removes or changes manual endpoints/state shape, app manual controls will break.
- When changing API routes or payload shape in ESP32 firmware, update app parsing/UI in the same change.

## Hardware/Wiring Gotchas

- ESP32-Uno UART:
  - Uno TX (`D11`) -> ESP32 RX (`GPIO20`) must be level-shifted/divided (5V to 3.3V).
- Current mixed RS485 setup is documented in `updated_wiring.md`:
  - One MAX3485 channel (3.3V logic) and one MAX485 channel (5V logic).
- If wiring changes, update both:
  - `updated_wiring.md`
  - Uno pin constants in `firmware/arduino-uno/uno_sensor_bridge/uno_sensor_bridge.ino`

## Change/Validation Discipline

- Keep diffs narrow and per-target.
- For firmware edits: compile the board you touched (ESP32 and/or Uno).
- For app UI/API edits: run at least `flutter analyze`; run `flutter test` when touching logic/widgets.
- Report exact commands run and whether they passed.
