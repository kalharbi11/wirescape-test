// SPDX-License-Identifier: GPL-3.0-only
// Original rhythmic modulation lanes, proposal 0024 section 4.4.
#pragma once
#include "wirescape_sdk.h"
#include "beat_clock.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wsmod {
struct RhythmLanes {
    static constexpr ws::Info info{"sequencing.rhythm_lanes","Rhythm Lanes",1};
    static constexpr std::array inputs{
        ws::port("clock","Clock",WS2_GATE,WS2_FIRST),
        ws::port("reset","Reset",WS2_TRIGGER,WS2_FIRST)};
    static constexpr std::array outputs{
        ws::port("lane1","Lane 1",WS2_CV_UNI),ws::port("lane2","Lane 2",WS2_CV_UNI),
        ws::port("lane3","Lane 3",WS2_CV_UNI),ws::port("lane4","Lane 4",WS2_CV_UNI)};
    static constexpr std::array params{
        ws::param("division_1", "Division 1", 0, 17, 7, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 0, 4, 1),
        ws::param("shape_1", "Shape 1", 0, 7, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 1, 4, 1),
        ws::param("width_1", "Width 1", 0, 1, 0.5, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 2),
        ws::param("phase_1", "Phase 1", 0, 1, 0, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 3),
        ws::param("probability_1", "Probability 1", 0, 1, 1, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 4),
        ws::param("steps_1", "Steps 1", 1, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 5, 4, 1),
        ws::param("fill_1", "Fill 1", 0, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 6, 4, 1),
        ws::param("rotate_1", "Rotate 1", 0, 15, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 7, 4, 1),
        ws::param("division_2", "Division 2", 0, 17, 10, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 8, 4, 1),
        ws::param("shape_2", "Shape 2", 0, 7, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 9, 4, 1),
        ws::param("width_2", "Width 2", 0, 1, 0.5, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 10),
        ws::param("phase_2", "Phase 2", 0, 1, 0, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 11),
        ws::param("probability_2", "Probability 2", 0, 1, 1, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 12),
        ws::param("steps_2", "Steps 2", 1, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 13, 4, 1),
        ws::param("fill_2", "Fill 2", 0, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 14, 4, 1),
        ws::param("rotate_2", "Rotate 2", 0, 15, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 15, 4, 1),
        ws::param("division_3", "Division 3", 0, 17, 13, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 16, 4, 1),
        ws::param("shape_3", "Shape 3", 0, 7, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 17, 4, 1),
        ws::param("width_3", "Width 3", 0, 1, 0.5, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 18),
        ws::param("phase_3", "Phase 3", 0, 1, 0, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 19),
        ws::param("probability_3", "Probability 3", 0, 1, 1, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 20),
        ws::param("steps_3", "Steps 3", 1, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 21, 4, 1),
        ws::param("fill_3", "Fill 3", 0, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 22, 4, 1),
        ws::param("rotate_3", "Rotate 3", 0, 15, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 23, 4, 1),
        ws::param("division_4", "Division 4", 0, 17, 2, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 24, 4, 1),
        ws::param("shape_4", "Shape 4", 0, 7, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 25, 4, 1),
        ws::param("width_4", "Width 4", 0, 1, 0.5, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 26),
        ws::param("phase_4", "Phase 4", 0, 1, 0, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 27),
        ws::param("probability_4", "Probability 4", 0, 1, 1, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 28),
        ws::param("steps_4", "Steps 4", 1, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 29, 4, 1),
        ws::param("fill_4", "Fill 4", 0, 16, 1, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 30, 4, 1),
        ws::param("rotate_4", "Rotate 4", 0, 15, 0, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 31, 4, 1),
        ws::param("clock_ppq", "Clock PPQ", 1, 24, 1, "", WS2_STEPPED | WS2_AUTOMATABLE, WS2_LINEAR, 32, 4, 1),
        ws::param("seed", "Seed", 0, 65535, 0, "", WS2_STEPPED | WS2_AUTOMATABLE, WS2_LINEAR, 33, 4, 1)};

    struct Lane {
        double start=0;
        uint64_t cycle=0;
        uint32_t division=7, shape=0;
        float width=.5f, phase=0, from=0, to=0;
        bool active=false, begun=false;
    };
    std::array<Lane,4> lanes{};
    wsbeat::BeatClock beat;
    ws::Schmitt reset_in;
    bool reset_high=false;
    uint64_t instance_seed=0;
    double rate=48000;

