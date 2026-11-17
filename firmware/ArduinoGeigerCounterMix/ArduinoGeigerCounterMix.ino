/*
  Geiger counter RadiationD-v1.1 (CAJOE) + BMP280 + ESP32
  Original author: Hans Carlos Hofmann; based on Andreas Spiess
  and Mark A. Heckler (@MkHeck). MIT License.
  Revised V1.5.0: restored optional v6 sensors, safe acquisition and MQTT status.

  Keep the original credentials.h beside this sketch (never publish it).
  Default MQTT prefix matches node-red/flows.json. Change MQTT_TOPIC_PREFIX
  to "lite" if your deployed Node-RED flow actually uses that prefix.
*/
#include <Arduino.h>
#include "credentials.h"
#include "HardwareConfig.h"
#include <WiFi.h>
#include <PubSubClient.h>
#if USE_I2C
#include <Wire.h>
#endif
#if ENABLE_BMP280
#include <Adafruit_BMP280.h>
#endif
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>
#include <string.h>

#if ENABLE_LCD
#include <SSD1306.h>
#endif
#if USE_IFTTT
#include <WiFiClientSecure.h>
#include "IFTTTWebhook.h"
#endif
#if USE_THINGSPEAK
#include <ThingSpeak.h>
#endif

#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "esp32"
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#include "ExtraSensors.h"

constexpr char VERSION[] = "V1.5.0";
constexpr int INPUT_PIN = GEIGER_INPUT_PIN;
constexpr int LED_PIN = STATUS_LED_PIN;
constexpr int I2C_SDA = SENSOR_I2C_SDA;
constexpr int I2C_SCL = SENSOR_I2C_SCL;
constexpr uint8_t LED_ON = LOW;   // Original board uses active-low LED.
constexpr uint8_t LED_OFF = HIGH;
// Original credentials.h has a trailing semicolon in both MQTT macros.
const char *const brokerHost = MQTT_SERVER;
const uint16_t brokerPort = MQTT_PORT;
constexpr float SEA_LEVEL_HPA = 1013.25f;
constexpr uint32_t CPM_INTERVAL_MS = 15000UL;
constexpr uint32_t CPH_INTERVAL_MS = 3600000UL;
constexpr uint32_t BMP_INTERVAL_MS = 60000UL;
constexpr uint32_t BMP_RETRY_MS = 30000UL;
constexpr uint32_t WIFI_RETRY_MS = 30000UL;
constexpr uint32_t MQTT_RETRY_MS = 5000UL;
constexpr uint32_t CLOUD_RETRY_MS = 60000UL;
constexpr float ALARM_HIGH_CPM = 200.0f;
constexpr float ALARM_LOW_CPM = 100.0f;
constexpr UBaseType_t HOURLY_QUEUE_DEPTH = 24;
constexpr UBaseType_t ALERT_QUEUE_DEPTH = 8;

constexpr char TOPIC_RAD[] = MQTT_TOPIC_PREFIX "/radiation";
constexpr char TOPIC_CPM[] = MQTT_TOPIC_PREFIX "/cpm";
constexpr char TOPIC_TEMP[] = MQTT_TOPIC_PREFIX "/temperature";
constexpr char TOPIC_PRESS[] = MQTT_TOPIC_PREFIX "/pressure";
constexpr char TOPIC_ALT[] = MQTT_TOPIC_PREFIX "/approxaltitude";
constexpr char TOPIC_OUTPUT[] = MQTT_TOPIC_PREFIX "/output";
constexpr char TOPIC_OUTPUT_STATE[] = MQTT_TOPIC_PREFIX "/output/state";
constexpr char TOPIC_STATUS[] = MQTT_TOPIC_PREFIX "/status";
constexpr char TOPIC_ALARM[] = MQTT_TOPIC_PREFIX "/alarm";
constexpr char TOPIC_BMP_STATUS[] = MQTT_TOPIC_PREFIX "/bmp280/status";
constexpr char TOPIC_GEIGER_STATUS[] = MQTT_TOPIC_PREFIX "/geiger/status";
constexpr char TOPIC_LCD_STATUS[] = MQTT_TOPIC_PREFIX "/lcd/status";

