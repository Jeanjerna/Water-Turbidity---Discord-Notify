#include <Arduino.h>
#include <SPI.h>
#include <Ucglib.h>
#include "Config.h"
#include "MonitorCore.h"
#include "MonitorPresentation.h"
#include "NetworkService.h"

namespace {
using namespace Monitor;
Ucglib_ST7735_18x128x160_HWSPI screen(Config::LCD_DC, Config::LCD_CS, Config::LCD_RESET);
WaterEngine engine;
Reading reading;
Reading displayReading;
DisplayStabilizer displayStabilizer;
NetworkStatus network;
Frame previousFrame;
bool firstFrame = true, networkOk = false;
uint32_t lastUi = 0, sampleStarted = 0, lastConversion = 0;
uint32_t rawSum = 0, mvSum = 0;
uint8_t sampleCount = 0;
bool collecting = false;
bool buttonRaw = false, buttonStable = false, holdHandled = false, setupQueued = false;
uint32_t rawChanged = 0, pressedAt = 0, setupGeneration = 0;

void setTone(Tone tone) {
  switch (tone) {
  case Tone::Green: screen.setColor(40, 255, 90); break;
  case Tone::Yellow: screen.setColor(255, 220, 0); break;
  case Tone::Red: screen.setColor(255, 70, 70); break;
  case Tone::Cyan: screen.setColor(0, 220, 255); break;
  default: screen.setColor(255, 255, 255);
  }
}
void render(const Frame &frame) {
  for (uint8_t row = 0; row < FRAME_ROWS; ++row) {
    if (!firstFrame && !strcmp(frame.text[row], previousFrame.text[row]) &&
        frame.tone[row] == previousFrame.tone[row])
      continue;
    screen.setColor(0, 0, 0);
    screen.drawBox(0, ROW_Y[row], 160, ROW_H[row]);
    screen.setFont(row == 1 ? ucg_font_ncenR18_tr : ucg_font_6x10_tr);
    setTone(frame.tone[row]);
    char clipped[27];
    snprintf(clipped, sizeof(clipped), "%s", frame.text[row]);
    // Runtime glyph metrics are the final guard against horizontal clipping.
    while (strlen(clipped) && screen.getStrWidth(clipped) > 156)
      clipped[strlen(clipped) - 1] = '\0';
    screen.setPrintPos(2, ROW_BASELINE[row]);
    screen.print(clipped);
  }
  previousFrame = frame;
  firstFrame = false;
}
void sampleSensor(uint32_t now) {
  if (!collecting && (reading.sampledAt == 0 || elapsed(now, sampleStarted, Config::SAMPLE_INTERVAL_MS))) {
    collecting = true;
    sampleStarted = now;
    sampleCount = 0;
    rawSum = mvSum = 0;
    lastConversion = now - Config::ADC_SAMPLE_SPACING_MS;
  }
  if (!collecting || !elapsed(now, lastConversion, Config::ADC_SAMPLE_SPACING_MS)) return;
  lastConversion = now;
  rawSum += analogRead(Config::SENSOR_PIN);
  mvSum += analogReadMilliVolts(Config::SENSOR_PIN);
  if (++sampleCount < Config::SAMPLE_COUNT) return;
  collecting = false;
  reading =
      engine.sample(rawSum / float(Config::SAMPLE_COUNT), mvSum / (1000.0f * Config::SAMPLE_COUNT), now);
  displayReading = displayStabilizer.update(reading);
  NetworkService::publish(reading);
  char ntu[16];
  formatNtu(ntu, sizeof(ntu), reading);
  char shown[16];
  formatNtu(shown, sizeof(shown), displayReading);
  Serial.printf("ADC=%.0f ADC=%.3fV sensor=%.3fV filtered=%.3fV NTU est.=%s LCD=%s status=%s latched=%s "
                "progress=%u/10\n",
                reading.raw, reading.adcVolts, reading.sensorVolts, reading.filteredVolts, ntu, shown,
                viewName(reading.view), stateName(reading.state), reading.progress);
}
void handleControls(uint32_t now) {
  const bool down = digitalRead(Config::BOOT_PIN) == LOW;
  if (down != buttonRaw) {
    buttonRaw = down;
    rawChanged = now;
  }
  if (buttonRaw != buttonStable && elapsed(now, rawChanged, Config::DEBOUNCE_MS)) {
    buttonStable = buttonRaw;
    if (buttonStable) {
      pressedAt = now;
      holdHandled = false;
    } else
      holdHandled = false;
  }
  if (buttonStable && !holdHandled && elapsed(now, pressedAt, Config::HOLD_MS)) {
    holdHandled = true;
    setupGeneration = network.portalGeneration;
    setupQueued = NetworkService::command(Command::SetupWifi);
    Serial.println(setupQueued ? "Wi-Fi setup requested" : "Network command unavailable");
  }
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 's' || c == 'S') {
      if (!NetworkService::command(Command::SendReport))
        Serial.println("Report command unavailable; try again later.");
    }
  }
}
} // namespace
void setup() {
  Serial.begin(115200);
  pinMode(Config::SENSOR_PIN, INPUT);
  pinMode(Config::BOOT_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::SENSOR_PIN, ADC_6db);
  SPI.begin(Config::LCD_SCK, -1, Config::LCD_MOSI, Config::LCD_CS);
  screen.begin(UCG_FONT_MODE_TRANSPARENT);
  screen.setRotate90();
  screen.clearScreen();
  Serial.println("Water Monitor | NTU est. | 3.3V sensor supply / fixed water reference / ADC 6dB");
  Serial.println(
      "Alert >100 for 10 samples; recover <80 for 10 samples. BOOT hold 3s: Wi-Fi setup; s: report.");
  Serial.println("Sensor V must use 3.3V. Keep the 10k/10k divider at GPIO34. Fixed water reference: 1.762V. "
                 "Prototype NTU estimate.");
  networkOk = NetworkService::begin();
  if (!networkOk) Serial.println("Network task could not start; sensor and LCD remain active.");
  render(buildFrame(displayReading, network, millis(), 0, false, networkOk));
}
void loop() {
  const uint32_t now = millis();
  handleControls(now);
  sampleSensor(now);
  NetworkService::status(network);
  if (network.portalOpen || network.portalGeneration != setupGeneration) setupQueued = false;
  if (elapsed(now, lastUi, Config::UI_INTERVAL_MS)) {
    lastUi = now;
    render(buildFrame(displayReading, network, now, buttonStable && !holdHandled ? now - pressedAt : 0,
                      setupQueued, networkOk));
  }
  delay(1);
}
