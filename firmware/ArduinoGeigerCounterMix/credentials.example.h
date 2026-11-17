#pragma once

// Copy to credentials.h and configure locally. Never commit credentials.h.
#define mySSID "YOUR_WIFI_SSID"
#define myPASSWORD "YOUR_WIFI_PASSWORD"
#define MQTT_SERVER "YOUR_MQTT_BROKER_HOST"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""

// Optional cloud services: enable only after configuring credentials.
#define ENABLE_IFTTT 0
#define EVENT_NAME "Radioactivity"
#define IFTTT_KEY ""
#define ENABLE_THINGSPEAK 0
#define SECRET_CH_ID 0UL
#define SECRET_WRITE_APIKEY ""

// Optional SSD1306 OLED; uses the same I2C bus as BMP280.
#define ENABLE_LCD 0
