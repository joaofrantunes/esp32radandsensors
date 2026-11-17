#pragma once

/*
  Select the installed hardware (1 = enabled, 0 = disabled).
  Recompile and upload after changing these switches.
  Existing defines in credentials.h or build flags may override defaults.
*/
// 0 = generic V1.6-compatible defaults; 1 = photographed CAJOE station.
// Individual ENABLE_* overrides still take precedence.
#ifndef STATION_PROFILE
#define STATION_PROFILE 1
#endif
#if STATION_PROFILE != 0 && STATION_PROFILE != 1
#error "STATION_PROFILE must be 0 or 1"
#endif
#ifndef ENABLE_GEIGER
#define ENABLE_GEIGER 1
#endif
#ifndef ENABLE_BMP280
#define ENABLE_BMP280 1
#endif
// Allow BME280 detection in the barometer slot. Requires Adafruit BME280.
#ifndef ENABLE_BME280
#define ENABLE_BME280 STATION_PROFILE
#endif
#ifndef ENABLE_LCD
#define ENABLE_LCD 0
#endif
#ifndef ENABLE_LED
#define ENABLE_LED 0
#endif
#ifndef ENABLE_AHT
#define ENABLE_AHT 1
#endif
#ifndef ENABLE_GPS
#define ENABLE_GPS 1
#endif
#ifndef ENABLE_NTP
#define ENABLE_NTP 1
#endif
// ADC requires confirmed external voltage divider and calibration.
#ifndef ENABLE_VOLTAGE
#define ENABLE_VOLTAGE 0
#endif
#ifndef ENABLE_MAG
#define ENABLE_MAG STATION_PROFILE
#endif
#ifndef ENABLE_LORA
#define ENABLE_LORA 0
#endif
#ifndef ENABLE_HALL
#define ENABLE_HALL STATION_PROFILE
#endif
#ifndef ENABLE_INTTEMP
#define ENABLE_INTTEMP STATION_PROFILE
#endif

// J305 beta/gamma: uncalibrated example from the other CAJOE station.
// Opt in only for indicative estimates; never use for radiation safety.
#ifndef ENABLE_DOSE_ESTIMATE
#define ENABLE_DOSE_ESTIMATE 0
#endif
#ifndef GEIGER_USVH_PER_CPM
#define GEIGER_USVH_PER_CPM 0.00332
#endif
#if ENABLE_DOSE_ESTIMATE != 0 && ENABLE_DOSE_ESTIMATE != 1
#error "ENABLE_DOSE_ESTIMATE must be 0 or 1"
#endif
static_assert(!ENABLE_DOSE_ESTIMATE || (ENABLE_GEIGER && GEIGER_USVH_PER_CPM >= 0.000000001 && GEIGER_USVH_PER_CPM <= 1.0),
              "Dose estimate requires Geiger and factor 1e-9 through 1");

// Optional cloud services; activate only with valid credentials.
#ifndef ENABLE_IFTTT
#define ENABLE_IFTTT 0
#endif
#ifndef ENABLE_THINGSPEAK
#define ENABLE_THINGSPEAK 0
#endif

// Reject accidental values other than 0/1 at build time.
#if (ENABLE_GEIGER != 0 && ENABLE_GEIGER != 1) || \
    (ENABLE_BMP280 != 0 && ENABLE_BMP280 != 1) || \
    (ENABLE_BME280 != 0 && ENABLE_BME280 != 1) || \
    (ENABLE_LCD != 0 && ENABLE_LCD != 1) || \
    (ENABLE_LED != 0 && ENABLE_LED != 1) || \
    (ENABLE_IFTTT != 0 && ENABLE_IFTTT != 1) || \
    (ENABLE_THINGSPEAK != 0 && ENABLE_THINGSPEAK != 1) || \
    (ENABLE_AHT != 0 && ENABLE_AHT != 1) || \
    (ENABLE_GPS != 0 && ENABLE_GPS != 1) || \
    (ENABLE_NTP != 0 && ENABLE_NTP != 1) || \
    (ENABLE_VOLTAGE != 0 && ENABLE_VOLTAGE != 1) || \
    (ENABLE_MAG != 0 && ENABLE_MAG != 1) || \
    (ENABLE_LORA != 0 && ENABLE_LORA != 1) || \
    (ENABLE_HALL != 0 && ENABLE_HALL != 1) || \
    (ENABLE_INTTEMP != 0 && ENABLE_INTTEMP != 1)
#error "HardwareConfig switches must be 0 or 1"
#endif

// Physical wiring recovered from ArduinoGeigerCounterMix_esp32_v6.ino.
#ifndef GEIGER_INPUT_PIN
#define GEIGER_INPUT_PIN 13
#endif
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN -1 // Set a verified free output GPIO before enabling LED.
#endif
#ifndef SENSOR_I2C_SDA
#define SENSOR_I2C_SDA 21
#endif
#ifndef SENSOR_I2C_SCL
#define SENSOR_I2C_SCL 22
#endif
#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3c
#endif

