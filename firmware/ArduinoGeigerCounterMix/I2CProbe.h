#pragma once
#include "HardwareConfig.h"
#if USE_I2C
#include <Arduino.h>
#include <Wire.h>
namespace I2CProbe {
inline bool ack(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}
inline bool write(uint8_t address, const uint8_t *bytes, size_t len) {
  Wire.beginTransmission(address);
  const bool wrote = Wire.write(bytes, len) == len;
  return Wire.endTransmission() == 0 && wrote;
}
inline bool read(uint8_t address, uint8_t *bytes, size_t len) {
  if (Wire.requestFrom(address, len) != len) return false;
  for (size_t i=0; i<len; ++i) {
    int value=Wire.read();
    if (value<0) return false;
    bytes[i]=uint8_t(value);
  }
  return true;
}
inline bool reg(uint8_t address, uint8_t index, uint8_t *bytes, size_t len) {
  return write(address, &index, 1) && read(address, bytes, len);
}
inline void report() {
  // Targeted scan only (bounded even with a faulty bus). ACK is not identity.
  const uint8_t addresses[] = {0x0D,0x1E,AHT_I2C_ADDRESS,OLED_I2C_ADDRESS,0x76,0x77};
  for (uint8_t address : addresses)
    Serial.printf("[I2C] 0x%02X %s\n", unsigned(address), ack(address) ? "ACK" : "no ACK");
}
}
#endif
