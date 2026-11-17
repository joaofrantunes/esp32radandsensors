#include <cassert>
#include "../../firmware/ArduinoGeigerCounterMix/ArduinoGeigerCounterMix.ino"

using namespace ExtraSensors;
bool emitted(const char *topic,const char *value=nullptr) {
  for (const auto &p : mqttClient.publications)
    if (p.first==topic && (!value || p.second==value)) return true;
  return false;
}
int main(){
  setup();
  assert(Serial2.started);
  char stamp[25];
  assert(!formatUtc(0,stamp,sizeof stamp));
  assert(formatUtc(1767225600,stamp,sizeof stamp));
  assert(strcmp(stamp,"2026-01-01T00:00:00Z")==0);
  uint8_t bytes[]={0x08,0x80,0,0x06,0,0};
  double rh=0,temp=0;
  assert(decodeAht(bytes,rh,temp) && rh==50 && temp==25);
  bytes[0]=0x88; assert(!decodeAht(bytes,rh,temp));
  gps.location.valid=true;
  Serial2.bytes.push_back('X');
  tick(100);
  Publisher pub; pub.receive();
  assert(pub.sample.state[AHT]==SENSOR_STATE_OK && pub.sample.state[GPS]==SENSOR_STATE_OK);
  assert(pub.sample.state[MAG]==SENSOR_STATE_OK && pub.sample.headingValid && pub.sample.heading==0);
  assert(pub.sample.state[VOLTAGE]==SENSOR_STATE_OK && fabs(pub.sample.voltage-3.72)<0.0001);
  assert(pub.sample.state[INTTEMP]==SENSOR_STATE_OK);
#if EXTRA_HALL_SUPPORTED
  assert(pub.sample.state[HALL]==SENSOR_STATE_OK && pub.sample.hall==0);
#else
  assert(pub.sample.state[HALL]==SENSOR_STATE_UNSUPPORTED);
#endif
  assert(LoRa.packets==1);
  assert(pub.publish(mqttClient,100));
  assert(emitted("esp32/lat_dd","0.000000"));
  assert(emitted("esp32/lon_dd","-9.123456"));
  assert(emitted("esp32/magangle","0.00"));
  assert(emitted("esp32/humidity","50.00"));
  assert(emitted("esp32/dewpoint","13.85"));
  assert(emitted("esp32/gps/altitude","42.00"));
  assert(emitted("esp32/gps/hdop","1.20"));
  assert(emitted("esp32/gps/satellites","8"));
  assert(emitted("esp32/mag/magnitude","100.00"));
  assert(emitted("esp32/voltage","3.720"));
  mqttClient.publications.clear();
  pub.statusRefresh=true;
  assert(pub.publish(mqttClient,100));
  assert(!emitted("esp32/humidity"));
  pub.pending=pub.statusRefresh=true;
  assert(pub.publish(mqttClient,100000));
  assert(emitted("esp32/gps/status","stale") && !emitted("esp32/lat_dd"));
  mqttClient.publications.clear();
  Wire.present=false; fakeMilliVolts=3100;
  gps.location.ageMs=9000; Serial2.bytes.push_back('X');
  tick(60100);
  pub.receive();
  assert(pub.sample.state[AHT]==SENSOR_STATE_UNAVAILABLE && pub.sample.state[MAG]==SENSOR_STATE_UNAVAILABLE);
  assert(pub.sample.state[VOLTAGE]==SENSOR_STATE_UNAVAILABLE && pub.sample.state[GPS]==SENSOR_STATE_NO_FIX);
  assert(pub.publish(mqttClient,60100));
  assert(!emitted("esp32/humidity") && !emitted("esp32/lat_dd") && !emitted("esp32/voltage"));
  assert(!emitted("esp32/dewpoint") && !emitted("esp32/gps/altitude") && !emitted("esp32/mag/magnitude"));
  puts("Extra sensor mock logic PASS");
}
