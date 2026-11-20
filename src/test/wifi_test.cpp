#include <Arduino.h>
#include <SPI.h>
#include <Ucglib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "Config.h"

namespace {
constexpr uint8_t BUTTON_PIN = 0; // BOOT, active LOW. EN is reset only.
constexpr uint32_t HOLD_MS = 3000;
constexpr uint32_t PORTAL_MS = 180000;
constexpr char AP_NAME[] = "WaterMonitor-Setup";
constexpr char AP_PASSWORD[] = "water1234"; // Local setup AP only; change if desired.
Ucglib_ST7735_18x128x160_HWSPI screen(Config::LCD_DC, Config::LCD_CS, Config::LCD_RESET);
WiFiManager manager;
uint32_t pressStarted = 0, portalStarted = 0, connectStarted = 0;
bool pressed = false, handled = false, portalOpen = false;
String previousView;
String previousSerialView;
uint32_t lastScreenUpdate = 0;

void show(const String &title, const String &a = "", const String &b = "", const String &c = "",
          const String &d = "") {
  const String view = title + "|" + a + "|" + b + "|" + c + "|" + d;
  // RSSI belongs on the LCD; log Wi-Fi connection changes only.
  const String serialView = title == "WI-FI CONNECTED" ? title + "|" + a + "|" + b : view;
  if (serialView != previousSerialView) {
    Serial.println(serialView);
    previousSerialView = serialView;
  }
  if (view == previousView) return;
  if (title == "WI-FI CONNECTED" && millis() - lastScreenUpdate < 2000) return;
  lastScreenUpdate = millis();
  previousView = view;
  screen.clearScreen();
  screen.setColor(0, 220, 255);
  screen.setPrintPos(4, 16);
  screen.print(title);
  screen.setColor(255, 255, 255);
  const String rows[] = {a, b, c, d};
  for (uint8_t i = 0; i < 4; ++i) {
    screen.setPrintPos(4, 40 + i * 23);
    screen.print(rows[i]);
  }
}

void openPortal() {
  if (portalOpen) return;
  portalStarted = millis();
  manager.startConfigPortal(AP_NAME, AP_PASSWORD);
  portalOpen = manager.getConfigPortalActive();
  if (!portalOpen) show("SETUP FAILED", "Hold BOOT to retry");
}
} // namespace

#ifdef DISCORD_TEST
#include "DiscordTest.h"
#endif

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  SPI.begin(Config::LCD_SCK, -1, Config::LCD_MOSI, Config::LCD_CS);
  screen.begin(UCG_FONT_MODE_TRANSPARENT);
  screen.setRotate90();
  screen.setFont(ucg_font_6x10_tr);
  manager.setDebugOutput(false); // Keep submitted credentials out of debug output.
  manager.setConfigPortalBlocking(false);
  manager.setConnectTimeout(15);
  manager.setSaveConnectTimeout(15);
  manager.setPreSaveConfigCallback([]() { show("CONNECTING...", "Trying saved network", "Please wait..."); });
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(); // Use credentials already saved in flash; no hardcoded SSID/password.
#ifdef DISCORD_TEST
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.println("Discord test: type s to send one message. No automatic sends.");
#endif
  connectStarted = millis();
  show("CONNECTING...", "Using saved Wi-Fi", "Hold BOOT 3s to setup");
}

void loop() {
  const uint32_t now = millis();
  const bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !pressed) {
    pressed = true;
    handled = false;
    pressStarted = now;
  }
  if (!down) {
    pressed = false;
    handled = false;
  }
  if (pressed && !handled && now - pressStarted >= HOLD_MS) {
    handled = true;
    openPortal(); // Does not erase credentials.
  }

  if (portalOpen) {
    manager.process(); // Portal remains usable while loop updates screen/button.
    portalOpen = manager.getConfigPortalActive();
    if (portalOpen && millis() - portalStarted >= PORTAL_MS) {
      manager.stopConfigPortal();
      portalOpen = false;
      connectStarted = millis() - 20000;
    }
  }

#ifdef DISCORD_TEST
  discordPoll();
  if (!portalOpen && discordResultActive()) {
    delay(20);
    return;
  }
#endif

  if (portalOpen) {
    show("WI-FI SETUP", AP_NAME, String("Pass: ") + AP_PASSWORD, "Open 192.168.4.1", "Save Wi-Fi (2.4GHz)");
  } else if (WiFi.status() == WL_CONNECTED) {
    show("WI-FI CONNECTED", WiFi.SSID().substring(0, 25), String("IP: ") + WiFi.localIP().toString(),
         String("Signal: ") + WiFi.RSSI() + " dBm", "Hold BOOT 3s: setup");
  } else if (now - connectStarted < 20000) {
    show("CONNECTING...", "Using saved Wi-Fi", "Hold BOOT 3s to setup");
  } else {
    show("WI-FI OFFLINE", "No network / lost link", "Auto reconnect enabled", "Hold BOOT 3s: setup");
  }
  delay(20);
}
