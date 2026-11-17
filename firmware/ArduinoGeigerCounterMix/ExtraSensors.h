#pragma once

// Optional acquisition recovered from the original esp32_v6. All hardware
// access happens in the Arduino measurement task; MQTT owns its own snapshot.
#include <Arduino.h>
#include "HardwareConfig.h"
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#include <time.h>
#if USE_I2C
#include <Wire.h>
#endif
#if ENABLE_GPS
#include <TinyGPSPlus.h>
#endif
#if ENABLE_LORA
#include <SPI.h>
#include <LoRa.h>
#endif
#if ENABLE_HALL && __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#if ENABLE_HALL && defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32 && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR == 2
#define EXTRA_HALL_SUPPORTED 1
#else
#define EXTRA_HALL_SUPPORTED 0
#endif

namespace ExtraSensors {
// Prefix enumerators to avoid ESP32 macros (e.g. DISABLED in esp32-hal-gpio.h).
enum State : uint8_t {
  SENSOR_STATE_DISABLED, SENSOR_STATE_WAITING, SENSOR_STATE_OK,
  SENSOR_STATE_UNAVAILABLE, SENSOR_STATE_NO_FIX, SENSOR_STATE_UNSUPPORTED,
  SENSOR_STATE_STALE
};
enum Module : uint8_t { AHT, VOLTAGE, GPS, NTP, MAG, LORA, HALL, INTTEMP, MODULE_COUNT };
struct Snapshot {
  State state[MODULE_COUNT];
  uint32_t sampledAt;
  double humidity, ahtTemp, voltage, latitude, longitude, x, y, z, heading, hall, intTemp;
  bool headingValid;
  char timestamp[25];
};
constexpr uint32_t INTERVAL = 60000UL;
constexpr uint32_t MAX_AGE = 90000UL;
constexpr const char *STATUS_TOPICS[] = {
  MQTT_TOPIC_PREFIX "/aht/status", MQTT_TOPIC_PREFIX "/voltage/status",
  MQTT_TOPIC_PREFIX "/gps/status", MQTT_TOPIC_PREFIX "/ntp/status",
  MQTT_TOPIC_PREFIX "/mag/status", MQTT_TOPIC_PREFIX "/lora/status",
  MQTT_TOPIC_PREFIX "/hall/status", MQTT_TOPIC_PREFIX "/inttemp/status"
};
inline const char *stateName(State s) {
  switch (s) {
    case SENSOR_STATE_DISABLED: return "disabled";
    case SENSOR_STATE_WAITING: return "waiting";
    case SENSOR_STATE_OK: return "ok";
    case SENSOR_STATE_UNAVAILABLE: return "unavailable";
    case SENSOR_STATE_NO_FIX: return "no_fix";
    case SENSOR_STATE_UNSUPPORTED: return "unsupported";
    default: return "stale";
  }
}
inline Snapshot initialSnapshot() {
  Snapshot s = {};
  s.state[AHT] = ENABLE_AHT ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  s.state[VOLTAGE] = ENABLE_VOLTAGE ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  s.state[GPS] = ENABLE_GPS ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  s.state[NTP] = ENABLE_NTP ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  s.state[MAG] = ENABLE_MAG ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  s.state[LORA] = ENABLE_LORA ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  s.state[HALL] = ENABLE_HALL ? (EXTRA_HALL_SUPPORTED ? SENSOR_STATE_WAITING : SENSOR_STATE_UNSUPPORTED) : SENSOR_STATE_DISABLED;
  s.state[INTTEMP] = ENABLE_INTTEMP ? SENSOR_STATE_WAITING : SENSOR_STATE_DISABLED;
  return s;
}
static QueueHandle_t queue = nullptr;
static uint32_t lastSample = 0;
static bool firstSample = true;
#if ENABLE_GPS
static TinyGPSPlus gps;
static uint32_t lastGpsByte = 0;
static bool sawGpsByte = false;
#endif
#if ENABLE_MAG
static bool magConfigured = false;
#endif
#if ENABLE_LORA
static bool loraReady = false;
static bool loraSending = false;
static uint32_t loraStarted = 0;
static unsigned long loraSequence = 0;
static double radioCpm = NAN;
#endif

#if ENABLE_AHT || ENABLE_MAG
inline bool writeI2c(uint8_t addr, const uint8_t *bytes, size_t len) {
  Wire.beginTransmission(addr);
  if (Wire.write(bytes, len) != len) {
    Wire.endTransmission();
    return false;
  }
  return Wire.endTransmission() == 0;
}
inline bool readI2c(uint8_t addr, uint8_t *bytes, size_t len) {
  // Wire timeout is configured by the main sketch; no endless wait for bytes.
  if (Wire.requestFrom(addr, len) != len) return false;
  for (size_t i = 0; i < len; ++i) {
    const int b = Wire.read();
    if (b < 0) return false;
    bytes[i] = static_cast<uint8_t>(b);
  }
  return true;
}
#endif

#if ENABLE_AHT
inline bool decodeAht(const uint8_t *b, double &humidity, double &temperature) {
  if ((b[0] & 0x80) || !(b[0] & 0x08)) return false;
  const uint32_t rawHumidity = (uint32_t(b[1]) << 12) | (uint32_t(b[2]) << 4) | (b[3] >> 4);
  const uint32_t rawTemp = (uint32_t(b[3] & 15) << 16) | (uint32_t(b[4]) << 8) | b[5];
  humidity = rawHumidity * 100.0 / 1048576.0;
  temperature = rawTemp * 200.0 / 1048576.0 - 50.0;
  return temperature >= -40.0 && temperature <= 85.0;
}
inline bool readAht(double &humidity, double &temperature) {
  uint8_t status = 0;
  if (!readI2c(AHT_I2C_ADDRESS, &status, 1) || (status & 0x80)) return false;
  if (!(status & 0x08)) {
    // AHT10 and AHT20 have different initialization commands.
    const uint8_t commands[2] = {0xBE, 0xE1};
    for (uint8_t command : commands) {
      const uint8_t calibrate[] = {command, 0x08, 0x00};
      if (!writeI2c(AHT_I2C_ADDRESS, calibrate, sizeof(calibrate))) continue;
      delay(20);
      if (readI2c(AHT_I2C_ADDRESS, &status, 1) && (status & 0x88) == 0x08) break;
      status = 0;
    }
    if (!(status & 0x08) || (status & 0x80)) return false;
  }
  const uint8_t trigger[] = {0xAC, 0x33, 0x00};
  if (!writeI2c(AHT_I2C_ADDRESS, trigger, sizeof(trigger))) return false;
  delay(85);
  // A bounded conversion wait; missing or stuck sensors never loop forever.
  for (int attempt = 0; attempt < 4; ++attempt) {
    uint8_t bytes[6];
    if (!readI2c(AHT_I2C_ADDRESS, bytes, sizeof(bytes))) return false;
    if (!(bytes[0] & 0x80)) return decodeAht(bytes, humidity, temperature);
    delay(20);
  }
  return false;
}
#endif

#if ENABLE_MAG
constexpr uint8_t MAG_ADDRESS = 0x1E;
inline bool readMagRegister(uint8_t reg, uint8_t *value, size_t len) {
  return writeI2c(MAG_ADDRESS, &reg, 1) && readI2c(MAG_ADDRESS, value, len);
}
inline bool initMag() {
  uint8_t id[3];
  if (!readMagRegister(0x0A, id, sizeof(id)) || id[0] != 'H' || id[1] != '4' || id[2] != '3') {
    return false; // QMC5883 is not register-compatible.
  }
  const uint8_t config[] = {0x00, 0x70, 0x20, 0x00}; // 8 samples, 15 Hz, gain 1.3 G, continuous
  if (!writeI2c(MAG_ADDRESS, config, sizeof(config))) return false;
  delay(100);
  return true;
}
inline bool readMag(Snapshot &s) {
  if (!magConfigured) magConfigured = initMag();
  if (!magConfigured) return false;
  uint8_t ready = 0;
  uint8_t b[6];
  if (!readMagRegister(0x09, &ready, 1) || !(ready & 1) ||
      !readMagRegister(0x03, b, sizeof(b))) {
    magConfigured = false;
    return false;
  }
  const int16_t x = static_cast<int16_t>((uint16_t(b[0]) << 8) | b[1]);
  const int16_t z = static_cast<int16_t>((uint16_t(b[2]) << 8) | b[3]);
  const int16_t y = static_cast<int16_t>((uint16_t(b[4]) << 8) | b[5]);
  if (x == -4096 || y == -4096 || z == -4096) return false; // overflow
  s.x = x * 100.0 / 1100.0;
  s.y = y * 100.0 / 1100.0;
  s.z = z * 100.0 / 980.0;
  // Individual zero axes are valid. Heading is undefined only when X=Y=0.
  s.headingValid = x != 0 || y != 0;
  if (s.headingValid) {
    s.heading = fmod(atan2(s.y, s.x) * 180.0 / 3.141592653589793 + MAG_DECLINATION_DEG, 360.0);
    if (s.heading < 0) s.heading += 360.0;
    s.headingValid = isfinite(s.heading);
  }
  return true;
}
#endif

inline bool formatUtc(time_t value, char *out, size_t len) {
  if (value < static_cast<time_t>(1577836800)) return false; // unsynchronized RTC
  struct tm utc = {};
  return gmtime_r(&value, &utc) &&
         strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &utc) == 20;
}
inline void setCpm(double cpm) {
#if ENABLE_LORA
  radioCpm = cpm;
#else
  (void)cpm;
#endif
}
inline void begin() {
  queue = xQueueCreate(1, sizeof(Snapshot));
  if (!queue) Serial.println("[EXTRA] Snapshot queue unavailable");
#if ENABLE_GPS
  Serial2.setRxBufferSize(1024);
  Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#endif
#if ENABLE_VOLTAGE
  analogReadResolution(12);
  analogSetPinAttenuation(VOLTAGE_ADC_PIN, ADC_11db);
#endif
}

