#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <Wire.h>

// ESP32-C3 firmware for thesis project:
// Automated Solar-Powered IoT-Based Monitoring and Nutrient Regulation
// Cycle: 30 minutes idle, 10 minutes active

struct NPK
{
  float n;
  float p;
  float k;
};

struct ZoneReadings
{
  float moisturePct;
  float tempC;
  float humidityPct;
  NPK npk;
};

struct ZoneThresholds
{
  float moistureMinPct;
  float tempMinC;
  float nMin;
  float pMin;
  float kMin;
};

// Explicit forward declarations avoid Arduino auto-prototype misses
// when large embedded HTML/JS payloads are present in this sketch.
void maintainWiFiConnection();
void emitReadings(uint8_t zoneIndex, const ZoneReadings &r);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void scanAndLogTargetSSID();
void setupMDNSService();
void stopMDNSService();

const uint8_t NUM_ZONES = 2;
const char *DEVICE_NAME = "Vertical Farm Controller";
const char *FIRMWARE_VERSION = "2.0.0";
const char *API_VERSION = "1.0.0";
const char *METHODOLOGY_PROFILE = "chapter3_baseline_v1";

// Timing configuration
const uint32_t IDLE_DURATION_MS = 30UL * 60UL * 1000UL;
const uint32_t ACTIVE_DURATION_MS = 10UL * 60UL * 1000UL;
const uint32_t BETWEEN_VALVE_DELAY_MS = 15UL * 1000UL;

const uint32_t MIN_WATER_PULSE_MS = 45UL * 1000UL;
const uint32_t MAX_WATER_PULSE_MS = 3UL * 60UL * 1000UL;
const uint32_t TEMP_RECOVERY_PULSE_MS = 90UL * 1000UL;

const uint32_t MIN_NUTRIENT_PULSE_MS = 30UL * 1000UL;
const uint32_t MAX_NUTRIENT_PULSE_MS = 2UL * 60UL * 1000UL;
const float DEFICIT_DEADBAND_RATIO = 0.05f;
const float SENSOR_SMOOTHING_ALPHA = 0.35f;
const float TEMP_RECOVERY_MOISTURE_MARGIN_PCT = 10.0f;
const float MIN_MOISTURE_FOR_NUTRIENT_PCT = 20.0f;
const float DEMAND_BLEND_MAX_WEIGHT = 0.70f;
const float DEMAND_BLEND_AVG_WEIGHT = 0.30f;
const uint32_t CLIMATE_SUPPORT_WATER_PULSE_MS = 30UL * 1000UL;
const uint32_t REDUCED_NUTRIENT_PULSE_MS = 10UL * 1000UL;
const float SOIL_MOISTURE_CRITICAL_DRY_PCT = 20.0f;
const float SOIL_MOISTURE_LOW_PCT = 30.0f;
const float SOIL_MOISTURE_OPTIMAL_HIGH_PCT = 45.0f;
const float TEMP_COLD_C = 20.0f;
const float TEMP_HOT_C = 35.0f;
const float HUMIDITY_DRY_PCT = 50.0f;
const float HUMIDITY_HUMID_PCT = 85.0f;
const float NUTRIENT_CRITICAL_LOW_PPM = 600.0f;
const float NUTRIENT_LOW_PPM = 800.0f;
const float NUTRIENT_OPTIMAL_HIGH_PPM = 1200.0f;
const float NUTRIENT_CRITICAL_HIGH_PPM = 1400.0f;
const float NPK_TO_PPM_SCALE = 20.0f;
const float TEMP_HOT_WATER_BOOST_RATIO = 0.20f;
const float HUMIDITY_DRY_WATER_BOOST_RATIO = 0.10f;
const float TEMP_COLD_WATER_REDUCTION_FACTOR = 0.60f;
const float HUMIDITY_HIGH_WATER_REDUCTION_FACTOR = 0.70f;

// Relay configuration (set to false if your relay board is active-low)
const bool VALVE_ACTIVE_HIGH = true;

// Water tank level input configuration
const uint8_t TANK_LEVEL_PIN = 3;
const bool TANK_LEVEL_ACTIVE_LOW = true;

// Pin map for ESP32-C3 (adjust for your exact board wiring)
const uint8_t WATER_VALVE_PIN = 6;
const uint8_t NUTRIENT_VALVE_PIN = 7;
const uint8_t DHT_PINS[NUM_ZONES] = {4, 5};
const uint8_t MOISTURE_PINS[NUM_ZONES] = {0, 1};
const uint8_t RS485_TX_PIN = 21;
const uint8_t RS485_RX_PIN = 20;
const uint8_t RS485_DE_RE_PIN = 10;

// Moisture calibration per zone (ADC raw values, 12-bit: 0..4095)
const int MOISTURE_DRY_RAW[NUM_ZONES] = {3000, 3000};
const int MOISTURE_WET_RAW[NUM_ZONES] = {1300, 1300};

// LCD 20x4 I2C (PCF8574 backpack)
const uint8_t LCD_I2C_ADDRESS = 0x27;
const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;
const uint32_t LCD_PAGE_INTERVAL_MS = 1000UL;
const uint32_t TELEMETRY_REFRESH_INTERVAL_MS = 3000UL;

// Wi-Fi and local API configuration
const bool ENABLE_LOCAL_API_SERVER = true;
const char *WIFI_SSID = "CondoIoT";
const char *WIFI_PASSWORD = "condo2026";
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const uint32_t WIFI_RETRY_INTERVAL_MS = 15000UL;
const wifi_power_t WIFI_TX_POWER_LEVEL = WIFI_POWER_8_5dBm;
const bool ENABLE_MDNS = true;
const char *MDNS_HOSTNAME = "plantcare";

// Email alert configuration
const bool ENABLE_EMAIL_ALERTS = false;
const char *SMTP_HOST = "smtp.gmail.com";
const uint16_t SMTP_PORT = 465;
const char *SMTP_USERNAME = "YOUR_EMAIL_USERNAME";
const char *SMTP_PASSWORD = "YOUR_EMAIL_PASSWORD"; // App password for Gmail
const char *EMAIL_FROM = "YOUR_EMAIL_FROM";
const char *EMAIL_TO = "YOUR_EMAIL_TO";
const uint32_t EMAIL_ALERT_COOLDOWN_MS = 30UL * 60UL * 1000UL;
const bool SMTP_ALLOW_INSECURE_TLS = true;

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);
WebServer webServer(80);

DHT dhtZone1(DHT_PINS[0], DHT22);
DHT dhtZone2(DHT_PINS[1], DHT22);
DHT *dhtSensors[NUM_ZONES] = {&dhtZone1, &dhtZone2};

ZoneThresholds thresholds[NUM_ZONES] = {
    {55.0f, 20.0f, 30.0f, 20.0f, 25.0f},
    {55.0f, 20.0f, 30.0f, 20.0f, 25.0f}};

enum Phase
{
  PHASE_IDLE,
  PHASE_ACTIVE
};

Phase phase = PHASE_IDLE;
uint32_t phaseStartedAt = 0;
bool cycleExecuted = false;
uint32_t cycleCount = 0;

ZoneReadings latestReadings[NUM_ZONES];
bool hasLatestReadings[NUM_ZONES] = {false, false};
bool latestTankLow = false;
uint32_t lastLCDUpdateAt = 0;

uint32_t lastWaterPulseMs = 0;
uint32_t lastNutrientPulseMs = 0;
uint32_t lastCycleElapsedMs = 0;
uint32_t lastCycleCompletedAtMs = 0;
uint32_t lastTelemetrySampleAtMs = 0;
bool waterValveOpen = false;
bool nutrientValveOpen = false;

String lastAlertMessage = "none";
bool serverStarted = false;
uint32_t lastWiFiReconnectAttemptAt = 0;
uint32_t lastWiFiBeginAt = 0;
bool hasPreferredWiFiBssid = false;
uint8_t preferredWiFiBssid[6] = {0, 0, 0, 0, 0, 0};
int32_t preferredWiFiChannel = 0;
bool mdnsStarted = false;
uint32_t lastEmailSentAt = 0;
bool lastCycleCriticalNutrientLockout = false;
bool lastWiFiConnected = false;
bool wifiStateInitialized = false;
bool prevMoistureLowState[NUM_ZONES] = {false, false};
bool prevNutrientLowState[NUM_ZONES] = {false, false};
bool prevNutrientHighState[NUM_ZONES] = {false, false};
bool prevNutrientCriticalState[NUM_ZONES] = {false, false};

