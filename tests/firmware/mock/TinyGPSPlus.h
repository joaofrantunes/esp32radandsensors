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
struct TinyGPSPlus { FakeLocation location; bool encode(char){return true;} };
