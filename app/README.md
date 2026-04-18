# VertiFarm App

Flutter companion app for the ESP32-C3 VertiFarm controller.

## Included In This First Pass

- QR scan for the printed device sticker
- SoftAP provisioning flow with manual SSID/password entry
- Local device persistence using the controller's last known IP
- 3-tab monitoring dashboard:
  - `Overview`
  - `Zones`
  - `Device`
- Polling of `GET /api/status` every `3 seconds`

## Expected Device QR Payload

```json
{"v":1,"model":"NRS-C3","deviceId":"plantcare-a1b2c3","setupAp":"NutrientReg-Setup-a1b2c3","setupIp":"192.168.4.1"}
```

## Main Device Endpoints

- `GET /api/provisioning/info`
- `POST /api/provisioning/configure`
- `GET /api/provisioning/result`
- `GET /api/info`
- `GET /api/status`
- `GET /healthz`
- `POST /api/device/reset-wifi`

## Run

```bash
flutter pub get
flutter run
```

## Platform Notes

- Android and iPhone both require the user to join the setup AP manually in Wi-Fi settings.
- The app then talks to the setup controller at `http://192.168.4.1`.
- Android cleartext HTTP is enabled in the app manifest for local-device API calls.
- iOS camera permission text is included for QR scanning.
