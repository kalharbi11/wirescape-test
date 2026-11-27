// SPDX-License-Identifier: GPL-3.0-only
#include "test_fixture.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include <memory>
#include <cstring>
using Mod=ws::Module<ws2ports::Lfo>;
static constexpr double tau=6.283185307179586476925286766559;
static void descriptor(){
 const auto*d=Mod::descriptor();
 VERIFY(d->input_count==2 && d->output_count==2 && d->param_count==10);
 const char*ids[]={"rate","depth","offset","sync","beats","shape","width","division","clock_ppq","seed"};
 for(unsigned p=0;p<10;++p) VERIFY(!std::strcmp(d->params[p].id,ids[p]) && d->params[p].legacy_index==p);
 VERIFY(d->inputs[0].hint==WS2_GATE && d->inputs[1].hint==WS2_TRIGGER);
 VERIFY(d->params[5].initial==0 && d->params[6].initial==.5f && d->params[7].initial==0);
 VERIFY(d->params[6].flags==d->params[0].flags);
 for(unsigned p: {5u,7u}) VERIFY((d->params[p].flags&(WS2_MODULATABLE|WS2_STEPPED))==(WS2_MODULATABLE|WS2_STEPPED));
 for(unsigned p: {8u,9u}) VERIFY((d->params[p].flags&WS2_STEPPED) && !(d->params[p].flags&WS2_MODULATABLE));
}
static std::unique_ptr<DspFixture> rig(unsigned channels=1){
 auto f=std::make_unique<DspFixture>(Mod::descriptor(),48000,channels);
 for(unsigned p=0;p<2;++p){f->ins[p].flags=0;for(unsigned c=0;c<channels;++c)std::fill_n(f->input[p][c],256,0.f);}
 return f;
}
static void identity(unsigned channels){
 auto f=rig(channels); double phase=0;
 for(int n=0;n<20;++n){f->run(17);for(unsigned i=0;i<17;++i){
  for(unsigned c=0;c<channels;++c){VERIFY(f->output[0][c][i]==float(std::sin(phase)));VERIFY(f->output[1][c][i]==float(phase/tau));}
  phase+=tau/48000.;if(phase>=tau)phase=std::fmod(phase,tau);
 }}
}
static void shapes(){
 auto f=rig(); ws2_transport t{sizeof(t),WS2_PLAYING,120,0,0,4,4,0,0};t.beat=.25;f->process.transport=&t;
 f->parameter[3][0][0]=1;
 const float expected[]={1,1,-.5f,.5f,1};
 for(unsigned s=0;s<7;++s){f->parameter[5][0][0]=float(s);f->run(1);
 float v=f->output[0][0][0]; VERIFY(std::isfinite(v)&&v>=-1&&v<=1);
 if(s<5)VERIFY(std::abs(v-expected[s])<1e-5f);}
 f->parameter[5][0][0]=4;f->parameter[6][0][0]=.1f;f->run(1);VERIFY(f->output[0][0][0]==-1);
 const double lengths[]={16,8,4,3,2,1.5,4./3,1,.75,2./3,.5,.375,1./3,.25,.1875,1./6,.125,1./12};
 for(unsigned d=0;d<18;++d){f->parameter[7][0][0]=float(d+1);t.beat=lengths[d]*.25;f->run(1);VERIFY(std::abs(f->output[1][0][0]-.25f)<1e-5f);}
}
static void clock_reset(){
 auto f=rig();f->ins[0].flags=f->ins[1].flags=WS2_CONNECTED;f->parameter[3][0][0]=1;f->parameter[8][0][0]=2;
 f->input[0][0][0]=f->input[0][0][100]=f->input[0][0][200]=1;f->input[1][0][0]=1;
 f->run(256);VERIFY(f->output[1][0][0]==0);VERIFY(std::abs(f->output[1][0][100]-.5f)<1e-5);VERIFY(std::abs(f->output[1][0][150]-.75f)<1e-5);VERIFY(f->output[1][0][200]<1e-5);
 auto r=rig(16);r->ins[1].flags=WS2_CONNECTED;r->input[1][0][10]=1;
 {allocation_watch::Scope watch;r->run(256);r->reset();r->run(17);VERIFY(watch.allocations()==0);}
 VERIFY(r->output[1][0][10]==0);
}
static void random_reset(){
 auto a=rig(),b=rig();a->parameter[5][0][0]=b->parameter[5][0][0]=6;
 a->parameter[9][0][0]=b->parameter[9][0][0]=12;
 a->parameter[0][0][0]=b->parameter[0][0][0]=20;
 for(int n=0;n<20;++n){a->run(256);b->run(256);VERIFY(!std::memcmp(a->output[0][0],b->output[0][0],256*sizeof(float)));}
 a->reset();b->reset();a->run(256);b->run(256);VERIFY(!std::memcmp(a->output[0][0],b->output[0][0],256*sizeof(float)));
}
int main(){descriptor();identity(1);identity(16);shapes();clock_reset();random_reset();}