#if 1
const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Automated Solar-Powered IoT-Based Monitoring and Nutrient Regulation System for Vertical Urban Farming Using Arduino-Based Sensors</title>
  <style>
    :root {
      --bg: #f3f1e8;
      --mist: #edf5eb;
      --panel: rgba(255, 255, 255, 0.82);
      --panel-strong: rgba(255, 255, 255, 0.94);
      --line: rgba(29, 62, 45, 0.12);
      --ink: #13271d;
      --muted: #5f7167;
      --deep: #17382c;
      --accent: #3e8b62;
      --water: #3c7f9c;
      --nutrient: #758b34;
      --warm: #c3773f;
      --ok: #2f7d4f;
      --warn: #ba7028;
      --danger: #ba4242;
      --shadow: 0 24px 60px rgba(18, 42, 32, 0.12);
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      color: var(--ink);
      font-family: "Avenir Next", "Segoe UI", "Trebuchet MS", sans-serif;
      min-height: 100vh;
      background:
        radial-gradient(circle at 10% 8%, rgba(134, 184, 122, 0.22) 0%, transparent 32%),
        radial-gradient(circle at 88% 6%, rgba(70, 130, 110, 0.18) 0%, transparent 28%),
        linear-gradient(180deg, #f7f3eb 0%, #eef5ee 52%, #edf3ea 100%);
    }

    .page {
      width: 100%;
      max-width: none;
      min-height: 100vh;
      margin: 0;
      padding: clamp(10px, 1.6vw, 22px);
    }

    .hero {
      position: relative;
      overflow: hidden;
      display: grid;
      grid-template-columns: minmax(0, 1.55fr) minmax(320px, 0.9fr);
      gap: 18px;
      border-radius: 30px;
      border: 0;
      background:
        radial-gradient(circle at top right, rgba(160, 208, 134, 0.22), transparent 32%),
        linear-gradient(145deg, rgba(19, 56, 43, 0.97), rgba(33, 76, 56, 0.96));
      color: #f3fbf1;
      padding: 24px;
      box-shadow: var(--shadow);
      animation: rise 0.45s ease;
    }

    .hero::before {
      content: "";
      position: absolute;
      inset: auto -10% -35% 30%;
      height: 280px;
      background: radial-gradient(circle, rgba(255, 255, 255, 0.12) 0%, transparent 62%);
      pointer-events: none;
    }

    .hero-copy,
    .hero-spotlight {
      position: relative;
      z-index: 1;
    }

    .eyebrow {
      margin: 0 0 8px;
      font-size: 12px;
      font-weight: 800;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: rgba(235, 248, 233, 0.72);
    }

    .hero-title {
      margin: 2px 0 0;
      line-height: 1.24;
      font-weight: 700;
      color: #f2fbef;
      max-width: 36ch;
      text-wrap: balance;
    }

    .hero h1 {
      margin: 0;
      line-height: 1.22;
      font-size: clamp(22px, 2.2vw, 36px);
      letter-spacing: -0.01em;
      font-family: Georgia, "Palatino Linotype", serif;
      max-width: 36ch;
      text-wrap: balance;
    }

    .hero .subtitle {
      margin: 12px 0 0;
      max-width: 58ch;
      color: rgba(225, 242, 220, 0.9);
      font-size: 15px;
      line-height: 1.55;
    }

    .meta {
      margin-top: 16px;
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      align-items: center;
    }

    .chip {
      border-radius: 999px;
      border: 1px solid rgba(255, 255, 255, 0.14);
      background: rgba(255, 255, 255, 0.08);
      color: #f4fff1;
      font-size: 12px;
      font-weight: 700;
      padding: 7px 12px;
      backdrop-filter: blur(12px);
    }

    .chip.offline {
      color: #ffd5d5;
      background: rgba(186, 66, 66, 0.14);
      border-color: rgba(255, 182, 182, 0.22);
    }

    .chip.fresh {
      color: #e6f9d8;
      background: rgba(120, 174, 86, 0.14);
    }

    .updated {
      margin-top: 18px;
      font-size: 12px;
      color: rgba(218, 236, 213, 0.74);
      font-weight: 700;
    }

    .phase-wrap {
      margin-top: 18px;
      display: grid;
      gap: 8px;
    }

    .phase-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      color: rgba(230, 244, 225, 0.9);
      font-size: 13px;
      font-weight: 700;
    }

    .phase-track {
      height: 10px;
      border-radius: 999px;
      overflow: hidden;
      background: rgba(255, 255, 255, 0.14);
      border: 1px solid rgba(255, 255, 255, 0.08);
    }

    .phase-fill {
      width: 0%;
      height: 100%;
      border-radius: inherit;
      background: linear-gradient(90deg, #9fdc80 0%, #6bb46f 100%);
      transition: width 0.4s ease;
    }

    .alert {
      margin-top: 16px;
      border-radius: 16px;
      border: 1px solid rgba(255, 208, 208, 0.18);
      background: rgba(186, 66, 66, 0.16);
      color: #ffe5e5;
      font-size: 14px;
      line-height: 1.45;
      font-weight: 700;
      padding: 12px 14px;
      display: none;
    }

    .alert.show { display: block; }

    .hero-spotlight {
      display: grid;
      gap: 16px;
      align-content: center;
      justify-items: center;
      padding: 12px 8px 4px;
    }

    .spotlight-ring {
      --value: 0;
      --accent: #8fd677;
      width: min(100%, 230px);
      aspect-ratio: 1;
      border-radius: 50%;
      display: grid;
      place-items: center;
      background:
        radial-gradient(circle at center, rgba(16, 34, 27, 0.94) 0 56%, transparent 57%),
        conic-gradient(var(--accent) calc(var(--value) * 1%), rgba(255, 255, 255, 0.14) 0);
      border: 1px solid rgba(255, 255, 255, 0.12);
      box-shadow: inset 0 0 0 12px rgba(255, 255, 255, 0.04);
    }

    .spotlight-core {
      display: grid;
      place-items: center;
      text-align: center;
      gap: 6px;
      width: 76%;
      max-width: 196px;
      min-height: 60%;
      padding: 10px 9px;
    }

    .spotlight-core span {
      font-size: clamp(10px, 1.1vw, 12px);
      font-weight: 900;
      letter-spacing: 0.12em;
      line-height: 1.22;
      text-transform: uppercase;
      color: rgba(214, 238, 208, 0.72);
      max-width: 13ch;
    }

    .spotlight-core strong {
      font-size: clamp(32px, 4.2vw, 46px);
      line-height: 1.03;
      letter-spacing: -0.04em;
      font-weight: 800;
      white-space: nowrap;
      font-variant-numeric: tabular-nums;
    }

    .spotlight-core small {
      color: rgba(217, 235, 211, 0.82);
      font-size: clamp(13px, 1.25vw, 15px);
      line-height: 1.3;
      max-width: 14ch;
    }

    .spotlight-stats {
      width: 100%;
      display: grid;
      gap: 10px;
    }

    .spot-stat {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
      padding: 10px 14px;
      border-radius: 16px;
      border: 1px solid rgba(255, 255, 255, 0.1);
      background: rgba(255, 255, 255, 0.08);
      color: rgba(238, 247, 236, 0.92);
      font-size: 13px;
    }

    .spot-stat strong {
      color: #ffffff;
      font-size: 16px;
      letter-spacing: -0.02em;
    }

    .section {
      margin-top: 18px;
    }

    .primary-grid {
      display: grid;
      gap: 16px;
      grid-template-columns: minmax(300px, 0.88fr) minmax(0, 1.55fr);
    }

    .zone-grid {
      display: grid;
      gap: 16px;
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .card {
      background: var(--panel);
      backdrop-filter: blur(12px);
      border: 0;
      border-radius: 26px;
      box-shadow: 0 14px 32px rgba(20, 45, 35, 0.08);
      padding: 18px;
      overflow: hidden;
      animation: rise 0.5s ease;
    }

    .card-head {
      display: flex;
      align-items: flex-start;
      justify-content: space-between;
      gap: 14px;
      margin-bottom: 16px;
    }

    .card-head h2 {
      margin: 2px 0 0;
      font-size: 25px;
      line-height: 1.05;
      letter-spacing: -0.04em;
      font-family: Georgia, "Palatino Linotype", serif;
      color: var(--deep);
    }

    .card-note {
      margin: -6px 0 0;
      color: var(--muted);
      font-size: 14px;
      line-height: 1.5;
    }

    .status-pill {
      flex-shrink: 0;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      min-height: 34px;
      border-radius: 999px;
      padding: 7px 12px;
      border: 1px solid transparent;
      font-size: 12px;
      font-weight: 800;
      letter-spacing: 0.06em;
      text-transform: uppercase;
    }

    .status-pill.good {
      color: var(--ok);
      background: rgba(47, 125, 79, 0.1);
      border-color: rgba(47, 125, 79, 0.16);
    }

    .status-pill.warn {
      color: var(--warn);
      background: rgba(186, 112, 40, 0.12);
      border-color: rgba(186, 112, 40, 0.18);
    }

    .status-pill.alert {
      color: var(--danger);
      background: rgba(186, 66, 66, 0.1);
      border-color: rgba(186, 66, 66, 0.16);
    }

    .status-pill.neutral {
      color: #5c7065;
      background: rgba(79, 102, 89, 0.08);
      border-color: rgba(79, 102, 89, 0.14);
    }

    .tank-shell-wrap {
      display: grid;
      grid-template-columns: 140px minmax(0, 1fr);
      gap: 18px;
      align-items: center;
    }

    .tank-shell {
      position: relative;
      width: 128px;
      height: 210px;
      margin: 0 auto;
      border-radius: 30px 30px 24px 24px;
      border: 2px solid rgba(27, 60, 44, 0.22);
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.82) 0%, rgba(240, 245, 240, 0.94) 100%);
      overflow: hidden;
      box-shadow: inset 0 0 0 12px rgba(255, 255, 255, 0.52);
    }

    .tank-cap {
      position: absolute;
      top: -12px;
      left: 50%;
      width: 58px;
      height: 18px;
      margin-left: -29px;
      border-radius: 12px 12px 6px 6px;
      background: rgba(23, 56, 44, 0.16);
      border: 2px solid rgba(23, 56, 44, 0.16);
    }

    .tank-fill {
      position: absolute;
      inset: auto 10px 10px 10px;
      height: 0%;
      border-radius: 20px 20px 16px 16px;
      background: linear-gradient(180deg, #86bfd8 0%, #3d7fa0 100%);
      transition: height 0.45s ease, background 0.45s ease;
      box-shadow: inset 0 8px 18px rgba(255, 255, 255, 0.22);
    }

    .tank-lines {
      position: absolute;
      inset: 26px 14px 14px;
      background:
        linear-gradient(180deg, transparent 19%, rgba(21, 39, 29, 0.1) 20%, transparent 21%, transparent 39%, rgba(21, 39, 29, 0.1) 40%, transparent 41%, transparent 59%, rgba(21, 39, 29, 0.1) 60%, transparent 61%, transparent 79%, rgba(21, 39, 29, 0.1) 80%, transparent 81%);
      pointer-events: none;
    }

    .tank-meta strong {
      display: block;
      font-size: 26px;
      line-height: 1.05;
      letter-spacing: -0.04em;
      color: var(--deep);
    }

    .tank-meta p {
      margin: 8px 0 0;
      color: var(--muted);
      line-height: 1.5;
      font-size: 14px;
    }

    .meter-panel {
      margin-top: 16px;
      display: grid;
      gap: 12px;
    }

    .meter-label {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      font-size: 13px;
      font-weight: 700;
      color: var(--muted);
    }

    .meter-track {
      width: 100%;
      height: 9px;
      overflow: hidden;
      border-radius: 999px;
      background: rgba(22, 48, 35, 0.08);
      border: 1px solid rgba(22, 48, 35, 0.06);
    }

    .meter-fill {
      width: 0%;
      height: 100%;
      border-radius: inherit;
      background: linear-gradient(90deg, rgba(62, 139, 98, 0.95), rgba(96, 168, 120, 0.95));
      transition: width 0.35s ease;
    }

    .zone-card {
      display: grid;
      gap: 18px;
      align-content: start;
    }

    .zone-rings {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      align-items: start;
    }

    .ring-stack {
      display: grid;
      justify-items: center;
      align-content: start;
      gap: 10px;
      text-align: center;
    }

    .progress-ring {
      --value: 0;
      --accent: var(--accent);
      width: min(100%, 172px);
      aspect-ratio: 1;
      border-radius: 50%;
      display: grid;
      place-items: center;
      background:
        radial-gradient(circle at center, rgba(255, 255, 255, 0.96) 0 56%, transparent 57%),
        conic-gradient(var(--accent) calc(var(--value) * 1%), rgba(21, 39, 29, 0.08) 0);
      box-shadow: inset 0 0 0 10px rgba(255, 255, 255, 0.5);
      position: relative;
    }

    .progress-ring.mini {
      width: min(100%, 160px);
    }

    .progress-ring::after {
      content: "";
      position: absolute;
      inset: 8px;
      border-radius: 50%;
      border: 1px solid rgba(22, 48, 35, 0.06);
    }

    .ring-core {
      position: relative;
      z-index: 1;
      display: grid;
      place-items: center;
      gap: 4px;
      text-align: center;
      width: 76%;
      max-width: 124px;
      min-height: 60%;
      padding: 9px 7px;
    }

    .ring-core span {
      font-size: clamp(10px, 0.95vw, 12px);
      text-transform: uppercase;
      letter-spacing: 0.11em;
      line-height: 1.2;
      font-weight: 900;
      color: var(--muted);
      max-width: 11ch;
    }

    .ring-core strong {
      font-size: clamp(23px, 2.1vw, 30px);
      line-height: 1.02;
      letter-spacing: -0.03em;
      font-weight: 800;
      color: var(--deep);
      white-space: nowrap;
      font-variant-numeric: tabular-nums;
    }

    .ring-core small {
      font-size: clamp(11px, 0.95vw, 13px);
      color: var(--muted);
      line-height: 1.25;
      max-width: 11ch;
    }

    .progress-ring.mini .ring-core {
      max-width: 114px;
    }

    .progress-ring.mini .ring-core strong {
      font-size: clamp(21px, 1.9vw, 27px);
    }

    .ring-caption {
      margin: 0;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.4;
      min-height: 38px;
      display: flex;
      align-items: flex-start;
      justify-content: center;
    }

    .metric-stack,
    .npk-stack {
      display: grid;
      gap: 12px;
    }

    .metric-row,
    .npk-row {
      display: grid;
      gap: 8px;
    }

    .metric-header,
    .npk-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }

    .metric-header span,
    .npk-header span {
      color: var(--muted);
      font-size: 13px;
      font-weight: 700;
    }

    .metric-header strong,
    .npk-header strong {
      color: var(--deep);
      font-size: 15px;
    }

    .metric-subtext {
      color: var(--muted);
      font-size: 12px;
    }

    .npk-scale {
      width: 100%;
      height: 8px;
      overflow: hidden;
      border-radius: 999px;
      background: rgba(23, 56, 44, 0.07);
      border: 1px solid rgba(23, 56, 44, 0.05);
    }

    .npk-fill {
      width: 0%;
      height: 100%;
      border-radius: inherit;
      background: linear-gradient(90deg, #8eb554 0%, #6a8735 100%);
      transition: width 0.35s ease;
    }

    .chart-card {
      display: grid;
      gap: 16px;
    }

    .chart-grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .chart-box {
      border: 0;
      border-radius: 22px;
      background: var(--panel-strong);
      padding: 14px;
    }

    .chart-box.wide {
      grid-column: 1 / -1;
    }

    .chart-title {
      margin: 0 0 6px;
      color: var(--deep);
      font-size: 14px;
      font-weight: 800;
      letter-spacing: 0.1em;
      text-transform: uppercase;
    }

    .chart-caption {
      margin: 0 0 12px;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.45;
    }

    .chart-box canvas {
      width: 100%;
      height: auto;
      display: block;
      background: linear-gradient(180deg, rgba(253, 254, 252, 0.95) 0%, rgba(244, 248, 244, 0.96) 100%);
      border-radius: 18px;
      border: 0;
    }

    .support-grid {
      display: grid;
      gap: 16px;
      grid-template-columns: 1fr;
    }

    .guide-grid {
      margin-top: 14px;
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .criteria-box {
      margin-top: 14px;
      padding: 14px 16px;
      border-radius: 18px;
      background: rgba(255, 255, 255, 0.74);
      display: grid;
      gap: 10px;
    }

    .criteria-title {
      margin: 0;
      color: var(--deep);
      font-size: 14px;
      font-weight: 900;
      letter-spacing: 0.08em;
      text-transform: uppercase;
    }

    .criteria-list {
      display: grid;
      gap: 8px;
    }

    .criteria-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 8px 10px;
      border-radius: 12px;
      background: rgba(244, 248, 244, 0.9);
      color: var(--muted);
      font-size: 13px;
    }

    .criteria-row strong {
      color: var(--deep);
      text-align: right;
      font-size: 13px;
    }

    .criteria-now {
      margin: 2px 0 0;
      color: var(--deep);
      font-size: 13px;
      font-weight: 800;
      line-height: 1.35;
    }

    .detail-sections {
      display: grid;
      gap: 16px;
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .detail-block {
      display: grid;
      gap: 12px;
      align-content: start;
    }

    .detail-block h3 {
      margin: 0;
      font-size: 13px;
      font-weight: 800;
      letter-spacing: 0.12em;
      text-transform: uppercase;
      color: var(--muted);
    }

    .detail-list,
    .zone-detail-list {
      display: grid;
      gap: 10px;
    }

    .detail-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 14px;
      padding: 11px 13px;
      border-radius: 16px;
      border: 0;
      background: rgba(255, 255, 255, 0.74);
      color: var(--muted);
      font-size: 13px;
    }

    .detail-row strong {
      color: var(--deep);
      text-align: right;
      font-size: 14px;
    }

    .callout {
      margin-bottom: 16px;
      padding: 14px 16px;
      border-radius: 18px;
      border: 0;
      background: rgba(255, 255, 255, 0.74);
    }

    .callout span {
      display: block;
      margin-bottom: 6px;
      color: var(--muted);
      font-size: 12px;
      font-weight: 800;
      letter-spacing: 0.12em;
      text-transform: uppercase;
    }

    .callout strong {
      color: var(--deep);
      font-size: 15px;
      line-height: 1.45;
    }

    .zone-detail-card {
      display: grid;
      gap: 10px;
      padding: 14px;
      border-radius: 18px;
      border: 0;
      background: rgba(255, 255, 255, 0.76);
    }

    .zone-detail-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }

    .zone-detail-head strong {
      color: var(--deep);
      font-size: 18px;
    }

    .zone-detail-grid {
      display: grid;
      gap: 9px;
    }

    .zone-detail-grid .detail-row {
      padding: 10px 12px;
      background: rgba(244, 248, 244, 0.9);
    }

    .empty-state {
      display: grid;
      place-items: center;
      min-height: 320px;
      border-radius: 22px;
      border: 0;
      background: rgba(255, 255, 255, 0.46);
      text-align: center;
      color: var(--muted);
      padding: 18px;
      line-height: 1.5;
    }

    @keyframes rise {
      from {
        opacity: 0;
        transform: translateY(14px);
      }
      to {
        opacity: 1;
        transform: translateY(0);
      }
    }

    @media (max-width: 1120px) {
      .hero,
      .primary-grid,
      .support-grid,
      .detail-sections,
      .guide-grid {
        grid-template-columns: 1fr;
      }

      .zone-grid,
      .chart-grid {
        grid-template-columns: 1fr;
      }
    }

    @media (max-width: 720px) {
      .page { padding: 12px; }

      .hero {
        padding: 18px;
        border-radius: 24px;
      }

      .hero h1 {
        font-size: 24px;
      }

      .hero-title {
        line-height: 1.3;
        max-width: none;
      }

      .spotlight-ring {
        width: min(100%, 210px);
      }

      .card {
        padding: 16px;
        border-radius: 22px;
      }

      .tank-shell-wrap {
        grid-template-columns: 1fr;
      }

      .zone-rings {
        grid-template-columns: 1fr;
      }

      .spotlight-core {
        width: 70%;
      }

      .ring-core {
        width: 66%;
      }

      .criteria-row {
        font-size: 12px;
      }

      .criteria-row strong {
        font-size: 12px;
      }
    }
  </style>
