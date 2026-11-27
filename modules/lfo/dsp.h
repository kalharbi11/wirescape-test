// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wirescape_sdk.h"
#include "beat_clock.h"
namespace ws2ports {
struct Lfo {
    static constexpr ws::Info info{"essentials.lfo","LFO",2};
    static constexpr std::array inputs{ws::port("clock","Clock",WS2_GATE,WS2_FIRST),
                                       ws::port("reset","Reset",WS2_TRIGGER,WS2_FIRST)};
    static constexpr std::array outputs{ws::port("control", "Control", WS2_CV_BI),
                                        ws::port("phase", "Phase", WS2_CV_UNI)};
    static constexpr uint32_t flags=WS2_MODULATABLE|WS2_PER_VOICE|WS2_AUDIO_RATE_OK|WS2_AUTOMATABLE;
    static constexpr std::array params{
        ws::param("rate","Rate",.01f,20,1,"Hz",flags,WS2_LINEAR,0),
        ws::param("depth","Depth",0,1,1,"",flags,WS2_LINEAR,1),
        ws::param("offset","Offset",-1,1,0,"",flags,WS2_LINEAR,2),
        ws::param("sync","Tempo sync",0,1,0,"",WS2_STEPPED,WS2_LINEAR,3,4,1),
        ws::param("beats","Beats/cycle",.125f,16,1,"beats",WS2_AUTOMATABLE|WS2_MODULATABLE,WS2_LOG,4),
        ws::param("shape","Shape",0,6,0,"",WS2_STEPPED|WS2_MODULATABLE,WS2_LINEAR,5,4,1),
        ws::param("width","Width",0,1,.5f,"",flags,WS2_LINEAR,6),
        ws::param("division","Division",0,18,0,"",WS2_STEPPED|WS2_MODULATABLE,WS2_LINEAR,7,4,1),
        ws::param("clock_ppq","Clock PPQ",1,24,1,"",WS2_STEPPED,WS2_LINEAR,8,4,1),
        ws::param("seed","Seed",0,65535,0,"",WS2_STEPPED,WS2_LINEAR,9,4,1)};
    // Double phase and beat-clock state avoid drift at low rates.
    static constexpr double tau=6.283185307179586476925286766559;
    std::array<double,16> phase{};
    std::array<wsbeat::BeatClock,16> beat{};
    std::array<ws::Schmitt,16> reset_in{};
    std::array<bool,16> reset_high{};
    std::array<uint64_t,16> cycle{};
    uint64_t instance_seed=0;
    void prepare(const ws::Setup&s){instance_seed=s.config.seed;reset();}
    void reset(){phase.fill(0);for(auto&b:beat)b.reset();reset_in={};reset_high={};cycle={};}
    static unsigned integer(float x,unsigned lo,unsigned hi,unsigned fallback){
        return unsigned(std::lround(std::clamp(std::isfinite(x)?x:float(fallback),float(lo),float(hi))));
    }
    // Counter-keyed bipolar random wave, bounded work even across transport locates.
    static uint64_t mix(uint64_t x){
        x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);
        return x^(x>>31);
    }
    static double random_value(uint64_t seed,unsigned voice,uint64_t n){
        return double(mix(mix(seed+UINT64_C(0x9e3779b97f4a7c15)*(voice+1))+n*UINT64_C(0x9e3779b97f4a7c15))>>40)*(2./16777216.)-1.;
    }
    static uint64_t cycle_index(double n){
        // Use a bounded modulo before integer conversion; negative transport positions are allowed.
        if(!std::isfinite(n))return 0;
        double x=std::fmod(std::floor(n),4294967296.);
        if(x<0)x+=4294967296.;
        return uint64_t(x);
    }
    static double wave(unsigned shape,double p,float width,uint64_t seed,unsigned voice,uint64_t n){
        const double u=p/tau;
        switch(shape){
            case 0:return std::sin(p);
            case 1:return u<.25?4*u:u<.75?2-4*u:4*u-4;
            case 2:return 2*u-1;
            case 3:return 1-2*u;
            case 4:return u<width?1.:-1.;
            case 5:return random_value(seed,voice,n);
            default:{const double a=random_value(seed,voice,n),z=random_value(seed,voice,n+1);
                return a+(z-a)*(u*u*(3-2*u));}
        }
    }
    void process(ws::Block&b){
        b.out(0).set_channels(b.channels());b.out(0).mark_constant(false);
        b.out(1).set_channels(b.channels());b.out(1).mark_constant(false);
        const bool clock_connected=b.in(0).connected(), reset_connected=b.in(1).connected();
        for(uint32_t group=0;group<b.channels();group+=4)for(uint32_t c=group;c<std::min(group+4,b.channels());++c){
            auto*out=b.out(0).ch(c);auto*phase_out=b.out(1).ch(c);
            for(uint32_t i=0;i<b.frames();++i){
                const bool high=reset_in[c].process(reset_connected?b.in(1).value(0,i):0.f);
                const bool edge=high&&!reset_high[c];reset_high[c]=high;
                if(edge){phase[c]=0;cycle[c]=0;}
                const bool sync=b.param(3).value(c,i)>=.5f;
                const bool locked=sync&&(clock_connected||b.transport());
                const double position=beat[c].step(clock_connected,b.in(0).value(0,i),edge,
                    float(integer(b.param(8).value(0,i),1,24,1)),b.transport(),i,b.sample_rate());
                uint64_t n=cycle[c];
                if(locked){
                    const unsigned division=integer(b.param(7).value(c,i),0,18,0);
                    const double length=division?wsbeat::division_beats(float(division-1)):b.param(4).value(c,i);
                    const double cycles=position/length;
                    if(std::isfinite(cycles)){phase[c]=tau*(cycles-std::floor(cycles));n=cycle_index(cycles);}
                    else{phase[c]=0;n=0;}
                    cycle[c]=n;
                }
                const unsigned shape=integer(b.param(5).value(c,i),0,6,0);
                const unsigned seed=integer(b.param(9).value(0,i),0,65535,0);
                // Keep the original sine/depth/offset operation order and phase output.
                if(shape==0)out[i]=static_cast<float>(std::sin(phase[c])*b.param(1).value(c,i)+b.param(2).value(c,i));
                else out[i]=static_cast<float>(wave(shape,phase[c],b.param(6).value(c,i),seed?seed:instance_seed,c,n)*b.param(1).value(c,i)+b.param(2).value(c,i));
                phase_out[i]=std::min(static_cast<float>(phase[c]/tau),0x1.fffffep-1f);
                if(!locked){phase[c]+=tau*b.param(0).value(c,i)/b.sample_rate();if(phase[c]>=tau){phase[c]=std::fmod(phase[c],tau);++cycle[c];}}
            }
        }
    }
};
}
