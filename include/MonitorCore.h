#pragma once
#include <math.h>
#include <stdint.h>
#include "Config.h"

namespace Monitor {
enum class WaterState : uint8_t { Unknown, Normal, High };
enum class WaterView : uint8_t { Starting, Normal, High, Recovering, CheckInput };
struct Reading {
  uint32_t sampledAt = 0, version = 0;
  float raw = 0, adcVolts = 0, sensorVolts = 0, filteredVolts = 0, ntu = 0;
  bool valid = false, ready = false;
  WaterState state = WaterState::Unknown;
  WaterView view = WaterView::Starting;
  uint8_t progress = 0;
};
inline float estimateNtu(float sensorVolts) {
  const float v = sensorVolts * (Config::CURVE_ZERO_VOLTS / Config::WATER_REFERENCE_VOLTS);
  // The quadratic is only used on its decreasing branch. Low voltage saturates
  // above the advertised range, rather than curving back down to a false zero.
  if (v < Config::CURVE_MIN_VOLTS) return 3000.0f;
  if (v >= Config::CURVE_ZERO_VOLTS) return 0;
  const float ntu = Config::CURVE_A * v * v + Config::CURVE_B * v + Config::CURVE_C;
  return ntu > 0 ? ntu : 0;
}
inline bool inputValid(float raw, float volts) {
  return isfinite(raw) && isfinite(volts) && raw >= 0 && raw < Config::ADC_RAW_LIMIT &&
         volts >= Config::ADC_MIN_VOLTS && volts < Config::ADC_MAX_VOLTS;
}
inline bool aboveAlert(float ntu) {
  return ntu > Config::ALERT_NTU;
}
inline bool belowRecovery(float ntu) {
  return ntu < Config::RECOVERY_NTU;
}
class MedianFilter {
  float values_[Config::MEDIAN_WINDOW] = {};
  uint8_t count_ = 0, next_ = 0;

public:
  void reset() { count_ = next_ = 0; }
  float add(float value) {
    values_[next_] = value;
    next_ = (next_ + 1) % Config::MEDIAN_WINDOW;
    if (count_ < Config::MEDIAN_WINDOW) ++count_;
    float sorted[Config::MEDIAN_WINDOW];
    for (uint8_t i = 0; i < count_; ++i)
      sorted[i] = values_[i];
    for (uint8_t i = 1; i < count_; ++i) {
      const float key = sorted[i];
      uint8_t j = i;
      while (j && sorted[j - 1] > key) {
        sorted[j] = sorted[j - 1];
        --j;
      }
      sorted[j] = key;
    }
    return count_ % 2 ? sorted[count_ / 2] : (sorted[count_ / 2 - 1] + sorted[count_ / 2]) * 0.5f;
  }
};
class WaterEngine {
  MedianFilter median_;
  Reading reading_;
  WaterState stable_ = WaterState::Unknown;
  uint8_t validRun_ = 0, highRun_ = 0, lowRun_ = 0;
  bool filtered_ = false, allInitialHigh_ = true;

public:
  Reading sample(float raw, float adcVolts, uint32_t now) {
    reading_.sampledAt = now;
    reading_.raw = raw;
    reading_.adcVolts = adcVolts;
    reading_.sensorVolts = adcVolts * Config::DIVIDER_RATIO;
    reading_.valid = inputValid(raw, adcVolts);
    if (!reading_.valid) {
      median_.reset();
      filtered_ = false;
      validRun_ = highRun_ = lowRun_ = 0;
      allInitialHigh_ = true;
      reading_.ready = false;
      reading_.view = WaterView::CheckInput;
      reading_.progress = 0;
      reading_.ntu = 0;
      reading_.filteredVolts = 0;
      // Retain the latched state across a fault; invalid input cannot recover HIGH.
      return reading_;
    }
    const float medianVolts = median_.add(reading_.sensorVolts);
    reading_.filteredVolts =
        filtered_ ? Config::EMA_ALPHA * medianVolts + (1 - Config::EMA_ALPHA) * reading_.filteredVolts
                  : medianVolts;
    filtered_ = true;
    reading_.ntu = estimateNtu(reading_.filteredVolts);
    return evaluate(now);
  }

private:
  Reading evaluate(uint32_t) {
    const bool high = aboveAlert(reading_.ntu);
    const bool low = belowRecovery(reading_.ntu);
    highRun_ = high ? uint8_t(highRun_ < Config::CONFIRM_SAMPLES ? highRun_ + 1 : highRun_) : 0;
    lowRun_ = low ? uint8_t(lowRun_ < Config::CONFIRM_SAMPLES ? lowRun_ + 1 : lowRun_) : 0;
    if (validRun_ < Config::CONFIRM_SAMPLES) {
      ++validRun_;
      allInitialHigh_ = allInitialHigh_ && high;
    }
    if (validRun_ < Config::CONFIRM_SAMPLES) {
      reading_.ready = false;
      reading_.view = WaterView::Starting;
      reading_.progress = validRun_;
      return reading_;
    }
    WaterState next = stable_;
    if (stable_ == WaterState::Unknown)
      next = allInitialHigh_ ? WaterState::High : WaterState::Normal;
    else if (stable_ == WaterState::Normal && highRun_ >= Config::CONFIRM_SAMPLES)
      next = WaterState::High;
    else if (stable_ == WaterState::High && lowRun_ >= Config::CONFIRM_SAMPLES)
      next = WaterState::Normal;
    if (next != stable_) {
      stable_ = next;
      ++reading_.version;
    }
    reading_.state = stable_;
    reading_.ready = true;
    reading_.view =
        stable_ == WaterState::High ? (lowRun_ ? WaterView::Recovering : WaterView::High) : WaterView::Normal;
    reading_.progress = stable_ == WaterState::High ? lowRun_ : highRun_;
    return reading_;
  }
};
inline const char *stateName(WaterState state) {
  return state == WaterState::High ? "HIGH" : state == WaterState::Normal ? "NORMAL" : "STARTING";
}
inline const char *viewName(WaterView view) {
  switch (view) {
  case WaterView::Starting: return "STARTING";
  case WaterView::Normal: return "NORMAL";
  case WaterView::High: return "HIGH";
  case WaterView::Recovering: return "RECOVERING";
  default: return "CHECK INPUT";
  }
}
inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}
inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}

