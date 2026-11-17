#include <cassert>
#include "../../firmware/ArduinoGeigerCounterMix/ArduinoGeigerCounterMix.ino"

int main() {
    using namespace ExtraSensors;
    setup();
    fakeMs=1000;
    tick(fakeMs);
    assert(loraSending && loraReady && LoRa.packets==1 && LoRa.starts==1);
    fakeMs+=100;
    tick(fakeMs);
    assert(loraSending && LoRa.packets==1 && LoRa.stops==0);
    LoRa.sending=false; // Radio has completed TX.
    fakeMs+=100;
    tick(fakeMs);
    assert(!loraSending && loraReady && LoRa.packets==1 && LoRa.stops==0);
    fakeMs=61000;
    tick(fakeMs);
    assert(loraSending && LoRa.packets==2 && LoRa.starts==1);
    // A transmitter stuck in TX must not stall acquisition or send another packet.
    fakeMs=loraStarted+5000;
    tick(fakeMs);
    assert(!loraSending && !loraReady && LoRa.stops==1 && LoRa.packets==2);
    fakeMs=121000;
    tick(fakeMs);
    assert(loraSending && loraReady && LoRa.starts==2 && LoRa.packets==3);
    puts("PASS: public LoRa API, async completion, bounded timeout and recovery");
}