// A monotonic (modulo 2^32) counter: the ISR is the only writer.
// Each acquisition interval has its own baseline. No ISR reset races.
portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t totalPulses = 0;
uint32_t cpmBaseline = 0;
uint32_t cphBaseline = 0;
uint32_t lastCpmMs = 0;
uint32_t lastCphMs = 0;
uint32_t lastBmpMs = 0;
uint32_t lastBmpAttemptMs = 0;
bool alarmActive = false;
bool bmpReady = false;
uint8_t bmpAddress = 0x76;
bool measurementWdt = false;
#if ENABLE_LCD
bool lcdReady = false;
uint32_t lastLcdMs = 0;
double lcdCpm = NAN;
float lcdTemperature = NAN;
#endif

struct RadiationSample {
  uint32_t pulses;
  uint32_t elapsedMs;
  double rate;
};

struct CpmSample {
  double rate;
  bool alarm;
};

struct EnvironmentSample {
  float temperature;
  float pressure;
  float altitude;
  bool valid;
};

// Queue length 1 = most recent live observation. Hourly history is FIFO.
QueueHandle_t cpmQueue = nullptr;
QueueHandle_t environmentQueue = nullptr;
QueueHandle_t hourlyQueue = nullptr;
#if USE_IFTTT
QueueHandle_t alertQueue = nullptr;
#endif
#if USE_THINGSPEAK
QueueHandle_t thingSpeakQueue = nullptr;
#endif
#if ENABLE_LCD
QueueHandle_t lcdStatusQueue = nullptr;
#endif

#if ENABLE_BMP280
Adafruit_BMP280 bmp(&Wire);  // One explicit I2C bus, not a duplicate TwoWire(0).
#endif
#if ENABLE_LCD
// OLED shares the BMP280 bus and uses the same SDA/SCL pins.
SSD1306 display(OLED_I2C_ADDRESS, I2C_SDA, I2C_SCL);
#endif

// MQTT client is accessed exclusively from mqttTask.
WiFiClient mqttTransport;
PubSubClient mqttClient(mqttTransport);
bool outputOn = false;
bool outputDirty = true;

void IRAM_ATTR ISR_impulse() {
  portENTER_CRITICAL_ISR(&pulseMux);
  ++totalPulses;
  portEXIT_CRITICAL_ISR(&pulseMux);
}

uint32_t readPulses() {
  portENTER_CRITICAL(&pulseMux);
  const uint32_t count = totalPulses;
  portEXIT_CRITICAL(&pulseMux);
  return count;
}

bool due(uint32_t now, uint32_t previous, uint32_t period) {
  return static_cast<uint32_t>(now - previous) >= period;
}

double normalizedRate(uint32_t pulses, uint32_t elapsedMs, double timebase) {
  return elapsedMs ? (static_cast<double>(pulses) * timebase / elapsedMs) : 0.0;
}

void enqueueFifo(QueueHandle_t queue, const void *sample, const char *label) {
  if (!queue || xQueueSend(queue, sample, 0) != pdPASS) {
    Serial.printf("[QUEUE] %s dropped (queue unavailable/full)\n", label);
  }
}

bool publishNumber(const char *topic, double value, bool retained = false) {
  if (!isfinite(value)) return false;
  char payload[40];
  const int length = snprintf(payload, sizeof(payload), "%.2f", value);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(payload)) return false;
  return mqttClient.publish(topic, payload, retained);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (!ENABLE_LED) return;
  if (strcmp(topic, TOPIC_OUTPUT) != 0) return;
  if (length == 2 && memcmp(payload, "on", 2) == 0) {
    outputOn = true;
  } else if (length == 3 && memcmp(payload, "off", 3) == 0) {
    outputOn = false;
  } else {
    Serial.println("[MQTT] Invalid output command ignored");
    return;
  }
  digitalWrite(LED_PIN, outputOn ? LED_ON : LED_OFF);
  outputDirty = true; // Publish outside callback, after PubSubClient.loop().
}

