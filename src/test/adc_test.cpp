#include <Arduino.h>
#include <SPI.h>
#include <Ucglib.h>
#include <esp_adc_cal.h>
#include "Config.h"

Ucglib_ST7735_18x128x160_HWSPI display(Config::LCD_DC, Config::LCD_CS, Config::LCD_RESET);
constexpr adc_attenuation_t ranges[] = {ADC_0db, ADC_2_5db, ADC_6db, ADC_11db};
constexpr adc_atten_t idfRanges[] = {ADC_ATTEN_DB_0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6, ADC_ATTEN_DB_11};
constexpr const char *labels[] = {"0dB", "2.5dB", "6dB", "11dB"};
constexpr uint8_t SAMPLES = 64;
uint32_t cycle = 0;

void setup() {
  Serial.begin(115200);
  pinMode(Config::SENSOR_PIN, INPUT);
  analogReadResolution(12);
  SPI.begin(Config::LCD_SCK, -1, Config::LCD_MOSI, Config::LCD_CS);
  display.begin(UCG_FONT_MODE_TRANSPARENT);
  display.setRotate90();
  display.setFont(ucg_font_6x10_tr);
  Serial.println("ADC comparison on GPIO34. Keep wiring, supply and probe position unchanged.");
  Serial.println("No voltage correction applied. Compare pin_V with a meter on GPIO34 to GND.");
  for (uint8_t i = 0; i < 4; ++i) {
    esp_adc_cal_characteristics_t characteristics = {};
    const auto source =
        esp_adc_cal_characterize(ADC_UNIT_1, idfRanges[i], ADC_WIDTH_BIT_12, 1100, &characteristics);
    const char *name = source == ESP_ADC_CAL_VAL_EFUSE_TP     ? "eFuse two-point"
                       : source == ESP_ADC_CAL_VAL_EFUSE_VREF ? "eFuse Vref"
                                                              : "default 1100mV Vref";
    Serial.printf("Calibration %s: %s\n", labels[i], name);
  }
}

void loop() {
  Serial.printf("--- ADC comparison cycle %lu ---\n", static_cast<unsigned long>(++cycle));
  display.clearScreen();
  display.setColor(0, 220, 255);
  display.setPrintPos(3, 12);
  display.print("ADC CHECK - NO WATER");
  display.setColor(255, 255, 255);
  for (uint8_t i = 0; i < 4; ++i) {
    analogSetPinAttenuation(Config::SENSOR_PIN, ranges[i]);
    delay(20);
    // Discard conversions after switching attenuation before averaging.
    for (uint8_t n = 0; n < 8; ++n) {
      analogReadMilliVolts(Config::SENSOR_PIN);
      delay(2);
    }
    uint32_t rawSum = 0, mvSum = 0;
    uint16_t low = 4095, high = 0;
    for (uint8_t n = 0; n < SAMPLES; ++n) {
      const uint16_t raw = analogRead(Config::SENSOR_PIN);
      rawSum += raw;
      low = min(low, raw);
      high = max(high, raw);
      mvSum += analogReadMilliVolts(Config::SENSOR_PIN);
      delay(2);
    }
    const float pinVolts = mvSum / (1000.0f * SAMPLES);
    const bool clipped = high >= 4090;
    Serial.printf("range=%s raw_avg=%.1f raw_min=%u raw_max=%u pin_V=%.3f sensor_V=%.3f%s\n", labels[i],
                  rawSum / float(SAMPLES), low, high, pinVolts, pinVolts * Config::DIVIDER_RATIO,
                  clipped ? " CLIPPED: ignore this range" : "");
    display.setPrintPos(3, 32 + 19 * i);
    display.print(labels[i]);
    display.print(": ");
    display.print(String(pinVolts, 3));
    display.print(clipped ? "V LIMIT" : "V");
  }
  // Restore the broad range between diagnostic sweeps.
  analogSetPinAttenuation(Config::SENSOR_PIN, ADC_11db);
  display.setPrintPos(3, 121);
  display.print("Compare meter at GPIO34");
  delay(3000);
}
