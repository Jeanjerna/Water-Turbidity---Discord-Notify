#pragma once
#include <stdint.h>
namespace Config {
constexpr uint8_t SENSOR_PIN = 34;
constexpr uint8_t LCD_SCK = 18, LCD_MOSI = 23, LCD_CS = 4, LCD_DC = 16, LCD_RESET = 17;
constexpr uint8_t BOOT_PIN = 0;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000, ADC_SAMPLE_SPACING_MS = 2;
constexpr uint8_t SAMPLE_COUNT = 64;
constexpr float DIVIDER_RATIO = 2.0f;
// Empirical project mapping, NOT a manufacturer-validated 3.3V conversion.
// See docs/FORMULA_REVIEW.md for sources and the normalization limitation.
constexpr float SENSOR_SUPPLY_VOLTS = 3.3f;
constexpr float WATER_REFERENCE_VOLTS = 1.762f;
constexpr float CURVE_MIN_VOLTS = 2.5f;
constexpr float CURVE_ZERO_VOLTS = 4.200246f; // Positive high-voltage root of the published polynomial.
constexpr float CURVE_A = -1120.4f, CURVE_B = 5742.3f, CURVE_C = -4352.9f;
constexpr float EMA_ALPHA = 0.2f;
constexpr uint8_t MEDIAN_WINDOW = 3; // Reject an isolated one-second spike before EMA.
constexpr float DISPLAY_NTU_DEADBAND = 5.0f;
constexpr uint8_t DISPLAY_CONFIRM_SAMPLES = 3;
// ESP32 ADC_6db calibrated range; 3.3V / divider ratio 2 gives nominal 1.65V maximum.
constexpr float ADC_MIN_VOLTS = 0.15f, ADC_MAX_VOLTS = 1.75f;
constexpr float ADC_RAW_LIMIT = 4000;
constexpr float ALERT_NTU = 100, RECOVERY_NTU = 80, DISPLAY_MAX_NTU = 1000;
constexpr uint8_t CONFIRM_SAMPLES = 10;
constexpr uint32_t HOLD_MS = 3000, DEBOUNCE_MS = 30, PORTAL_MS = 180000;
constexpr uint32_t WIFI_CONNECT_MS = 20000, HEALTH_INTERVAL_MS = 60000;
constexpr uint32_t MANUAL_INTERVAL_MS = 10000, RESULT_HOLD_MS = 7000;
constexpr uint32_t RETRY_FIRST_MS = 30000, RETRY_SECOND_MS = 60000, RETRY_LATER_MS = 300000;
constexpr uint32_t SNAPSHOT_MAX_AGE_MS = 2500, UI_INTERVAL_MS = 100;
constexpr uint32_t HTTP_CONNECT_MS = 5000, HTTP_READ_MS = 5000, TLS_TIMEOUT_SECONDS = 8;
constexpr uint32_t NETWORK_STACK_BYTES = 16384;
constexpr int64_t MIN_TIME_EPOCH = 1704067200; // 2024-01-01 UTC
constexpr char AP_NAME[] = "WaterMonitor-Setup";
constexpr char AP_PASSWORD[] = "water1234";
constexpr char HEALTH_URL[] = "https://discord.com/api/v10/gateway";
} // namespace Config
