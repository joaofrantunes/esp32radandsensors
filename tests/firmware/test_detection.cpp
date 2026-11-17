#include <cassert>
#include "../../firmware/ArduinoGeigerCounterMix/ArduinoGeigerCounterMix.ino"
int main() {
    using namespace ExtraSensors;
    setup();
    assert(bmpReady && barometerKind==BARO_BMP280 && bmpAddress==0x77);
    Wire.baroAddress=0x76; Wire.baroChip=0x60; initBmp();
    assert(bmpReady && barometerKind==BARO_BME280 && bmpAddress==0x76);
    fakeMs=60000; sampleBmp(fakeMs);
    EnvironmentSample sample; assert(xQueuePeek(environmentQueue,&sample,0));
    assert(sample.valid && sample.temperature==22 && sample.pressure==1010);
    Wire.baroChip=0x42; initBmp(); assert(!bmpReady && barometerKind==BARO_NONE);
    Wire.baroChip=0x58; initBmp(); assert(bmpReady && barometerKind==BARO_BMP280);
    Snapshot s=initialSnapshot();
    assert(readMag(s) && magKind==MAG_HMC5883L && fabs(s.x-100)<0.001);
    Wire.magnetometer=0x0d;
    assert(!readMag(s) && magKind==MAG_NONE);
    assert(readMag(s) && magKind==MAG_QMC5883L);
    assert(fabs(s.x-100)<0.001 && fabs(s.y+50)<0.001 && fabs(s.z-30)<0.001);
    assert(s.headingValid && s.heading>333 && s.heading<334);
    Wire.qmcStatus=3; assert(!readMag(s)); // overflow
    Wire.qmcStatus=0; assert(!readMag(s)); // data not ready
    Wire.qmcStatus=1; assert(readMag(s));
    Wire.present=false; assert(!readMag(s) && magKind==MAG_NONE);
    Wire.present=true; Wire.qmcId=0x42; assert(!readMag(s) && magKind==MAG_NONE);
    Wire.magnetometer=0x1e; Wire.hmcIdValid=false;
    assert(!readMag(s) && magKind==MAG_NONE);
    Wire.hmcIdValid=true; assert(readMag(s) && magKind==MAG_HMC5883L);
    double humidity=0,temperature=0;
    for (uint8_t command : {0xbe,0xe1}) {
        Wire.ahtCalibrated=false; Wire.ahtInitCommand=command;
        assert(readAht(humidity,temperature) && Wire.ahtCalibrated);
        assert(fabs(humidity-50)<0.001 && fabs(temperature-25)<0.001);
    }
    Wire.busy=true; assert(!readAht(humidity,temperature)); Wire.busy=false;
    puts("PASS: BMP/BME and HMC/QMC identification, conversion, removal, recovery and invalid IDs");
    puts("PASS: AHT10/AHT20-compatible initialization and busy rejection");
}