</head>
<body>
  <main class="page">
    <section class="hero">
      <div class="hero-copy">
        <p class="eyebrow">Plant Care Dashboard</p>
        <h1 class="hero-title">Automated Solar-Powered IoT-Based Monitoring and Nutrient Regulation System for Vertical Urban Farming Using Arduino-Based Sensors</h1>
        <div class="meta">
          <span id="phaseChip" class="chip">Step: --</span>
          <span id="cycleChip" class="chip">Round: --</span>
          <span id="telemetryChip" class="chip">Last update: --</span>
          <span id="wifiChip" class="chip">Connection: --</span>
        </div>
        <div class="phase-wrap">
          <div class="phase-row">
            <span>Step progress</span>
            <strong id="phaseProgressValue">--</strong>
          </div>
          <div class="phase-track">
            <div id="phaseProgressBar" class="phase-fill"></div>
          </div>
        </div>
        <div id="tankAlert" class="alert"></div>
        <div id="updated" class="updated">Last update: --</div>
      </div>

      <div class="hero-spotlight">
        <div id="spotlightRing" class="spotlight-ring">
          <div class="spotlight-core">
            <span>Average soil wetness</span>
            <strong id="spotlightValue">--</strong>
            <small id="spotlightMeta">Waiting for live readings</small>
          </div>
        </div>
        <div class="spotlight-stats">
          <div class="spot-stat">
            <span>Average plant food</span>
            <strong id="spotNutrient">--</strong>
          </div>
          <div class="spot-stat">
            <span>Water tank</span>
            <strong id="spotTank">--</strong>
          </div>
          <div class="spot-stat">
            <span>Time left</span>
            <strong id="spotRemaining">--</strong>
          </div>
        </div>
      </div>
    </section>

    <section class="section primary-grid">
      <article class="card">
        <div class="card-head">
          <div>
            <p class="eyebrow" style="color: var(--muted);">Important Safety</p>
            <h2>Water Tank Level</h2>
          </div>
          <span id="tankBadge" class="status-pill neutral">--</span>
        </div>
        <div class="tank-shell-wrap">
          <div class="tank-shell">
            <div class="tank-cap"></div>
            <div id="tankFill" class="tank-fill"></div>
            <div class="tank-lines"></div>
          </div>
          <div class="tank-meta">
            <strong id="tankValue">--</strong>
            <p id="tankText">Waiting for tank reading.</p>
            <div class="meter-panel">
              <div class="meter-label">
                <span>How recent</span>
                <strong id="sampleAgeText">--</strong>
              </div>
              <div class="meter-track">
                <div id="sampleAgeBar" class="meter-fill"></div>
              </div>
            </div>
          </div>
        </div>
        <div class="guide-grid">
          <div class="criteria-box">
            <p class="criteria-title">Plant Food Guide (ppm)</p>
            <div class="criteria-list">
              <div class="criteria-row"><span>Below 600</span><strong>Very low (unsafe)</strong></div>
              <div class="criteria-row"><span>600 to 799</span><strong>Low</strong></div>
              <div class="criteria-row"><span>800 to 1200</span><strong>Good range</strong></div>
              <div class="criteria-row"><span>1201 to 1400</span><strong>A little high</strong></div>
              <div class="criteria-row"><span>Above 1400</span><strong>Too high (unsafe)</strong></div>
            </div>
            <p id="foodGuideNow" class="criteria-now">Current level: Waiting for live reading.</p>
          </div>
          <div class="criteria-box">
            <p class="criteria-title">Soil Wetness Guide (%)</p>
            <div class="criteria-list">
              <div class="criteria-row"><span>Below 20%</span><strong>Very dry</strong></div>
              <div class="criteria-row"><span>20% to 29%</span><strong>A little dry</strong></div>
              <div class="criteria-row"><span>30% to 45%</span><strong>Good range</strong></div>
              <div class="criteria-row"><span>Above 45%</span><strong>Too wet</strong></div>
            </div>
            <p id="wetGuideNow" class="criteria-now">Current level: Waiting for live reading.</p>
          </div>
        </div>
      </article>

      <div id="zoneSpotlights" class="zone-grid"></div>
    </section>

    <section class="section card chart-card">
      <div class="card-head">
        <div>
          <p class="eyebrow" style="color: var(--muted);">Simple Charts</p>
          <h2>Readings Over Time</h2>
        </div>
      </div>
      <p class="card-note">Simple chart views help you spot changes quickly.</p>
      <div class="chart-grid">
        <div class="chart-box">
          <p class="chart-title">Soil Wetness Over Time</p>
          <p class="chart-caption">Both plant areas are shown together for easy comparison.</p>
          <canvas id="chartMoisture" height="190"></canvas>
        </div>
        <div class="chart-box">
          <p class="chart-title">Air Temperature Over Time</p>
          <p class="chart-caption">Shows how warm or cool the air has been recently.</p>
          <canvas id="chartTemp" height="190"></canvas>
        </div>
        <div class="chart-box wide">
          <p class="chart-title">Plant Food Levels (Now)</p>
          <p class="chart-caption">N, P, and K are shown here as extra details.</p>
          <canvas id="chartNpk" height="220"></canvas>
        </div>
      </div>
    </section>

    <section class="section support-grid">
      <article class="card">
        <div class="card-head">
          <div>
            <p class="eyebrow" style="color: var(--muted);">More Information</p>
            <h2>Extra Details</h2>
          </div>
        </div>
        <div class="callout">
          <span>Latest warning</span>
          <strong id="lastAlert">No warnings yet.</strong>
        </div>
        <div class="detail-sections">
          <section class="detail-block">
            <h3>System Summary</h3>
            <div id="systemDetails" class="detail-list"></div>
          </section>
          <section class="detail-block">
            <h3>Area Details</h3>
            <div id="zoneDetails" class="zone-detail-list"></div>
          </section>
        </div>
      </article>
    </section>
  </main>

  <script>
    const REFRESH_MS = 3000;
    const HISTORY_LIMIT = 24;
    const SAMPLE_AGE_MAX_MS = 15000;

    const phaseChipEl = document.getElementById('phaseChip');
    const cycleChipEl = document.getElementById('cycleChip');
    const telemetryChipEl = document.getElementById('telemetryChip');
    const wifiChipEl = document.getElementById('wifiChip');
    const updatedEl = document.getElementById('updated');
    const tankAlertEl = document.getElementById('tankAlert');
    const phaseProgressValueEl = document.getElementById('phaseProgressValue');
    const phaseProgressBarEl = document.getElementById('phaseProgressBar');
    const spotlightRingEl = document.getElementById('spotlightRing');
    const spotlightValueEl = document.getElementById('spotlightValue');
    const spotlightMetaEl = document.getElementById('spotlightMeta');
    const spotNutrientEl = document.getElementById('spotNutrient');
    const spotTankEl = document.getElementById('spotTank');
    const spotRemainingEl = document.getElementById('spotRemaining');
    const tankBadgeEl = document.getElementById('tankBadge');
    const tankFillEl = document.getElementById('tankFill');
    const tankValueEl = document.getElementById('tankValue');
    const tankTextEl = document.getElementById('tankText');
    const sampleAgeTextEl = document.getElementById('sampleAgeText');
    const sampleAgeBarEl = document.getElementById('sampleAgeBar');
    const zoneSpotlightsEl = document.getElementById('zoneSpotlights');
    const systemDetailsEl = document.getElementById('systemDetails');
    const zoneDetailsEl = document.getElementById('zoneDetails');
    const lastAlertEl = document.getElementById('lastAlert');
    const foodGuideNowEl = document.getElementById('foodGuideNow');
    const wetGuideNowEl = document.getElementById('wetGuideNow');

    const chartMoistureEl = document.getElementById('chartMoisture');
    const chartTempEl = document.getElementById('chartTemp');
    const chartNpkEl = document.getElementById('chartNpk');

    const history = {
      1: { moisture: [], temp: [] },
      2: { moisture: [], temp: [] }
    };

    let lastStatus = null;

    function generatePreviewSeries(minY, maxY, phaseShift, bias) {
      const out = [];
      const center = (minY + maxY) * 0.5 + (bias || 0);
      const amp = (maxY - minY) * 0.14;
      for (let i = 0; i < 12; i++) {
        const angle = (i + (phaseShift || 0)) * 0.62;
        const val = center + amp * Math.sin(angle) + amp * 0.28 * Math.cos(angle * 0.6);
        out.push(clamp(val, minY, maxY));
      }
      return out;
    }

    function toNum(value, fallback) {
      const n = Number(value);
      return Number.isFinite(n) ? n : fallback;
    }

    function clamp(value, min, max) {
      return Math.max(min, Math.min(max, value));
    }

    function escapeHtml(value) {
      return String(value == null ? '' : value)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }

    function titleize(text) {
      return String(text || '--')
        .replace(/_/g, ' ')
        .replace(/\b[a-z]/g, function(match) {
          return match.toUpperCase();
        });
    }

    function friendlyBand(band) {
      switch (String(band || '')) {
        case 'optimal':
          return 'Good';
        case 'critical_dry':
          return 'Very dry';
        case 'slightly_dry':
          return 'A little dry';
        case 'wet':
          return 'Too wet';
        case 'cold':
          return 'Too cold';
        case 'hot':
          return 'Too hot';
        case 'dry_air':
          return 'Air too dry';
        case 'humid_high':
          return 'Air too humid';
        case 'low':
          return 'Low';
        case 'slightly_high':
          return 'A little high';
        case 'critical_imbalance':
          return 'Unsafe';
        default:
          return '--';
      }
    }

    function signalStrengthLabel(rssiValue) {
      const rssi = toNum(rssiValue, -200);
      if (rssi >= -60) {
        return 'Strong';
      }
      if (rssi >= -70) {
        return 'Good';
      }
      if (rssi >= -80) {
        return 'Fair';
      }
      return 'Weak';
    }

    function foodLevelText(ppm) {
      const n = toNum(ppm, NaN);
      if (!Number.isFinite(n)) {
        return '--';
      }
      if (n < 600) {
        return 'Very low (unsafe)';
      }
      if (n < 800) {
        return 'Low';
      }
      if (n <= 1200) {
        return 'Good range';
      }
      if (n <= 1400) {
        return 'A little high';
      }
      return 'Too high (unsafe)';
    }

    function soilWetnessLevelText(pct) {
      const n = toNum(pct, NaN);
      if (!Number.isFinite(n)) {
        return '--';
      }
      if (n < 20) {
        return 'Very dry';
      }
      if (n < 30) {
        return 'A little dry';
      }
      if (n <= 45) {
        return 'Good range';
      }
      return 'Too wet';
    }

    function fmtMs(ms) {
      const totalMs = Math.max(0, Math.round(toNum(ms, 0)));
      if (totalMs < 1000) {
        return totalMs + ' ms';
      }

      const sec = Math.floor(totalMs / 1000);
      const min = Math.floor(sec / 60);
      const remSec = sec % 60;
      const hours = Math.floor(min / 60);
      const remMin = min % 60;

      if (hours > 0) {
        return hours + 'h ' + remMin + 'm';
      }
      if (min > 0) {
        return min + 'm ' + remSec + 's';
      }
      return sec + 's';
    }

    function fmtClock(ms) {
      const totalSec = Math.max(0, Math.floor(toNum(ms, 0) / 1000));
      const min = Math.floor(totalSec / 60);
      const sec = totalSec % 60;
      return String(min).padStart(2, '0') + ':' + String(sec).padStart(2, '0');
    }

    function fmtValue(value, digits, suffix) {
      const n = toNum(value, NaN);
      if (Number.isNaN(n)) {
        return '--';
      }
      return n.toFixed(digits) + (suffix || '');
    }

    function average(values) {
      let sum = 0;
      let count = 0;
      for (let i = 0; i < values.length; i++) {
        const n = toNum(values[i], NaN);
        if (Number.isFinite(n)) {
          sum += n;
          count++;
        }
      }
      return count ? (sum / count) : NaN;
    }

    function getZone(zones, id) {
      if (!Array.isArray(zones)) {
        return null;
      }
      for (let i = 0; i < zones.length; i++) {
        if (Number(zones[i].zone) === id) {
          return zones[i];
        }
      }
      return null;
    }

    function pushHistory(zoneId, key, value) {
      if (!Number.isFinite(value) || !history[zoneId] || !history[zoneId][key]) {
        return;
      }

      const arr = history[zoneId][key];
      arr.push(value);
      if (arr.length > HISTORY_LIMIT) {
        arr.shift();
      }
    }

    function toneFromBand(band) {
      switch (String(band || '')) {
        case 'optimal':
          return 'good';
        case 'critical_dry':
        case 'critical_imbalance':
          return 'alert';
        case 'slightly_dry':
        case 'wet':
        case 'cold':
        case 'hot':
        case 'dry_air':
        case 'humid_high':
        case 'low':
        case 'slightly_high':
          return 'warn';
        default:
          return 'neutral';
      }
    }

    function accentFor(metric, tone) {
      if (tone === 'alert') {
        return '#ba4242';
      }
      if (tone === 'warn') {
        if (metric === 'temp') {
          return '#c3773f';
        }
        return '#ba7028';
      }

      if (metric === 'humidity') {
        return '#3c7f9c';
      }
      if (metric === 'nutrient') {
        return '#758b34';
      }
      if (metric === 'temp') {
        return '#c3773f';
      }
      return '#3e8b62';
    }

    function nutrientScore(ppm) {
      const n = toNum(ppm, NaN);
      if (!Number.isFinite(n)) {
        return 0;
      }
      if (n <= 600 || n >= 1400) {
        return 12;
      }
      const ideal = 1000;
      return clamp(100 - (Math.abs(n - ideal) / 400) * 100, 0, 100);
    }

    function setRingValue(el, value, accent) {
      if (!el) {
        return;
      }
      el.style.setProperty('--value', String(clamp(toNum(value, 0), 0, 100)));
      if (accent) {
        el.style.setProperty('--accent', accent);
      }
    }

    function buildMetricBar(label, value, pct, color, state) {
      return `
        <div class="metric-row">
          <div class="metric-header">
            <span>${escapeHtml(label)}</span>
            <strong>${escapeHtml(value)}</strong>
          </div>
          <div class="meter-track">
            <div class="meter-fill" style="width:${clamp(pct, 0, 100)}%; background:linear-gradient(90deg, ${color}, ${color}cc);"></div>
          </div>
          <div class="metric-subtext">${escapeHtml(state)}</div>
        </div>`;
    }

    function buildNpkBar(label, value) {
      const numeric = toNum(value, NaN);
      const pct = Number.isFinite(numeric) ? clamp((numeric / 40) * 100, 0, 100) : 0;
      return `
        <div class="npk-row">
          <div class="npk-header">
            <span>${escapeHtml(label)}</span>
            <strong>${escapeHtml(fmtValue(numeric, 1, ''))}</strong>
          </div>
          <div class="npk-scale">
            <div class="npk-fill" style="width:${pct}%;"></div>
          </div>
        </div>`;
    }

    function buildDetailRow(label, value) {
      return `<div class="detail-row"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong></div>`;
    }

    function buildZoneCard(zone, zoneId) {
      if (!zone || !zone.hasData) {
        return `
          <article class="card zone-card">
            <div class="card-head">
              <div>
                <p class="eyebrow" style="color: var(--muted);">Plant Area ${zoneId}</p>
                <h2>Main Readings</h2>
              </div>
              <span class="status-pill neutral">Waiting</span>
            </div>
            <div class="empty-state">Area ${zoneId} has no live reading yet.</div>
          </article>`;
      }

      const moisture = clamp(toNum(zone.soilMoisturePct, 0), 0, 100);
      const nutrient = toNum(zone.nutrientPpm, NaN);
      const temp = toNum(zone.tempC, NaN);
      const humidity = clamp(toNum(zone.humidityPct, 0), 0, 100);
      const moistureTone = toneFromBand(zone.moistureBand);
      const nutrientTone = toneFromBand(zone.nutrientBand);
      const tempTone = toneFromBand(zone.tempBand);
      const humidityTone = toneFromBand(zone.humidityBand);

      return `
        <article class="card zone-card">
          <div class="card-head">
            <div>
              <p class="eyebrow" style="color: var(--muted);">Plant Area ${zoneId}</p>
              <h2>Main Readings</h2>
            </div>
            <span class="status-pill ${moistureTone}">${escapeHtml(friendlyBand(zone.moistureBand))}</span>
          </div>
          <div class="zone-rings">
            <div class="ring-stack">
              <div class="progress-ring" style="--value:${moisture}; --accent:${accentFor('moisture', moistureTone)};">
                <div class="ring-core">
                  <span>Soil wetness</span>
                  <strong>${escapeHtml(fmtValue(moisture, 1, '%'))}</strong>
                  <small>${escapeHtml(friendlyBand(zone.moistureBand))}</small>
                </div>
              </div>
              <p class="ring-caption">Best range: 30% to 45%</p>
            </div>
            <div class="ring-stack">
              <div class="progress-ring mini" style="--value:${nutrientScore(nutrient)}; --accent:${accentFor('nutrient', nutrientTone)};">
                <div class="ring-core">
                  <span>Plant food ppm</span>
                  <strong>${escapeHtml(fmtValue(nutrient, 0, ''))}</strong>
                  <small>${escapeHtml(friendlyBand(zone.nutrientBand))}</small>
                </div>
              </div>
              <p class="ring-caption">Score from the current plant food level.</p>
            </div>
          </div>
          <div class="metric-stack">
            ${buildMetricBar('Air temperature', fmtValue(temp, 1, ' C'), clamp((toNum(temp, 0) / 50) * 100, 0, 100), accentFor('temp', tempTone), friendlyBand(zone.tempBand))}
            ${buildMetricBar('Air moisture', fmtValue(humidity, 1, '%'), humidity, accentFor('humidity', humidityTone), friendlyBand(zone.humidityBand))}
          </div>
          <div class="npk-stack">
            ${buildNpkBar('Plant food N', zone.npk && zone.npk.n)}
            ${buildNpkBar('Plant food P', zone.npk && zone.npk.p)}
            ${buildNpkBar('Plant food K', zone.npk && zone.npk.k)}
          </div>
        </article>`;
    }

    function buildZoneDetail(zone, zoneId) {
      if (!zone || !zone.hasData) {
        return `
          <div class="zone-detail-card">
            <div class="zone-detail-head">
              <strong>Area ${zoneId}</strong>
              <span class="status-pill neutral">No data</span>
            </div>
            <div class="detail-row"><span>Status</span><strong>Waiting for first reading</strong></div>
          </div>`;
      }

      return `
        <div class="zone-detail-card">
          <div class="zone-detail-head">
            <strong>Area ${zoneId}</strong>
            <span class="status-pill ${toneFromBand(zone.nutrientBand)}">${escapeHtml(friendlyBand(zone.nutrientBand))}</span>
          </div>
          <div class="zone-detail-grid">
            ${buildDetailRow('Soil wetness status', friendlyBand(zone.moistureBand))}
            ${buildDetailRow('Temperature status', friendlyBand(zone.tempBand))}
            ${buildDetailRow('Air moisture status', friendlyBand(zone.humidityBand))}
            ${buildDetailRow('Plant food level', fmtValue(zone.nutrientPpm, 0, ' ppm'))}
            ${buildDetailRow('N / P / K', `${fmtValue(zone.npk && zone.npk.n, 1, '')} / ${fmtValue(zone.npk && zone.npk.p, 1, '')} / ${fmtValue(zone.npk && zone.npk.k, 1, '')}`)}
          </div>
        </div>`;
    }

    function prepCanvas(canvas) {
      if (!canvas) {
        return null;
      }

      const rect = canvas.getBoundingClientRect();
      const cssWidth = Math.max(280, rect.width || 0);
      const cssHeight = Number(canvas.getAttribute('height')) || 190;
      const dpr = window.devicePixelRatio || 1;

      canvas.width = Math.floor(cssWidth * dpr);
      canvas.height = Math.floor(cssHeight * dpr);

      const ctx = canvas.getContext('2d');
      if (!ctx) {
        return null;
      }

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, cssWidth, cssHeight);

      return { ctx: ctx, w: cssWidth, h: cssHeight };
    }

    function syncChartHeights() {
      const viewportWidth = window.innerWidth || document.documentElement.clientWidth || 1200;
      let moistureH = 190;
      let tempH = 190;
      let npkH = 220;

      if (viewportWidth <= 720) {
        moistureH = 138;
        tempH = 138;
        npkH = 168;
      } else if (viewportWidth <= 1120) {
        moistureH = 158;
        tempH = 158;
        npkH = 190;
      }

      chartMoistureEl.setAttribute('height', String(moistureH));
      chartTempEl.setAttribute('height', String(tempH));
      chartNpkEl.setAttribute('height', String(npkH));
    }

    function drawLineChart(canvas, series, minY, maxY) {
      const prep = prepCanvas(canvas);
      if (!prep) {
        return;
      }

      const ctx = prep.ctx;
      const w = prep.w;
      const h = prep.h;

      let hasData = false;
      for (let i = 0; i < series.length; i++) {
        if (series[i].values.length > 0) {
          hasData = true;
          break;
        }
      }

      const drawSeries = hasData ? series : [
        { name: series[0].name, color: series[0].color, values: generatePreviewSeries(minY, maxY, 0, (maxY - minY) * 0.03) },
        { name: series[1].name, color: series[1].color, values: generatePreviewSeries(minY, maxY, 2.4, (maxY - minY) * -0.03) }
      ];

      const left = 42;
      const right = 16;
      const top = 24;
      const bottom = 28;
      const pw = w - left - right;
      const ph = h - top - bottom;

      const bg = ctx.createLinearGradient(0, 0, 0, h);
      bg.addColorStop(0, '#fcfdfb');
      bg.addColorStop(1, '#f3f7f3');
      ctx.fillStyle = bg;
      ctx.fillRect(0, 0, w, h);

      ctx.strokeStyle = 'rgba(23, 56, 44, 0.1)';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = top + (ph * i) / 4;
        ctx.beginPath();
        ctx.moveTo(left, y);
        ctx.lineTo(left + pw, y);
        ctx.stroke();

        const val = maxY - ((maxY - minY) * i) / 4;
        ctx.fillStyle = '#617468';
        ctx.font = '11px Avenir Next';
        ctx.textAlign = 'right';
        ctx.fillText(val.toFixed(0), left - 8, y + 4);
      }

      for (let s = 0; s < drawSeries.length; s++) {
        const item = drawSeries[s];
        const len = item.values.length;
        if (!len) {
          continue;
        }

        const area = [];
        for (let i = 0; i < len; i++) {
          const x = left + (pw * i) / Math.max(1, len - 1);
          const normalized = clamp((item.values[i] - minY) / Math.max(0.001, maxY - minY), 0, 1);
          const y = top + (1 - normalized) * ph;
          area.push({ x: x, y: y });
        }

        ctx.beginPath();
        ctx.moveTo(area[0].x, top + ph);
        for (let i = 0; i < area.length; i++) {
          ctx.lineTo(area[i].x, area[i].y);
        }
        ctx.lineTo(area[area.length - 1].x, top + ph);
        ctx.closePath();

        const fill = ctx.createLinearGradient(0, top, 0, top + ph);
        fill.addColorStop(0, item.color + '44');
        fill.addColorStop(1, item.color + '08');
        ctx.fillStyle = fill;
        ctx.fill();

        ctx.beginPath();
        for (let i = 0; i < area.length; i++) {
          if (i === 0) {
            ctx.moveTo(area[i].x, area[i].y);
          } else {
            ctx.lineTo(area[i].x, area[i].y);
          }
        }
        ctx.strokeStyle = item.color;
        ctx.lineWidth = 2.5;
        ctx.stroke();

        const lastPoint = area[area.length - 1];
        ctx.fillStyle = item.color;
        ctx.beginPath();
        ctx.arc(lastPoint.x, lastPoint.y, 4, 0, Math.PI * 2);
        ctx.fill();
      }

      ctx.textAlign = 'left';
      ctx.font = '12px Avenir Next';
      for (let i = 0; i < drawSeries.length; i++) {
        const x = left + i * 122;
        ctx.fillStyle = drawSeries[i].color;
        ctx.fillRect(x, 8, 16, 5);
        ctx.fillStyle = '#365546';
        ctx.fillText(drawSeries[i].name, x + 22, 13);
      }

      if (!hasData) {
        ctx.textAlign = 'right';
        ctx.fillStyle = '#6c8274';
        ctx.font = '10px Avenir Next';
        ctx.fillText('Sample view while waiting for live readings', left + pw, 13);
      }
    }

    function drawNpkChart(canvas, z1, z2) {
      const prep = prepCanvas(canvas);
      if (!prep) {
        return;
      }

      const ctx = prep.ctx;
      const w = prep.w;
      const h = prep.h;

      const hasData = z1 && z1.hasData && z1.npk && z2 && z2.hasData && z2.npk;
      const labels = ['N', 'P', 'K'];
      const z1Vals = hasData ? [toNum(z1.npk.n, 0), toNum(z1.npk.p, 0), toNum(z1.npk.k, 0)] : [31, 21, 27];
      const z2Vals = hasData ? [toNum(z2.npk.n, 0), toNum(z2.npk.p, 0), toNum(z2.npk.k, 0)] : [29, 19, 25];
      const maxVal = Math.max(30, Math.max(z1Vals[0], z1Vals[1], z1Vals[2], z2Vals[0], z2Vals[1], z2Vals[2]) * 1.25);

      const left = 42;
      const right = 18;
      const top = 26;
      const bottom = 34;
      const pw = w - left - right;
      const ph = h - top - bottom;

      const bg = ctx.createLinearGradient(0, 0, 0, h);
      bg.addColorStop(0, '#fcfdfb');
      bg.addColorStop(1, '#f3f7f3');
      ctx.fillStyle = bg;
      ctx.fillRect(0, 0, w, h);

      ctx.strokeStyle = 'rgba(23, 56, 44, 0.1)';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = top + (ph * i) / 4;
        ctx.beginPath();
        ctx.moveTo(left, y);
        ctx.lineTo(left + pw, y);
        ctx.stroke();

        const val = maxVal - (maxVal * i) / 4;
        ctx.fillStyle = '#617468';
        ctx.font = '11px Avenir Next';
        ctx.textAlign = 'right';
        ctx.fillText(val.toFixed(0), left - 8, y + 4);
      }

      const colors = ['#3e8b62', '#88bc68'];
      const groupWidth = pw / labels.length;
      const barWidth = Math.min(34, groupWidth * 0.26);

      for (let i = 0; i < labels.length; i++) {
        const cx = left + groupWidth * i + groupWidth / 2;
        const h1 = (clamp(z1Vals[i], 0, maxVal) / maxVal) * ph;
        const h2 = (clamp(z2Vals[i], 0, maxVal) / maxVal) * ph;

        ctx.fillStyle = colors[0];
        ctx.fillRect(cx - barWidth - 5, top + ph - h1, barWidth, h1);
        ctx.fillStyle = colors[1];
        ctx.fillRect(cx + 5, top + ph - h2, barWidth, h2);

        ctx.fillStyle = '#365546';
        ctx.font = '12px Avenir Next';
        ctx.textAlign = 'center';
        ctx.fillText(labels[i], cx, top + ph + 18);
      }

      ctx.textAlign = 'left';
      ctx.font = '12px Avenir Next';
      ctx.fillStyle = colors[0];
      ctx.fillRect(left, 8, 16, 5);
      ctx.fillStyle = '#365546';
      ctx.fillText('Area 1', left + 22, 13);
      ctx.fillStyle = colors[1];
      ctx.fillRect(left + 96, 8, 16, 5);
      ctx.fillStyle = '#365546';
      ctx.fillText('Area 2', left + 118, 13);

      if (!hasData) {
        ctx.textAlign = 'right';
        ctx.fillStyle = '#6c8274';
        ctx.font = '10px Avenir Next';
        ctx.fillText('Sample view while waiting for live readings', left + pw, 13);
      }
    }

    function renderCharts(z1, z2) {
      syncChartHeights();

      drawLineChart(chartMoistureEl, [
        { name: 'Area 1', color: '#3e8b62', values: history[1].moisture },
        { name: 'Area 2', color: '#8dbc68', values: history[2].moisture }
      ], 0, 100);

      drawLineChart(chartTempEl, [
        { name: 'Area 1', color: '#3c7f9c', values: history[1].temp },
        { name: 'Area 2', color: '#80b9d5', values: history[2].temp }
      ], 0, 50);

      drawNpkChart(chartNpkEl, z1, z2);
    }

    function render(status) {
      lastStatus = status;

      const zones = Array.isArray(status.zones) ? status.zones : [];
      const z1 = getZone(zones, 1);
      const z2 = getZone(zones, 2);
      const avgMoisture = average([
        z1 && z1.hasData ? z1.soilMoisturePct : NaN,
        z2 && z2.hasData ? z2.soilMoisturePct : NaN
      ]);
      const avgNutrient = average([
        z1 && z1.hasData ? z1.nutrientPpm : NaN,
        z2 && z2.hasData ? z2.nutrientPpm : NaN
      ]);
      const sampleAge = toNum(status.sampleAgeMs, 0);
      const phase = String(status.phase || '--').toUpperCase();
      const stepText = phase === 'ACTIVE' ? 'Working' : (phase === 'IDLE' ? 'Waiting' : '--');
      const tankLow = Boolean(status.tankLow);
      const phaseDuration = Math.max(1, toNum(status.phaseDurationMs, 0));
      const phaseElapsed = clamp(toNum(status.phaseElapsedMs, 0), 0, phaseDuration);
      const phasePct = clamp((phaseElapsed / phaseDuration) * 100, 0, 100);
      const telemetryTone = sampleAge <= 6000 ? 'fresh' : '';

      phaseChipEl.textContent = 'Step: ' + stepText;
      cycleChipEl.textContent = 'Round: ' + String(toNum(status.cycle, 0));
      telemetryChipEl.textContent = 'Last update: ' + fmtMs(sampleAge) + ' ago';
      telemetryChipEl.className = 'chip' + (telemetryTone ? ' ' + telemetryTone : '');
      wifiChipEl.textContent = (status.wifi && status.wifi.connected) ? 'Connection: online' : 'Connection: offline';
      wifiChipEl.className = (status.wifi && status.wifi.connected) ? 'chip' : 'chip offline';
      updatedEl.textContent = 'Last update: ' + new Date().toLocaleString();

      phaseProgressValueEl.textContent = fmtMs(status.phaseRemainingMs) + ' left';
      phaseProgressBarEl.style.width = phasePct.toFixed(1) + '%';

      setRingValue(spotlightRingEl, Number.isFinite(avgMoisture) ? avgMoisture : 0, '#8fd677');
      spotlightValueEl.textContent = Number.isFinite(avgMoisture) ? fmtValue(avgMoisture, 1, '%') : '--';
      spotlightMetaEl.textContent = Number.isFinite(avgMoisture) ? 'Across both plant areas' : 'Waiting for live readings';
      spotNutrientEl.textContent = Number.isFinite(avgNutrient) ? fmtValue(avgNutrient, 0, ' ppm') : '--';
      spotTankEl.textContent = tankLow ? 'Low' : 'OK';
      spotRemainingEl.textContent = fmtMs(status.phaseRemainingMs);

      if (foodGuideNowEl) {
        if (Number.isFinite(avgNutrient)) {
          foodGuideNowEl.textContent =
            'Current level: ' + fmtValue(avgNutrient, 0, ' ppm') + ' -> ' + foodLevelText(avgNutrient);
        } else {
          foodGuideNowEl.textContent = 'Current level: Waiting for live reading.';
        }
      }

      if (wetGuideNowEl) {
        if (Number.isFinite(avgMoisture)) {
          wetGuideNowEl.textContent =
            'Current level: ' + fmtValue(avgMoisture, 1, '%') + ' -> ' + soilWetnessLevelText(avgMoisture);
        } else {
          wetGuideNowEl.textContent = 'Current level: Waiting for live reading.';
        }
      }

      tankBadgeEl.className = 'status-pill ' + (tankLow ? 'alert' : 'good');
      tankBadgeEl.textContent = tankLow ? 'Low water' : 'Tank OK';
      tankFillEl.style.height = (tankLow ? 18 : 82) + '%';
      tankFillEl.style.background = tankLow
        ? 'linear-gradient(180deg, #e6a37f 0%, #ba4242 100%)'
        : 'linear-gradient(180deg, #86bfd8 0%, #3d7fa0 100%)';
      tankValueEl.textContent = tankLow ? 'Please refill tank' : 'Tank is ready';
      tankTextEl.textContent = tankLow
        ? 'Watering is paused until the tank has water again.'
        : 'Water is available for watering.';
      sampleAgeTextEl.textContent = fmtMs(sampleAge);
      sampleAgeBarEl.style.width = String(clamp(100 - (sampleAge / SAMPLE_AGE_MAX_MS) * 100, 8, 100)) + '%';
      sampleAgeBarEl.style.background = sampleAge <= 6000
        ? 'linear-gradient(90deg, rgba(62, 139, 98, 0.95), rgba(96, 168, 120, 0.95))'
        : 'linear-gradient(90deg, rgba(195, 119, 63, 0.95), rgba(186, 112, 40, 0.95))';

      if (tankLow) {
        tankAlertEl.className = 'alert show';
        tankAlertEl.textContent = 'Water tank is low. Please refill to continue normal watering.';
      } else if (status.criticalNutrientLockout) {
        tankAlertEl.className = 'alert show';
        tankAlertEl.textContent = 'Plant food level is unsafe. Automatic feeding is paused for safety.';
      } else {
        tankAlertEl.className = 'alert';
        tankAlertEl.textContent = '';
      }

      zoneSpotlightsEl.innerHTML =
        buildZoneCard(z1, 1) +
        buildZoneCard(z2, 2);

      systemDetailsEl.innerHTML =
        buildDetailRow('Device name', status.deviceName || '--') +
        buildDetailRow('Software version', `${status.firmwareVersion || '--'} / ${status.apiVersion || '--'}`) +
        buildDetailRow('Connection strength', status.wifi && status.wifi.connected ? signalStrengthLabel(status.wifi.rssi) : 'Offline') +
        buildDetailRow('Device address', status.wifi && status.wifi.connected ? (status.wifi.ip || '--') : '--') +
        buildDetailRow('Water pump now', status.waterValveOpen ? 'On' : 'Off') +
        buildDetailRow('Plant food pump now', status.nutrientValveOpen ? 'On' : 'Off') +
        buildDetailRow('Last watering time', fmtMs(status.lastWaterPulseMs)) +
        buildDetailRow('Last feeding time', fmtMs(status.lastNutrientPulseMs)) +
        buildDetailRow('Last work time', fmtMs(status.lastCycleElapsedMs));

      zoneDetailsEl.innerHTML =
        buildZoneDetail(z1, 1) +
        buildZoneDetail(z2, 2);

      lastAlertEl.textContent = status.lastAlert && status.lastAlert !== 'none'
        ? status.lastAlert
        : 'No warning right now. System is running normally.';

      if (z1 && z1.hasData) {
        pushHistory(1, 'moisture', clamp(toNum(z1.soilMoisturePct, 0), 0, 100));
        pushHistory(1, 'temp', clamp(toNum(z1.tempC, 0), 0, 50));
      }
      if (z2 && z2.hasData) {
        pushHistory(2, 'moisture', clamp(toNum(z2.soilMoisturePct, 0), 0, 100));
        pushHistory(2, 'temp', clamp(toNum(z2.tempC, 0), 0, 50));
      }

      renderCharts(z1, z2);
    }

    async function fetchStatus() {
      try {
        const res = await fetch('/api/status', { cache: 'no-store' });
        if (!res.ok) {
          throw new Error('HTTP ' + res.status);
        }

        const data = await res.json();
        render(data);
      } catch (err) {
        updatedEl.textContent = 'Last update: disconnected, retrying...';
        wifiChipEl.textContent = 'Connection: offline';
        wifiChipEl.className = 'chip offline';
      }
    }

    window.addEventListener('resize', function() {
      if (!lastStatus) {
        return;
      }

      const zones = Array.isArray(lastStatus.zones) ? lastStatus.zones : [];
      renderCharts(getZone(zones, 1), getZone(zones, 2));
    });

    renderCharts(null, null);
    fetchStatus();
    setInterval(fetchStatus, REFRESH_MS);
  </script>
