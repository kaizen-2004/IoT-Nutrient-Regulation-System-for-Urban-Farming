# AI Agent Context: Thesis System Specification

This file is the source-of-truth context for any AI agent working on this repository.

## 1) Thesis Title

`Automated Solar-Powered IoT-Based Monitoring and Nutrient Regulation System for Vertical Urban Farming Using Arduino-Based Sensors`

## 2) Final Architecture Decisions (Current Baseline)

- Microcontroller: `ESP32-C3` (final choice for this repo).
- Runtime model: `firmware-only` on microcontroller.
- No PC/Go backend is part of the current system.
- Local dashboard is served directly by ESP32-C3 over Wi-Fi.
- Remote internet access is intentionally deferred for now.

## 3) Core Functional Requirements

- Monitor soil moisture for 2 zones using capacitive sensors.
- Monitor NPK for 2 zones using NPK sensors.
- Monitor ambient temperature/humidity using 2x DHT22.
- Control 2 valves via relays:
  - Valve 1: water
  - Valve 2: nutrient solution
- If moisture is below threshold, open water valve.
- If N/P/K is below threshold, open nutrient valve.
- If temperature is below threshold, water valve may open for recovery.
- Add timing guard/delay to prevent excessive flow.
- Sampling/actuation cycle must conserve power:
  - `30 minutes idle -> 10 minutes active -> repeat`

## 4) Added Requirements Confirmed Later

- Tank water level sensor:
  - If tank is low, block water valve for safety.
- LCD display:
  - Final display type is `20x4 LCD` (I2C), not 16x2.
- Notifications:
  - Email alert support (SMTP) for tank-low event.
- IoT dashboard:
  - On-device dashboard available from ESP32-C3 web server.

## 5) Current Implementation Status

- Main firmware: `firmware/esp32-c3/esp32_c3_controller.ino`
- Documentation:
  - Root overview: `README.md`
  - Firmware setup and pin map: `firmware/esp32-c3/README.md`
  - Detailed component list: `PARTS_LIST.md`

Implemented features in firmware:

- Control cycle and valve timing logic.
- Tank-low safety lockout.
- 20x4 LCD real-time pages.
- Local web dashboard (`/`) and JSON API (`/api/status`).
- SMTP email alert logic (configurable; disabled by default).
- Wi-Fi reconnect handling.

## 6) Important Constraints for Future Agents

- Do not reintroduce PC runtime services unless user explicitly asks.
- Keep ESP32-C3 as default target unless user requests hardware migration.
- Preserve thesis scope (monitoring + automation + safety + dashboard + notifications).
- Keep repository firmware-centric and easy to deploy from Arduino IDE.
- Prefer local-network operation as default mode.

## 7) Known Gaps / Next Technical Improvements

- Production hardening areas:
  - sensor calibration persistence
  - fault tolerance for disconnected sensors
  - stronger email TLS/certificate validation
  - optional mDNS/local hostname support in firmware

## 8) High-Level Hardware Set

- 1x ESP32-C3 dev board
- 2x capacitive soil moisture sensors
- 2x DHT22 sensors
- 2x NPK sensors (RS485 class)
- 1x tank level sensor
- 2x relay channels
- 2x solenoid valves (water + nutrient)
- 1x 20x4 I2C LCD
- Solar + battery + charge controller power system

For exact models and quantities, use `PARTS_LIST.md`.

## 9) Local Access Model (Current)

- Dashboard runs on ESP32-C3 web server, typically:
  - `http://<esp-ip>/`
  - `http://<esp-ip>/api/status`
- Recommended user setup:
  - reserve fixed IP in router DHCP
  - optionally map local hostname
  - add dashboard to phone home screen for app-like use

## 10) Summary for Agent Handoff

This repository represents a finalized ESP32-C3 firmware solution for a 2-zone vertical farming automation thesis system. The software performs periodic sensing, threshold-based valve control, tank-low safety enforcement, local display/dashboard monitoring, and optional email alerts. Continue enhancements within this architecture unless the user explicitly changes project direction.