// Called only by the network task, after Wi-Fi is available.
inline void startNtp() {
#if ENABLE_NTP
  configTime(0, 0, NTP_SERVER); // SNTP is asynchronous; no getLocalTime() wait.
#endif
}

inline void tick(uint32_t now) {
#if ENABLE_GPS
  // Drain UART continuously even between MQTT snapshots. Never close UART.
  for (unsigned i = 0; i < 512 && Serial2.available() > 0; ++i) {
    const int c = Serial2.read();
    if (c >= 0) {
      gps.encode(static_cast<char>(c));
      sawGpsByte = true;
      lastGpsByte = now;
    }
  }
#endif
#if ENABLE_LORA
  if (loraSending) {
    if (!LoRa.isTransmitting()) loraSending = false;
    else if (now - loraStarted >= 5000UL) {
      LoRa.end(); // only on timeout; next interval reinitializes SPI.
      loraSending = loraReady = false;
    }
  }
#endif
  if (!firstSample && now - lastSample < INTERVAL) return;
  firstSample = false;
  lastSample = now;
  Snapshot s = initialSnapshot();
  s.sampledAt = now;
#if ENABLE_AHT
  s.state[AHT] = readAht(s.humidity, s.ahtTemp) ? SENSOR_STATE_OK : SENSOR_STATE_UNAVAILABLE;
#endif
#if ENABLE_VOLTAGE
  uint32_t sumMv = 0;
  bool saturated = false;
  for (unsigned i = 0; i < 16; ++i) {
    const uint32_t mv = analogReadMilliVolts(VOLTAGE_ADC_PIN);
    if (mv >= 3100 || analogRead(VOLTAGE_ADC_PIN) >= 4090) saturated = true;
    sumMv += mv;
  }
  const double adc = sumMv / 16000.0;
  s.voltage = adc * VOLTAGE_DIVIDER_GAIN + VOLTAGE_OFFSET_V;
  // A zero ADC can also be a disconnected divider: suppress ambiguous readings.
  s.state[VOLTAGE] = !saturated && sumMv > 0 && isfinite(s.voltage) &&
                     s.voltage >= 0.0 ? SENSOR_STATE_OK : SENSOR_STATE_UNAVAILABLE;
#endif
#if ENABLE_GPS
  if (!sawGpsByte || now - lastGpsByte > 5000UL) {
    s.state[GPS] = SENSOR_STATE_UNAVAILABLE;
  } else if (gps.location.isValid() && gps.location.age() <= 5000UL) {
    s.latitude = gps.location.lat();
    s.longitude = gps.location.lng();
    s.state[GPS] = isfinite(s.latitude) && isfinite(s.longitude) &&
        fabs(s.latitude) <= 90.0 && fabs(s.longitude) <= 180.0 ? SENSOR_STATE_OK : SENSOR_STATE_NO_FIX;
  } else {
    s.state[GPS] = SENSOR_STATE_NO_FIX;
  }
#endif
#if ENABLE_NTP
  s.state[NTP] = formatUtc(time(nullptr), s.timestamp, sizeof(s.timestamp)) ? SENSOR_STATE_OK : SENSOR_STATE_WAITING;
#endif
#if ENABLE_MAG
  s.state[MAG] = readMag(s) ? SENSOR_STATE_OK : SENSOR_STATE_UNAVAILABLE;
#endif
#if ENABLE_LORA
  if (!loraReady) {
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SS_PIN);
    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    loraReady = LoRa.begin(LORA_FREQUENCY_HZ);
  }
  s.state[LORA] = loraReady ? SENSOR_STATE_OK : SENSOR_STATE_UNAVAILABLE;
  // Diagnostic/CPM transmission only, not a gateway or delivery confirmation.
  if (loraReady && !loraSending && LoRa.beginPacket()) {
    char packet[64];
    if (ENABLE_GEIGER && isfinite(radioCpm))
      snprintf(packet, sizeof(packet), "geiger seq=%lu cpm=%.2f", loraSequence++, radioCpm);
    else
      snprintf(packet, sizeof(packet), "geiger seq=%lu", loraSequence++);
    LoRa.print(packet);
    loraSending = LoRa.endPacket(true) != 0;
    loraStarted = millis();
    if (!loraSending) s.state[LORA] = SENSOR_STATE_UNAVAILABLE;
  }