</body>
</html>
)HTML";
#endif
const char *phaseText()
{
  return (phase == PHASE_IDLE) ? "idle" : "active";
}

bool isConfiguredValue(const char *value)
{
  if (value == nullptr || strlen(value) == 0)
  {
    return false;
  }
  return strncmp(value, "YOUR_", 5) != 0;
}

bool hasWiFiConfig()
{
  return isConfiguredValue(WIFI_SSID) && isConfiguredValue(WIFI_PASSWORD);
}

bool hasEmailConfig()
{
  return isConfiguredValue(SMTP_HOST) && isConfiguredValue(SMTP_USERNAME) && isConfiguredValue(SMTP_PASSWORD) && isConfiguredValue(EMAIL_FROM) && isConfiguredValue(EMAIL_TO);
}

void setAlertMessage(const String &message)
{
  lastAlertMessage = message;
  Serial.printf("[ALERT] %s\n", message.c_str());
}

void serviceNetwork()
{
  if (ENABLE_LOCAL_API_SERVER && serverStarted)
  {
    webServer.handleClient();
  }
}

void delayWithService(uint32_t delayMs)
{
  uint32_t startedAt = millis();
  while ((uint32_t)(millis() - startedAt) < delayMs)
  {
    maintainWiFiConnection();
    serviceNetwork();
    delay(20);
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Serial.println("[WiFiEvent] STA connected to AP.");
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.printf("[WiFiEvent] STA got IP: %s\n", WiFi.localIP().toString().c_str());
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.printf("[WiFiEvent] STA disconnected. reason=%d\n", (int)info.wifi_sta_disconnected.reason);
    break;
  default:
    Serial.printf("[WiFiEvent] id=%d\n", (int)event);
    break;
  }
}

