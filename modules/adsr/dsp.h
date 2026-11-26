// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../port_state.h"
namespace ws2ports {
struct Adsr {
    static constexpr ws::Info info{"essentials.adsr","ADSR",2};
    static constexpr std::array inputs{ws::port("gate","Gate",WS2_GATE),ws::port("notes","Notes",WS2_EV_NOTES,WS2_POLY,0,nullptr,WS2_EVENT)};
    static constexpr std::array outputs{ws::port("envelope","Envelope",WS2_CV_UNI)};
    static constexpr uint32_t flags=WS2_MODULATABLE|WS2_PER_VOICE|WS2_AUTOMATABLE;
    static constexpr std::array params{ws::param("attack","Attack",.0001f,30,.01f,"s",flags,WS2_LINEAR,0),ws::param("decay","Decay",.0001f,30,.1f,"s",flags,WS2_LINEAR,1),ws::param("sustain","Sustain",0,1,.7f,"",flags|WS2_AUDIO_RATE_OK,WS2_LINEAR,2),ws::param("release","Release",.0001f,30,.2f,"s",flags,WS2_LINEAR,3),
        ws::param("mode","Mode (Gate/Trigger)",0,1,0,"",WS2_STEPPED|WS2_AUTOMATABLE,WS2_LINEAR,4,4,1),
        ws::param("hold","Hold",0,30,0,"s",flags,WS2_LINEAR,5),
        // MOD-2 (lab 0024 §4.1): appended; 0 keeps the pre-delay output bit-identical.
        ws::param("delay","Delay",0,30,0,"s",flags,WS2_LINEAR,6)};
    // pending: samples left before a delayed start (0: none). The previous segment runs meanwhile.
    struct Voice {float level=0,start=0;uint64_t elapsed=0,duration=1,hold_left=0,pending=0;int stage=0;bool gated=false,triggered=false;uint32_t source=0;int32_t note=0;ws::Schmitt edge;};
    std::array<Voice,16>voices{};
    bool trigger_mode_seen=false; // only the default Gate path uses original fast-loop
    bool delay_seen=false;        // MOD-2: a delayed start happened; the general path runs from now on
    double state_rate=48000.;
    void prepare(const ws::Setup&s){state_rate=s.sample_rate();}void reset(){for(auto&v:voices)v=Voice{};trigger_mode_seen=false;delay_seen=false;}

