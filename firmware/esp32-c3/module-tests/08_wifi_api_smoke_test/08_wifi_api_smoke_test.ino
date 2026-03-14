#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

const uint32_t SERIAL_BAUD = 115200;

// Set these before upload.
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const uint32_t WIFI_RETRY_INTERVAL_MS = 15000UL;

WebServer server(80);

uint32_t lastRetryAt = 0;
bool serverStarted = false;

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

void sendCommonHeaders()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
}

void handleOptions()
{
  sendCommonHeaders();
  server.send(204, "text/plain", "");
}

void handleRoot()
{
  sendCommonHeaders();
  server.send(200, "text/plain", "NILA Wi-Fi/API smoke test online.");
}

void handleHealthz()
{
  sendCommonHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleStatus()
{
  String body = "{";
  body += "\"uptimeMs\":";
  body += String(millis());
  body += ",\"wifiConnected\":";
  body += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  body += ",\"ip\":\"";
  body += WiFi.localIP().toString();
  body += "\",\"rssi\":";
  body += String(WiFi.RSSI());
  body += "}";

  sendCommonHeaders();
  server.send(200, "application/json", body);
}

void setupServerIfNeeded()
{
  if (serverStarted || WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/healthz", HTTP_GET, handleHealthz);
  server.on("/healthz", HTTP_OPTIONS, handleOptions);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.onNotFound([]()
                    {
    sendCommonHeaders();
    server.send(404, "application/json", "{\"error\":\"not_found\"}"); });

  server.begin();
  serverStarted = true;

  Serial.printf("HTTP server started. IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Test URLs:\n");
  Serial.printf("  http://%s/healthz\n", WiFi.localIP().toString().c_str());
  Serial.printf("  http://%s/api/status\n", WiFi.localIP().toString().c_str());
}

void beginWiFi()
{
  Serial.printf("Connecting Wi-Fi SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectWiFiBlocking()
{
  if (!hasWiFiConfig())
  {
    Serial.println("Wi-Fi not configured. Update WIFI_SSID/WIFI_PASSWORD first.");
    return;
  }

  beginWiFi();
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
    Serial.println("Initial Wi-Fi connect failed. Will retry in loop.");
  }
}

void maintainWiFi()
{
  if (!hasWiFiConfig())
  {
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - lastRetryAt) < WIFI_RETRY_INTERVAL_MS)
  {
    return;
  }

  lastRetryAt = now;
  Serial.println("Wi-Fi reconnect attempt...");
  WiFi.disconnect();
  delay(20);
  beginWiFi();
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(400);
  Serial.println();
  Serial.println("=== NILA Module Test 08: Wi-Fi/API Smoke Test ===");
  Serial.println("Edit WIFI_SSID and WIFI_PASSWORD before upload.");

  connectWiFiBlocking();
  setupServerIfNeeded();
}

void loop()
{
  maintainWiFi();
  setupServerIfNeeded();

  if (serverStarted)
  {
    server.handleClient();
  }

  delay(20);
}
