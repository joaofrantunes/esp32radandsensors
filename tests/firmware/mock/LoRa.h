#pragma once
struct FakeLoRa {
  bool ready=true,sending=false; unsigned packets=0;
  void setPins(int,int,int){}
  bool begin(long){return ready;}
  void end(){sending=false;}
  bool isTransmitting(){return sending;}
  bool beginPacket(){return !sending;}
  void print(const char*){}
  int endPacket(bool async){sending=async;++packets;return 1;}
};
inline FakeLoRa LoRa;
