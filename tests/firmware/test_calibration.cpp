#include <cassert>
#include "../../firmware/ArduinoGeigerCounterMix/ArduinoGeigerCounterMix.ino"
int main() {
    using namespace ExtraSensors;
    Snapshot s=initialSnapshot();
    assert(readMag(s)); // HMC raw 100,0,0 uT (mock)
    assert(fabs(s.x-180)<0.001 && fabs(s.y+5)<0.001 && fabs(s.z-3)<0.001);
    assert(s.headingValid && s.heading>8.40 && s.heading<8.42);
    puts("PASS: magnetic offset, scale and declination correction");
}