void scanAndLogTargetSSID()
{
  WiFi.mode(WIFI_STA);
  int16_t found = WiFi.scanNetworks(false, true);
  if (found < 0)
  {
    hasPreferredWiFiBssid = false;
    preferredWiFiChannel = 0;
    Serial.printf("Wi-Fi scan failed. code=%d\n", (int)found);
    return;
  }

  hasPreferredWiFiBssid = false;
  preferredWiFiChannel = 0;
  bool targetFound = false;
  int16_t targetRssi = -127;
  uint8_t targetEnc = 255;
  int16_t bestRssi = -127;
  uint8_t bssidBuf[6] = {0, 0, 0, 0, 0, 0};

  Serial.printf("Wi-Fi scan found %d AP(s).\n", (int)found);
  for (int16_t i = 0; i < found; i++)
  {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    uint8_t enc = WiFi.encryptionType(i);
    if (ssid == WIFI_SSID)
    {
      targetFound = true;
      int32_t ch = WiFi.channel(i);
      String bssidStr = WiFi.BSSIDstr(i);
      Serial.printf("SSID match: bssid=%s ch=%d rssi=%d enc=%u\n",
                    bssidStr.c_str(), (int)ch, (int)rssi, (unsigned)enc);

      if (!hasPreferredWiFiBssid || rssi > bestRssi)
      {
        bestRssi = (int16_t)rssi;
        targetRssi = (int16_t)rssi;
        targetEnc = enc;
        preferredWiFiChannel = ch > 0 ? ch : 0;
        uint8_t *bssid = WiFi.BSSID(i, bssidBuf);
        if (bssid)
        {
          for (uint8_t j = 0; j < 6; j++)
          {
            preferredWiFiBssid[j] = bssid[j];
          }
          hasPreferredWiFiBssid = true;
        }
      }
    }
  }

  if (targetFound)
  {
    if (hasPreferredWiFiBssid)
    {
      Serial.printf("Target SSID '%s' selected BSSID=%02X:%02X:%02X:%02X:%02X:%02X ch=%d RSSI=%d dBm enc=%u\n",
                    WIFI_SSID,
                    preferredWiFiBssid[0], preferredWiFiBssid[1], preferredWiFiBssid[2],
                    preferredWiFiBssid[3], preferredWiFiBssid[4], preferredWiFiBssid[5],
                    (int)preferredWiFiChannel, (int)targetRssi, (unsigned)targetEnc);
    }
    else
    {
      Serial.printf("Target SSID '%s' detected. RSSI=%d dBm, enc=%u\n",
                    WIFI_SSID, (int)targetRssi, (unsigned)targetEnc);
    }
  }
  else
  {
    Serial.printf("Target SSID '%s' not found in scan.\n", WIFI_SSID);
  }

  WiFi.scanDelete();
}

void beginWiFiConnection(bool resetBeforeBegin = false)
{
  if (resetBeforeBegin)
  {
    // Stop any stale in-progress join before reapplying credentials.
    // Some Arduino-ESP32 3.3.x reconnect paths can keep STA in a busy state,
    // so force Wi-Fi off briefly before starting a fresh STA session.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    delay(120);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  if (hasPreferredWiFiBssid && preferredWiFiChannel > 0)
  {
    Serial.printf("Wi-Fi begin with locked BSSID=%02X:%02X:%02X:%02X:%02X:%02X ch=%d\n",
                  preferredWiFiBssid[0], preferredWiFiBssid[1], preferredWiFiBssid[2],
                  preferredWiFiBssid[3], preferredWiFiBssid[4], preferredWiFiBssid[5],
                  (int)preferredWiFiChannel);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, preferredWiFiChannel, preferredWiFiBssid, true);
  }
  else
  {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  WiFi.setTxPower(WIFI_TX_POWER_LEVEL);
  Serial.printf("Wi-Fi TX power set to level=%d\n", (int)WIFI_TX_POWER_LEVEL);
  lastWiFiBeginAt = millis();
}

void setupMDNSService()
{
  if (!ENABLE_MDNS || !ENABLE_LOCAL_API_SERVER || WiFi.status() != WL_CONNECTED || mdnsStarted)
  {
    return;
  }

  if (!MDNS.begin(MDNS_HOSTNAME))
  {
    Serial.println("mDNS start failed.");
    return;
  }

  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "path", "/");
  mdnsStarted = true;
  Serial.printf("mDNS: http://%s.local/\n", MDNS_HOSTNAME);
}

void stopMDNSService()
{
  if (!mdnsStarted)
  {
    return;
  }

  MDNS.end();
  mdnsStarted = false;
  Serial.println("mDNS stopped.");
}

void maintainWiFiConnection()
{
  if ((!ENABLE_LOCAL_API_SERVER && !ENABLE_EMAIL_ALERTS) || !hasWiFiConfig())
  {
    return;
  }

  uint32_t now = millis();
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED)
  {
    return;
  }

  // Give the current join process enough time to settle before triggering another retry.
  // On ESP32-C3, status may report disconnected while the STA is still trying to connect.
  if (status == WL_IDLE_STATUS || (lastWiFiBeginAt != 0 && (uint32_t)(now - lastWiFiBeginAt) < WIFI_CONNECT_TIMEOUT_MS))
  {
    return;
  }

  if ((uint32_t)(now - lastWiFiReconnectAttemptAt) < WIFI_RETRY_INTERVAL_MS)
  {
    return;
  }

  lastWiFiReconnectAttemptAt = now;
  Serial.printf("Wi-Fi reconnect attempt... (status=%d)\n", (int)status);
  if (status == WL_NO_SSID_AVAIL)
  {
    scanAndLogTargetSSID();
  }
  beginWiFiConnection(true);
}

void notifyWiFiTransition()
{
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (!wifiStateInitialized)
  {
    lastWiFiConnected = connected;
    wifiStateInitialized = true;
    return;
  }

  if (connected == lastWiFiConnected)
  {
    return;
  }

  lastWiFiConnected = connected;
  if (connected)
  {
    setupMDNSService();
    setAlertMessage("System reconnected successfully.");
  }
  else
  {
    stopMDNSService();
    setAlertMessage("Connection lost. Attempting to reconnect.");
  }
}

