#pragma once
struct FakeLoRa {
  bool ready=true,sending=false; unsigned packets=0, starts=0, stops=0;
  void setPins(int,int,int){}
  bool begin(long){++starts;return ready;}
  void end(){++stops;sending=false;}
  int beginPacket(int=0){return isTransmitting()?0:1;}
  void print(const char*){}
  int endPacket(bool async){sending=async;++packets;return 1;}
private:
  // Match the real Sandeep Mistry library: callers must use public methods.
  bool isTransmitting(){return sending;}
};
inline FakeLoRa LoRa;
