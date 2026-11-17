#include "Arduino.h"
struct FakeLocation {
  bool valid=false;
  unsigned long ageMs=0;
  double latitude=0,longitude=-9.123456;
  bool isValid(){return valid;}
  unsigned long age(){return ageMs;}
  double lat(){return latitude;}
  double lng(){return longitude;}
};
struct FakeGpsValue {
  bool valid=true; unsigned long ageMs=0;
  bool isValid(){return valid;} unsigned long age(){return ageMs;}
  double meters(){return 42.0;} double hdop(){return 1.2;} unsigned long value(){return 8;}
};
struct TinyGPSPlus { FakeLocation location; FakeGpsValue altitude, hdop, satellites; bool encode(char){return true;} };
