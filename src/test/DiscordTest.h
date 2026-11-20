#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef DISCORD_WEBHOOK_URL
#define DISCORD_WEBHOOK_URL ""
#endif

extern const char discordRoots[] asm("_binary_certs_discord_roots_pem_start");
uint32_t discordResultStarted = 0, discordLastAttempt = 0;
bool discordHasResult = false, discordAttempted = false;

bool discordResultActive() {
  return discordHasResult && millis() - discordResultStarted < 7000;
}

void discordResult(const String &title, const String &detail) {
  show(title, detail, "Serial: s = send test", "BOOT 3s: Wi-Fi setup");
  discordResultStarted = millis();
  discordHasResult = true;
}

void discordPoll() {
  bool requested = false;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 's' || c == 'S') requested = true;
  }
  if (!requested) return;
  if (portalOpen) {
    discordResult("CLOSE WI-FI SETUP", "Finish Wi-Fi first");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    discordResult("WI-FI OFFLINE", "Connect Wi-Fi first");
    return;
  }
  String url = DISCORD_WEBHOOK_URL;
  const String prefix = "https://discord.com/api/webhooks/";
  if (!url.startsWith(prefix) || url.length() < prefix.length() + 25 || url.indexOf('?') >= 0 ||
      url.indexOf('#') >= 0) {
    discordResult("SET WEBHOOK URL", "include/secrets.h");
    return;
  }
  if (time(nullptr) < 1700000000) {
    discordResult("WAIT FOR TIME", "NTP syncing; try again");
    return;
  }
  if (discordAttempted && millis() - discordLastAttempt < 10000) {
    discordResult("PLEASE WAIT", "10s between attempts");
    return;
  }
  Serial.printf("TLS verification time (UTC epoch): %lld\n", static_cast<long long>(time(nullptr)));
  discordAttempted = true;
  discordLastAttempt = millis();
  show("SENDING DISCORD...", "HTTPS: please wait");
  WiFiClientSecure tls;
  tls.setCACert(discordRoots);
  tls.setHandshakeTimeout(15);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  // wait=true asks Discord to confirm message creation.
  if (!http.begin(tls, url + "?wait=true")) {
    discordResult("HTTPS FAILED", "Could not start client");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST("{\"content\":\"ESP32 Water Monitor: Discord test OK! This is a connection "
                               "test, not a sensor reading.\",\"allowed_mentions\":{\"parse\":[]}}");
  Serial.printf("Discord HTTP status: %d\n", status);
  // Never log the URL, token, or response body (may contain webhook details).
  if (status == 200)
    discordResult("DISCORD SENT!", "Check your channel");
  else if (status == 429)
    discordResult("RATE LIMITED", "Wait before retrying");
  else if (status == 401 || status == 403 || status == 404)
    discordResult("WEBHOOK ERROR", "Check URL / permissions");
  else if (status < 0) {
    Serial.println(HTTPClient::errorToString(status));
    discordResult("NETWORK/TLS ERROR", "Check internet & time");
  } else
    discordResult("SEND FAILED", String("HTTP: ") + status);
  http.end();
}