float deficitRatio(float actual, float threshold)
{
  if (threshold <= 0.0f || actual >= threshold)
  {
    return 0.0f;
  }
  float ratio = (threshold - actual) / threshold;
  if (ratio < 0.0f)
  {
    return 0.0f;
  }
  if (ratio > 1.0f)
  {
    return 1.0f;
  }
  if (ratio < DEFICIT_DEADBAND_RATIO)
  {
    return 0.0f;
  }
  return ratio;
}

uint32_t scaleDuration(uint32_t minMs, uint32_t maxMs, float ratio)
{
  if (ratio < 0.0f)
  {
    ratio = 0.0f;
  }
  if (ratio > 1.0f)
  {
    ratio = 1.0f;
  }
  if (maxMs <= minMs)
  {
    return minMs;
  }
  return minMs + (uint32_t)((maxMs - minMs) * ratio);
}

float smoothReading(float previous, float current, float alpha)
{
  if (alpha <= 0.0f)
  {
    return previous;
  }
  if (alpha >= 1.0f)
  {
    return current;
  }
  return (previous * (1.0f - alpha)) + (current * alpha);
}

float nutrientPpmFromNPK(const NPK &npk)
{
  return (npk.n + npk.p + npk.k) * NPK_TO_PPM_SCALE;
}

bool isNutrientCritical(float ppm)
{
  return (ppm < NUTRIENT_CRITICAL_LOW_PPM) || (ppm > NUTRIENT_CRITICAL_HIGH_PPM);
}

bool isNutrientLowBand(float ppm)
{
  return (ppm >= NUTRIENT_CRITICAL_LOW_PPM) && (ppm < NUTRIENT_LOW_PPM);
}

bool isNutrientSlightlyHighBand(float ppm)
{
  return (ppm > NUTRIENT_OPTIMAL_HIGH_PPM) && (ppm <= NUTRIENT_CRITICAL_HIGH_PPM);
}

const char *moistureBandText(float moisturePct)
{
  if (moisturePct < SOIL_MOISTURE_CRITICAL_DRY_PCT)
  {
    return "critical_dry";
  }
  if (moisturePct < SOIL_MOISTURE_LOW_PCT)
  {
    return "slightly_dry";
  }
  if (moisturePct <= SOIL_MOISTURE_OPTIMAL_HIGH_PCT)
  {
    return "optimal";
  }
  return "wet";
}

const char *temperatureBandText(float tempC)
{
  if (tempC < TEMP_COLD_C)
  {
    return "cold";
  }
  if (tempC <= TEMP_HOT_C)
  {
    return "optimal";
  }
  return "hot";
}

const char *humidityBandText(float humidityPct)
{
  if (humidityPct < HUMIDITY_DRY_PCT)
  {
    return "dry_air";
  }
  if (humidityPct <= HUMIDITY_HUMID_PCT)
  {
    return "optimal";
  }
  return "humid_high";
}

const char *nutrientBandText(float nutrientPpm)
{
  if (isNutrientCritical(nutrientPpm))
  {
    return "critical_imbalance";
  }
  if (isNutrientLowBand(nutrientPpm))
  {
    return "low";
  }
  if (isNutrientSlightlyHighBand(nutrientPpm))
  {
    return "slightly_high";
  }
  return "optimal";
}

void updateThresholdNotifications()
{
  for (uint8_t zone = 0; zone < NUM_ZONES; zone++)
  {
    if (!hasLatestReadings[zone])
    {
      continue;
    }

    const ZoneReadings &r = latestReadings[zone];
    float nutrientPpm = nutrientPpmFromNPK(r.npk);

    bool moistureLow = r.moisturePct < SOIL_MOISTURE_LOW_PCT;
    bool nutrientLow = isNutrientLowBand(nutrientPpm);
    bool nutrientHigh = isNutrientSlightlyHighBand(nutrientPpm);
    bool nutrientCritical = isNutrientCritical(nutrientPpm);

    if (moistureLow && !prevMoistureLowState[zone])
    {
      setAlertMessage(String("Zone ") + String(zone + 1) + ": soil moisture low. Irrigation activated.");
    }
    if (!moistureLow && prevMoistureLowState[zone])
    {
      setAlertMessage(String("Zone ") + String(zone + 1) + ": soil moisture normal. Irrigation stopped.");
    }

    if (nutrientLow && !prevNutrientLowState[zone])
    {
      setAlertMessage(String("Zone ") + String(zone + 1) + ": nutrient low. Dosing activated.");
    }
    if (nutrientHigh && !prevNutrientHighState[zone])
    {
      setAlertMessage(String("Zone ") + String(zone + 1) + ": nutrient high. Dosing reduced.");
    }
    if (nutrientCritical && !prevNutrientCriticalState[zone])
    {
      setAlertMessage(String("Zone ") + String(zone + 1) + ": critical nutrient imbalance. Check solution.");
    }

    prevMoistureLowState[zone] = moistureLow;
    prevNutrientLowState[zone] = nutrientLow;
    prevNutrientHighState[zone] = nutrientHigh;
    prevNutrientCriticalState[zone] = nutrientCritical;
  }
}

uint32_t blendDemandDuration(uint32_t maxDurationMs, uint32_t sumDurationMs, uint8_t demandCount)
{
  if (demandCount == 0)
  {
    return 0;
  }

  uint32_t avgDurationMs = sumDurationMs / demandCount;
  float blendedMs = (DEMAND_BLEND_MAX_WEIGHT * (float)maxDurationMs) + (DEMAND_BLEND_AVG_WEIGHT * (float)avgDurationMs);
  if (blendedMs < 0.0f)
  {
    blendedMs = 0.0f;
  }

  uint32_t out = (uint32_t)blendedMs;
  if (out == 0 && maxDurationMs > 0)
  {
    out = 1;
  }
  if (out > maxDurationMs)
  {
    out = maxDurationMs;
  }
  return out;
}

void setValve(uint8_t pin, bool open)
{
  bool level = open ? VALVE_ACTIVE_HIGH : !VALVE_ACTIVE_HIGH;
  digitalWrite(pin, level ? HIGH : LOW);

  if (pin == WATER_VALVE_PIN)
  {
    bool changed = (waterValveOpen != open);
    waterValveOpen = open;
    if (changed && phaseStartedAt > 0)
    {
      setAlertMessage(open ? "Water pump is running." : "Water pump stopped.");
    }
  }
  else if (pin == NUTRIENT_VALVE_PIN)
  {
    bool changed = (nutrientValveOpen != open);
    nutrientValveOpen = open;
    if (changed && phaseStartedAt > 0)
    {
      setAlertMessage(open ? "Nutrient dosing in progress." : "Nutrient dosing completed.");
    }
  }
}

void closeAllValves()
{
  setValve(WATER_VALVE_PIN, false);
  setValve(NUTRIENT_VALVE_PIN, false);
}

void runValveFor(uint8_t pin, uint32_t durationMs, const char *valveName)
{
  if (durationMs == 0)
  {
    return;
  }

  Serial.printf("{\"type\":\"actuation\",\"valve\":\"%s\",\"durationMs\":%lu}\n", valveName, (unsigned long)durationMs);
  setValve(pin, true);

  uint32_t startedAt = millis();
  while ((uint32_t)(millis() - startedAt) < durationMs)
  {
    maintainWiFiConnection();
    serviceNetwork();
    delay(50);
  }

  setValve(pin, false);
}

bool isTankLevelLow()
{
  uint8_t lowVotes = 0;
  const uint8_t sampleCount = 12;

  for (uint8_t i = 0; i < sampleCount; i++)
  {
    int raw = digitalRead(TANK_LEVEL_PIN);
    bool low = TANK_LEVEL_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
    if (low)
    {
      lowVotes++;
    }
    delay(2);
  }

  return lowVotes > (sampleCount / 2);
}

float moisturePercentFromRaw(int raw, int dryRaw, int wetRaw)
{
  int span = dryRaw - wetRaw;
  if (span == 0)
  {
    return 0.0f;
  }

  float pct = (float)(dryRaw - raw) * 100.0f / (float)span;
  if (pct < 0.0f)
  {
    pct = 0.0f;
  }
  if (pct > 100.0f)
  {
    pct = 100.0f;
  }
  return pct;
}

float readSoilMoisturePercent(uint8_t zoneIndex)
{
  if (zoneIndex >= NUM_ZONES)
  {
    return 0.0f;
  }

  const uint8_t pin = MOISTURE_PINS[zoneIndex];
  long sumRaw = 0;
  const uint8_t sampleCount = 8;

  for (uint8_t i = 0; i < sampleCount; i++)
  {
    sumRaw += analogRead(pin);
    delay(5);
  }

  int avgRaw = (int)(sumRaw / sampleCount);
  return moisturePercentFromRaw(avgRaw, MOISTURE_DRY_RAW[zoneIndex], MOISTURE_WET_RAW[zoneIndex]);
}

NPK readNPK(uint8_t zoneIndex)
{
  // Replace this stub with your NPK sensor integration (typically RS485/Modbus).
  NPK npk;
  npk.n = 25.0f + zoneIndex;
  npk.p = 18.0f + zoneIndex;
  npk.k = 22.0f + zoneIndex;
  return npk;
}

ZoneReadings readZone(uint8_t zoneIndex)
{
  ZoneReadings readings;
  readings.moisturePct = readSoilMoisturePercent(zoneIndex);

  float temp = dhtSensors[zoneIndex]->readTemperature();
  float humidity = dhtSensors[zoneIndex]->readHumidity();

  if (isnan(temp))
  {
    temp = hasLatestReadings[zoneIndex] ? latestReadings[zoneIndex].tempC : 25.0f;
  }
  if (isnan(humidity))
  {
    humidity = hasLatestReadings[zoneIndex] ? latestReadings[zoneIndex].humidityPct : 60.0f;
  }

  readings.tempC = temp;
  readings.humidityPct = humidity;
  readings.npk = readNPK(zoneIndex + 1);

  if (hasLatestReadings[zoneIndex])
  {
    const ZoneReadings &previous = latestReadings[zoneIndex];
    readings.moisturePct = smoothReading(previous.moisturePct, readings.moisturePct, SENSOR_SMOOTHING_ALPHA);
    readings.tempC = smoothReading(previous.tempC, readings.tempC, SENSOR_SMOOTHING_ALPHA);
    readings.humidityPct = smoothReading(previous.humidityPct, readings.humidityPct, SENSOR_SMOOTHING_ALPHA);
    readings.npk.n = smoothReading(previous.npk.n, readings.npk.n, SENSOR_SMOOTHING_ALPHA);
    readings.npk.p = smoothReading(previous.npk.p, readings.npk.p, SENSOR_SMOOTHING_ALPHA);
    readings.npk.k = smoothReading(previous.npk.k, readings.npk.k, SENSOR_SMOOTHING_ALPHA);
  }

  return readings;
}

bool haveCompleteTelemetry()
{
  for (uint8_t zone = 0; zone < NUM_ZONES; zone++)
  {
    if (!hasLatestReadings[zone])
    {
      return false;
    }
  }
  return true;
}

void refreshTelemetry(bool emitEvents)
{
  for (uint8_t zone = 0; zone < NUM_ZONES; zone++)
  {
    ZoneReadings readings = readZone(zone);
    latestReadings[zone] = readings;
    hasLatestReadings[zone] = true;

    if (emitEvents)
    {
      emitReadings(zone, readings);
    }
  }

  latestTankLow = isTankLevelLow();
  lastTelemetrySampleAtMs = millis();
  updateThresholdNotifications();
}

void refreshTelemetryIfDue()
{
  uint32_t now = millis();
  if (haveCompleteTelemetry() && (uint32_t)(now - lastTelemetrySampleAtMs) < TELEMETRY_REFRESH_INTERVAL_MS)
  {
    return;
  }

  refreshTelemetry(false);
}

uint32_t computeWaterPulse(const ZoneReadings &r, const ZoneThresholds &t, bool &moistureLow, bool &tempLow)
{
  (void)t;

  moistureLow = r.moisturePct < SOIL_MOISTURE_LOW_PCT;
  tempLow = r.tempC < TEMP_COLD_C;

  if (r.moisturePct > SOIL_MOISTURE_OPTIMAL_HIGH_PCT)
  {
    return 0;
  }

  uint32_t waterMs = 0;
  if (r.moisturePct < SOIL_MOISTURE_CRITICAL_DRY_PCT)
  {
    waterMs = MAX_WATER_PULSE_MS;
  }
  else if (r.moisturePct < SOIL_MOISTURE_LOW_PCT)
  {
    float drynessRatio = deficitRatio(r.moisturePct, SOIL_MOISTURE_LOW_PCT);
    waterMs = scaleDuration(MIN_WATER_PULSE_MS, MAX_WATER_PULSE_MS, drynessRatio);
  }
  else if (r.tempC > TEMP_HOT_C || r.humidityPct < HUMIDITY_DRY_PCT)
  {
    waterMs = CLIMATE_SUPPORT_WATER_PULSE_MS;
  }

  if (r.tempC > TEMP_HOT_C && waterMs > 0)
  {
    float heatRatio = (r.tempC - TEMP_HOT_C) / TEMP_HOT_C;
    if (heatRatio < 0.0f)
    {
      heatRatio = 0.0f;
    }
    if (heatRatio > 1.0f)
    {
      heatRatio = 1.0f;
    }
    waterMs += (uint32_t)(MAX_WATER_PULSE_MS * TEMP_HOT_WATER_BOOST_RATIO * heatRatio);
  }

  if (r.humidityPct < HUMIDITY_DRY_PCT && waterMs > 0)
  {
    float dryAirRatio = (HUMIDITY_DRY_PCT - r.humidityPct) / HUMIDITY_DRY_PCT;
    if (dryAirRatio < 0.0f)
    {
      dryAirRatio = 0.0f;
    }
    if (dryAirRatio > 1.0f)
    {
      dryAirRatio = 1.0f;
    }
    waterMs += (uint32_t)(MAX_WATER_PULSE_MS * HUMIDITY_DRY_WATER_BOOST_RATIO * dryAirRatio);
  }

  if (r.tempC < TEMP_COLD_C)
  {
    waterMs = (uint32_t)(waterMs * TEMP_COLD_WATER_REDUCTION_FACTOR);
  }
  if (r.humidityPct > HUMIDITY_HUMID_PCT)
  {
    waterMs = (uint32_t)(waterMs * HUMIDITY_HIGH_WATER_REDUCTION_FACTOR);
  }

  if (waterMs > MAX_WATER_PULSE_MS)
  {
    waterMs = MAX_WATER_PULSE_MS;
  }
  if (moistureLow && waterMs > 0 && waterMs < MIN_WATER_PULSE_MS)
  {
    waterMs = MIN_WATER_PULSE_MS;
  }

  return waterMs;
}