// AHT10/AHT20 family (0x38); auto-detect HMC5883L/QMC5883L.
#ifndef AHT_I2C_ADDRESS
#define AHT_I2C_ADDRESS 0x38
#endif
#ifndef MAG_DECLINATION_DEG
#define MAG_DECLINATION_DEG 0.0 // Set current local declination after calibration.
#endif
// Optional hard-iron subtraction (uT) and per-axis scale correction.
// Defaults intentionally leave measured field unchanged.
#ifndef MAG_OFFSET_X
#define MAG_OFFSET_X 0.0
#endif
#ifndef MAG_OFFSET_Y
#define MAG_OFFSET_Y 0.0
#endif
#ifndef MAG_OFFSET_Z
#define MAG_OFFSET_Z 0.0
#endif
#ifndef MAG_SCALE_X
#define MAG_SCALE_X 1.0
#endif
#ifndef MAG_SCALE_Y
#define MAG_SCALE_Y 1.0
#endif
#ifndef MAG_SCALE_Z
#define MAG_SCALE_Z 1.0
#endif
static_assert(MAG_SCALE_X > 0 && MAG_SCALE_Y > 0 && MAG_SCALE_Z > 0,
              "Magnetometer scale must be positive");
// Enable temporarily to log rapid raw magnetic samples for calibration.
#ifndef ENABLE_MAG_CAL_LOG
#define ENABLE_MAG_CAL_LOG 0
#endif
#if ENABLE_MAG_CAL_LOG != 0 && ENABLE_MAG_CAL_LOG != 1
#error "ENABLE_MAG_CAL_LOG must be 0 or 1"
#endif
#ifndef GPS_RX_PIN
#define GPS_RX_PIN 16
#endif
#ifndef GPS_TX_PIN
#define GPS_TX_PIN 17
#endif
#ifndef GPS_BAUD
#define GPS_BAUD 9600
#endif
#ifndef VOLTAGE_ADC_PIN
#define VOLTAGE_ADC_PIN 34
#endif
// Original v6 provisional calibration. Verify against a multimeter before use.
#ifndef VOLTAGE_DIVIDER_GAIN
#define VOLTAGE_DIVIDER_GAIN 2.0
#endif
#ifndef VOLTAGE_OFFSET_V
#define VOLTAGE_OFFSET_V 0.42
#endif
#ifndef NTP_SERVER
#define NTP_SERVER "pool.ntp.org"
#endif
#ifndef LORA_SCK_PIN
#define LORA_SCK_PIN 5
#endif
#ifndef LORA_MISO_PIN
#define LORA_MISO_PIN 19
#endif
#ifndef LORA_MOSI_PIN
#define LORA_MOSI_PIN 27
#endif
#ifndef LORA_SS_PIN
#define LORA_SS_PIN 18
#endif
#ifndef LORA_RST_PIN
#define LORA_RST_PIN 14
#endif
#ifndef LORA_DIO0_PIN
#define LORA_DIO0_PIN 26
#endif
#ifndef LORA_FREQUENCY_HZ
#define LORA_FREQUENCY_HZ 433000000L // Original setting; verify module and local regulations.
#endif

#define USE_I2C (ENABLE_BMP280 || ENABLE_LCD || ENABLE_AHT || ENABLE_MAG)
// Compile-time pin ownership check; -1 means a disabled function.
namespace HardwarePins {
constexpr int pins[] = {
  ENABLE_GEIGER ? GEIGER_INPUT_PIN : -1,
  ENABLE_LED ? STATUS_LED_PIN : -1,
  USE_I2C ? SENSOR_I2C_SDA : -1, USE_I2C ? SENSOR_I2C_SCL : -1,
  ENABLE_GPS ? GPS_RX_PIN : -1, ENABLE_GPS ? GPS_TX_PIN : -1,
  ENABLE_VOLTAGE ? VOLTAGE_ADC_PIN : -1,
  ENABLE_LORA ? LORA_SCK_PIN : -1, ENABLE_LORA ? LORA_MISO_PIN : -1,
  ENABLE_LORA ? LORA_MOSI_PIN : -1, ENABLE_LORA ? LORA_SS_PIN : -1,
  ENABLE_LORA ? LORA_RST_PIN : -1, ENABLE_LORA ? LORA_DIO0_PIN : -1
};
constexpr unsigned count = sizeof(pins) / sizeof(pins[0]);
constexpr bool distinct(unsigned i, unsigned j) {
  return i >= count ? true : j >= count ? distinct(i + 1, i + 2) :
         ((pins[i] < 0 || pins[j] < 0 || pins[i] != pins[j]) && distinct(i, j + 1));
}
static_assert(distinct(0, 1), "Pin conflict: check HardwareConfig.h");
static_assert(!ENABLE_LED || (STATUS_LED_PIN >= 0 && STATUS_LED_PIN <= 33 &&
              !(STATUS_LED_PIN >= 6 && STATUS_LED_PIN <= 11)),
              "Enable LED only with a verified free output GPIO");
static_assert(!ENABLE_VOLTAGE || (VOLTAGE_ADC_PIN >= 32 && VOLTAGE_ADC_PIN <= 39),
              "Classic ESP32 Wi-Fi voltage acquisition requires ADC1 (GPIO32-39)");
static_assert(!ENABLE_VOLTAGE || VOLTAGE_DIVIDER_GAIN > 0.0,
              "Voltage divider gain must be positive");
}

// Cloud radiation services depend on Geiger acquisition.
#define USE_IFTTT (ENABLE_GEIGER && ENABLE_IFTTT)
#define USE_THINGSPEAK (ENABLE_GEIGER && ENABLE_THINGSPEAK)
