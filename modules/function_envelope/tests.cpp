// SPDX-License-Identifier: GPL-3.0-only
#include "dsp.h"
#include "../../tests/native/abi2/utility_port_tests.h"
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
static void latched_curve(){auto a=std::make_unique<DspFixture>(ws::Module<ws2ports::FunctionEnvelope>::descriptor()),b=std::make_unique<DspFixture>(ws::Module<ws2ports::FunctionEnvelope>::descriptor());for(auto*f:{a.get(),b.get()}){f->ins[0].flags=WS2_CONNECTED|WS2_CONSTANT;f->input[0][0][0]=1;f->parameter[2][0][0]=-1;f->run(1);}b->parameter[2][0][0]=1;a->run(17);b->run(17);for(int i=0;i<17;++i)VERIFY(a->output[0][0][i]==b->output[0][0][i]);float held=b->output[0][0][16];a->input[0][0][0]=b->input[0][0][0]=0;a->run(17);b->run(17);VERIFY(b->output[0][0][0]>0&&b->output[0][0][0]<held);bool differ=false;for(int i=0;i<17;++i)differ|=a->output[0][0][i]!=b->output[0][0][i];VERIFY(differ);b->reset();b->run(1);VERIFY(b->output[0][0][0]==0);}

// ---- MOD-1 (lab 0024 §4.1): Trigger mode, hold and delay. Written before the DSP.
// A frozen, independent copy of the pre-MOD-1 per-sample calculation (lab c083b26).
struct OldEnvelope {
    double level=0,start=0,endpoint=0,rise=.01,fall=.1,curve=0,latched_curve=0;
    uint64_t length=1,elapsed=1;bool high=false;float last_rise=.01f,last_fall=.1f,last_curve=0;
    float sample(float gate,float r,float f,float c,double rate){
        if(r!=last_rise){rise=r;last_rise=r;}if(f!=last_fall){fall=f;last_fall=f;}if(c!=last_curve){curve=c;last_curve=c;}
        bool h=std::isfinite(gate)&&gate>=.5f;
        if(h!=high){high=h;start=level;endpoint=h?1.:0.;latched_curve=curve;double s=h?rise:fall;
            length=std::max<uint64_t>(1,static_cast<uint64_t>(std::llround(s*rate)));elapsed=0;}
        if(elapsed<length){++elapsed;if(elapsed==length)level=endpoint;else{double u=static_cast<double>(elapsed)/static_cast<double>(length),k=4.*latched_curve;
            double shaped=k==0.?u:std::expm1(k*u)/std::expm1(k);level=start+(endpoint-start)*shaped;}}
        return static_cast<float>(level);
    }
};
using Fe=ws::Module<ws2ports::FunctionEnvelope>;
static std::unique_ptr<DspFixture> rig(double rate=48000.,uint32_t channels=1){
    auto f=std::make_unique<DspFixture>(Fe::descriptor(),rate,channels);
    const auto*d=f->d;
    VERIFY(d->param_count==6);
    const char*ids[]={"rise","fall","curve","mode","hold","delay"};
    for(uint32_t p=0;p<6;++p)VERIFY(!std::strcmp(d->params[p].id,ids[p])&&d->params[p].legacy_index==p);
    VERIFY(d->params[3].flags==(WS2_STEPPED|WS2_AUTOMATABLE)&&d->params[3].initial==0&&d->params[3].maximum==1);
    for(uint32_t p:{4u,5u})VERIFY(d->params[p].flags==ws2ports::port_flags&&d->params[p].initial==0&&
                                  d->params[p].minimum==0&&d->params[p].maximum==30);
    f->ins[0].flags=WS2_CONNECTED;
    return f;
}
static float bits_equal(float a,float b){return std::memcmp(&a,&b,4)==0;}
// Gate mode with delay 0 is bit-identical to the frozen copy, including parameter changes.
static void gate_mode_matches_the_old_calculation(){
    for(double rate:{44100.,48000.,96000.}){
        auto f=rig(rate);OldEnvelope old;uint32_t frame=0;
        for(uint32_t count:{1u,17u,64u,128u,17u,256u,64u,128u,200u}){
            const float r=frame<300?.001f:.0007f,fl=frame<500?.002f:.0013f,c=frame<200?.5f:-.75f;
            f->parameter[0][0][0]=r;f->parameter[1][0][0]=fl;f->parameter[2][0][0]=c;
            f->parameter[4][0][0]=.2f; // hold is inert in Gate mode
            for(uint32_t i=0;i<count;++i){const auto t=frame+i;f->input[0][0][i]=(t>=5&&t<68)||(t>=110&&t<177)||(t>=343&&t<455)||t>=700?1.f:0.f;}
            f->run(count);
            for(uint32_t i=0;i<count;++i)VERIFY(bits_equal(old.sample(f->input[0][0][i],r,fl,c,rate),f->output[0][0][i]));
            frame+=count;
        }
    }
}
// Trigger: delay -> rise -> hold -> fall to 0, ignoring the falling edge.
static void trigger_timing(){
    auto f=rig();
    f->parameter[0][0][0]=.001f;  // rise 48
    f->parameter[1][0][0]=.001f;  // fall 48
    f->parameter[3][0][0]=1;      // Trigger
    f->parameter[4][0][0]=.0005f; // hold 24
    for(uint32_t i=0;i<256;++i)f->input[0][0][i]=i<4?1.f:0.f;
    f->run(256);
    const float*o=f->output[0][0];
    VERIFY(o[0]>0&&o[46]<1&&o[47]==1);
    for(uint32_t i=47;i<=47+24;++i)VERIFY(o[i]==1);  // hold: 24 samples after the rise ends
    VERIFY(o[72]<1&&o[72]>0);
    VERIFY(o[71+47]>0&&o[71+48]==0);
    for(uint32_t i=120;i<256;++i)VERIFY(o[i]==0);
}
// A new edge restarts from the current level: no jump to 0 or 1.
static void trigger_retrigger_level(){
    auto f=rig();
    f->parameter[0][0][0]=.001f;f->parameter[1][0][0]=.002f;f->parameter[3][0][0]=1;
    for(uint32_t i=0;i<256;++i)f->input[0][0][i]=i<4||(i>=100&&i<104)?1.f:0.f;
    f->run(256);
    const float*o=f->output[0][0];
    VERIFY(o[99]<1&&o[99]>.3f);           // falling
    VERIFY(o[100]>o[99]&&o[100]<o[99]+.05f);
    VERIFY(o[147]==1);                    // the restarted rise lasts 48 samples
}
// Delay, Gate mode: from idle, the old calculation fed a gate that rises n samples later.
static void gate_delay_shifts_the_rise(){
    for(double rate:{44100.,48000.,96000.}){
        auto f=rig(rate);OldEnvelope old;
        f->parameter[0][0][0]=.001f;f->parameter[1][0][0]=.002f;f->parameter[2][0][0]=.3f;
        f->parameter[5][0][0]=.0005f;
        const uint32_t n=static_cast<uint32_t>(std::llround(.0005f*rate));uint32_t frame=0;
        for(uint32_t count:{1u,17u,64u,128u,17u,256u}){
            for(uint32_t i=0;i<count;++i){const auto t=frame+i;f->input[0][0][i]=t>=5&&t<300?1.f:0.f;}
            f->run(count);
            for(uint32_t i=0;i<count;++i){const auto t=frame+i;
                const float shifted=t>=5+n&&t<300?1.f:0.f;
                VERIFY(bits_equal(old.sample(shifted,.001f,.002f,.3f,rate),f->output[0][0][i]));}
            frame+=count;
        }
    }
}
// Delay, Gate mode: a fall during the wait cancels the rise; the fall starts from the current level.
static void gate_falls_during_the_wait(){
    auto f=rig();
    f->parameter[0][0][0]=.001f;f->parameter[1][0][0]=.002f;
    for(uint32_t i=0;i<256;++i)f->input[0][0][i]=i<100||(i>=150&&i<180)?1.f:0.f;
    f->run(100);                          // risen to 1
    VERIFY(f->output[0][0][99]==1);
    std::copy(f->input[0][0]+100,f->input[0][0]+256,f->input[0][0]);
    f->parameter[5][0][0]=100.f/48000.f;  // rise at 150 waits 100 samples; the gate falls at 180
    f->run(156);
    const float*o=f->output[0][0];        // index = t - 100
    for(uint32_t i=1;i<80;++i)VERIFY(o[i]<=o[i-1]);   // the fall from 100 keeps going through the wait
    VERIFY(o[80]<o[79]&&o[80]>0);         // fall restarted from the current level at 180
    for(uint32_t i=81;i<156;++i)VERIFY(o[i]<=o[i-1]);
}
// Delay, Trigger mode, with a retrigger inside the wait (restarts the wait) and latching.
static void trigger_delay_and_latching(){
    auto f=rig();
    f->parameter[0][0][0]=.001f;f->parameter[1][0][0]=.001f;f->parameter[3][0][0]=1;
    f->parameter[4][0][0]=.0005f;f->parameter[5][0][0]=.0005f;  // hold 24, delay 24
    for(uint32_t i=0;i<256;++i)f->input[0][0][i]=i<4||(i>=10&&i<14)?1.f:0.f;
    f->run(20);
    // Changes after the start edge (at 10) do not affect this cycle.
    f->parameter[0][0][0]=.01f;f->parameter[1][0][0]=.01f;f->parameter[4][0][0]=1;f->parameter[5][0][0]=1;
    std::fill_n(f->input[0][0],256,0.f);
    f->run(236);
    std::vector<float> o(f->output[0][0],f->output[0][0]+236);  // index = t - 20
    // attack starts at 10 + 24 = 34
    for(uint32_t t=20;t<34;++t)VERIFY(o[t-20]==0);
    VERIFY(o[34-20]>0&&o[34+47-20]==1);
    for(uint32_t t=34+47;t<=34+47+24;++t)VERIFY(o[t-20]==1);
    VERIFY(o[34+47+25-20]<1&&o[34+47+24+48-20]==0);
}
static void sixteen_channels(){
    auto f=rig(48000.,16);
    f->pars[5].channels=16;f->parameter[0][0][0]=.001f;
    for(uint32_t c=0;c<16;++c){f->parameter[5][c][0]=float(c)/48000.f;for(uint32_t i=0;i<128;++i)f->input[0][c][i]=1;}
    f->run(128);
    for(uint32_t c=0;c<16;++c){for(uint32_t i=0;i<c;++i)VERIFY(f->output[0][c][i]==0);
        VERIFY(f->output[0][c][c]>0&&f->output[0][c][c+47]==1);}
}
static void state_v1_loads_and_v2_keeps_the_cycle(){
    // Version 1 (pre-MOD-1 layout): voice 0 held high at 1.
    std::vector<uint8_t> v1;
    auto u32=[&](uint32_t x){for(int k=0;k<4;++k)v1.push_back(uint8_t(x>>(8*k)));};
    auto f32=[&](float x){uint32_t b;std::memcpy(&b,&x,4);u32(b);};
    auto f64=[&](double x){uint64_t b;std::memcpy(&b,&x,8);u32(uint32_t(b));u32(uint32_t(b>>32));};
    u32(1);
    for(int v=0;v<16;++v){f64(v==0?1:0);f64(0);f64(v==0?1:0);f64(.01);f64(.1);f64(0);f64(0);
        u32(1);u32(1);u32(v==0?1:0);f32(.01f);f32(.1f);f32(0);}
    auto a=rig();
    VERIFY(port_load(*a,v1)==WS2_OK);
    a->parameter[0][0][0]=.01f;a->parameter[1][0][0]=.1f;
    std::fill_n(a->input[0][0],16,1.f);a->run(16);
    for(int i=0;i<16;++i)VERIFY(a->output[0][0][i]==1);
    // Version 2 mid-wait, mid-hold: the copy continues identically.
    for(int stop:{30,100}){
        auto b=rig(),c=rig();
        for(auto*f:{b.get(),c.get()}){f->parameter[0][0][0]=.001f;f->parameter[1][0][0]=.001f;
            f->parameter[3][0][0]=1;f->parameter[4][0][0]=.001f;f->parameter[5][0][0]=.001f;}
        for(uint32_t i=0;i<256;++i)b->input[0][0][i]=i<4?1.f:0.f;
        b->run(stop);
        const auto saved=port_save(*b);
        VERIFY(saved[0]==2);
        VERIFY(port_load(*c,saved)==WS2_OK&&port_save(*c)==saved);
        for(auto*f:{b.get(),c.get()})std::fill_n(f->input[0][0],256,0.f);
        b->run(200);c->run(200);
        VERIFY(std::memcmp(b->output[0][0],c->output[0][0],200*sizeof(float))==0);
    }
}
static void mod1_tests(){
    gate_mode_matches_the_old_calculation();trigger_timing();trigger_retrigger_level();
    gate_delay_shifts_the_rise();gate_falls_during_the_wait();trigger_delay_and_latching();
    sixteen_channels();state_v1_loads_and_v2_keeps_the_cycle();
    std::puts("MOD-1 Function Envelope trigger/hold/delay tests PASS");
}
int main(int argc,char**argv){latched_curve();mod1_tests();return utility_port_tests("function_envelope",argc,argv,3);}
