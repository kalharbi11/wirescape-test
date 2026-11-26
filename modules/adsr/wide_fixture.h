// SPDX-License-Identifier: GPL-3.0-only
// MOD-2 workaround (DESIGN.md "Limitations"): a folder-local copy of the lab's
// tests/native/abi2/dsp_fixture.h at LAB_PIN c083b26 with ports=8, because ADSR now has 7 parameters
// and the lab fixture holds at most 6. Only the include list and `ports` differ from the lab copy.
#pragma once
#include "../vco/dsp.h"
#include "dsp.h"
#include "../vca/dsp.h"
#include <cstdio>
#include <cstdlib>
#define VERIFY(x) do{if(!(x)){std::fprintf(stderr,"ABI2 fixture line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
// Fixed storage callback harness; allocate the harness itself before watching.
struct DspFixture {
 static constexpr uint32_t ports=8,channels=16,frames=256;
 struct Pool {alignas(64)unsigned char bytes[32768]{};size_t used=0;}pool;
 const ws2_descriptor*d;void*state=nullptr;ws2_config config{};ws2_host host{};
 alignas(32)float input[ports][channels][frames]{},parameter[ports][channels][frames]{},output[ports][channels][frames]{};
 const float*ip[ports][channels]{},*pp[ports][channels]{};float*op[ports][channels]{};
 ws2_signal ins[ports]{},pars[ports]{};ws2_signal_out outs[ports]{};ws2_event_list event_ports[ports]{};
 ws2_process process{};
 static void*WS2_CALL alloc(void*ctx,size_t bytes,size_t alignment,uint32_t){auto&p=*static_cast<Pool*>(ctx);p.used=(p.used+alignment-1)&~(alignment-1);if(p.used+bytes>sizeof(p.bytes))return nullptr;auto*r=p.bytes+p.used;p.used+=bytes;return r;}
 DspFixture(const ws2_descriptor*desc,double rate=48000,uint32_t count=1):d(desc){
  VERIFY(d->input_count<=ports&&d->output_count<=ports&&d->param_count<=ports);
  host={sizeof(host),&pool,alloc};config={sizeof(config),rate,frames,channels,42,nullptr,0};state=d->create(&host);VERIFY(state);VERIFY(d->prepare(state,&config,&host)==WS2_OK);
  for(uint32_t p=0;p<ports;++p){for(uint32_t c=0;c<channels;++c){ip[p][c]=input[p][c];pp[p][c]=parameter[p][c];op[p][c]=output[p][c];}
   ins[p]={sizeof(ws2_signal),ip[p],count,WS2_CONNECTED};pars[p]={sizeof(ws2_signal),pp[p],1,WS2_CONSTANT};outs[p]={sizeof(ws2_signal_out),op[p],count,0,channels};event_ports[p]={sizeof(ws2_event_list),0,nullptr};}
  for(uint32_t p=0;p<d->param_count;++p)for(uint32_t c=0;c<channels;++c)std::fill_n(parameter[p][c],frames,d->params[p].initial);
  for(uint32_t p=0;p<d->input_count;++p){if(d->inputs[p].kind==WS2_EVENT){ins[p].channels=0;ins[p].flags=WS2_CONSTANT;}else for(uint32_t c=0;c<count;++c)for(uint32_t i=0;i<frames;++i)input[p][c][i]=std::sin(.13f*i+.2f*c)*(p==1?.2f:1.f);}
  process.struct_size=sizeof(process);process.frames=frames;process.channels=count;process.sample_rate=rate;process.input_count=d->input_count;process.output_count=d->output_count;process.param_count=d->param_count;process.inputs=ins;process.outputs=outs;process.params=pars;process.event_inputs=event_ports;process.events={sizeof(ws2_event_list),0,nullptr};
 }
 ~DspFixture(){d->destroy(state);}
 void run(uint32_t n=frames){process.frames=n;VERIFY(d->process(state,&process)==WS2_OK);process.frame_position+=n;}
 void reset(){d->reset(state);process.frame_position=0;}
};