    void prepare(const ws::Setup &s) {
        rate=s.sample_rate(); instance_seed=s.config.seed; reset();
    }
    void reset() {
        beat.reset(); reset_in=ws::Schmitt{}; reset_high=false; lanes={};
    }
    static float unit(float x,float fallback=0) noexcept {
        return std::clamp(std::isfinite(x)?x:fallback,0.f,1.f);
    }
    static uint32_t integer(float x,uint32_t lo,uint32_t hi,uint32_t fallback) noexcept {
        return static_cast<uint32_t>(std::lround(std::clamp(std::isfinite(x)?x:float(fallback),float(lo),float(hi))));
    }

    // Bjorklund quotient/remainder expansion. At most 16 leaves and 16 levels.
    static void expand(int level,const int *counts,const int *remainders,
                       std::array<bool,16> &pattern,unsigned &at) noexcept {
        if(level==-1) {pattern[at++]=false;return;}
        if(level==-2) {pattern[at++]=true;return;}
        for(int i=0;i<counts[level];++i) expand(level-1,counts,remainders,pattern,at);
        if(remainders[level]) expand(level-2,counts,remainders,pattern,at);
    }
    static bool euclidean(unsigned n,unsigned k,unsigned rotate,uint64_t cycle) noexcept {
        n=std::clamp(n,1u,16u); k=std::min(k,n);
        if(k==0) return false;
        if(k==n) return true;
        int counts[16]{},remainders[16]{};
        int divisor=int(n-k),level=0;
        remainders[0]=int(k);
        do {
            counts[level]=divisor/remainders[level];
            remainders[level+1]=divisor%remainders[level];
            divisor=remainders[level++];
        } while(remainders[level]>1);
        counts[level]=divisor;
        std::array<bool,16> pattern{};
        unsigned at=0;expand(level,counts,remainders,pattern,at);
        unsigned first=0;while(first<n && !pattern[first]) ++first;
        return pattern[(first+unsigned(cycle%n)+n-rotate%n)%n];
    }

