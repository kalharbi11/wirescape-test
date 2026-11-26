// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../port_state.h"
namespace ws2ports {
// MOD-1 (wirescape-test, lab 0024 §4.1): appended mode (Gate/Trigger), hold and delay. With mode 0 and
// delay 0 every sample takes the pre-MOD-1 arithmetic below unchanged (bit-identical).
struct FunctionEnvelope {
 static constexpr ws::Info info{"essentials.function_envelope","Function Envelope",2};
 static constexpr std::array inputs{ws::port("gate","Gate",WS2_GATE)};static constexpr std::array outputs{ws::port("envelope","Envelope",WS2_CV_UNI)};
 static constexpr std::array params{ws::param("rise","Rise",.0001f,30,.01f,"s",port_flags,WS2_LINEAR,0),ws::param("fall","Fall",.0001f,30,.1f,"s",port_flags,WS2_LINEAR,1),ws::param("curve","Curve",-1,1,0,"",port_flags,WS2_LINEAR,2),
  // Appended (MOD-1). mode is setParam-only: switching mid-note is not musically meaningful (0023 R3.3).
  ws::param("mode","Mode (Gate/Trigger)",0,1,0,"",WS2_STEPPED|WS2_AUTOMATABLE,WS2_LINEAR,3,4,1),
  ws::param("hold","Hold",0,30,0,"s",port_flags,WS2_LINEAR,4),
  ws::param("delay","Delay",0,30,0,"s",port_flags,WS2_LINEAR,5)};
 static constexpr uint32_t max_samples=11520000; // 30 s at 384 kHz, the version-1 length bound
 // MOD-1 fields: pending (samples before a delayed rise), hold_left (samples left at 1 in Trigger
 // mode), and the values latched at the start edge (l_*). trig: this cycle runs in Trigger mode.
 struct Voice{double level=0,start=0,endpoint=0,rise=.01,fall=.1,curve=0,latched_curve=0;uint64_t length=1,elapsed=1;bool high=false;float last_rise=.01f,last_fall=.1f,last_curve=0;
  double l_rise=.01,l_fall=.1,l_curve=0;uint32_t pending=0,hold_left=0,l_hold=0;bool trig=false;};
 std::array<Voice,16>voices{};double rate=48000;
 void prepare(const ws::Setup&s){rate=s.sample_rate();reset();}
 void reset(){for(auto&v:voices){v.level=v.start=v.endpoint=0;v.elapsed=v.length=1;v.high=false;v.pending=v.hold_left=v.l_hold=0;v.trig=false;}}
 uint32_t samples(float seconds)const{return static_cast<uint32_t>(std::llround(std::clamp(seconds,0.f,30.f)*rate));}
 // A segment from the current level, exactly as the pre-MOD-1 edge code starts one.
 void segment(Voice&v,double endpoint,double seconds,double curve)const{v.start=v.level;v.endpoint=endpoint;v.latched_curve=curve;v.length=std::max<uint64_t>(1,static_cast<uint64_t>(std::llround(seconds*rate)));v.elapsed=0;}
 void process(ws::Block&b){b.out(0).set_channels(b.channels());b.out(0).mark_constant(false);for(uint32_t c=0;c<b.channels();++c){auto&v=voices[c];for(uint32_t i=0;i<b.frames();++i){float rise=b.param(0).value(c,i),fall=b.param(1).value(c,i),curve=b.param(2).value(c,i);if(rise!=v.last_rise){v.rise=rise;v.last_rise=rise;}if(fall!=v.last_fall){v.fall=fall;v.last_fall=fall;}if(curve!=v.last_curve){v.curve=curve;v.last_curve=curve;}
  // A delayed rise begins n samples after its edge: count down before this sample's edge check.
  if(v.pending&&--v.pending==0)segment(v,1.,v.l_rise,v.l_curve);
  float gate=b.in(0).value(c,i);bool high=std::isfinite(gate)&&gate>=.5f;if(high!=v.high){v.high=high;
   if(high){
    // Start edge: rise, fall, curve, delay and hold latch here.
    v.trig=b.param(3).value(c,i)>.5f;v.l_rise=v.rise;v.l_fall=v.fall;v.l_curve=v.curve;v.l_hold=samples(b.param(4).value(c,i));v.hold_left=0;
    const uint32_t wait=samples(b.param(5).value(c,i));
    if(wait)v.pending=wait;else{v.pending=0;segment(v,1.,v.rise,v.curve);}
   }else if(!v.trig){v.pending=0;v.hold_left=0;segment(v,0.,v.fall,v.curve);} // Gate mode: unchanged fall
  }
  if(v.elapsed<v.length){++v.elapsed;if(v.elapsed==v.length){v.level=v.endpoint;
    // Trigger mode: the rise ends, hold for l_hold samples at 1, then fall to 0.
    if(v.trig&&v.endpoint==1.&&!v.pending){if(v.l_hold)v.hold_left=v.l_hold;else segment(v,0.,v.l_fall,v.l_curve);}}
   else{double u=static_cast<double>(v.elapsed)/static_cast<double>(v.length),k=4.*v.latched_curve;double shaped=k==0.?u:std::expm1(k*u)/std::expm1(k);v.level=v.start+(v.endpoint-v.start)*shaped;}}
  else if(v.hold_left&&--v.hold_left==0)segment(v,0.,v.l_fall,v.l_curve);
  b.out(0).ch(c)[i]=static_cast<float>(v.level);}}}
 // State version 2 appends, per voice: l_rise, l_fall, l_curve (f64), pending, hold_left, l_hold, trig (u32).
 int save_state(ws::Writer w){if(!w.u32(2))return WS2_CAPACITY;for(auto&v:voices){for(double x:{v.level,v.start,v.endpoint,v.rise,v.fall,v.curve,v.latched_curve})if(!write_double(w,x))return WS2_CAPACITY;if(!w.u32(uint32_t(v.length))||!w.u32(uint32_t(v.elapsed))||!w.u32(v.high?1:0)||!w.f32(v.last_rise)||!w.f32(v.last_fall)||!w.f32(v.last_curve))return WS2_CAPACITY;
  for(double x:{v.l_rise,v.l_fall,v.l_curve})if(!write_double(w,x))return WS2_CAPACITY;if(!w.u32(v.pending)||!w.u32(v.hold_left)||!w.u32(v.l_hold)||!w.u32(v.trig?1:0))return WS2_CAPACITY;}return WS2_OK;}
 int load_state(ws::Reader r){uint32_t version;if(!r.u32(version))return WS2_INVALID;if(version!=1&&version!=2)return WS2_UNSUPPORTED;auto staged=voices;for(auto&v:staged){for(double*x:{&v.level,&v.start,&v.endpoint,&v.rise,&v.fall,&v.curve,&v.latched_curve})if(!read_double(r,*x))return WS2_INVALID;uint32_t length,elapsed,high;if(!r.u32(length)||!r.u32(elapsed)||!r.u32(high)||!r.f32(v.last_rise)||!r.f32(v.last_fall)||!r.f32(v.last_curve)||!length||length>max_samples||elapsed>length||high>1||v.level<0||v.level>1||v.start<0||v.start>1||(v.endpoint!=0&&v.endpoint!=1)||v.rise<double(.0001f)||v.rise>30||v.fall<double(.0001f)||v.fall>30||std::abs(v.curve)>1||std::abs(v.latched_curve)>1||!std::isfinite(v.last_rise)||!std::isfinite(v.last_fall)||!std::isfinite(v.last_curve)||v.last_rise<.0001f||v.last_rise>30||v.last_fall<.0001f||v.last_fall>30||std::abs(v.last_curve)>1)return WS2_INVALID;v.length=length;v.elapsed=elapsed;v.high=high!=0;
  if(version==1){v.l_rise=v.rise;v.l_fall=v.fall;v.l_curve=v.curve;v.pending=v.hold_left=v.l_hold=0;v.trig=false;continue;}
  uint32_t trig;for(double*x:{&v.l_rise,&v.l_fall,&v.l_curve})if(!read_double(r,*x))return WS2_INVALID;
  if(!r.u32(v.pending)||!r.u32(v.hold_left)||!r.u32(v.l_hold)||!r.u32(trig)||trig>1||v.pending>max_samples||v.hold_left>max_samples||v.l_hold>max_samples||v.l_rise<double(.0001f)||v.l_rise>30||v.l_fall<double(.0001f)||v.l_fall>30||std::abs(v.l_curve)>1)return WS2_INVALID;v.trig=trig!=0;}
  voices=staged;return WS2_OK;}
};
}
