#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include <deque>
#include <vector>
#include <string>
#include <cstring>
#include <time.h>
using byte=uint8_t;
#define IRAM_ATTR
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define FALLING 2
#define ESP_OK 0
using UBaseType_t=unsigned;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL_ISR(portMUX_TYPE*){}
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE*){}
inline void portENTER_CRITICAL(portMUX_TYPE*){}
inline void portEXIT_CRITICAL(portMUX_TYPE*){}
inline uint32_t fakeMs=0;
inline uint32_t millis(){return fakeMs;}
inline void delay(int){}
inline int pinModeCalls=0;
inline void pinMode(int,int){++pinModeCalls;}
inline int pinValue=-1;
inline void digitalWrite(int,int value){pinValue=value;}
inline int digitalPinToInterrupt(int p){return p;}
inline int interruptCalls=0;
inline void attachInterrupt(int,void(*)(),int){++interruptCalls;}
struct SerialType{void begin(int){} void println(const char*){} template<class... T> void printf(const char*,T...){} };
inline SerialType Serial;
constexpr int SERIAL_8N1=0, ADC_11db=3;
struct FakeSerial2 {std::deque<int> bytes; bool started=false; void setRxBufferSize(size_t){} void begin(int,int,int,int){started=true;} int available(){return bytes.size();} int read(){int c=bytes.front();bytes.pop_front();return c;}};
inline FakeSerial2 Serial2;
inline unsigned fakeMilliVolts=1650;
inline void analogSetPinAttenuation(int,int){}
inline void analogReadResolution(int){}
inline int analogRead(int){return 2000;}
inline uint32_t analogReadMilliVolts(int){return fakeMilliVolts;}
inline void configTime(long,int,const char*){}
inline int hallRead(){return 0;}
inline float temperatureRead(){return 42;}
struct ESPType{uint64_t getEfuseMac(){return 42;}};
inline ESPType ESP;
struct String{String(double,int){}};
constexpr int WHITE=1, TEXT_ALIGN_CENTER=0, ArialMT_Plain_24=0;
struct SSD1306{SSD1306(int,int,int){} void init(){} void flipScreenVertically(){} void setFont(int){} void clear(){} void setColor(int){} void setTextAlignment(int){} void drawString(int,int,const char*){} void drawString(int,int,String){} void display(){}};
struct Queue { size_t capacity,bytes; std::deque<std::vector<unsigned char>> entries; };
using QueueHandle_t=Queue*;
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
inline QueueHandle_t xQueueCreate(size_t capacity,size_t bytes){return new Queue{capacity,bytes,{}};}
inline int xQueueSend(QueueHandle_t q,const void* data,int){if(q->entries.size()>=q->capacity)return 0;auto p=(const unsigned char*)data;q->entries.emplace_back(p,p+q->bytes);return 1;}
inline int xQueueOverwrite(QueueHandle_t q,const void* data){q->entries.clear();return xQueueSend(q,data,0);}
inline int xQueuePeek(QueueHandle_t q,void* data,int){if(q->entries.empty())return 0;memcpy(data,q->entries.front().data(),q->bytes);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* data,int t){if(!xQueuePeek(q,data,t))return 0;q->entries.pop_front();return 1;}
inline int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*){return pdPASS;}
inline void vTaskDelay(int){}
inline int esp_task_wdt_status(void*){return ESP_OK;}
inline int esp_task_wdt_add(void*){return ESP_OK;}
inline int esp_task_wdt_reset(){return ESP_OK;}
#define WIFI_STA 1
#define WL_CONNECTED 3
struct WiFiClient{void stop(){}};
struct WiFiType{void mode(int){} void setAutoReconnect(bool){} void begin(const char*,const char*){} int status(){return WL_CONNECTED;} bool reconnect(){return true;}};
inline WiFiType WiFi;
struct PubSubClient{
  PubSubClient(WiFiClient&){}
  bool connect(const char*,const char*,int,bool,const char*){return true;}
  bool connect(const char*,const char*,const char*,const char*,int,bool,const char*){return true;}
  std::vector<std::pair<std::string,std::string>> publications;
  unsigned subscriptions=0;
  bool publish(const char* topic,const char* value,bool=false){publications.emplace_back(topic,value);return true;}
  bool subscribe(const char*,int){++subscriptions;return true;}
  bool connected(){return true;}
  bool loop(){return true;}
  int state(){return 0;}
  void setServer(const char*,int){}
  void setCallback(void(*)(char*,byte*,unsigned int)){}
  void setKeepAlive(int){}
  void setSocketTimeout(int){}
};
struct TwoWire {
  unsigned starts=0; bool present=true; bool busy=false; int address=0; uint8_t reg=0;
  std::deque<int> rx;
  void begin(int,int,int){++starts;} void setTimeOut(int){} void beginTransmission(int a){address=a;}
  size_t write(const uint8_t *b,size_t n){if(n)reg=b[0];return n;}
  int endTransmission(){return present?0:4;}
  size_t requestFrom(uint8_t addr,size_t len){
    rx.clear(); if(!present)return 0;
    if(addr==0x38 && len==1)rx={busy?0x88:0x08};
    if(addr==0x38 && len==6)rx={busy?0x88:0x08,0x80,0,0x06,0,0};
    if(addr==0x1e && reg==0x0a)rx={'H','4','3'};
    if(addr==0x1e && reg==0x09)rx={1};
    if(addr==0x1e && reg==0x03)rx={0x04,0x4c,0,0,0,0};
    return rx.size();
  }
  int read(){if(rx.empty())return -1;int v=rx.front();rx.pop_front();return v;}
};
inline TwoWire Wire;
struct Adafruit_BMP280{Adafruit_BMP280(TwoWire*){} bool begin(int addr){return addr==0x77;} float readTemperature(){return 21;} float readPressure(){return 101325;}};
struct IFTTTWebhook{IFTTTWebhook(const char*,const char*){} int trigger(const char*){return 0;}};
struct ThingSpeakType {void begin(WiFiClient&){} int writeField(unsigned long,int,float,const char*){return 200;}};
inline ThingSpeakType ThingSpeak;