    // A70: portable, versioned stopped-engine state. Do not touch the render path.
    static bool write_u64(ws::Writer out,uint64_t v){
        return out.u32(uint32_t(v))&&out.u32(uint32_t(v>>32));
    }
    static bool read_u64(ws::Reader in,uint64_t&v){
        uint32_t lo,hi;
        if(!in.u32(lo)||!in.u32(hi))return false;
        v=uint64_t(lo)|(uint64_t(hi)<<32);return true;
    }
    int save_state(ws::Writer out)const{
        // Version 2 (MOD-2) appends delay_seen after trigger_mode_seen and pending after each voice.
        if(!out.u32(2)||!write_double(out,state_rate)||!out.u32(trigger_mode_seen?1u:0u)||
           !out.u32(delay_seen?1u:0u))return WS2_CAPACITY;
        for(const auto&v:voices){
            auto edge=v.edge; // Probe the Schmitt latch at a value in its hysteresis band.
            uint32_t note;std::memcpy(&note,&v.note,sizeof(note));
            if(!out.u32(uint32_t(v.stage))||!out.f32(v.level)||!out.f32(v.start)||
               !write_u64(out,v.elapsed)||!write_u64(out,v.duration)||!write_u64(out,v.hold_left)||
               !out.u32(v.gated?1u:0u)||!out.u32(v.triggered?1u:0u)||!out.u32(v.source)||
               !out.u32(note)||!out.u32(edge.process(.375f)?1u:0u)||!write_u64(out,v.pending))return WS2_CAPACITY;
        }
        return WS2_OK;
    }
    int load_state(ws::Reader in){
        uint32_t version,seen,dseen=0;
        double saved_rate;
        if(!in.u32(version))return WS2_INVALID;
        if(version!=1&&version!=2)return WS2_UNSUPPORTED;
        if(!read_double(in,saved_rate)||saved_rate!=state_rate||!in.u32(seen)||seen>1)return WS2_INVALID;
        if(version==2&&(!in.u32(dseen)||dseen>1))return WS2_INVALID;
        std::array<Voice,16> staged{};
        for(auto&v:staged){
            uint32_t stage,gated,triggered,note,edge;
            if(!in.u32(stage)||stage>4||!in.f32(v.level)||!in.f32(v.start)||
               !std::isfinite(v.level)||!std::isfinite(v.start)||
               !read_u64(in,v.elapsed)||!read_u64(in,v.duration)||!read_u64(in,v.hold_left)||
               !v.duration||v.elapsed>v.duration||
               !in.u32(gated)||gated>1||!in.u32(triggered)||triggered>1||
               !in.u32(v.source)||!in.u32(note)||!in.u32(edge)||edge>1)return WS2_INVALID;
            // At most 30 s at 192 kHz, plus one for a start taken from an event.
            if(version==2&&(!read_u64(in,v.pending)||v.pending>5760001))return WS2_INVALID;
            v.stage=int(stage);v.gated=gated!=0;v.triggered=triggered!=0;
            std::memcpy(&v.note,&note,sizeof(note));
            if(edge)v.edge.process(1.f);
        }
        voices=staged;trigger_mode_seen=seen!=0;delay_seen=dseen!=0;
        return WS2_OK;
    }
    void start(Voice&v,int stage,float seconds,double rate){v.stage=stage;v.start=v.level;v.elapsed=0;v.duration=std::max<uint64_t>(1,static_cast<uint64_t>(std::llround(seconds*rate)));}
    // MOD-2: a start edge (gate rise or NOTE_ON). `delay` latches here; the attack begins `delay`
    // later (Attack latches when it begins, as every stage does). A start from an event is applied
    // before its sample, so it counts one more to begin at the same sample as a cable edge.
    void edge_on(Voice&v,ws::Block&b,uint32_t c,uint32_t i,bool from_event){
        v.gated=true;v.triggered=b.param(4).value(c,i)>.5f;trigger_mode_seen |= v.triggered;
        const uint64_t wait=static_cast<uint64_t>(std::llround(std::clamp(b.param(6).value(c,i),0.f,30.f)*b.sample_rate()));
        if(wait){v.pending=wait+(from_event?1:0);delay_seen=true;}
        else{v.pending=0;start(v,1,b.param(0).value(c,i),b.sample_rate());}
    }
    // A gate fall or matching NOTE_OFF: Gate mode releases from the current level and cancels a wait.
    void edge_off(Voice&v,ws::Block&b,uint32_t c,uint32_t i){
        v.gated=false;if(!v.triggered){v.pending=0;start(v,4,b.param(3).value(c,i),b.sample_rate());}
    }
    // CHOKE still invalidates the old note immediately; decay its residual
    // level over a fixed 5 ms instead of making a discontinuous CV step.
    // Reuse the normal Release stage, leaving the per-sample loop untouched.
    void choke(Voice&v,double rate){
        const float level=v.level;
        v=Voice{};
        if(level>0.f){v.level=level;start(v,4,.005f,rate);}
    }
    void process_default_gate(ws::Block&b){
        b.out(0).set_channels(b.channels());b.out(0).mark_constant(false);
        b.split([&](uint32_t first,uint32_t end){
            for(uint32_t group=0;group<b.channels();group+=4)for(uint32_t c=group;c<std::min(group+4,b.channels());++c){auto&v=voices[c];auto*out=b.out(0).ch(c);
                for(uint32_t i=first;i<end;++i){
                    if(b.in(0).connected()){
                        bool high=v.edge.process(b.in(0).value(c,i));
                        if(high&&!v.gated){v.gated=true;start(v,1,b.param(0).value(c,i),b.sample_rate());}
                        else if(!high&&v.gated){v.gated=false;start(v,4,b.param(3).value(c,i),b.sample_rate());}
                    }
                    float sustain=b.param(2).value(c,i);
                    if(v.stage==1){v.level=v.start+(1-v.start)*static_cast<float>(++v.elapsed)/static_cast<float>(v.duration);if(v.elapsed>=v.duration){v.level=1;start(v,2,b.param(1).value(c,i),b.sample_rate());}}
                    else if(v.stage==2){v.level=v.start+(sustain-v.start)*static_cast<float>(++v.elapsed)/static_cast<float>(v.duration);if(v.elapsed>=v.duration){v.level=sustain;v.stage=3;}}
                    else if(v.stage==3)v.level=sustain;
                    else if(v.stage==4){v.level=v.start*(1-static_cast<float>(++v.elapsed)/static_cast<float>(v.duration));if(v.elapsed>=v.duration){v.level=0;v.stage=0;}}
                    out[i]=v.level;
                }
            }
        },[&](const ws2_event&e){
            uint32_t c=e.channel<0?0:static_cast<uint32_t>(e.channel);if(c>=b.channels()||b.in(0).connected())return;
            auto&v=voices[c];uint32_t at=std::min(e.offset,b.frames()-1);
            if(e.type==WS2_NOTE_ON){v.source=e.source_id;v.note=e.note_id;v.gated=true;start(v,1,b.param(0).value(c,at),b.sample_rate());}
            else if((e.type==WS2_NOTE_OFF||e.type==WS2_NOTE_CHOKE)&&v.source==e.source_id&&v.note==e.note_id){if(e.type==WS2_NOTE_CHOKE)choke(v,b.sample_rate());else if(v.gated){v.gated=false;start(v,4,b.param(3).value(c,at),b.sample_rate());}}
        });
    }
    void process(ws::Block&b){
        // For the default constant mode, run exactly the pre-OB Gate engine.
        // There is no extra branch in its per-voice hot loop.
        if(!trigger_mode_seen && !delay_seen && b.param(4).constant() && b.param(4).channels()==1 &&
           b.param(4).value(0,0)<=.5f && b.param(6).constant() && b.param(6).channels()==1 &&
           b.param(6).value(0,0)<=0.f){process_default_gate(b);return;}
        b.out(0).set_channels(b.channels());b.out(0).mark_constant(false);
        b.split([&](uint32_t first,uint32_t end){
            for(uint32_t group=0;group<b.channels();group+=4)for(uint32_t c=group;c<std::min(group+4,b.channels());++c){auto&v=voices[c];auto*out=b.out(0).ch(c);
                // Use the exact pre-OB Gate loop for the default constant mode.
                // Its hot path keeps the M1 instruction footprint and float order;
                // Trigger/retrigger and automated mode changes take the general path.
                const bool original_gate = !v.triggered && !v.pending && b.param(4).constant() &&
                                           b.param(4).value(c,first) <= .5f && b.param(6).constant() &&
                                           b.param(6).value(c,first) <= 0.f;
                if (original_gate) {
                for(uint32_t i=first;i<end;++i){
                    if(b.in(0).connected()){
                        bool high=v.edge.process(b.in(0).value(c,i));
                        if(high&&!v.gated){v.gated=true;start(v,1,b.param(0).value(c,i),b.sample_rate());}
                        else if(!high&&v.gated){v.gated=false;start(v,4,b.param(3).value(c,i),b.sample_rate());}
                    }
                    float sustain=b.param(2).value(c,i);
                    if(v.stage==1){v.level=v.start+(1-v.start)*static_cast<float>(++v.elapsed)/static_cast<float>(v.duration);if(v.elapsed>=v.duration){v.level=1;start(v,2,b.param(1).value(c,i),b.sample_rate());}}
                    else if(v.stage==2){v.level=v.start+(sustain-v.start)*static_cast<float>(++v.elapsed)/static_cast<float>(v.duration);if(v.elapsed>=v.duration){v.level=sustain;v.stage=3;}}
                    else if(v.stage==3)v.level=sustain;
                    else if(v.stage==4){v.level=v.start*(1-static_cast<float>(++v.elapsed)/static_cast<float>(v.duration));if(v.elapsed>=v.duration){v.level=0;v.stage=0;}}
                    out[i]=v.level;
                }
                } else {
                for(uint32_t i=first;i<end;++i){
                    if(v.pending&&--v.pending==0)start(v,1,b.param(0).value(c,i),b.sample_rate());
                    if(b.in(0).connected()){
                        bool high=v.edge.process(b.in(0).value(c,i));
                        if(high&&!v.gated)edge_on(v,b,c,i,false);
                        else if(!high&&v.gated)edge_off(v,b,c,i);
                    }
                    float sustain=b.param(2).value(c,i);
                    if(v.stage==1){v.level=v.start+(1-v.start)*static_cast<float>(++v.elapsed)/static_cast<float>(v.duration);if(v.elapsed>=v.duration){v.level=1;start(v,2,b.param(1).value(c,i),b.sample_rate());}}
                    else if(v.stage==2){v.level=v.start+(sustain-v.start)*static_cast<float>(++v.elapsed)/static_cast<float>(v.duration);if(v.elapsed>=v.duration){v.level=sustain;if(v.triggered){
                            v.hold_left=static_cast<uint64_t>(std::llround(std::clamp(b.param(5).value(c,i),0.f,30.f)*b.sample_rate()));
                            if(v.hold_left==0)start(v,4,b.param(3).value(c,i),b.sample_rate());
                            else v.stage=3;
                        }else v.stage=3;}}
                    else if(v.stage==3){v.level=sustain;if(v.triggered&&v.hold_left&&--v.hold_left==0)
                        start(v,4,b.param(3).value(c,i),b.sample_rate());}
                    else if(v.stage==4){v.level=v.start*(1-static_cast<float>(++v.elapsed)/static_cast<float>(v.duration));if(v.elapsed>=v.duration){v.level=0;v.stage=0;}}
                    out[i]=v.level;
                }
                }
            }
        },[&](const ws2_event&e){
            uint32_t c=e.channel<0?0:static_cast<uint32_t>(e.channel);if(c>=b.channels()||b.in(0).connected())return;
            auto&v=voices[c];uint32_t at=std::min(e.offset,b.frames()-1);
            if(e.type==WS2_NOTE_ON){v.source=e.source_id;v.note=e.note_id;edge_on(v,b,c,at,true);}
            else if((e.type==WS2_NOTE_OFF||e.type==WS2_NOTE_CHOKE)&&v.source==e.source_id&&v.note==e.note_id){if(e.type==WS2_NOTE_CHOKE)choke(v,b.sample_rate());else if(v.gated)edge_off(v,b,c,at);}
        });
    }
};
}
