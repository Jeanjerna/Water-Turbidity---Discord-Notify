#pragma once
#include <stdint.h>
#include "MonitorCore.h"
namespace Monitor {
enum class WifiState : uint8_t { Connecting, Connected, Offline, Setup };
enum class NetState : uint8_t {
  NotChecked,
  WaitTime,
  Checking,
  Reachable,
  DnsError,
  TlsError,
  Timeout,
  ConnectError,
  HttpError
};
inline NetState classifyNetwork(int httpCode, int tlsCode, bool dnsOk = true) {
  if (!dnsOk) return NetState::DnsError;
  if (httpCode > 0) return NetState::Reachable;
  if (httpCode == -11 || tlsCode == -0x6800) return NetState::Timeout;
  if (tlsCode) return NetState::TlsError;
  return NetState::ConnectError;
}
inline bool networkReady(bool wifiConnected, bool portalOpen, int64_t epoch) {
  return wifiConnected && !portalOpen && epoch >= Config::MIN_TIME_EPOCH;
}
enum class SendState : uint8_t {
  NotSent,
  Pending,
  Sending,
  Sent,
  WaitWifi,
  WaitTime,
  MissingUrl,
  RateLimited,
  Retry,
  Blocked,
  Failed
};
enum class Command : uint8_t { SetupWifi, SendReport };
struct NetworkStatus {
  WifiState wifi = WifiState::Connecting;
  NetState net = NetState::NotChecked;
  SendState sending = SendState::NotSent;
  SendState lastResult = SendState::NotSent;
  uint32_t resultAt = 0, retrySeconds = 0;
  bool hasResult = false, portalOpen = false;
  int httpCode = 0, netHttpCode = 0, rssi = 0;
  uint32_t portalGeneration = 0;
  char ip[16] = "--", ssid[33] = "";
};
} // namespace Monitor
