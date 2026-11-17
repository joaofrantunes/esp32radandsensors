#include <cassert>
#include "../../firmware/ArduinoGeigerCounterMix/ArduinoGeigerCounterMix.ino"
int main(){
    setup();
    assert(!!cpmQueue == !!ENABLE_GEIGER);
    assert(!!hourlyQueue == !!ENABLE_GEIGER);
    assert(!!environmentQueue == !!ENABLE_BMP280);
    assert(interruptCalls == ENABLE_GEIGER);
    assert(pinModeCalls == ENABLE_GEIGER + ENABLE_LED);
    assert(Wire.starts == !!(ENABLE_BMP280 || ENABLE_LCD));
    connectMqtt("mock");
    assert(mqttClient.subscriptions == ENABLE_LED);
    bool statusPublished = false;
    for (const auto &p : mqttClient.publications)
        if (p.first == TOPIC_GEIGER_STATUS)
            statusPublished = p.second == (ENABLE_GEIGER ? "enabled" : "disabled");
    assert(statusPublished);
    char topic[]=MQTT_TOPIC_PREFIX "/output";
    byte on[]={'o','n'};
    const int before=pinValue;
    mqttCallback(topic,on,2);
    assert(pinValue == (ENABLE_LED ? LOW : before));
    fakeMs=15000;
#if ENABLE_GEIGER
    for(int i=0;i<50;i++) ISR_impulse();
    sampleRadiation(fakeMs);
    CpmSample c; assert(xQueuePeek(cpmQueue,&c,0) && c.rate==200 && c.alarm);
    fakeMs=30000; sampleRadiation(fakeMs); assert(!alarmActive);
    fakeMs=3600000; sampleRadiation(fakeMs);
    RadiationSample h; assert(xQueuePeek(hourlyQueue,&h,0) && h.rate==50 && h.pulses==50);
#endif
#if ENABLE_BMP280
    sampleBmp(fakeMs);
    EnvironmentSample env; assert(xQueuePeek(environmentQueue,&env,0) && env.valid);
    Wire.present=false; fakeMs+=60000; sampleBmp(fakeMs);
    assert(xQueuePeek(environmentQueue,&env,0) && !env.valid);
    Wire.present=true;
#endif
#if ENABLE_LCD
    sampleLcd(fakeMs); assert(lcdReady);
    Wire.present=false; fakeMs+=15000; sampleLcd(fakeMs); assert(!lcdReady);
    bool ready=true; assert(xQueuePeek(lcdStatusQueue,&ready,0) && !ready);
    Wire.present=true; fakeMs+=15000; sampleLcd(fakeMs); assert(lcdReady);
#endif
}
