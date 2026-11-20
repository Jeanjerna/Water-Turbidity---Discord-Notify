#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "MonitorCore.h"
#include "MonitorPresentation.h"
using namespace Monitor;
static unsigned checks = 0;
#define CHECK(x)                                                                                             \
  do {                                                                                                       \
    ++checks;                                                                                                \
    if (!(x)) {                                                                                              \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                                   \
      return 1;                                                                                              \
    }                                                                                                        \
  } while (0)
static float adcFor(float ntu) {
  const double a = Config::CURVE_A, b = Config::CURVE_B, c = Config::CURVE_C - ntu;
  return float((-b - sqrt(b * b - 4 * a * c)) / (2 * a) * Config::WATER_REFERENCE_VOLTS /
               Config::CURVE_ZERO_VOLTS / Config::DIVIDER_RATIO);
}
static Reading feed(WaterEngine &engine, float adc, unsigned count, uint32_t &t) {
  Reading r;
  while (count--) {
    t += 1000;
    r = engine.sample(900, adc, t);
  }
  return r;
}
static Reading readyReading(WaterState state, uint32_t version = 1) {
  Reading r;
  r.valid = r.ready = true;
  r.state = state;
  r.version = version;
  r.view = state == WaterState::High ? WaterView::High : WaterView::Normal;
  r.raw = 913;
  r.adcVolts = .881f;
  r.sensorVolts = r.filteredVolts = 1.762f;
  r.ntu = state == WaterState::High ? 150 : 0;
  return r;
}
int main() {
  CHECK(fabs(estimateNtu(Config::WATER_REFERENCE_VOLTS)) < 0.01f);
  CHECK(fabs(estimateNtu(adcFor(100) * 2) - 100) < 0.02f);
  CHECK(estimateNtu(adcFor(100) * 2 + .00002f) < 100);
  CHECK(estimateNtu(adcFor(100) * 2 - .00002f) > 100);
  CHECK(estimateNtu(4.5f) == 0);
  CHECK(fabs(estimateNtu(4.0f * Config::WATER_REFERENCE_VOLTS / Config::CURVE_ZERO_VOLTS) - 689.9f) < 0.02f);
  CHECK(estimateNtu(1.762f) == 0); // The fixed clear-water reference maps to zero NTU.
  CHECK(estimateNtu(.31f) > 1000); // never fold the quadratic back down at low input
  CHECK(!inputValid(900, .149f));
  CHECK(inputValid(900, .15f));
  CHECK(!inputValid(4000, .8f));
  CHECK(!inputValid(900, 3.0f));
  CHECK(inputValid(3500, 1.65f)); // Nominal maximum: 3.3V sensor through 1:1 divider.
  CHECK(inputValid(3500, 1.749f));
  CHECK(!inputValid(3500, 1.75f));
  CHECK(!inputValid(3500, 1.751f));
  CHECK(!inputValid(NAN, .8f));
  CHECK(!inputValid(900, INFINITY));
  CHECK(!aboveAlert(100) && aboveAlert(100.01f));
  CHECK(!belowRecovery(80) && belowRecovery(79.99f));
  MedianFilter median;
  CHECK(median.add(1) == 1);
  CHECK(median.add(1) == 1);
  CHECK(median.add(1) == 1);
  CHECK(median.add(9) == 1);
  CHECK(median.add(1) == 1); // isolated spike rejected
  CHECK(median.add(2) == 2);
  CHECK(median.add(2) == 2); // sustained change passes
  median.reset();
  CHECK(median.add(7) == 7);
  DisplayStabilizer display;
  Reading screen = readyReading(WaterState::Normal);
  screen.ntu = 20;
  CHECK(display.update(screen).ntu == 20);
  for (float jitter : {21.f, 19.f, 24.9f, 20.2f}) {
    screen.ntu = jitter;
    CHECK(display.update(screen).ntu == 20);
  }
  screen.ntu = 30;
  CHECK(display.update(screen).ntu == 20);
  screen.ntu = 31;
  CHECK(display.update(screen).ntu == 20);
  screen.ntu = 32;
  CHECK(display.update(screen).ntu == 32);
  screen.ntu = 40;
  CHECK(display.update(screen).ntu == 32);
  screen.ntu = 20;
  CHECK(display.update(screen).ntu == 32); // direction reversal resets count
  screen.ntu = 21;
  CHECK(display.update(screen).ntu == 32);
  screen.ntu = 22;
  CHECK(display.update(screen).ntu == 22);
  screen.ntu = 23;
  CHECK(display.update(screen).ntu == 22);
  screen.ntu = 30;
  CHECK(display.update(screen).ntu == 22);
  screen.ntu = 24;
  CHECK(display.update(screen).ntu == 22); // back into deadband cancels candidate
  screen.ntu = 30;
  CHECK(display.update(screen).ntu == 22);
  screen.ntu = 31;
  CHECK(display.update(screen).ntu == 22);
  screen.version = 2;
  screen.state = WaterState::High;
  screen.ntu = 102;
  CHECK(display.update(screen).ntu == 102); // confirmed alarm never hidden by display hold
  screen.valid = false;
  CHECK(!display.update(screen).valid);
  screen.valid = true;
  screen.ntu = 200;
  CHECK(display.update(screen).ntu == 200);
  screen.ntu = 1001;
  CHECK(display.update(screen).ntu == 1001);
  screen.ntu = 999;
  CHECK(display.update(screen).ntu == 999); // overflow display cannot stick
  uint32_t t = 0;
  WaterEngine spikeTest;
  feed(spikeTest, .881f, 12, t);
  auto beforeSpike = feed(spikeTest, .881f, 1, t);
  auto afterSpike = feed(spikeTest, .70f, 1, t);
  CHECK(fabs(beforeSpike.filteredVolts - afterSpike.filteredVolts) < 0.000001f);
  CHECK(afterSpike.state == WaterState::Normal && afterSpike.progress == 0);
  CHECK(fabs(feed(spikeTest, .881f, 1, t).filteredVolts - beforeSpike.filteredVolts) < 0.000001f);
  WaterEngine normal;
  Reading r = feed(normal, .881f, 9, t);
  CHECK(!r.ready);
  CHECK(r.progress == 9);
  r = feed(normal, .881f, 1, t);
  CHECK(r.ready && r.state == WaterState::Normal && r.version == 1);
  uint32_t version = r.version;
  r = feed(normal, .881f, 30, t);
  CHECK(r.version == version); // no duplicate initial events
  // Count confirmed, filtered high samples; 9 must not change the stable state.
  unsigned consecutive = 0;
  for (unsigned i = 0; i < 100; ++i) {
    r = feed(normal, adcFor(300), 1, t);
    if (r.ntu > 100)
      ++consecutive;
    else
      consecutive = 0;
    if (consecutive == 9) CHECK(r.state == WaterState::Normal);
    if (consecutive == 10) {
      CHECK(r.state == WaterState::High);
      break;
    }
  }
  CHECK(consecutive == 10 && r.version == 2);
  r = feed(normal, adcFor(90), 80, t);
  CHECK(r.state == WaterState::High); // deadband holds HIGH
  consecutive = 0;
  for (unsigned i = 0; i < 100; ++i) {
    r = feed(normal, .881f, 1, t);
    if (r.ntu < 80)
      ++consecutive;
    else
      consecutive = 0;
    if (consecutive == 9) CHECK(r.view == WaterView::Recovering && r.state == WaterState::High);
    if (consecutive == 10) {
      CHECK(r.state == WaterState::Normal);
      break;
    }
  }
  CHECK(consecutive == 10 && r.version == 3);
  // Interrupted high run must start again after the condition clears.
  bool pending = false;
  for (unsigned i = 0; i < 100; ++i) {
    r = feed(normal, adcFor(140), 1, t);
    if (r.progress == 4) {
      pending = true;
      break;
    }
  }
  CHECK(pending && r.state == WaterState::Normal);
  r = feed(normal, adcFor(0), 80, t);
  CHECK(r.progress == 0 && r.state == WaterState::Normal);
  WaterEngine startupHigh;
  r = feed(startupHigh, adcFor(150), 10, t);
  CHECK(r.state == WaterState::High && r.version == 1);
  r = startupHigh.sample(4095, 3.1f, t += 1000);
  CHECK(!r.valid && !r.ready && r.view == WaterView::CheckInput && r.state == WaterState::High);
  r = feed(startupHigh, adcFor(90), 10, t);
  CHECK(r.ready && r.state == WaterState::High && r.version == 1); // fault cannot fabricate recovery
  WaterEngine filterReset;
  feed(filterReset, .881f, 10, t);
  filterReset.sample(4000, 3, t += 1000);
  r = feed(filterReset, adcFor(300), 1, t);
  CHECK(fabs(r.filteredVolts - adcFor(300) * 2) < .000001f && !r.ready);
  // A mixed initial set is NORMAL, exactly as the chosen startup rule specifies.
  WaterEngine mixed;
  feed(mixed, .881f, 1, t);
  r = feed(mixed, adcFor(500), 9, t);
  CHECK(r.state == WaterState::Normal);

  DeliveryPolicy policy;
  Reading current = readyReading(WaterState::Normal);
  policy.observe(current);
  CHECK(policy.pending());
  CHECK(!strcmp(policy.event(current, false), "INITIAL STATUS"));
  policy.online(true);
  policy.success(1);
  CHECK(!policy.pending());
  policy.observe(current);
  CHECK(!policy.pending());
  current = readyReading(WaterState::High, 2);
  policy.observe(current);
  CHECK(policy.pending() && !strcmp(policy.event(current, false), "HIGH ALERT"));
  policy.online(false);
  current = readyReading(WaterState::Normal, 3);
  policy.observe(current);
  policy.online(true);
  CHECK(policy.resync() && !strcmp(policy.event(current, false), "RECONNECTED / CURRENT STATUS"));
  CHECK(!strcmp(policy.event(current, true), "MANUAL REPORT"));
  policy.success(3);
  CHECK(!policy.pending());
  policy.online(false);
  policy.online(true);
  CHECK(policy.pending()); // same-state reconnect summary
  policy.success(3);
  policy.observe(readyReading(WaterState::High, 4));
  policy.observe(readyReading(WaterState::Normal, 5));
  policy.success(4);
  CHECK(policy.pending()); // later state arrived while request was blocked
  policy.success(5);
  CHECK(!policy.pending());
  policy.online(false);
  CHECK(!strcmp(policy.event(readyReading(WaterState::Normal, 5), false), "RECONNECTED / CURRENT STATUS"));
  policy.success(5);
  policy.online(true);
  CHECK(!policy.pending()); // reconnecting POST must not cause a second summary
  auto invalid = readyReading(WaterState::High, 6);
  invalid.valid = false;
  policy.observe(invalid);
  CHECK(!policy.pending());

  RetryPolicy backoff;
  CHECK(backoff.canAttempt(0));
  backoff.attempted(0);
  backoff.complete(-1, 0);
  CHECK(backoff.remaining(1000) == 29000 && !backoff.canAttempt(29999));
  CHECK(backoff.canAttempt(30000));
  backoff.attempted(30000);
  backoff.complete(500, 30000);
  CHECK(backoff.remaining(30000) == 60000);
  backoff.attempted(90000);
  backoff.complete(503, 90000);
  CHECK(backoff.remaining(90000) == 300000);
  backoff.attempted(390000);
  backoff.complete(-11, 390000);
  CHECK(backoff.remaining(390000) == 300000);
  backoff.complete(200, 690000);
  CHECK(backoff.remaining(690000) == 0);
  for (int code : {401, 403, 404, 400}) {
    RetryPolicy permanent;
    permanent.complete(code, 0);
    CHECK(permanent.blocked() && !permanent.canAttempt(999999));
    permanent.manualRetry();
    CHECK(!permanent.blocked() && permanent.canAttempt(999999));
  }
  CHECK(retryMilliseconds(0, 64.57) == 64820);
  CHECK(retryMilliseconds(65, 64.57) == 65250);
  CHECK(retryMilliseconds(NAN, INFINITY) == 60250);
  CHECK(retryMilliseconds(1e20, 0) == 0x7ffff000u);
  CHECK(classifyNetwork(-1, 0, false) == NetState::DnsError);
  CHECK(classifyNetwork(-1, -0x2700) == NetState::TlsError);
  CHECK(classifyNetwork(-11, 0) == NetState::Timeout);
  CHECK(classifyNetwork(-1, 0) == NetState::ConnectError);
  for (int code : {200, 401, 403, 404, 429, 500, 503})
    CHECK(classifyNetwork(code, 0) == NetState::Reachable);
  CHECK(!networkReady(false, false, 1800000000));
  CHECK(!networkReady(true, false, 0));
  CHECK(!networkReady(true, true, 1800000000));
  CHECK(networkReady(true, false, 1800000000));
  RetryPolicy rate;
  rate.attempted(0);
  rate.complete(429, 0, 65250);
  rate.manualRetry();
  CHECK(!rate.canAttempt(65000) && rate.canAttempt(65250));
  RetryPolicy wrap;
  wrap.attempted(0xfffffff0u);
  wrap.complete(-1, 0xfffffff0u);
  CHECK(!wrap.canAttempt(20) && wrap.canAttempt(uint32_t(0xfffffff0u + 30000u)));
  RetryPolicy manual;
  manual.attempted(1000);
  manual.complete(200, 1000);
  CHECK(!manual.canAttempt(10999) && manual.canAttempt(11000));

  NetworkStatus n;
  n.wifi = WifiState::Connected;
  strcpy(n.ip, "192.168.100.200");
  unsigned frames = 0;
  for (int view = 0; view <= 4; ++view)
    for (int net = 0; net <= 8; ++net)
      for (int send = 0; send <= 10; ++send)
        for (int portal = 0; portal < 2; ++portal) {
          Reading v = readyReading(WaterState::High);
          v.view = WaterView(view);
          v.progress = 9;
          v.valid = view != 4;
          v.ready = view != 0 && view != 4;
          v.ntu = 1500;
          v.raw = 4095;
          v.adcVolts = 3;
          v.sensorVolts = 6;
          n.net = NetState(net);
          n.sending = SendState(send);
          n.lastResult = n.sending;
          n.hasResult = true;
          n.httpCode = send % 2 ? -11 : 429;
          n.resultAt = 1000;
          n.retrySeconds = 300;
          n.portalOpen = portal;
          const Frame f = buildFrame(v, n, 15000);
          for (unsigned row = 0; row < FRAME_ROWS; ++row) {
            CHECK(strlen(f.text[row]) <= 26);
            CHECK(ROW_Y[row] + ROW_H[row] <= 128);
            if (row + 1 < FRAME_ROWS) CHECK(ROW_Y[row] + ROW_H[row] <= ROW_Y[row + 1]);
          }
          ++frames;
        }
  Reading high = readyReading(WaterState::High);
  high.ntu = 1001;
  CHECK(!strcmp(buildFrame(high, n, 1000).text[1], "1000+"));
  high.valid = false;
  CHECK(!strcmp(buildFrame(high, n, 1000).text[1], "--"));
  n.portalOpen = false;
  n.hasResult = true;
  n.httpCode = 200;
  n.sending = SendState::Sent;
  n.resultAt = 0;
  CHECK(strstr(buildFrame(readyReading(WaterState::Normal), n, 6000).text[7], "6s"));
  CHECK(strstr(buildFrame(high, n, 1000, 0, false, false).text[5], "FAILED"));
  printf("PASS: %u assertions; %u LCD status combinations; no network requests.\n", checks, frames);
  return 0;
}
