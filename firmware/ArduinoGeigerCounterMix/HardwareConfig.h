#pragma once

/*
  Select the installed hardware (1 = enabled, 0 = disabled).
  Recompile and upload after changing these switches.
  Existing defines in credentials.h or build flags may override defaults.
*/
#ifndef ENABLE_GEIGER
#define ENABLE_GEIGER 1
#endif
#ifndef ENABLE_BMP280
#define ENABLE_BMP280 1
#endif
#ifndef ENABLE_LCD
#define ENABLE_LCD 0
#endif
#ifndef ENABLE_LED
#define ENABLE_LED 1
#endif

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
    (ENABLE_LCD != 0 && ENABLE_LCD != 1) || \
    (ENABLE_LED != 0 && ENABLE_LED != 1) || \
    (ENABLE_IFTTT != 0 && ENABLE_IFTTT != 1) || \
    (ENABLE_THINGSPEAK != 0 && ENABLE_THINGSPEAK != 1)
#error "HardwareConfig switches must be 0 or 1"
#endif

// Physical wiring. Preserve the original assignments.
#ifndef GEIGER_INPUT_PIN
#define GEIGER_INPUT_PIN 16
#endif
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 22
#endif
#ifndef SENSOR_I2C_SDA
#define SENSOR_I2C_SDA 0
#endif
#ifndef SENSOR_I2C_SCL
#define SENSOR_I2C_SCL 4
#endif
#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3c
#endif

// Cloud radiation services depend on Geiger acquisition.
#define USE_IFTTT (ENABLE_GEIGER && ENABLE_IFTTT)
#define USE_THINGSPEAK (ENABLE_GEIGER && ENABLE_THINGSPEAK)
