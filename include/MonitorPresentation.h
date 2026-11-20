#pragma once
#include <stdio.h>
#include <string.h>
#include "MonitorTypes.h"
namespace Monitor {
enum class Tone : uint8_t { White, Green, Yellow, Red, Cyan };
constexpr uint8_t FRAME_ROWS = 9;
constexpr uint8_t ROW_Y[FRAME_ROWS] = {0, 12, 40, 52, 64, 76, 88, 100, 114};
constexpr uint8_t ROW_H[FRAME_ROWS] = {12, 28, 12, 12, 12, 12, 12, 14, 14};
constexpr uint8_t ROW_BASELINE[FRAME_ROWS] = {9, 35, 49, 61, 73, 85, 97, 110, 124};
struct Frame {
  char text[FRAME_ROWS][27] = {};
  Tone tone[FRAME_ROWS] = {};
};
inline const char *sendName(SendState s) {
  switch (s) {
  case SendState::Pending: return "PENDING";
  case SendState::Sending: return "SENDING";
  case SendState::Sent: return "SENT";
  case SendState::WaitWifi: return "WAIT WIFI";
  case SendState::WaitTime: return "WAIT NTP";
  case SendState::MissingUrl: return "SET URL";
  case SendState::RateLimited: return "RATE";
  case SendState::Retry: return "RETRY";
  case SendState::Blocked: return "BLOCKED";
  case SendState::Failed: return "FAILED";
  default: return "NOT SENT";
  }
}
inline Tone sendTone(SendState s) {
  if (s == SendState::Sent) return Tone::Green;
  if (s == SendState::Blocked || s == SendState::Failed || s == SendState::MissingUrl ||
      s == SendState::Retry)
    return Tone::Red;
  return Tone::Yellow;
}
inline void formatNtu(char *out, size_t capacity, const Reading &r) {
  if (!r.valid)
    snprintf(out, capacity, "--");
  else if (r.ntu > Config::DISPLAY_MAX_NTU)
    snprintf(out, capacity, "1000+");
  else
    snprintf(out, capacity, "%.1f", r.ntu);
}
// Stabilize only the headline; alerts and Discord always use the real filtered NTU.
class DisplayStabilizer {
  bool initialized_ = false;
  float shown_ = 0;
  uint32_t version_ = 0;
  int8_t direction_ = 0;
  uint8_t confirmations_ = 0;

public:
  Reading update(const Reading &input) {
    Reading output = input;
    if (!input.valid || !isfinite(input.ntu)) {
      initialized_ = false;
      confirmations_ = 0;
      direction_ = 0;
      return output;
    }
    const bool rangeChanged =
        initialized_ && ((shown_ > Config::DISPLAY_MAX_NTU) != (input.ntu > Config::DISPLAY_MAX_NTU));
    if (!initialized_ || input.version != version_ || rangeChanged) {
      shown_ = input.ntu;
      initialized_ = true;
      confirmations_ = 0;
      direction_ = 0;
    } else {
      const float delta = input.ntu - shown_;
      if (fabsf(delta) < Config::DISPLAY_NTU_DEADBAND) {
        confirmations_ = 0;
        direction_ = 0;
      } else {
        const int8_t direction = delta > 0 ? 1 : -1;
        if (direction != direction_) {
          direction_ = direction;
          confirmations_ = 1;
        } else if (confirmations_ < Config::DISPLAY_CONFIRM_SAMPLES)
          ++confirmations_;
        if (confirmations_ >= Config::DISPLAY_CONFIRM_SAMPLES) {
          shown_ = input.ntu;
          confirmations_ = 0;
          direction_ = 0;
        }
      }
    }
    version_ = input.version;
    output.ntu = shown_;
    return output;
  }
};
inline Frame buildFrame(const Reading &r, const NetworkStatus &n, uint32_t now, uint32_t holdMs = 0,
                        bool setupQueued = false, bool networkTaskOk = true) {
  Frame f;
  for (auto &tone : f.tone)
    tone = Tone::White;
  snprintf(f.text[0], 27, n.portalOpen ? "WI-FI SETUP / NTU est." : "WATER MONITOR / NTU est.");
  f.tone[0] = Tone::Cyan;
  formatNtu(f.text[1], 27, r);
  f.tone[1] = !r.valid                      ? Tone::Red
              : !r.ready                    ? Tone::Yellow
              : r.state == WaterState::High ? Tone::Red
                                            : Tone::Green;
  if (isfinite(r.sensorVolts) && isfinite(r.adcVolts))
    snprintf(f.text[2], 27, "Sensor:%.3fV ADC:%.3fV", r.sensorVolts, r.adcVolts);
  else
    snprintf(f.text[2], 27, "Sensor:-- ADC:--");
  snprintf(f.text[3], 27, "Raw:%.0f Supply:%.1fV", r.raw, Config::SENSOR_SUPPLY_VOLTS);
  if (r.view == WaterView::Starting)
    snprintf(f.text[4], 27, "STARTING %u/%u", r.progress, Config::CONFIRM_SAMPLES);
  else if (r.view == WaterView::Recovering)
    snprintf(f.text[4], 27, "RECOVERING %u/%u", r.progress, Config::CONFIRM_SAMPLES);
  else if (r.view == WaterView::Normal && r.progress)
    snprintf(f.text[4], 27, "NORMAL HIGH? %u/%u", r.progress, Config::CONFIRM_SAMPLES);
  else
    snprintf(f.text[4], 27, "%s", viewName(r.view));
  f.tone[4] = r.view == WaterView::CheckInput || r.view == WaterView::High ? Tone::Red
              : r.view != WaterView::Normal || r.progress                  ? Tone::Yellow
                                                                           : Tone::Green;
  switch (n.wifi) {
  case WifiState::Connected:
    snprintf(f.text[5], 27, "WiFi:OK %s", n.ip);
    f.tone[5] = Tone::Green;
    break;
  case WifiState::Offline:
    snprintf(f.text[5], 27, "WiFi:OFFLINE / reconnect");
    f.tone[5] = Tone::Red;
    break;
  case WifiState::Setup:
    snprintf(f.text[5], 27, "WiFi:SETUP");
    f.tone[5] = Tone::Yellow;
    break;
  default: snprintf(f.text[5], 27, "WiFi:CONNECTING"); f.tone[5] = Tone::Yellow;
  }
  const char *net = "NOT CHECKED";
  f.tone[6] = Tone::Yellow;
  switch (n.net) {
  case NetState::WaitTime: net = "WAIT NTP TIME"; break;
  case NetState::Checking: net = "CHECKING HTTPS"; break;
  case NetState::Reachable:
    net = "DISCORD REACHABLE";
    f.tone[6] = Tone::Green;
    break;
  case NetState::DnsError:
    net = "DNS ERROR";
    f.tone[6] = Tone::Red;
    break;
  case NetState::TlsError:
    net = "TLS ERROR";
    f.tone[6] = Tone::Red;
    break;
  case NetState::Timeout:
    net = "TIMEOUT";
    f.tone[6] = Tone::Red;
    break;
  case NetState::ConnectError:
    net = "CONNECT ERROR";
    f.tone[6] = Tone::Red;
    break;
  case NetState::HttpError:
    net = "SERVICE ERROR";
    f.tone[6] = Tone::Red;
    break;
  default: break;
  }
  snprintf(f.text[6], 27, "Net:%s", net);
  f.tone[7] = sendTone(n.sending);
  if (n.hasResult) {
    const uint32_t ageSeconds = uint32_t(now - n.resultAt) / 1000;
    char last[10];
    if (n.httpCode)
      snprintf(last, sizeof(last), "L:%d", n.httpCode);
    else
      snprintf(last, sizeof(last), "L:%s",
               n.lastResult == SendState::MissingUrl ? "URL"
               : n.lastResult == SendState::WaitTime ? "NTP"
                                                     : "WIFI");
    if (ageSeconds < 3600)
      snprintf(f.text[7], 27, "DC:%s %s %lus", sendName(n.sending), last, (unsigned long)ageSeconds);
    else
      snprintf(f.text[7], 27, "DC:%s %s %luh", sendName(n.sending), last, (unsigned long)(ageSeconds / 3600));
  } else
    snprintf(f.text[7], 27, "DC:%s", sendName(n.sending));
  snprintf(f.text[8], 27, "BOOT 3s WiFi | s report");
  if (holdMs) snprintf(f.text[8], 27, "BOOT hold %lu/3s", (unsigned long)(holdMs / 1000));
  if (setupQueued) snprintf(f.text[8], 27, "WiFi setup queued...");
  if (n.retrySeconds) snprintf(f.text[8], 27, "Retry %lus | BOOT 3s WiFi", (unsigned long)n.retrySeconds);
  if (n.portalOpen) {
    snprintf(f.text[3], 27, "AP:%s", Config::AP_NAME);
    snprintf(f.text[4], 27, "Pass:%s", Config::AP_PASSWORD);
    f.tone[4] = Tone::Yellow;
    snprintf(f.text[5], 27, "Open http://192.168.4.1");
    f.tone[5] = Tone::Cyan;
    snprintf(f.text[6], 27,
             n.wifi == WifiState::Connecting ? "Connecting... please wait" : "Save 2.4GHz Wi-Fi (3 min)");
    f.tone[6] = Tone::Yellow;
    snprintf(f.text[8], 27, "Sensor sampling continues");
  }
  if (!networkTaskOk) {
    snprintf(f.text[5], 27, "WiFi:TASK START FAILED");
    f.tone[5] = Tone::Red;
    snprintf(f.text[6], 27, "Net:UNAVAILABLE");
    f.tone[6] = Tone::Red;
    snprintf(f.text[7], 27, "DC:UNAVAILABLE");
    f.tone[7] = Tone::Red;
    snprintf(f.text[8], 27, "Sensor/LCD still running");
  }
  return f;
}
} // namespace Monitor