uint32_t computeNutrientPulse(const ZoneReadings &r, const ZoneThresholds &t, bool &nLow, bool &pLow, bool &kLow, bool &blockedByDrySoil)
{
  float nRatio = deficitRatio(r.npk.n, t.nMin);
  float pRatio = deficitRatio(r.npk.p, t.pMin);
  float kRatio = deficitRatio(r.npk.k, t.kMin);

  nLow = nRatio > 0.0f;
  pLow = pRatio > 0.0f;
  kLow = kRatio > 0.0f;
  blockedByDrySoil = r.moisturePct < MIN_MOISTURE_FOR_NUTRIENT_PCT;

  if (blockedByDrySoil)
  {
    return 0;
  }

  float nutrientPpm = nutrientPpmFromNPK(r.npk);
  if (isNutrientCritical(nutrientPpm))
  {
    return 0;
  }

  if (isNutrientLowBand(nutrientPpm))
  {
    float correctionRatio = (NUTRIENT_LOW_PPM - nutrientPpm) / (NUTRIENT_LOW_PPM - NUTRIENT_CRITICAL_LOW_PPM);
    if (correctionRatio < 0.0f)
    {
      correctionRatio = 0.0f;
    }
    if (correctionRatio > 1.0f)
    {
      correctionRatio = 1.0f;
    }
    return scaleDuration(MIN_NUTRIENT_PULSE_MS, MAX_NUTRIENT_PULSE_MS, correctionRatio);
  }

  if (isNutrientSlightlyHighBand(nutrientPpm))
  {
    return REDUCED_NUTRIENT_PULSE_MS;
  }

  return 0;
}

void emitReadings(uint8_t zoneIndex, const ZoneReadings &r)
{
  float nutrientPpm = nutrientPpmFromNPK(r.npk);
  Serial.printf(
      "{\"type\":\"reading\",\"zone\":%u,\"moisturePct\":%.2f,\"tempC\":%.2f,\"humidityPct\":%.2f,\"nutrientPpm\":%.2f,\"n\":%.2f,\"p\":%.2f,\"k\":%.2f}\n",
      zoneIndex + 1,
      r.moisturePct,
      r.tempC,
      r.humidityPct,
      nutrientPpm,
      r.npk.n,
      r.npk.p,
      r.npk.k);
}

void writeLCDLines(const char *line1, const char *line2, const char *line3, const char *line4)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  lcd.setCursor(0, 2);
  lcd.print(line3);
  lcd.setCursor(0, 3);
  lcd.print(line4);
}

void formatMMSS(uint32_t durationMs, char *out, size_t outSize)
{
  uint32_t totalSec = durationMs / 1000UL;
  uint32_t minutes = totalSec / 60UL;
  uint32_t seconds = totalSec % 60UL;
  snprintf(out, outSize, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}

void renderLCDPage()
{
  char line1[21];
  char line2[21];
  char line3[21];
  char line4[21];

  for (uint8_t i = 0; i < 20; i++)
  {
    line1[i] = ' ';
    line2[i] = ' ';
    line3[i] = ' ';
    line4[i] = ' ';
  }
  line1[20] = '\0';
  line2[20] = '\0';
  line3[20] = '\0';
  line4[20] = '\0';

  uint32_t phaseDurationMs = (phase == PHASE_IDLE) ? IDLE_DURATION_MS : ACTIVE_DURATION_MS;
  uint32_t phaseElapsedMs = (uint32_t)(millis() - phaseStartedAt);
  uint32_t phaseRemainingMs = (phaseElapsedMs < phaseDurationMs) ? (phaseDurationMs - phaseElapsedMs) : 0;
  char remainingText[6];
  formatMMSS(phaseRemainingMs, remainingText, sizeof(remainingText));

  const char *phaseShort = (phase == PHASE_IDLE) ? "IDLE" : "ACTV";
  const char *wifiShort = (WiFi.status() == WL_CONNECTED) ? "OK" : "NO";
  const char *waterShort = waterValveOpen ? "ON" : "OFF";
  const char *nutrientShort = nutrientValveOpen ? "ON" : "OFF";

  String moisture1 = hasLatestReadings[0] ? String((int)(latestReadings[0].moisturePct + 0.5f)) : String("--");
  String moisture2 = hasLatestReadings[1] ? String((int)(latestReadings[1].moisturePct + 0.5f)) : String("--");

  if (latestTankLow)
  {
    snprintf(line1, sizeof(line1), "!!! TANK LOW !!!");
    snprintf(line2, sizeof(line2), "REFILL WATER TANK");
    snprintf(line3, sizeof(line3), "WATER VALVE BLOCKED");
    snprintf(line4, sizeof(line4), "Z1:%s Z2:%s Wi:%s", moisture1.c_str(), moisture2.c_str(), wifiShort);
  }
  else
  {
    snprintf(line1, sizeof(line1), "%s Cy:%lu Wi:%s", phaseShort, (unsigned long)cycleCount, wifiShort);
    snprintf(line2, sizeof(line2), "Tank:OK  Rem:%s", remainingText);
    snprintf(line3, sizeof(line3), "Mois Z1:%s Z2:%s", moisture1.c_str(), moisture2.c_str());
    snprintf(line4, sizeof(line4), "Valve W:%s N:%s", waterShort, nutrientShort);
  }

  writeLCDLines(line1, line2, line3, line4);
}

void updateLCD()
{
  uint32_t now = millis();
  if ((uint32_t)(now - lastLCDUpdateAt) < LCD_PAGE_INTERVAL_MS)
  {
    return;
  }

  lastLCDUpdateAt = now;
  renderLCDPage();
}

String jsonEscape(const String &input)
{
  String out;
  out.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); i++)
  {
    char c = input[i];
    switch (c)
    {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }

  return out;
}

String buildStatusJSON()
{
  uint32_t uptimeMs = millis();
  uint32_t sampleAgeMs = haveCompleteTelemetry() ? (uint32_t)(uptimeMs - lastTelemetrySampleAtMs) : 0;
  uint32_t phaseDurationMs = (phase == PHASE_IDLE) ? IDLE_DURATION_MS : ACTIVE_DURATION_MS;
  uint32_t phaseElapsedMs = (phaseStartedAt > 0) ? (uint32_t)(uptimeMs - phaseStartedAt) : 0;
  if (phaseElapsedMs > phaseDurationMs)
  {
    phaseElapsedMs = phaseDurationMs;
  }
  uint32_t phaseRemainingMs = (phaseElapsedMs < phaseDurationMs) ? (phaseDurationMs - phaseElapsedMs) : 0;
  String json;
  json.reserve(2500);

  json += "{";
  json += "\"deviceName\":\"";
  json += jsonEscape(DEVICE_NAME);
  json += "\",";
  json += "\"firmwareVersion\":\"";
  json += jsonEscape(FIRMWARE_VERSION);
  json += "\",";
  json += "\"apiVersion\":\"";
  json += jsonEscape(API_VERSION);
  json += "\",";
  json += "\"methodologyProfile\":\"";
  json += jsonEscape(METHODOLOGY_PROFILE);
  json += "\",";
  json += "\"phase\":\"";
  json += phaseText();
  json += "\",";
  json += "\"cycle\":";
  json += String(cycleCount);
  json += ",";
  json += "\"phaseDurationMs\":";
  json += String(phaseDurationMs);
  json += ",";
  json += "\"phaseElapsedMs\":";
  json += String(phaseElapsedMs);
  json += ",";
  json += "\"phaseRemainingMs\":";
  json += String(phaseRemainingMs);
  json += ",";
  json += "\"tankLow\":";
  json += (latestTankLow ? "true" : "false");
  json += ",";
  json += "\"lastWaterPulseMs\":";
  json += String(lastWaterPulseMs);
  json += ",";
  json += "\"lastNutrientPulseMs\":";
  json += String(lastNutrientPulseMs);
  json += ",";
  json += "\"lastCycleElapsedMs\":";
  json += String(lastCycleElapsedMs);
  json += ",";
  json += "\"lastCycleCompletedAtMs\":";
  json += String(lastCycleCompletedAtMs);
  json += ",";
  json += "\"uptimeMs\":";
  json += String(uptimeMs);
  json += ",";
  json += "\"lastTelemetrySampleAtMs\":";
  json += String(lastTelemetrySampleAtMs);
  json += ",";
  json += "\"sampleAgeMs\":";
  json += String(sampleAgeMs);
  json += ",";
  json += "\"criticalNutrientLockout\":";
  json += (lastCycleCriticalNutrientLockout ? "true" : "false");
  json += ",";
  json += "\"waterValveOpen\":";
  json += (waterValveOpen ? "true" : "false");
  json += ",";
  json += "\"nutrientValveOpen\":";
  json += (nutrientValveOpen ? "true" : "false");
  json += ",";
  json += "\"lastAlert\":\"";
  json += jsonEscape(lastAlertMessage);
  json += "\",";

  json += "\"wifi\":{";
  json += "\"connected\":";
  json += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",\"rssi\":";
  json += String(WiFi.RSSI());
  json += "},";

  json += "\"zones\":[";
  for (uint8_t i = 0; i < NUM_ZONES; i++)
  {
    if (i > 0)
    {
      json += ",";
    }

    json += "{";
    json += "\"zone\":";
    json += String(i + 1);
    json += ",\"hasData\":";
    json += (hasLatestReadings[i] ? "true" : "false");

    if (hasLatestReadings[i])
    {
      float nutrientPpm = nutrientPpmFromNPK(latestReadings[i].npk);
      json += ",\"soilMoisturePct\":";
      json += String(latestReadings[i].moisturePct, 2);
      json += ",\"tempC\":";
      json += String(latestReadings[i].tempC, 2);
      json += ",\"humidityPct\":";
      json += String(latestReadings[i].humidityPct, 2);
      json += ",\"nutrientPpm\":";
      json += String(nutrientPpm, 2);
      json += ",\"moistureBand\":\"";
      json += moistureBandText(latestReadings[i].moisturePct);
      json += "\"";
      json += ",\"tempBand\":\"";
      json += temperatureBandText(latestReadings[i].tempC);
      json += "\"";
      json += ",\"humidityBand\":\"";
      json += humidityBandText(latestReadings[i].humidityPct);
      json += "\"";
      json += ",\"nutrientBand\":\"";
      json += nutrientBandText(nutrientPpm);
      json += "\"";
      json += ",\"npk\":{";
      json += "\"n\":";
      json += String(latestReadings[i].npk.n, 2);
      json += ",\"p\":";
      json += String(latestReadings[i].npk.p, 2);
      json += ",\"k\":";
      json += String(latestReadings[i].npk.k, 2);
      json += "}";
    }

    json += "}";
  }
  json += "]";
  json += "}";

  return json;
}

String buildInfoJSON()
{
  String json;
  json.reserve(1024);

  json += "{";
  json += "\"deviceName\":\"";
  json += jsonEscape(DEVICE_NAME);
  json += "\",";
  json += "\"firmwareVersion\":\"";
  json += jsonEscape(FIRMWARE_VERSION);
  json += "\",";
  json += "\"apiVersion\":\"";
  json += jsonEscape(API_VERSION);
  json += "\",";
  json += "\"methodologyProfile\":\"";
  json += jsonEscape(METHODOLOGY_PROFILE);
  json += "\",";
  json += "\"numZones\":";
  json += String(NUM_ZONES);
  json += ",";
  json += "\"telemetryRefreshMs\":";
  json += String(TELEMETRY_REFRESH_INTERVAL_MS);
  json += ",";
  json += "\"thresholds\":{";
  json += "\"soilMoisture\":{\"criticalDryLt\":";
  json += String(SOIL_MOISTURE_CRITICAL_DRY_PCT, 1);
  json += ",\"lowLt\":";
  json += String(SOIL_MOISTURE_LOW_PCT, 1);
  json += ",\"optimalHighLe\":";
  json += String(SOIL_MOISTURE_OPTIMAL_HIGH_PCT, 1);
  json += "},";
  json += "\"temperature\":{\"coldLt\":";
  json += String(TEMP_COLD_C, 1);
  json += ",\"hotGt\":";
  json += String(TEMP_HOT_C, 1);
  json += "},";
  json += "\"humidity\":{\"dryLt\":";
  json += String(HUMIDITY_DRY_PCT, 1);
  json += ",\"humidHighGt\":";
  json += String(HUMIDITY_HUMID_PCT, 1);
  json += "},";
  json += "\"nutrientPpm\":{\"criticalLowLt\":";
  json += String(NUTRIENT_CRITICAL_LOW_PPM, 1);
  json += ",\"lowLt\":";
  json += String(NUTRIENT_LOW_PPM, 1);
  json += ",\"optimalHighLe\":";
  json += String(NUTRIENT_OPTIMAL_HIGH_PPM, 1);
  json += ",\"criticalHighGt\":";
  json += String(NUTRIENT_CRITICAL_HIGH_PPM, 1);
  json += "}";
  json += "},";
  json += "\"localApiEnabled\":";
  json += (ENABLE_LOCAL_API_SERVER ? "true" : "false");
  json += ",";
  json += "\"uptimeMs\":";
  json += String(millis());
  json += "}";

  return json;
}