bool connectMqtt(const char *clientId) {
  bool connected;
  if (strlen(MQTT_USER) != 0) {
    connected = mqttClient.connect(clientId, MQTT_USER, MQTT_PASSWORD,
                                  TOPIC_STATUS, 1, true, "offline");
  } else {
    connected = mqttClient.connect(clientId, TOPIC_STATUS, 1, true, "offline");
  }
  if (!connected) {
    Serial.printf("[MQTT] Connection failed (rc=%d)\n", mqttClient.state());
    return false;
  }
  if ((ENABLE_LED && !mqttClient.subscribe(TOPIC_OUTPUT, 1)) ||
      !mqttClient.publish(TOPIC_STATUS, "online", true) ||
      !mqttClient.publish(TOPIC_GEIGER_STATUS, ENABLE_GEIGER ? "enabled" : "disabled", true) ||
      (!ENABLE_GEIGER && !mqttClient.publish(TOPIC_ALARM, "disabled", true))) {
    mqttTransport.stop();
    return false;
  }
  outputDirty = true;
  Serial.println("[MQTT] Connected");
  return true;
}

void mqttTask(void *parameter) {
  (void)parameter;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(mySSID, myPASSWORD);
  uint32_t lastWifiAttempt = millis();
  uint32_t lastMqttAttempt = millis() - MQTT_RETRY_MS;
  char clientId[32];
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "geiger-%012llX",
           static_cast<unsigned long long>(mac & 0xFFFFFFFFFFFFULL));
  mqttClient.setServer(brokerHost, brokerPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(20);
  mqttClient.setSocketTimeout(3);

  CpmSample cpm = {};
  EnvironmentSample env = {};
  bool cpmPending = false;
  bool envPending = false;
  bool haveCpm = false;
  bool lcdPending = true;
  bool lcdAvailable = false;
  ExtraSensors::Publisher extra;
  bool ntpStarted = false;

  for (;;) {
    const uint32_t now = millis();
    extra.receive();
    if (cpmQueue && xQueueReceive(cpmQueue, &cpm, 0) == pdTRUE) {
      cpmPending = haveCpm = true;
    }
    if (environmentQueue && xQueueReceive(environmentQueue, &env, 0) == pdTRUE) {
      envPending = true;
    }
#if ENABLE_LCD
    if (lcdStatusQueue && xQueueReceive(lcdStatusQueue, &lcdAvailable, 0) == pdTRUE) {
      lcdPending = true;
    }
#endif

    if (WiFi.status() != WL_CONNECTED) {
      if (mqttClient.connected()) mqttTransport.stop();
      if (due(now, lastWifiAttempt, WIFI_RETRY_MS)) {
        lastWifiAttempt = now;
        WiFi.reconnect();
        Serial.println("[WiFi] Reconnecting");
      }
    } else {
      if (!ntpStarted) {
        ExtraSensors::startNtp();
        ntpStarted = true;
      }
      if (!mqttClient.connected() && due(now, lastMqttAttempt, MQTT_RETRY_MS)) {
        // A network call can block this task, never the acquisition loop.
        if (connectMqtt(clientId)) {
          cpmPending = haveCpm;
          envPending = true; // Refresh status even when BMP280 is disabled.
          lcdPending = true;
          extra.statusRefresh = true;
        }
        lastMqttAttempt = millis();
      }
      if (mqttClient.connected() && mqttClient.loop()) {
        bool ok = true;
        if (outputDirty) {
          ok = mqttClient.publish(TOPIC_OUTPUT_STATE,
                                  ENABLE_LED ? (outputOn ? "on" : "off") : "disabled", true);
          if (ok) outputDirty = false;
        }
        if (ok && lcdPending) {
          ok = mqttClient.publish(TOPIC_LCD_STATUS,
                                  ENABLE_LCD ? (lcdAvailable ? "ok" : "unavailable") : "disabled", true);
          if (ok) lcdPending = false;
        }
        if (ok && cpmPending) {
          ok = publishNumber(TOPIC_CPM, cpm.rate) &&
               mqttClient.publish(TOPIC_ALARM, cpm.alarm ? "on" : "off", true);
          if (ok) cpmPending = false;
        }
        if (ok && envPending) {
          ok = mqttClient.publish(TOPIC_BMP_STATUS,
                                  ENABLE_BMP280 ? (env.valid ? "ok" : "unavailable") : "disabled", true);
          if (ok && ENABLE_BMP280 && env.valid) {
            ok = publishNumber(TOPIC_TEMP, env.temperature) &&
                 publishNumber(TOPIC_PRESS, env.pressure) &&
                 publishNumber(TOPIC_ALT, env.altitude);
          }
          if (ok) envPending = false;
        }
        // Remove a historical observation only if publication was accepted.
        RadiationSample hour;
        if (ok && hourlyQueue && xQueuePeek(hourlyQueue, &hour, 0) == pdTRUE) {
          ok = publishNumber(TOPIC_RAD, hour.rate);
          if (ok) xQueueReceive(hourlyQueue, &hour, 0);
        }
        if (ok) ok = extra.publish(mqttClient, millis());
        if (!ok) {
          Serial.println("[MQTT] Publish failed; retry after reconnection");
          mqttTransport.stop();
          lastMqttAttempt = millis();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#if USE_IFTTT || USE_THINGSPEAK
// HTTP/HTTPS must not block MQTT or measurement (separate task).
void cloudTask(void *parameter) {
  (void)parameter;
#if USE_THINGSPEAK
  WiFiClient tsClient;
  ThingSpeak.begin(tsClient);
  uint32_t lastTsAttempt = millis() - CLOUD_RETRY_MS;
#endif
#if USE_IFTTT
  uint32_t lastAlertAttempt = millis() - CLOUD_RETRY_MS;
#endif
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
#if USE_IFTTT
      double alert;
      if (due(millis(), lastAlertAttempt, CLOUD_RETRY_MS) &&
          xQueuePeek(alertQueue, &alert, 0) == pdTRUE) {
        char value[40];
        snprintf(value, sizeof(value), "%.2f", alert);
        IFTTTWebhook webhook(IFTTT_KEY, EVENT_NAME);
        // Preserve original IFTTTWebhook convention: 0 = success.
        const int result = webhook.trigger(value);
        lastAlertAttempt = millis();
        if (result == 0) xQueueReceive(alertQueue, &alert, 0);
        Serial.printf("[IFTTT] Result=%d\n", result);
      }
#endif
#if USE_THINGSPEAK
      RadiationSample hour;
      if (due(millis(), lastTsAttempt, CLOUD_RETRY_MS) &&
          xQueuePeek(thingSpeakQueue, &hour, 0) == pdTRUE) {
        int status = ThingSpeak.writeField(SECRET_CH_ID, 1,
                                           static_cast<float>(hour.rate), SECRET_WRITE_APIKEY);
        lastTsAttempt = millis();
        if (status == 200) xQueueReceive(thingSpeakQueue, &hour, 0);
        Serial.printf("[ThingSpeak] HTTP result=%d\n", status);
      }
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
#endif

#if ENABLE_BMP280
void initBmp() {
  // BMP280 modules commonly use either address.
  bmpAddress = 0x76;
  bmpReady = bmp.begin(bmpAddress);
  if (!bmpReady) {
    bmpAddress = 0x77;
    bmpReady = bmp.begin(bmpAddress);
  }
  lastBmpAttemptMs = millis();
  if (!bmpReady) {
    Serial.println("[BMP280] Not detected; acquisition continues");
    const EnvironmentSample invalid = {NAN, NAN, NAN, false};
    if (environmentQueue) xQueueOverwrite(environmentQueue, &invalid);
  } else {
    Serial.println("[BMP280] Sensor ready");
  }
}

void sampleBmp(uint32_t now) {
  if (!bmpReady) {
    if (due(now, lastBmpAttemptMs, BMP_RETRY_MS)) initBmp();
    if (!bmpReady) return;
  }
  if (!due(now, lastBmpMs, BMP_INTERVAL_MS)) return;
  lastBmpMs = now;
  EnvironmentSample sample = {NAN, NAN, NAN, false};
  Wire.beginTransmission(bmpAddress);
  if (Wire.endTransmission() == 0) {
    sample.temperature = bmp.readTemperature();
    sample.pressure = bmp.readPressure() / 100.0f;
  }
  sample.valid = isfinite(sample.temperature) && isfinite(sample.pressure) &&
                 sample.temperature >= -40.0f && sample.temperature <= 85.0f &&
                 sample.pressure >= 300.0f && sample.pressure <= 1100.0f;
  // Compute altitude from the SAME pressure observation, not a second reading.
  sample.altitude = sample.valid ?
      44330.0f * (1.0f - powf(sample.pressure / SEA_LEVEL_HPA, 0.1903f)) : NAN;
  if (!sample.valid || !isfinite(sample.altitude)) {
    sample.valid = false;
    bmpReady = false;
    lastBmpAttemptMs = millis();
    Serial.println("[BMP280] Invalid reading; will retry sensor");
  } else {
    Serial.printf("[BMP280] %.2f C, %.2f hPa, %.2f m\n",
                  sample.temperature, sample.pressure, sample.altitude);
  }
  if (environmentQueue) xQueueOverwrite(environmentQueue, &sample);
#if ENABLE_LCD
  lcdTemperature = sample.valid ? sample.temperature : NAN;
#endif
}
#endif

#if ENABLE_GEIGER
void sampleRadiation(uint32_t now) {
  const bool cpmDue = due(now, lastCpmMs, CPM_INTERVAL_MS);
  const bool cphDue = due(now, lastCphMs, CPH_INTERVAL_MS);
  if (!cpmDue && !cphDue) return;
  const uint32_t pulses = readPulses();
  if (cpmDue) {
    const uint32_t delta = pulses - cpmBaseline;
    const uint32_t elapsed = now - lastCpmMs;
    cpmBaseline = pulses;
    lastCpmMs = now;
    const double cpm = normalizedRate(delta, elapsed, 60000.0);
    ExtraSensors::setCpm(cpm);
    // Alarm starts armed, including when the first sample is already high.
    if (!alarmActive && cpm >= ALARM_HIGH_CPM) {
      alarmActive = true;
      Serial.println("[ALARM] High CPM");
#if USE_IFTTT
      enqueueFifo(alertQueue, &cpm, "alarm");
#endif
    } else if (alarmActive && cpm < ALARM_LOW_CPM) {
      alarmActive = false;
    }
    const CpmSample sample = {cpm, alarmActive};
    if (cpmQueue) xQueueOverwrite(cpmQueue, &sample);
    Serial.printf("[CPM] pulses=%lu elapsed=%lu ms CPM=%.2f\n",
                  static_cast<unsigned long>(delta),
                  static_cast<unsigned long>(elapsed), cpm);
#if ENABLE_LCD
    lcdCpm = cpm;
#endif
  }
  if (cphDue) {
    const uint32_t delta = pulses - cphBaseline;
    const uint32_t elapsed = now - lastCphMs;
    cphBaseline = pulses;
    lastCphMs = now;
    const RadiationSample sample = {delta, elapsed, normalizedRate(delta, elapsed, 3600000.0)};
    enqueueFifo(hourlyQueue, &sample, "MQTT hourly");
#if USE_THINGSPEAK
    enqueueFifo(thingSpeakQueue, &sample, "ThingSpeak hourly");
#endif
    Serial.printf("[CPH] pulses=%lu elapsed=%lu ms CPH=%.2f\n",
                  static_cast<unsigned long>(delta),
                  static_cast<unsigned long>(elapsed), sample.rate);
  }
}
#endif

#if ENABLE_LCD
void sampleLcd(uint32_t now) {
  if (!due(now, lastLcdMs, CPM_INTERVAL_MS)) return;
  lastLcdMs = now;
  Wire.beginTransmission(OLED_I2C_ADDRESS);
  const bool present = Wire.endTransmission() == 0;
  if (present != lcdReady) {
    lcdReady = present;
    if (lcdReady) {
      display.init();
      display.flipScreenVertically();
      display.setFont(ArialMT_Plain_24);
    }
    Serial.println(lcdReady ? "[OLED] Present" : "[OLED] Unavailable");
  }
  if (lcdStatusQueue) xQueueOverwrite(lcdStatusQueue, &lcdReady);
  if (!lcdReady) return;
  display.clear();
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (ENABLE_GEIGER && isfinite(lcdCpm)) {
    display.drawString(64, 0, alarmActive ? "ALARM" : "Radioactivity");
    display.drawString(64, 30, String(lcdCpm, 1));
  } else if (ENABLE_BMP280 && isfinite(lcdTemperature)) {
    display.drawString(64, 0, "Temp. C");
    display.drawString(64, 30, String(lcdTemperature, 1));
  } else {
    display.drawString(64, 0, "ESP32");
    display.drawString(64, 30, "No data");
  }
  display.display();
}
#endif

void setup() {
  Serial.begin(115200);
  Serial.printf("Geiger counter %s\n", VERSION);
  Serial.printf("[CONFIG] Geiger=%d BMP280=%d OLED=%d LED=%d IFTTT=%d ThingSpeak=%d\n",
                ENABLE_GEIGER, ENABLE_BMP280, ENABLE_LCD, ENABLE_LED, USE_IFTTT, USE_THINGSPEAK);
  if (ENABLE_LED) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
  }
#if ENABLE_GEIGER
  pinMode(INPUT_PIN, INPUT);
  lastCpmMs = lastCphMs = millis();
  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), ISR_impulse, FALLING);

  cpmQueue = xQueueCreate(1, sizeof(CpmSample));
  hourlyQueue = xQueueCreate(HOURLY_QUEUE_DEPTH, sizeof(RadiationSample));
#endif
#if ENABLE_BMP280
  environmentQueue = xQueueCreate(1, sizeof(EnvironmentSample));
#endif
#if ENABLE_LCD
  lcdStatusQueue = xQueueCreate(1, sizeof(bool));
#endif
#if USE_IFTTT
  alertQueue = xQueueCreate(ALERT_QUEUE_DEPTH, sizeof(double));
#endif
#if USE_THINGSPEAK
  thingSpeakQueue = xQueueCreate(HOURLY_QUEUE_DEPTH, sizeof(RadiationSample));
#endif

  // Use core-provided TWDT if initialized; no global reconfiguration, which
  // differs between Arduino-ESP32 2.x and 3.x. Only monitor acquisition.
  if (esp_task_wdt_status(nullptr) == ESP_OK ||
      esp_task_wdt_add(nullptr) == ESP_OK) {
    measurementWdt = true;
  } else {
    Serial.println("[WDT] Task watchdog unavailable in this core configuration");
  }

  // Shared v6 I2C bus: GPIO21/22, one Wire instance.
#if USE_I2C
  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  Wire.setTimeOut(50);
#endif
#if ENABLE_LCD
  lastLcdMs = millis() - CPM_INTERVAL_MS;
#endif
#if ENABLE_BMP280
  lastBmpMs = millis() - BMP_INTERVAL_MS; // First sample as soon as ready.
  initBmp();
#endif
  ExtraSensors::begin();

  if ((ENABLE_GEIGER && (!cpmQueue || !hourlyQueue)) ||
      (ENABLE_BMP280 && !environmentQueue) ||
#if ENABLE_LCD
      !lcdStatusQueue ||
#endif
      xTaskCreate(mqttTask, "geiger-mqtt", 6144, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("[ERROR] MQTT task unavailable; local measurement continues");
  }
#if USE_IFTTT || USE_THINGSPEAK
  bool cloudReady = true;
#if USE_IFTTT
  cloudReady = cloudReady && alertQueue != nullptr;
#endif
#if USE_THINGSPEAK
  cloudReady = cloudReady && thingSpeakQueue != nullptr;
#endif
  if (!cloudReady ||
      xTaskCreate(cloudTask, "geiger-cloud", 12288, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("[ERROR] Cloud task unavailable; local measurement continues");
  }
#endif
}

void loop() {
  if (measurementWdt) esp_task_wdt_reset();
#if ENABLE_GEIGER
  sampleRadiation(millis());
#endif
#if ENABLE_BMP280
  sampleBmp(millis());
#endif
#if ENABLE_LCD
  sampleLcd(millis());
#endif
  ExtraSensors::tick(millis());
  delay(10);
}
