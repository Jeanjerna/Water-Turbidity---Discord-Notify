#include "NetworkService.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef DISCORD_WEBHOOK_URL
#define DISCORD_WEBHOOK_URL ""
#endif

extern const char discordRoots[] asm("_binary_certs_discord_roots_pem_start");
namespace NetworkService {
namespace {
using namespace Monitor;
QueueHandle_t readingQueue = nullptr, statusQueue = nullptr, commandQueue = nullptr;
NetworkStatus current;
Reading latest;
DeliveryPolicy delivery;
RetryPolicy retry;
bool haveReading = false, manualPending = false, portalOpen = false;
bool wasWifi = false, healthChecked = false, taskAvailable = false;
uint32_t wifiStarted = 0, portalStarted = 0, healthAt = 0;
uint32_t lastManual = 0;
bool manualSeen = false;

void publishStatus() {
  current.portalOpen = portalOpen;
  current.retrySeconds = (retry.remaining(millis()) + 999) / 1000;
  xQueueOverwrite(statusQueue, &current);
}
void result(SendState state, int httpCode = 0) {
  if ((state == SendState::WaitWifi || state == SendState::WaitTime) && current.hasResult &&
      !elapsed(millis(), current.resultAt, Config::RESULT_HOLD_MS)) {
    current.sending = state;
    publishStatus();
    return; // Keep the previous result/age visible.
  }
  current.sending = current.lastResult = state;
  current.httpCode = httpCode;
  current.resultAt = millis();
  current.hasResult = true;
  publishStatus();
}
void refreshReading() {
  Reading value;
  if (xQueueReceive(readingQueue, &value, 0) == pdTRUE) {
    latest = value;
    haveReading = true;
    delivery.observe(latest);
  }
}
bool fresh() {
  return haveReading && !elapsed(millis(), latest.sampledAt, Config::SNAPSHOT_MAX_AGE_MS);
}
bool timeReady() {
  return time(nullptr) >= Config::MIN_TIME_EPOCH;
}
bool validWebhook(const String &url) {
  const String prefix = "https://discord.com/api/webhooks/";
  if (!url.startsWith(prefix) || url.indexOf('?') >= 0 || url.indexOf('#') >= 0) return false;
  const int slash = url.indexOf('/', prefix.length());
  if (slash <= int(prefix.length()) || url.length() - slash < 20) return false;
  for (size_t i = prefix.length(); i < size_t(slash); ++i)
    if (!isDigit(url[i])) return false;
  for (size_t i = slash + 1; i < url.length(); ++i)
    if (!isAlphaNumeric(url[i]) && url[i] != '_' && url[i] != '-') return false;
  return true;
}
uint32_t retryAfter(HTTPClient &http) {
  double seconds = http.header("Retry-After").toDouble();
  // Only parse a 429 body. Never print webhook responses or credentials.
  String body = http.getString();
  JsonDocument doc;
  if (!deserializeJson(doc, body)) {
    const double fromBody = doc["retry_after"] | 0.0;
    if (isfinite(fromBody) && fromBody > seconds) seconds = fromBody;
  }
  if (!isfinite(seconds) || seconds <= 0) seconds = 60;
  return retryMilliseconds(seconds, 0);
}
struct RequestResult {
  int code = -1;
  uint32_t waitMs = 60000;
};
RequestResult request(const String &url, const String *payload) {
  RequestResult response;
  current.net = NetState::Checking;
  publishStatus();
  IPAddress address;
  if (!WiFi.hostByName("discord.com", address)) {
    current.net = NetState::DnsError;
    current.netHttpCode = 0;
    return response;
  }
  WiFiClientSecure tls;
  tls.setCACert(discordRoots);
  tls.setHandshakeTimeout(Config::TLS_TIMEOUT_SECONDS);
  HTTPClient http;
  http.setConnectTimeout(Config::HTTP_CONNECT_MS);
  http.setTimeout(Config::HTTP_READ_MS);
  if (!http.begin(tls, url)) {
    current.net = NetState::ConnectError;
    return response;
  }
  const char *headers[] = {"Retry-After"};
  http.collectHeaders(headers, 1);
  if (payload) {
    http.addHeader("Content-Type", "application/json");
    response.code = http.POST(*payload);
  } else
    response.code = http.GET();
  if (response.code == 429) response.waitMs = retryAfter(http);
  current.netHttpCode = response.code;
  if (response.code > 0)
    current.net = NetState::Reachable;
  else {
    char tlsError[128] = {};
    const int tlsCode = tls.lastError(tlsError, sizeof(tlsError));
    current.net = classifyNetwork(response.code, tlsCode);
    Serial.printf("Network request failed: transport=%d TLS=%d\n", response.code, tlsCode);
  }
  http.end();
  return response;
}
void sendReport() {
  const String url = DISCORD_WEBHOOK_URL;
  if (!validWebhook(url)) {
    result(SendState::MissingUrl);
    retry.complete(400, millis());
    manualPending = false;
    return;
  }
  const Reading sent = latest;
  const bool isManual = manualPending;
  const char *event = delivery.event(sent, isManual);
  const char *eventIcon = !sent.valid                      ? "⚠️"
                          : isManual                       ? "📋"
                          : delivery.resync()              ? "📶"
                          : sent.state == WaterState::High ? "🚨"
                                                           : "💧";
  String message;
  message.reserve(480);
  message = String("━━━━━━━━━━━━━━━━━━━━\n") + eventIcon + " **Water Monitor — " + event + "**\n\n";
  message += "🌊 **NTU est.: ";
  if (!sent.valid)
    message += "unavailable";
  else if (sent.ntu > Config::DISPLAY_MAX_NTU)
    message += "1000+";
  else
    message += String(sent.ntu, 1);
  message += "**\n";
  if (!sent.valid)
    message += "⚠️ **Status:** Check sensor input";
  else if (!sent.ready)
    message += "⏳ **Status:** Collecting readings";
  else if (sent.view == WaterView::Recovering)
    message +=
        "🟡 **Status:** Recovering (" + String(sent.progress) + "/" + String(Config::CONFIRM_SAMPLES) + ")";
  else if (sent.state == WaterState::High)
    message += "🔴 **Status:** High turbidity estimate";
  else
    message += "🟢 **Status:** Normal range";
  message += "\n📌 **Confirmed state:** " + String(stateName(sent.state));
  message += "\n\n⚡ **Sensor voltage:** " + String(sent.sensorVolts, 3) + " V";
  message += "\n🔌 **ESP32 input:** " + String(sent.adcVolts, 3) + " V";
  message += "\n━━━━━━━━━━━━━━━━━━━━";
  JsonDocument doc;
  doc["content"] = message;
  doc["allowed_mentions"]["parse"].to<JsonArray>();
  String payload;
  serializeJson(doc, payload);
  current.sending = SendState::Sending;
  publishStatus();
  retry.attempted(millis());
  const RequestResult response = request(url + "?wait=true", &payload);
  manualPending = false;
  // Consume the newest sensor state before acknowledging a possibly older send.
  refreshReading();
  retry.complete(response.code, millis(), response.waitMs);
  Serial.printf("Discord HTTP status: %d\n", response.code);
  if (response.code == 200) {
    if (sent.valid && sent.ready) delivery.success(sent.version);
    result(SendState::Sent, 200);
  } else if (response.code == 429)
    result(SendState::RateLimited, 429);
  else if (retry.blocked())
    result(SendState::Blocked, response.code);
  else
    result(SendState::Retry, response.code);
  if (isManual && response.code != 200 && !retry.blocked()) manualPending = true;
  if (response.code < 0)
    delivery.online(false);
  else
    delivery.online(true);
  healthAt = millis();
  healthChecked = true;
}
void updateWifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasWifi) {
    Serial.println(connected ? "Wi-Fi connected" : "Wi-Fi disconnected");
    current.net = NetState::NotChecked;
    if (!connected) {
      delivery.online(false);
      current.ip[0] = '\0';
    }
    healthChecked = false;
    wasWifi = connected;
  }
  if (connected) {
    current.wifi = portalOpen ? WifiState::Setup : WifiState::Connected;
    snprintf(current.ip, sizeof(current.ip), "%s", WiFi.localIP().toString().c_str());
    snprintf(current.ssid, sizeof(current.ssid), "%s", WiFi.SSID().c_str());
    current.rssi = WiFi.RSSI();
  } else {
    current.wifi = portalOpen                                                ? WifiState::Setup
                   : elapsed(millis(), wifiStarted, Config::WIFI_CONNECT_MS) ? WifiState::Offline
                                                                             : WifiState::Connecting;
  }
}
void task(void *) {
  // All WiFiManager and network objects are owned exclusively by this task.
  WiFiManager manager;
  manager.setDebugOutput(false);
  manager.setConfigPortalBlocking(false);
  manager.setConnectTimeout(15);
  manager.setSaveConnectTimeout(15);
  manager.setPreSaveConfigCallback([]() {
    current.wifi = WifiState::Connecting;
    publishStatus();
  });
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  wifiStarted = millis();
  for (;;) {
    refreshReading();
    Command commandValue;
    while (xQueueReceive(commandQueue, &commandValue, 0) == pdTRUE) {
      if (commandValue == Command::SetupWifi && !portalOpen) {
        ++current.portalGeneration;
        portalStarted = millis();
        current.wifi = WifiState::Setup;
        current.portalOpen = true;
        publishStatus();
        manager.startConfigPortal(Config::AP_NAME, Config::AP_PASSWORD);
        portalOpen = manager.getConfigPortalActive();
      } else if (commandValue == Command::SendReport) {
        if (manualSeen && !elapsed(millis(), lastManual, Config::MANUAL_INTERVAL_MS)) {
          Serial.println("Manual report: wait 10 seconds between commands.");
        } else {
          manualSeen = true;
          lastManual = millis();
          retry.manualRetry();
          if (portalOpen)
            result(SendState::WaitWifi);
          else if (WiFi.status() != WL_CONNECTED)
            result(SendState::WaitWifi);
          else if (!timeReady())
            result(SendState::WaitTime);
          else
            manualPending = true;
        }
      }
    }
    if (portalOpen) {
      manager.process();
      portalOpen = manager.getConfigPortalActive();
      if (portalOpen && elapsed(millis(), portalStarted, Config::PORTAL_MS)) {
        manager.stopConfigPortal();
        portalOpen = false;
      }
      refreshReading(); // Portal saves may block while testing credentials.
    }
    updateWifi();
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !portalOpen && !timeReady()) current.net = NetState::WaitTime;
    const bool canNetwork = networkReady(connected, portalOpen, time(nullptr));
    const bool eligible = manualPending || (delivery.pending() && latest.valid && latest.ready);
    const bool resultHeld = current.hasResult && !elapsed(millis(), current.resultAt, Config::RESULT_HOLD_MS);
    if (eligible && !resultHeld) {
      if (!connected || portalOpen)
        current.sending = SendState::WaitWifi;
      else if (!timeReady())
        current.sending = SendState::WaitTime;
      else if (!retry.blocked() && retry.canAttempt(millis()) && fresh()) {
        sendReport();
      } else if (!retry.blocked() && current.sending != SendState::RateLimited &&
                 current.sending != SendState::Retry)
        current.sending = SendState::Pending;
    }
    // A verified response from Discord demonstrates reachability, not general
    // internet availability. Webhook authorization errors remain separate.
    if (canNetwork && (!healthChecked || elapsed(millis(), healthAt, Config::HEALTH_INTERVAL_MS)) &&
        current.sending != SendState::Sending && retry.remaining(millis()) == 0) {
      const RequestResult response = request(Config::HEALTH_URL, nullptr);
      healthChecked = true;
      healthAt = millis();
      if (response.code == 200)
        delivery.online(true);
      else {
        if (response.code > 0) current.net = NetState::HttpError;
        delivery.online(false);
        if (response.code == 429) retry.complete(429, millis(), response.waitMs);
      }
    }
    publishStatus();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
} // namespace
bool begin() {
  readingQueue = xQueueCreate(1, sizeof(Monitor::Reading));
  statusQueue = xQueueCreate(1, sizeof(Monitor::NetworkStatus));
  commandQueue = xQueueCreate(4, sizeof(Monitor::Command));
  if (!readingQueue || !statusQueue || !commandQueue) return false;
  taskAvailable = xTaskCreatePinnedToCore(task, "water-network", Config::NETWORK_STACK_BYTES, nullptr, 1,
                                          nullptr, 0) == pdPASS;
  return taskAvailable;
}
void publish(const Monitor::Reading &reading) {
  if (taskAvailable) xQueueOverwrite(readingQueue, &reading);
}
bool command(Monitor::Command commandValue) {
  return taskAvailable && xQueueSend(commandQueue, &commandValue, 0) == pdTRUE;
}
bool status(Monitor::NetworkStatus &value) {
  return taskAvailable && xQueuePeek(statusQueue, &value, 0) == pdTRUE;
}
} // namespace NetworkService