String base64Encode(const String &input)
{
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  const uint8_t *data = reinterpret_cast<const uint8_t *>(input.c_str());
  size_t len = input.length();

  String out;
  out.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3)
  {
    uint32_t octetA = data[i];
    uint32_t octetB = (i + 1 < len) ? data[i + 1] : 0;
    uint32_t octetC = (i + 2 < len) ? data[i + 2] : 0;

    uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

    out += table[(triple >> 18) & 0x3F];
    out += table[(triple >> 12) & 0x3F];
    out += (i + 1 < len) ? table[(triple >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? table[triple & 0x3F] : '=';
  }

  return out;
}

bool smtpExpectCode(WiFiClientSecure &client, const char *expectedCode, uint32_t timeoutMs)
{
  uint32_t start = millis();

  while ((uint32_t)(millis() - start) < timeoutMs)
  {
    while (client.available())
    {
      String line = client.readStringUntil('\n');
      line.trim();

      if (line.length() >= 3 && isDigit(line[0]) && isDigit(line[1]) && isDigit(line[2]))
      {
        if (line.length() < 4 || line[3] != '-')
        {
          Serial.printf("SMTP <= %s\n", line.c_str());
          return line.substring(0, 3) == expectedCode;
        }
      }
    }

    if (!client.connected() && !client.available())
    {
      break;
    }

    serviceNetwork();
    delay(10);
  }

  return false;
}

bool smtpSendCmd(WiFiClientSecure &client, const String &command, const char *expectedCode)
{
  Serial.printf("SMTP => %s\n", command.c_str());
  client.print(command);
  client.print("\r\n");
  return smtpExpectCode(client, expectedCode, 10000);
}

bool sendEmailAlert(const String &subject, const String &body)
{
  if (!ENABLE_EMAIL_ALERTS || !hasWiFiConfig() || !hasEmailConfig() || WiFi.status() != WL_CONNECTED)
  {
    return false;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - lastEmailSentAt) < EMAIL_ALERT_COOLDOWN_MS)
  {
    return false;
  }

  WiFiClientSecure client;
  client.setTimeout(10);
  if (SMTP_ALLOW_INSECURE_TLS)
  {
    client.setInsecure();
  }

  if (!client.connect(SMTP_HOST, SMTP_PORT))
  {
    setAlertMessage("Email failed: SMTP connect");
    return false;
  }

  if (!smtpExpectCode(client, "220", 10000))
  {
    setAlertMessage("Email failed: SMTP 220");
    return false;
  }
  if (!smtpSendCmd(client, "EHLO esp32-c3", "250"))
  {
    setAlertMessage("Email failed: EHLO");
    return false;
  }
  if (!smtpSendCmd(client, "AUTH LOGIN", "334"))
  {
    setAlertMessage("Email failed: AUTH LOGIN");
    return false;
  }
  if (!smtpSendCmd(client, base64Encode(String(SMTP_USERNAME)), "334"))
  {
    setAlertMessage("Email failed: username");
    return false;
  }
  if (!smtpSendCmd(client, base64Encode(String(SMTP_PASSWORD)), "235"))
  {
    setAlertMessage("Email failed: password");
    return false;
  }
  if (!smtpSendCmd(client, String("MAIL FROM:<") + String(EMAIL_FROM) + ">", "250"))
  {
    setAlertMessage("Email failed: MAIL FROM");
    return false;
  }
  if (!smtpSendCmd(client, String("RCPT TO:<") + String(EMAIL_TO) + ">", "250"))
  {
    setAlertMessage("Email failed: RCPT TO");
    return false;
  }
  if (!smtpSendCmd(client, "DATA", "354"))
  {
    setAlertMessage("Email failed: DATA");
    return false;
  }

  String payload;
  payload.reserve(512);
  payload += String("From: <") + String(EMAIL_FROM) + ">\r\n";
  payload += String("To: <") + String(EMAIL_TO) + ">\r\n";
  payload += String("Subject: ") + subject + "\r\n";
  payload += "Content-Type: text/plain; charset=\"utf-8\"\r\n";
  payload += "\r\n";
  payload += body;
  payload += "\r\n.\r\n";

  client.print(payload);

  if (!smtpExpectCode(client, "250", 10000))
  {
    setAlertMessage("Email failed: body send");
    return false;
  }

  smtpSendCmd(client, "QUIT", "221");
  lastEmailSentAt = now;
  setAlertMessage(String("Email sent: ") + subject);
  return true;
}

void maybeSendTankLowEmail()
{
  String subject = "Vertical Farm Alert: Tank Low";
  String body = "Tank level is LOW. Water valve actions are blocked until tank level recovers.\n";
  body += String("Cycle: ") + String(cycleCount) + "\n";
  body += String("Device IP: ") + WiFi.localIP().toString() + "\n";
  sendEmailAlert(subject, body);
}

void sendResponseHeaders()
{
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
}

void sendJsonResponse(int statusCode, const String &body)
{
  sendResponseHeaders();
  webServer.send(statusCode, "application/json", body);
}

void sendTextResponse(int statusCode, const String &body)
{
  sendResponseHeaders();
  webServer.send(statusCode, "text/plain", body);
}

void handleOptions()
{
  sendResponseHeaders();
  webServer.send(204, "text/plain", "");
}

void handleRoot()
{
  sendResponseHeaders();
  webServer.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
}

void handleStatus()
{
  sendJsonResponse(200, buildStatusJSON());
}

void handleInfo()
{
  sendJsonResponse(200, buildInfoJSON());
}

void handleHealth()
{
  sendJsonResponse(200, "{\"status\":\"ok\"}");
}

void setupApiServer()
{
  if (!ENABLE_LOCAL_API_SERVER || WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/", HTTP_OPTIONS, handleOptions);
  webServer.on("/api/info", HTTP_GET, handleInfo);
  webServer.on("/api/info", HTTP_OPTIONS, handleOptions);
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/status", HTTP_OPTIONS, handleOptions);
  webServer.on("/healthz", HTTP_GET, handleHealth);
  webServer.on("/healthz", HTTP_OPTIONS, handleOptions);
  webServer.onNotFound([]()
                       { sendJsonResponse(404, "{\"error\":\"not_found\"}"); });

  webServer.begin();
  serverStarted = true;
  setupMDNSService();

  Serial.printf("Local API: http://%s/api/status\n", WiFi.localIP().toString().c_str());
  if (ENABLE_MDNS)
  {
    Serial.printf("Local API (mDNS): http://%s.local/api/status\n", MDNS_HOSTNAME);
  }
  setAlertMessage(String("Local API online at ") + WiFi.localIP().toString());
}

void executeControlCycle()
{
  cycleCount++;
  uint32_t cycleStart = millis();
  bool wasCriticalLockout = lastCycleCriticalNutrientLockout;

  uint32_t requestedWaterMs = 0;
  uint32_t requestedNutrientMs = 0;
  uint32_t maxWaterMs = 0;
  uint32_t maxNutrientMs = 0;
  uint32_t sumWaterMs = 0;
  uint32_t sumNutrientMs = 0;
  uint8_t waterDemandCount = 0;
  uint8_t nutrientDemandCount = 0;
  uint8_t nutrientBlockedZones = 0;
  uint8_t criticalNutrientZones = 0;
  bool criticalNutrientImbalance = false;

  if (!haveCompleteTelemetry() || (uint32_t)(millis() - lastTelemetrySampleAtMs) >= TELEMETRY_REFRESH_INTERVAL_MS)
  {
    refreshTelemetry(true);
  }
  else
  {
    for (uint8_t zone = 0; zone < NUM_ZONES; zone++)
    {
      emitReadings(zone, latestReadings[zone]);
    }
    latestTankLow = isTankLevelLow();
  }

  for (uint8_t zone = 0; zone < NUM_ZONES; zone++)
  {
    ZoneReadings readings = latestReadings[zone];

    bool moistureLow = false;
    bool tempLow = false;
    bool nLow = false;
    bool pLow = false;
    bool kLow = false;
    bool nutrientBlockedByDrySoil = false;
    float nutrientPpm = nutrientPpmFromNPK(readings.npk);
    bool nutrientCritical = isNutrientCritical(nutrientPpm);
    bool nutrientSlightlyHigh = isNutrientSlightlyHighBand(nutrientPpm);

    uint32_t waterMs = computeWaterPulse(readings, thresholds[zone], moistureLow, tempLow);
    uint32_t nutrientMs = computeNutrientPulse(readings, thresholds[zone], nLow, pLow, kLow, nutrientBlockedByDrySoil);

    if (nutrientCritical)
    {
      criticalNutrientImbalance = true;
      criticalNutrientZones++;
      nutrientMs = 0;
    }

    if (waterMs > maxWaterMs)
    {
      maxWaterMs = waterMs;
    }
    if (nutrientMs > maxNutrientMs)
    {
      maxNutrientMs = nutrientMs;
    }
    if (waterMs > 0)
    {
      sumWaterMs += waterMs;
      waterDemandCount++;
    }
    if (nutrientMs > 0)
    {
      sumNutrientMs += nutrientMs;
      nutrientDemandCount++;
    }
    if (nutrientBlockedByDrySoil)
    {
      nutrientBlockedZones++;
    }

    Serial.printf(
        "{\"type\":\"decision\",\"zone\":%u,\"waterMs\":%lu,\"nutrientMs\":%lu,\"moistureLow\":%s,\"tempLow\":%s,\"nLow\":%s,\"pLow\":%s,\"kLow\":%s,\"nutrientBlockedByDrySoil\":%s,\"nutrientPpm\":%.2f,\"nutrientSlightlyHigh\":%s,\"nutrientCritical\":%s}\n",
        zone + 1,
        (unsigned long)waterMs,
        (unsigned long)nutrientMs,
        moistureLow ? "true" : "false",
        tempLow ? "true" : "false",
        nLow ? "true" : "false",
        pLow ? "true" : "false",
        kLow ? "true" : "false",
        nutrientBlockedByDrySoil ? "true" : "false",
        nutrientPpm,
        nutrientSlightlyHigh ? "true" : "false",
        nutrientCritical ? "true" : "false");
  }

  requestedWaterMs = blendDemandDuration(maxWaterMs, sumWaterMs, waterDemandCount);
  requestedNutrientMs = blendDemandDuration(maxNutrientMs, sumNutrientMs, nutrientDemandCount);

  lastCycleCriticalNutrientLockout = criticalNutrientImbalance;
  if (criticalNutrientImbalance)
  {
    requestedWaterMs = 0;
    requestedNutrientMs = 0;
    Serial.printf(
        "{\"type\":\"safety\",\"event\":\"critical_nutrient_lockout\",\"zones\":%u}\n",
        criticalNutrientZones);
    if (!wasCriticalLockout)
    {
      setAlertMessage("Critical nutrient imbalance. Automation paused.");
    }
  }
  else if (wasCriticalLockout)
  {
    setAlertMessage("Nutrient concentration back in safe range.");
  }

  if (nutrientBlockedZones > 0 && nutrientDemandCount == 0)
  {
    Serial.printf(
        "{\"type\":\"safety\",\"event\":\"nutrient_delayed\",\"reason\":\"soil_too_dry\",\"blockedZones\":%u}\n",
        nutrientBlockedZones);
  }

  bool previousTankLow = latestTankLow;
  bool tankLow = isTankLevelLow();
  latestTankLow = tankLow;

  Serial.printf("{\"type\":\"tank\",\"low\":%s}\n", tankLow ? "true" : "false");

  if (tankLow && !previousTankLow)
  {
    setAlertMessage("Water tank level is low. Refill required.");
    maybeSendTankLowEmail();
  }

  if (tankLow && requestedWaterMs > 0)
  {
    Serial.println("{\"type\":\"safety\",\"event\":\"water_blocked\",\"reason\":\"tank_low\"}");
    requestedWaterMs = 0;
  }

  uint32_t elapsed = millis() - cycleStart;
  uint32_t remaining = (elapsed < ACTIVE_DURATION_MS) ? (ACTIVE_DURATION_MS - elapsed) : 0;

  if (requestedWaterMs > remaining)
  {
    requestedWaterMs = remaining;
  }
  if (requestedWaterMs > 0)
  {
    runValveFor(WATER_VALVE_PIN, requestedWaterMs, "water");
  }

  elapsed = millis() - cycleStart;
  remaining = (elapsed < ACTIVE_DURATION_MS) ? (ACTIVE_DURATION_MS - elapsed) : 0;

  if (requestedNutrientMs > 0 && remaining > 0)
  {
    uint32_t delayMs = BETWEEN_VALVE_DELAY_MS;
    if (delayMs > remaining)
    {
      delayMs = remaining;
    }

    if (delayMs > 0)
    {
      delayWithService(delayMs);
    }

    elapsed = millis() - cycleStart;
    remaining = (elapsed < ACTIVE_DURATION_MS) ? (ACTIVE_DURATION_MS - elapsed) : 0;

    if (requestedNutrientMs > remaining)
    {
      requestedNutrientMs = remaining;
    }

    if (requestedNutrientMs > 0)
    {
      runValveFor(NUTRIENT_VALVE_PIN, requestedNutrientMs, "nutrient");
    }
  }

  closeAllValves();

  lastWaterPulseMs = requestedWaterMs;
  lastNutrientPulseMs = requestedNutrientMs;
  lastCycleElapsedMs = millis() - cycleStart;
  lastCycleCompletedAtMs = millis();
}

void enterPhase(Phase newPhase)
{
  phase = newPhase;
  phaseStartedAt = millis();
  cycleExecuted = false;

  Serial.printf("{\"type\":\"phase\",\"phase\":\"%s\",\"atMs\":%lu}\n", phaseText(), (unsigned long)phaseStartedAt);
}

void connectWiFiBlocking()
{
  if ((!ENABLE_LOCAL_API_SERVER && !ENABLE_EMAIL_ALERTS) || !hasWiFiConfig())
  {
    Serial.println("Wi-Fi disabled or credentials not configured.");
    return;
  }

  scanAndLogTargetSSID();
  beginWiFiConnection();

  Serial.printf("Connecting Wi-Fi SSID: %s\n", WIFI_SSID);
  uint32_t startedAt = millis();

  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - startedAt) < WIFI_CONNECT_TIMEOUT_MS)
  {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("Wi-Fi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  }
  else
  {
    Serial.println("Wi-Fi initial connection failed. Will retry in loop.");
  }
}

void setup()
{
  Serial.begin(115200);
  WiFi.onEvent(onWiFiEvent);

  pinMode(WATER_VALVE_PIN, OUTPUT);
  pinMode(NUTRIENT_VALVE_PIN, OUTPUT);
  pinMode(TANK_LEVEL_PIN, INPUT_PULLUP);

  closeAllValves();

  dhtZone1.begin();
  dhtZone2.begin();

  for (uint8_t zone = 0; zone < NUM_ZONES; zone++)
  {
    analogSetPinAttenuation(MOISTURE_PINS[zone], ADC_11db);
  }

  Wire.begin();
  lcd.init();
  lcd.backlight();
  writeLCDLines("System booting", "ESP32-C3 online", "Loading sensors", "Please wait");

  connectWiFiBlocking();
  setupApiServer();
  lastTelemetrySampleAtMs = millis() - TELEMETRY_REFRESH_INTERVAL_MS;

  enterPhase(PHASE_IDLE);
  lastLCDUpdateAt = 0;
}

void loop()
{
  maintainWiFiConnection();
  notifyWiFiTransition();
  if (ENABLE_LOCAL_API_SERVER && !serverStarted && WiFi.status() == WL_CONNECTED)
  {
    setupApiServer();
  }

  refreshTelemetryIfDue();
  serviceNetwork();

  uint32_t now = millis();

  if (phase == PHASE_IDLE)
  {
    if ((uint32_t)(now - phaseStartedAt) >= IDLE_DURATION_MS)
    {
      enterPhase(PHASE_ACTIVE);
    }
    updateLCD();
    delay(50);
    return;
  }

  if (phase == PHASE_ACTIVE)
  {
    if (!cycleExecuted)
    {
      executeControlCycle();
      cycleExecuted = true;
    }

    if ((uint32_t)(now - phaseStartedAt) >= ACTIVE_DURATION_MS)
    {
      enterPhase(PHASE_IDLE);
    }

    updateLCD();
    delay(50);
    return;
  }
}
