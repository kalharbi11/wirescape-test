// SPDX-License-Identifier: GPL-3.0-only
// MOD-6 acceptance tests, written before implementation.
#include "dsp.h"
#include "test_fixture.h"
#include <memory>
#include "../../tests/native/abi2/allocation_watch.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using R = wsmod::RhythmLanes;
using Mod = ws::Module<R>;
static void euclidean() {
    const auto pattern = [](unsigned steps, unsigned fill, unsigned rotate) {
        std::string s;
        for (unsigned i=0;i<steps;++i) s += R::euclidean(steps,fill,rotate,i) ? '1' : '0';
        return s;
    };
    VERIFY(pattern(8,3,0)=="10010010");
    VERIFY(pattern(8,3,1)=="01001001");
    VERIFY(pattern(5,2,0)=="10100");
    for (unsigned n=1;n<=16;++n)
        for (unsigned k=0;k<=16;++k)
            for (unsigned r=0;r<16;++r) {
                auto s=pattern(n,k,r);
                VERIFY(std::count(s.begin(),s.end(),'1')==std::min(n,k));
            }
}
static void descriptor() {
    const auto *d=Mod::descriptor();
    VERIFY(d->param_count==34 && d->input_count==2 && d->output_count==4);
    const char *fields[]={"division","shape","width","phase","probability","steps","fill","rotate"};
    const float division[]={7,10,13,2};
    for (unsigned l=0;l<4;++l) {
        for (unsigned p=0;p<8;++p) {
            const auto &v=d->params[8*l+p];
            VERIFY(v.id==std::string(fields[p])+"_"+std::to_string(l+1));
            VERIFY(v.legacy_index==8*l+p && (v.flags&WS2_MODULATABLE));
            VERIFY(bool(v.flags&WS2_STEPPED)==(p<2 || p>=5));
        }
        VERIFY(d->params[8*l].initial==division[l]);
        VERIFY(d->outputs[l].hint==WS2_CV_UNI);
    }
    VERIFY(!std::strcmp(d->inputs[0].id,"clock") && d->inputs[0].hint==WS2_GATE);
    VERIFY(!std::strcmp(d->inputs[1].id,"reset") && d->inputs[1].hint==WS2_TRIGGER);
    for(unsigned p=32;p<34;++p) VERIFY((d->params[p].flags&WS2_STEPPED) && !(d->params[p].flags&WS2_MODULATABLE));
}
struct Rig {
    std::unique_ptr<DspFixture> storage;
    DspFixture &f;
    ws2_transport t{sizeof(t),WS2_PLAYING,1440,0,0,4,4,0,0};
    uint64_t frame=0;
    explicit Rig(unsigned channels=1):storage(std::make_unique<DspFixture>(Mod::descriptor(),48000,channels)),f(*storage) {
        f.ins[0].flags=0; f.ins[1].flags=WS2_CONNECTED;
        f.process.transport=&t;
        for(unsigned l=0;l<4;++l) f.parameter[l*8][0][0]=7;
        f.parameter[33][0][0]=123;
    }
    void run(unsigned n, bool reset=false) {
        t.beat=double(frame)/2000.;
        std::fill_n(f.input[0][0],n,0.f);
        std::fill_n(f.input[1][0],n,0.f);
        if(reset) f.input[1][0][0]=1;
        f.run(n); frame+=n;
    }
    float cycle() {
        run(1); float v=f.output[0][0][0];
        unsigned remaining=1999;
        while(remaining){unsigned n=std::min(remaining,256u);run(n);remaining-=n;}
        return v;
    }
};
static void probability_and_reset() {
    Rig a,b;
    a.f.parameter[4][0][0]=b.f.parameter[4][0][0]=.5f;
    std::array<float,32> sequence{};
    unsigned active=0;
    for(auto &v:sequence){v=a.cycle();VERIFY(v==b.cycle());active+=v==1;}
    VERIFY(active>0 && active<32);
    a.f.reset(); a.frame=0;
    for(auto v:sequence) VERIFY(v==a.cycle());
    a.f.parameter[4][0][0]=0;
    for(int i=0;i<8;++i) VERIFY(a.cycle()==0);
}
static void latching() {
    Rig r;
    r.run(128);
    VERIFY(r.f.output[0][0][0]==1);
    r.f.parameter[2][0][0]=0;
    r.run(128);
    VERIFY(r.f.output[0][0][127]==1);
    for(unsigned i=0;i<6;++i) r.run(256);
    r.run(208); // 2000 frames since start
    r.run(1); VERIFY(r.f.output[0][0][0]==0);
    r.f.parameter[2][0][0]=1;
    r.run(1,true); VERIFY(r.f.output[0][0][0]==1);
}
static void shapes_and_phase() {
    VERIFY(R::wave(0,.25f,.5f,0,1)==1 && R::wave(0,.75f,.5f,0,1)==0);
    VERIFY(R::wave(1,0,.5f,0,1)==1 && R::wave(1,.5f,.5f,0,1)==0);
    VERIFY(R::wave(2,.25f,.5f,0,1)==.25f);
    VERIFY(R::wave(3,.25f,.5f,0,1)==.75f);
    VERIFY(R::wave(4,.5f,.5f,0,1)==1);
    VERIFY(std::abs(R::wave(5,.5f,.5f,0,1)-1)<1e-5);
    VERIFY(R::wave(6,.2f,.5f,0,.75f)==.75f);
    VERIFY(R::wave(7,.5f,1,0,1)==.5f);
    Rig r;
    r.f.parameter[3][0][0]=.25f;
    r.run(256); VERIFY(r.f.output[0][0][255]==0);
    r.run(256); VERIFY(r.f.output[0][0][244]==1);
}
static void realtime() {
    Rig r(16);
    for(unsigned l=0;l<4;++l) r.f.parameter[l*8+1][0][0]=float(l+4);
    {allocation_watch::Scope watch;
     for(int i=0;i<16;++i) r.run(256);
     r.f.reset();r.run(17);
     VERIFY(watch.allocations()==0);}
    for(unsigned l=0;l<4;++l) {
        VERIFY(r.f.outs[l].channels==1);
        for(unsigned i=0;i<17;++i) VERIFY(std::isfinite(r.f.output[l][0][i]) && r.f.output[l][0][i]>=0 && r.f.output[l][0][i]<=1);
    }
}
static std::vector<uint8_t> save(DspFixture &f) {
    std::vector<uint8_t> bytes;
    ws2_writer w{sizeof(w), &bytes, [](void *ctx, const void *p, uint32_t n) {
        auto &b = *static_cast<std::vector<uint8_t> *>(ctx);
        b.insert(b.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        return int(WS2_OK);
    }};
    VERIFY(f.d->save_state(f.state, &w) == WS2_OK);
    return bytes;
}
static int load(DspFixture &f, const std::vector<uint8_t> &bytes) {
    struct Cursor { const std::vector<uint8_t> &b; size_t pos = 0; } c{bytes};
    ws2_reader r{sizeof(r), &c, [](void *ctx, void *p, uint32_t n) {
        auto &c = *static_cast<Cursor *>(ctx);
        if (n > c.b.size() - c.pos) return int(WS2_INVALID);
        std::memcpy(p, c.b.data() + c.pos, n);
        c.pos += n;
        return int(WS2_OK);
    }, uint32_t(bytes.size())};
    return f.d->load_state(f.state, &r);
}
static void state_round_trip() {
    Rig a,b;
    a.f.parameter[1][0][0]=b.f.parameter[1][0][0]=7;
    a.run(256);a.run(17);
    auto snapshot=save(a.f);
    VERIFY(load(b.f,snapshot)==WS2_OK && save(b.f)==snapshot);
    b.frame=a.frame;
    for(int i=0;i<20;++i){
        a.run(128);b.run(128);
        for(unsigned l=0;l<4;++l)
            VERIFY(std::memcmp(a.f.output[l][0],b.f.output[l][0],128*sizeof(float))==0);
    }
    auto before=save(b.f);
    snapshot.pop_back();
    VERIFY(load(b.f,snapshot)==WS2_INVALID && save(b.f)==before);
}
static void clock_timing() {
    Rig r;
    r.f.ins[0].flags=WS2_CONNECTED;
    r.f.parameter[32][0][0]=2;
    r.f.parameter[0][0][0]=10; // half-beat cycle, one per clock edge
    std::fill_n(r.f.input[0][0],256,0.f);
    std::fill_n(r.f.input[1][0],256,0.f);
    r.f.input[0][0][0]=r.f.input[0][0][100]=r.f.input[0][0][200]=1;
    r.f.input[1][0][0]=1;
    r.f.run(256);
    VERIFY(r.f.output[0][0][0]==1 && r.f.output[0][0][99]==1);
    VERIFY(r.f.output[0][0][100]==1 && r.f.output[0][0][151]==0);
    VERIFY(r.f.output[0][0][200]==1 && r.f.output[0][0][251]==0);
}
static void all_divisions() {
    const double expected[]={16,8,4,3,2,1.5,4./3,1,.75,2./3,.5,.375,1./3,.25,.1875,1./6,.125,1./12};
    for(unsigned i=0;i<18;++i) VERIFY(wsbeat::division_beats(float(i))==expected[i]);
}
int main(){clock_timing();all_divisions();euclidean();descriptor();probability_and_reset();latching();shapes_and_phase();realtime();state_round_trip();}