// Delivery policy is independent of Wi-Fi/HTTP so the production rules can be
// exercised on a host with simulated time and transport responses.
class DeliveryPolicy {
  uint32_t latestVersion_ = 0, deliveredVersion_ = 0;
  bool observed_ = false, online_ = false, everOnline_ = false, gap_ = false;
  bool pending_ = false, resync_ = false, everSent_ = false;

public:
  void observe(const Reading &r) {
    if (!r.valid || !r.ready) return;
    if (!observed_ || latestVersion_ != r.version) {
      observed_ = true;
      latestVersion_ = r.version;
      pending_ = !everSent_ || deliveredVersion_ != latestVersion_;
    }
  }
  void online(bool value) {
    if (!value && online_) gap_ = true;
    if (value && !online_ && everOnline_ && gap_) {
      resync_ = true;
      pending_ = true;
      gap_ = false;
    }
    online_ = value;
    if (value) everOnline_ = true;
  }
  bool pending() const { return pending_; }
  bool resync() const { return resync_; }
  const char *event(const Reading &r, bool manual) const {
    if (manual) return "MANUAL REPORT";
    if (resync_ || gap_) return "RECONNECTED / CURRENT STATUS";
    if (r.state == WaterState::High) return "HIGH ALERT";
    return everSent_ ? "RECOVERED" : "INITIAL STATUS";
  }
  void success(uint32_t version) {
    deliveredVersion_ = version;
    everSent_ = true;
    resync_ = false;
    gap_ = false;
    online_ = everOnline_ = true; // The successful POST itself proves reconnection.
    pending_ = observed_ && latestVersion_ != deliveredVersion_;
  }
};
inline uint32_t retryMilliseconds(double headerSeconds, double bodySeconds) {
  double seconds = isfinite(headerSeconds) && headerSeconds > 0 ? headerSeconds : 0;
  if (isfinite(bodySeconds) && bodySeconds > seconds) seconds = bodySeconds;
  if (seconds <= 0) seconds = 60;
  const double milliseconds = ceil(seconds * 1000.0) + 250;
  return milliseconds > 2147479552.0 ? 0x7ffff000u : uint32_t(milliseconds);
}
class RetryPolicy {
  uint8_t failures_ = 0;
  bool waiting_ = false, blocked_ = false, attempted_ = false;
  uint32_t due_ = 0, lastAttempt_ = 0;

public:
  bool canAttempt(uint32_t now) const {
    return !blocked_ && (!waiting_ || deadlineReached(now, due_)) &&
           (!attempted_ || elapsed(now, lastAttempt_, Config::MANUAL_INTERVAL_MS));
  }
  void attempted(uint32_t now) {
    attempted_ = true;
    lastAttempt_ = now;
  }
  void manualRetry() { blocked_ = false; } // Never bypass a rate-limit/backoff deadline.
  void complete(int status, uint32_t now, uint32_t retryAfterMs = 60000) {
    if (status == 200) {
      failures_ = 0;
      waiting_ = blocked_ = false;
      return;
    }
    if (status == 401 || status == 403 || status == 404 ||
        (status >= 400 && status < 500 && status != 408 && status != 429)) {
      blocked_ = true;
      waiting_ = false;
      return;
    }
    uint32_t delayMs;
    if (status == 429)
      delayMs = retryAfterMs;
    else {
      if (failures_ < 3) ++failures_;
      delayMs = failures_ == 1   ? Config::RETRY_FIRST_MS
                : failures_ == 2 ? Config::RETRY_SECOND_MS
                                 : Config::RETRY_LATER_MS;
    }
    if (delayMs < 1000) delayMs = 1000;
    if (delayMs > 0x7ffff000u) delayMs = 0x7ffff000u;
    due_ = now + delayMs;
    waiting_ = true;
  }
  bool blocked() const { return blocked_; }
  uint32_t remaining(uint32_t now) const { return waiting_ && !deadlineReached(now, due_) ? due_ - now : 0; }
};
} // namespace Monitor