#endif
#if EXTRA_HALL_SUPPORTED
  s.hall = hallRead();
  s.state[HALL] = SENSOR_STATE_OK; // zero is a valid signed raw reading.
#endif
#if ENABLE_INTTEMP
#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  s.intTemp = temperatureRead();
  s.state[INTTEMP] = isfinite(s.intTemp) && s.intTemp >= -40.0 && s.intTemp <= 125.0 ? SENSOR_STATE_OK : SENSOR_STATE_UNAVAILABLE;
#else
  s.state[INTTEMP] = SENSOR_STATE_UNSUPPORTED;
#endif
#endif
  // Snapshot can be transferred across tasks without sharing mutable sensors.
  if (queue) xQueueOverwrite(queue, &s);
}

// Network-task-owned cache: publish only valid values, including legitimate zeros.
struct Publisher {
  Snapshot sample = initialSnapshot();
  bool pending = true;
  bool haveSample = false;
  bool statusRefresh = true;
  bool staleReported = false;
  void receive() {
    if (queue && xQueueReceive(queue, &sample, 0) == pdTRUE) {
      haveSample = pending = statusRefresh = true;
      staleReported = false;
    }
  }
  bool numeric(PubSubClient &client, const char *topic, double value, int precision = 2) {
    if (!isfinite(value)) return false;
    char buf[48];
    const int n = snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return n > 0 && static_cast<size_t>(n) < sizeof(buf) && client.publish(topic, buf);
  }
  bool publish(PubSubClient &client, uint32_t now) {
    const bool fresh = haveSample && now - sample.sampledAt <= MAX_AGE;
    if (haveSample && !fresh && !staleReported) statusRefresh = true;
    if (!pending && !statusRefresh) return true;
    for (unsigned i = 0; i < MODULE_COUNT; ++i) {
      State state = sample.state[i];
      if (!fresh && state == SENSOR_STATE_OK) state = SENSOR_STATE_STALE;
      if (!client.publish(STATUS_TOPICS[i], stateName(state), true)) return false;
    }
    if (pending && fresh) {
      if (sample.state[AHT] == SENSOR_STATE_OK &&
          (!numeric(client, MQTT_TOPIC_PREFIX "/humidity", sample.humidity) ||
           !numeric(client, MQTT_TOPIC_PREFIX "/temphum", sample.ahtTemp))) return false;
      if (sample.state[VOLTAGE] == SENSOR_STATE_OK &&
          !numeric(client, MQTT_TOPIC_PREFIX "/voltage", sample.voltage, 3)) return false;
      if (sample.state[GPS] == SENSOR_STATE_OK &&
          (!numeric(client, MQTT_TOPIC_PREFIX "/lat_dd", sample.latitude, 6) ||
           !numeric(client, MQTT_TOPIC_PREFIX "/lon_dd", sample.longitude, 6))) return false;
      if (sample.state[NTP] == SENSOR_STATE_OK &&
          !client.publish(MQTT_TOPIC_PREFIX "/timestamp", sample.timestamp)) return false;
      if (sample.state[MAG] == SENSOR_STATE_OK &&
          (!numeric(client, MQTT_TOPIC_PREFIX "/magx", sample.x) ||
           !numeric(client, MQTT_TOPIC_PREFIX "/magy", sample.y) ||
           !numeric(client, MQTT_TOPIC_PREFIX "/magz", sample.z) ||
           (sample.headingValid && !numeric(client, MQTT_TOPIC_PREFIX "/magangle", sample.heading)))) return false;
      if (sample.state[HALL] == SENSOR_STATE_OK &&
          !numeric(client, MQTT_TOPIC_PREFIX "/hall", sample.hall)) return false;
      if (sample.state[INTTEMP] == SENSOR_STATE_OK &&
          !numeric(client, MQTT_TOPIC_PREFIX "/inttemp", sample.intTemp)) return false;
    }
    pending = statusRefresh = false;
    staleReported = haveSample && !fresh;
    return true;
  }
};
}