    // Counter-keyed random stream: fixed work even after a transport jump.
    static uint64_t mix(uint64_t x) noexcept {
        x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);
        return x^(x>>31);
    }
    static float random(uint64_t seed,unsigned lane,uint64_t cycle,unsigned stream) noexcept {
        const auto key=mix(seed+UINT64_C(0x9e3779b97f4a7c15)*(lane*2+stream+1));
        return float(mix(key+UINT64_C(0x9e3779b97f4a7c15)*cycle)>>40)*(1.f/16777216.f);
    }
    static float wave(unsigned shape,float u,float w,float from,float to) noexcept {
        switch(shape) {
            case 0:return u<w?1.f:0.f;
            case 1:return u<w/16.f?1.f:0.f;
            case 2:return u;
            case 3:return 1.f-u;
            case 4:return w==0?1.f-u:w==1?u:u<w?u/w:(1.f-u)/(1.f-w);
            case 5:return .5f-.5f*std::cos(6.2831853071795864769f*u);
            case 6:return to;
            default:{
                const float x=w>0?std::min(u/w,1.f):1.f;
                return from+(to-from)*(x*x*(3.f-2.f*x));
            }
        }
    }
    void latch(Lane &l,ws::Block &b,unsigned k,unsigned frame) noexcept {
        const unsigned p=8*k;
        const auto get=[&](unsigned i){return b.param(p+i).value(0,frame);};
        constexpr uint32_t defaults[]={7,10,13,2};
        l.division=integer(get(0),0,17,defaults[k]);
        l.shape=integer(get(1),0,7,0);
        l.width=unit(get(2),.5f);
        l.phase=unit(get(3));
        const float prob=unit(get(4),1);
        const unsigned n=integer(get(5),1,16,1), f=integer(get(6),0,16,1), r=integer(get(7),0,15,0);
        const auto seed=integer(b.param(33).value(0,frame),0,65535,0);
        const uint64_t key=seed?seed:instance_seed;
        l.from=l.active?l.to:0.f;
        l.to=random(key,k,l.cycle,1);
        l.active=euclidean(n,f,r,l.cycle) && random(key,k,l.cycle,0)<prob;
        l.begun=true;
    }
    void process(ws::Block &b) {
        for(unsigned k=0;k<4;++k) {b.out(k).set_channels(1);b.out(k).mark_constant(false);}
        const bool connected=b.in(0).connected();
        const auto *t=b.transport();
        for(unsigned i=0;i<b.frames();++i) {
            const bool high=reset_in.process(b.in(1).value(0,i));
            const bool edge=high&&!reset_high; reset_high=high;
            const float ppq=float(integer(b.param(32).value(0,i),1,24,1));
            const double now=beat.step(connected,b.in(0).value(0,i),edge,ppq,t,i,rate);
            if(edge) lanes={};
            for(unsigned k=0;k<4;++k) {
                auto &l=lanes[k];
                if(!std::isfinite(now)) {b.out(k).ch(0)[i]=0;continue;}
                if(!l.begun || now<l.start-1e-9) {
                    l=Lane{};l.start=now;latch(l,b,k,i);
                } else {
                    const double old=wsbeat::kDivisionBeats[l.division];
                    if(now-l.start+1e-9>=old) {
                        // Complete the old cycle before applying a new division.
                        // Large jumps skip elapsed cycles using the old latched length.
                        const double count=std::floor((now-l.start+1e-9)/old);
                        l.cycle+=static_cast<uint64_t>(std::fmod(count,4294967296.));
                        l.start+=count*old;
                        latch(l,b,k,i);
                        // If the newly latched cycle is shorter than the overshoot, start here.
                        if(now-l.start>=wsbeat::kDivisionBeats[l.division]) l.start=now;
                    }
                }
                const float u=float(std::max(0.,(now-l.start)/wsbeat::kDivisionBeats[l.division]));
                // Phase delays the onset within the cycle; no wrapping into inactive cycles.
                b.out(k).ch(0)[i]=l.active && u>=l.phase && l.phase<1
                    ? unit(wave(l.shape,unit(u-l.phase),l.width,l.from,l.to)):0.f;
            }
        }
    }

    // Portable state v1; all fields encoded explicitly and checked before replacing live state.
    static bool put64(ws::Writer w,uint64_t v){return w.u32(uint32_t(v))&&w.u32(uint32_t(v>>32));}
    static bool get64(ws::Reader r,uint64_t &v){
        uint32_t lo,hi;if(!r.u32(lo)||!r.u32(hi))return false;
        v=uint64_t(lo)|(uint64_t(hi)<<32);return true;
    }
    static bool putd(ws::Writer w,double d){uint64_t v;std::memcpy(&v,&d,8);return put64(w,v);}
    static bool getd(ws::Reader r,double &d){uint64_t v;if(!get64(r,v))return false;std::memcpy(&d,&v,8);return true;}
    int save_state(ws::Writer w) {
        const auto c=beat.save();
        bool ok=w.u32(1)&&putd(w,c.base)&&putd(w,c.beat)&&putd(w,c.origin)&&putd(w,c.held_fraction)&&
            putd(w,c.samples_per_beat)&&put64(w,c.since)&&put64(w,c.period)&&put64(w,c.edges)&&w.u32(c.flags)&&
            w.u32(reset_high?1u:0u)&&put64(w,instance_seed);
        for(const auto &l:lanes) ok=ok&&putd(w,l.start)&&put64(w,l.cycle)&&w.u32(l.division)&&w.u32(l.shape)&&
            w.f32(l.width)&&w.f32(l.phase)&&w.f32(l.from)&&w.f32(l.to)&&w.u32(l.active?1u:0u)&&w.u32(l.begun?1u:0u);
        return ok?WS2_OK:WS2_CAPACITY;
    }
    int load_state(ws::Reader r) {
        uint32_t version,high;uint64_t seed;
        if(!r.u32(version))return WS2_INVALID;
        if(version!=1)return WS2_UNSUPPORTED;
        wsbeat::BeatClock::State c{};
        if(!getd(r,c.base)||!getd(r,c.beat)||!getd(r,c.origin)||!getd(r,c.held_fraction)||
           !getd(r,c.samples_per_beat)||!get64(r,c.since)||!get64(r,c.period)||!get64(r,c.edges)||!r.u32(c.flags)||
           !r.u32(high)||high>1||!get64(r,seed))return WS2_INVALID;
        std::array<Lane,4> staged{};
        const auto valid=[](float v){return std::isfinite(v)&&v>=0&&v<=1;};
        for(auto &l:staged) {
            uint32_t active,begun;
            if(!getd(r,l.start)||!get64(r,l.cycle)||!r.u32(l.division)||!r.u32(l.shape)||
               !r.f32(l.width)||!r.f32(l.phase)||!r.f32(l.from)||!r.f32(l.to)||!r.u32(active)||!r.u32(begun)||
               !std::isfinite(l.start)||l.division>17||l.shape>7||!valid(l.width)||!valid(l.phase)||
               !valid(l.from)||!valid(l.to)||active>1||begun>1)return WS2_INVALID;
            l.active=active!=0;l.begun=begun!=0;
        }
        wsbeat::BeatClock staged_clock;
        if(!staged_clock.restore(c))return WS2_INVALID;
        beat=staged_clock;lanes=staged;instance_seed=seed;reset_high=high!=0;reset_in=ws::Schmitt{};
        if(reset_high)reset_in.process(1.f);
        return WS2_OK;
    }
};
} // namespace wsmod
