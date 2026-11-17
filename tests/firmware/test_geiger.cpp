#include <cassert>
#include <cstring>
#include "../../firmware/ArduinoGeigerCounterMix/GeigerStats.h"
int main() {
    using namespace GeigerStats;
    Accumulator a;
    char json[640];
    auto r = a.record(15000,5,15000);
    assert(r.total==5 && !r.minute.ready && !r.hour.ready);
    assert(encode(r,json,sizeof json));
    assert(strstr(json,"\"cpm60\":null") && strstr(json,"\"cph_est\":1200.000") && strstr(json,"\"dose_rate_est\":null"));
    assert(!encode(r,json,10));
    for(unsigned i=2;i<=240;++i) {
        r=a.record(i*15000,5,15000);
        assert(r.minute.ready==(i>=4));
        assert(r.fiveMinutes.ready==(i>=20));
        assert(r.hour.ready==(i>=240));
    }
    assert(r.total==1200 && r.minute.counts==20 && r.fiveMinutes.counts==100 && r.hour.counts==1200);
    assert(r.minute.cpm()==20 && r.hour.elapsed==3600000 && r.hour.cpm()*60==1200);
    assert(encode(r,json,sizeof json,0.00332));
    assert(strstr(json,"\"dose_rate_est\":0.066400") && strstr(json,"\"dose_total_est\":0.066400") && strstr(json,"\"dose_ratio\":0.003320000"));
    r=a.record(3615000,10,15000);
    assert(r.total==1210 && r.hour.counts==1205 && r.minute.counts==25);
    Accumulator delayed;
    r=delayed.record(120000,20,120000);
    assert(!r.minute.ready && !r.fiveMinutes.ready);
    for(unsigned i=1;i<=4;++i) r=delayed.record(120000+i*15000,0,15000);
    assert(r.minute.ready && r.minute.counts==0 && r.minute.cpm()==0);
    assert(encode(r,json,sizeof json));
    assert(strstr(json,"\"sigma_cpm\":null"));
    Accumulator irregular;
    for(unsigned i=1;i<=4;++i) r=irregular.record(i*15100,5,15100);
    assert(r.minute.ready && r.minute.elapsed==60400 && r.minute.cpm()<20);
    puts("PASS: Geiger warm-up, rollover, actual exposure, gaps, zero counts, dose opt-in");
}
