// SPDX-License-Identifier: GPL-3.0-only
#include "../../engine/engine2.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include "../adsr/dsp.h"
#include "../output/dsp.h"
#include "../vca/dsp.h"
#include "../vco/dsp.h"
#include "dsp.h"
#include "../../tests/native/abi2/dsp_fixture.h"
#include <memory>
#include <cstring>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace wirescape;
static void check(bool value, const char *why) {
    if (!value) throw std::runtime_error(why);
}
struct NoteSpec {
    int type;
    int key;
    int id = -1;
    int channel = 0;
    uint32_t source = 1;
};
static ws2_event note(const NoteSpec &spec) {
    ws2_event e{};
    e.struct_size = sizeof(e);
    e.type = spec.type;
    e.key = spec.key;
    e.note_id = spec.id;
    e.channel = spec.channel;
    e.source_id = spec.source;
    e.value = 1;
    return e;
}
static void shared_tests() {
    using namespace voice;
    NoteToCv core;
    Config cfg;
    cfg.max_voices = 3;
    core.configure(cfg);
    std::vector<ws2_event> log;
    auto emit = [&](const ws2_event &e) { log.push_back(e); };
    for (int key : {60, 69, 72}) core.event(note({WS2_NOTE_ON, key, key}), emit);
    check(core.allocator().voice(0).pitch == 0 && core.allocator().voice(1).pitch == .75f &&
              core.allocator().voice(2).pitch == 1,
          "CV01 pitch");
    cfg.max_voices = 2;
    core.configure(cfg);
    log.clear();
    core.event(note({WS2_NOTE_ON, 60, 1}), emit);
    core.event(note({WS2_NOTE_ON, 64, 2}), emit);
    auto e = note({WS2_NOTE_ON, 67, 3});
    e.offset = 10;
    core.event(e, emit);
    check(log.size() == 4 && log[2].type == WS2_NOTE_CHOKE && log[2].key == 60 && log[2].offset == 10 &&
              log[3].type == WS2_NOTE_ON && log[3].offset == 10,
          "CV04 choke before on");
    core.event(note({WS2_NOTE_OFF, 60, 1}), emit);
    check(core.allocator().active_count() == 2, "stale off cannot close replacement");
    core.reset();
    core.event(note({WS2_NOTE_ON, 60}), emit);
    core.event(note({WS2_NOTE_ON, 60}), emit);
    core.event(note({WS2_NOTE_OFF, 60}), emit);
    check(!core.allocator().voice(0).gate && core.allocator().voice(1).gate, "anonymous repeated key FIFO");
    core.reset();
    core.event(note({WS2_NOTE_ON, 60, -1, 0}), emit);
    core.event(note({WS2_NOTE_ON, 60, -1, 1}), emit);
    e = note({WS2_NOTE_OFF, 60});
    core.sustain(1, 0, true, e, emit);
    core.event(e, emit);
    core.event(note({WS2_NOTE_OFF, 60, -1, 1}), emit);
    check(core.allocator().voice(0).gate && !core.allocator().voice(1).gate, "CV05 channel isolated sustain");
    e.offset = 12;
    core.sustain(1, 0, false, e, emit);
    check(core.allocator().active_count() == 0 && log.back().offset == 12 && log.back().type == WS2_NOTE_OFF,
          "pedal release emits off");
    cfg.mode = Mode::Mono;
    cfg.legato = true;
    core.configure(cfg);
    core.event(note({WS2_NOTE_ON, 60, 1}), emit);
    core.allocator().clear_triggers();
    core.event(note({WS2_NOTE_ON, 64, 2}), emit);
    check(core.allocator().voice(0).gate && !core.allocator().voice(0).trigger, "CV07 mono legato holds gate");
    core.event(note({WS2_NOTE_OFF, 64, 2}), emit);
    check(core.allocator().voice(0).key == 60 && core.allocator().voice(0).gate, "mono fallback");
    for (auto mode : {Mode::Rotate, Mode::Reuse, Mode::Reset, Mode::Mono}) {
        cfg.mode = mode;
        cfg.legato = false;
        core.configure(cfg);
        Allocator oracle(cfg);
        for (int key : {60, 62, 64, 66}) {
            auto event = note({WS2_NOTE_ON, key, key});
            core.event(event, emit);
            oracle.note_on({0, key, 0, static_cast<int16_t>(key), 1});
            for (int c = 0; c < oracle.voice_count(); ++c)
                check(core.allocator().voice(c).key == oracle.voice(c).key &&
                          core.allocator().voice(c).gate == oracle.voice(c).gate,
                      "CV09 allocator oracle");
        }
    }
    core.release_all(UINT32_MAX, e, emit);
    check(core.allocator().active_count() == 0, "release all");
}
static void live_voice_resize() {
    engine2::Config cfg;
    cfg.max_block = 128;
    engine2::Engine engine(cfg);
    const auto producer = uint32_t(engine.register_producer());
    check(engine.add_node(1, ws::Module<ws2ports::HostKeyboard>::descriptor()) &&
          engine.add_node(2, ws::Module<ws2ports::Vco>::descriptor()) &&
          engine.connect(1, 0, 2, 0) && engine.output(2, 0, 0) &&
          engine.set_parameter(1, 0, 4.f) && engine.prepare(), "resize graph");
    float samples[128]{};
    float *rows[]{samples};
    auto render = [&] {
        allocation_watch::Scope scope;
        check(engine.render(rows, 1, 128) == WS2_OK && scope.allocations() == 0,
              "resize render allocation-free");
    };
    for (int key : {60, 64, 67, 71})
        check(engine.enqueue(producer, 1, WS2_DIRECT_PORT, note({WS2_NOTE_ON, key, key}), 0) == WS2_OK,
              "resize note on");
    render();
    engine2::Outgoing event;
    while (engine.pop_egress(event)) {}
    const auto kept = [&] {
        for (uint32_t i = 0; i < 128; ++i) {
            check(engine.output_sample(1, 1, 0, i) == 1 && engine.output_sample(1, 1, 1, i) == 1,
                  "resize kept gates");
            check(engine.output_sample(1, 0, 0, i) == 0 && engine.output_sample(1, 0, 1, i) == 4.f / 12,
                  "resize kept pitches");
        }
    };
    const auto parameter = [&](uint32_t index, float value, uint32_t offset = 0) {
        ws2_event e{};
        e.struct_size = sizeof(e);
        e.type = WS2_PARAM;
        e.index = index;
        e.value = value;
        check(engine.enqueue(producer, 1, WS2_DIRECT_PORT, e, engine.meter().frame + offset) == WS2_OK,
              "live parameter enqueue");
    };
    parameter(0, 2.f);
    render();
    check(engine.output_channels(1, 1) == 2, "shrink width");
    kept();
    unsigned offs = 0;
    while (engine.pop_egress(event)) if (event.node == 1 && event.port == 3) {
        check((event.event.type == WS2_NOTE_OFF || event.event.type == WS2_NOTE_CHOKE) &&
              event.event.channel >= 2 && event.event.channel < 4 &&
              event.event.note_id == (event.event.channel == 2 ? 67 : 71), "shrink releases removed IDs only");
        ++offs;
    }
    check(offs == 2, "shrink two releases");
    parameter(0, 2.f);
    render();
    kept();
    while (engine.pop_egress(event)) check(event.node != 1 || event.port != 3, "same count emits no notes");
    parameter(0, 4.f);
    render();
    check(engine.output_channels(1, 1) == 4, "grow width");
    kept();
    for (uint32_t c = 2; c < 4; ++c)
        for (uint32_t i = 0; i < 128; ++i)
            check(engine.output_sample(1, 1, c, i) == 0, "new lanes idle");
    while (engine.pop_egress(event)) check(event.node != 1 || event.port != 3, "grow emits no notes");
    // Nonconstant parameter signals and an event at a later offset must
    // not be folded into the constant-span fast path.
    parameter(0, 1.f, 32);
    parameter(0, 2.f, 64);
    check(engine.enqueue(producer, 1, WS2_DIRECT_PORT, note({WS2_NOTE_ON, 79, 79}),
                         engine.meter().frame + 72) == WS2_OK, "midblock note on");
    render();
    check(engine.output_channels(1, 1) == 2, "midblock grow width");
    for (uint32_t i = 0; i < 128; ++i)
        check(engine.output_sample(1, 1, 0, i) == 1 &&
              engine.output_sample(1, 0, 0, i) == 0, "midblock keeps first voice");
    for (uint32_t i = 64; i < 128; ++i)
        check(engine.output_sample(1, 1, 1, i) == (i >= 72 ? 1.f : 0.f),
              "midblock new lane idle until note");
    unsigned mid_off = 0, mid_on = 0;
    while (engine.pop_egress(event)) if (event.node == 1 && event.port == 3) {
        if (event.event.type == WS2_NOTE_OFF) {
            check(event.event.note_id == 64 && event.event.offset == 32, "midblock release offset");
            ++mid_off;
        } else if (event.event.type == WS2_NOTE_ON) {
            check(event.event.note_id == 79 && event.event.offset == 72, "midblock note offset");
            ++mid_on;
        }
    }
    check(mid_off == 1 && mid_on == 1, "midblock release and note");
    parameter(1, 3.f);
    render();
    check(engine.output_channels(1, 1) == 1 && engine.output_sample(1, 1, 0, 127) == 0,
          "mode change retains existing release semantics");
}
static void graph_tests() {
    for (auto rate : {44100., 48000., 96000.})
        for (uint32_t voices : {1u, 4u, 16u})
            for (uint32_t frames : {1u, 17u, 128u, 256u}) {
                engine2::Config cfg;
                cfg.sample_rate = rate;
                cfg.max_block = 256;
                engine2::Engine engine(cfg);
                engine.register_producer();
                check(engine.add_node(1, ws::Module<ws2ports::HostKeyboard>::descriptor()), "keyboard add");
                check(engine.add_node(2, ws::Module<ws2ports::Vco>::descriptor()) &&
                          engine.add_node(3, ws::Module<ws2ports::Adsr>::descriptor()) &&
                          engine.add_node(4, ws::Module<ws2ports::Vca>::descriptor()) &&
                          engine.add_node(5, ws::Module<ws2ports::Output>::descriptor()),
                      "synth add");
                check(engine.connect(1, 0, 2, 0) && engine.connect(1, 1, 3, 0) && engine.connect(2, 0, 4, 0) &&
                          engine.connect(3, 0, 4, 1) && engine.connect(4, 0, 5, 0) && engine.output(5, 0, 0) &&
                          engine.prepare(),
                      "prepare graph");
                check(engine.set_parameter(1, 0, float(voices)), "voice count");
                for (int key : {60, 64, 67, 72}) {
                    auto e = note({WS2_NOTE_ON, key, key});
                    check(engine.enqueue(0, 1, WS2_DIRECT_PORT, e, 0) == WS2_OK, "note enqueue");
                }
                float samples[256]{};
                float *rows[] = {samples};
                int result;
                size_t allocations;
                {
                    allocation_watch::Scope scope;
                    result = engine.render(rows, 1, frames);
                    allocations = scope.allocations();
                }
                check(result == WS2_OK && allocations == 0, "callback renders without allocation");
                check(engine.output_channels(1, 0) == voices && engine.output_channels(2, 0) == voices,
                      "source propagates poly width");
                check(engine.output_channels(5, 0) == 1, "Output SUM collapses voices");
                if (voices == 1 && frames == 1)
                    check(engine.output_sample(1, 1, 0, 0) == 0, "stolen mono voice has retrigger gap");
                check(engine.render(rows, 1, frames) == WS2_OK, "settled chord render");
                int active = 0;
                for (uint32_t c = 0; c < voices; ++c) active += engine.output_sample(1, 1, c, frames - 1) > .5f;
                check(active == int(std::min(voices, 4u)), "four-note chord active channels");
                for (uint32_t i = 0; i < frames; ++i) check(std::isfinite(samples[i]), "finite audio");
                engine.panic();
                check(engine.render(rows, 1, frames) == WS2_OK, "panic render");
                for (uint32_t c = 0; c < voices; ++c)
                    check(engine.output_sample(1, 1, c, frames - 1) == 0, "panic clears every gate");
            }
}
static void reclamation_burst() {
    engine2::Config config;
    config.max_block = 128;
    config.max_channels = 1;
    engine2::Engine engine(config);
    engine.register_producer();
    check(engine.add_node(1, ws::Module<ws2ports::HostKeyboard>::descriptor()) &&
              engine.add_node(2, ws::Module<ws2ports::Vco>::descriptor()) && engine.connect(1, 0, 2, 0) &&
              engine.output(2, 0, 0) && engine.prepare(),
          "burst graph");
    float samples[128]{};
    float *rows[]{samples};
    engine2::Outgoing outgoing;
    auto drain = [&]() {
        while (engine.pop_egress(outgoing)) {
        }
    };
    ws2_event event{};
    event.struct_size = sizeof(event);
    event.key = 60;
    event.value = 1;
    event.note_id = -1;
    event.type = WS2_NOTE_ON;
    // Stolen anonymous activations pin the first127 mappings until their offs.
    for (uint32_t source = 0; source < 127; ++source) {
        event.source_id = source;
        check(engine.enqueue(0, 1, WS2_DIRECT_PORT, event, engine.meter().frame) == WS2_OK, "prime burst source");
        check(engine.render(rows, 1, 128) == WS2_OK, "prime burst render");
        drain();
    }
    long long worst = 0;
    for (uint32_t burst = 0; burst < 16; ++burst) {
        event.note_id = 7;
        for (uint32_t pair = 0; pair < 32; ++pair) {
            event.source_id = 1000 + burst * 32 + pair;
            event.type = WS2_NOTE_ON;
            check(engine.enqueue(0, 1, WS2_DIRECT_PORT, event, engine.meter().frame + uint64_t{pair} * 2) == WS2_OK, "burst on");
            event.type = WS2_NOTE_OFF;
            check(engine.enqueue(0, 1, WS2_DIRECT_PORT, event, engine.meter().frame + uint64_t{pair} * 2 + 1) == WS2_OK,
                  "burst off");
        }
        const auto begin = std::chrono::steady_clock::now();
        int result;
        size_t allocations;
        {
            allocation_watch::Scope scope;
            result = engine.render(rows, 1, 128);
            allocations = scope.allocations();
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
        worst = std::max(worst, static_cast<long long>(elapsed));
        check(result == WS2_OK && allocations == 0 && engine.meter().faults == 0, "reclamation burst allocation/fault");
        check(engine.output_sample(1, 1, 0, 127) == 0, "burst releases final gate");
        drain();
    }
    std::cout << "source_reclamation rate=48000 block=128 events=64 bursts=16 worst_ns=" << worst
              << " period_ns=2666666 allocations=0 (timing report only)\n";
}

static std::vector<uint8_t> e7c_save(DspFixture& f) {
    check(f.d->save_state && f.d->load_state, "A70 voice-chain hooks present");
    std::vector<uint8_t> bytes;
    ws2_writer w{sizeof(w), &bytes, [](void* ctx, const void* p, uint32_t n) {
        auto& b = *static_cast<std::vector<uint8_t>*>(ctx);
        auto* first = static_cast<const uint8_t*>(p);
        b.insert(b.end(), first, first+n);
        return int(WS2_OK);
    }};
    check(f.d->save_state(f.state, &w) == WS2_OK && !bytes.empty(), "A70 voice-chain save");
    return bytes;
}
static int e7c_load(DspFixture& f, const std::vector<uint8_t>& bytes) {
    struct Cursor { const std::vector<uint8_t>& b; size_t pos = 0; } c{bytes};
    ws2_reader r{sizeof(r), &c, [](void* ctx, void* p, uint32_t n) {
        auto& c = *static_cast<Cursor*>(ctx);
        if (n > c.b.size()-c.pos) return int(WS2_INVALID);
        std::memcpy(p, c.b.data()+c.pos, n); c.pos += n;
        return int(WS2_OK);
    }, uint32_t(bytes.size())};
    return f.d->load_state(f.state, &r);
}
static void e7c_keyboard_roundtrip() {
    const auto* d = ws::Module<ws2ports::HostKeyboard>::descriptor();
    auto a=std::make_unique<DspFixture>(d,48000.,1);
    auto b=std::make_unique<DspFixture>(d,48000.,1);
    auto rate=std::make_unique<DspFixture>(d,44100.,1);
    std::vector<ws2_event> log_a,log_b;
    ws2_event_out out_a[4]{},out_b[4]{};
    auto push=[](void* ctx,uint32_t,const ws2_event* e){
        static_cast<std::vector<ws2_event>*>(ctx)->push_back(*e);return int(WS2_OK);
    };
    out_a[3]={sizeof(ws2_event_out),&log_a,3,push};
    out_b[3]={sizeof(ws2_event_out),&log_b,3,push};
    a->process.event_outputs=out_a; b->process.event_outputs=out_b;
    for(auto*f:{a.get(),b.get(),rate.get()})f->parameter[0][0][0]=2.f;
    auto on=note({WS2_NOTE_ON,67,391,0,7});
    on.offset=17;
    a->process.events={sizeof(ws2_event_list),1,&on};
    a->run(128);
    a->process.events={sizeof(ws2_event_list),0,nullptr};
    a->run(128);
    check(log_a.size()==1 && log_a[0].note_id==391,"held explicit note emitted");
    const auto saved=e7c_save(*a);
    check(e7c_load(*b,saved)==WS2_OK,"fresh instance load");
    log_a.clear();
    auto compare=[&](){
        a->run(128);b->run(128);
        for(uint32_t p=0;p<3;++p){
            check(a->outs[p].channels==b->outs[p].channels,"keyboard restored width");
            for(uint32_t c=0;c<a->outs[p].channels;++c)
                check(std::memcmp(a->output[p][c],b->output[p][c],128*sizeof(float))==0,"keyboard CV continuation");
        }
    };
    compare();
    check(log_a.empty()&&log_b.empty(),"restore does not re-emit");
    auto off=note({WS2_NOTE_OFF,67,391,0,7});
    off.offset=31;
    a->process.events=b->process.events={sizeof(ws2_event_list),1,&off};
    compare();
    check(log_a.size()==1&&log_b.size()==1&&log_b[0].type==WS2_NOTE_OFF&&
        log_b[0].note_id==391&&log_b[0].source_id==7&&log_b[0].offset==31,
        "restored held identity must emit later NOTE_OFF");
    auto current=e7c_save(*b),bad=saved;bad[0]=99;
    check(e7c_load(*b,bad)==WS2_UNSUPPORTED&&e7c_save(*b)==current,"unknown version refused atomically");
    bad=saved;bad.pop_back();
    check(e7c_load(*b,bad)==WS2_INVALID&&e7c_save(*b)==current,"truncation refused atomically");
    check(e7c_load(*rate,saved)==WS2_INVALID,"C2 rate mismatch refused");
    // Version-1 wire header: Keyboard 20 bytes, core cursor at byte 60.
    for(size_t offset:{size_t(28),size_t(32),size_t(60),size_t(64),size_t(72)}){
        bad=saved;bad[offset]=255;
        check(e7c_load(*b,bad)==WS2_INVALID&&e7c_save(*b)==current,
              "malformed policy, priority, cursor or source rejected atomically");
    }
}

static void e7c_keyboard_anonymous_and_pending() {
    const auto* d=ws::Module<ws2ports::HostKeyboard>::descriptor();
    auto a=std::make_unique<DspFixture>(d,48000.,1);
    auto b=std::make_unique<DspFixture>(d,48000.,1);
    struct Sink { bool accept=true; std::vector<ws2_event> notes; } sa,sb;
    auto push=[](void* ctx,uint32_t,const ws2_event* e){
        auto& s=*static_cast<Sink*>(ctx);
        if(!s.accept)return int(WS2_CAPACITY);
        s.notes.push_back(*e);return int(WS2_OK);
    };
    ws2_event_out oa[4]{},ob[4]{};
    oa[3]={sizeof(ws2_event_out),&sa,3,push};
    ob[3]={sizeof(ws2_event_out),&sb,3,push};
    a->process.event_outputs=oa;b->process.event_outputs=ob;
    for(auto*f:{a.get(),b.get()}){
        f->parameter[0][0][0]=2.f;
        f->parameter[1][0][0]=3.f; // Mono: earlier held note must be restored for fallback.
    }
    auto on1=note({WS2_NOTE_ON,60,-1,0,13});
    auto on2=note({WS2_NOTE_ON,67,-1,0,13});
    auto run=[](DspFixture& f,ws2_event* e){
        f.process.events={sizeof(ws2_event_list),e?1u:0u,e};f.run(128);
    };
    run(*a,&on1);run(*a,&on2);
    const auto saved=e7c_save(*a);
    check(e7c_load(*b,saved)==WS2_OK,"mono anonymous stack restores");
    sa.notes.clear();
    run(*a,nullptr);run(*b,nullptr);
    check(sa.notes.empty()&&sb.notes.empty(),"mono restore no duplicate attack");
    auto off2=note({WS2_NOTE_OFF,67,-1,0,13});
    run(*a,&off2);run(*b,&off2);
    check(sa.notes.size()==sb.notes.size()&&sb.notes.size()==2&&
          sb.notes[1].type==WS2_NOTE_ON&&sb.notes[1].key==60&&
          sb.notes[1].note_id==sa.notes[1].note_id,"anonymous mono fallback identity");
    for(uint32_t p=0;p<3;++p)
        check(std::memcmp(a->output[p][0],b->output[p][0],128*sizeof(float))==0,
              "mono fallback CV bit-identical");
    auto off1=note({WS2_NOTE_OFF,60,-1,0,13});
    sb.accept=false;
    run(*b,&off1); // delivered note, refused release => pending release
    auto sink=[](void*,const void*,uint32_t){return int(WS2_OK);};
    ws2_writer w{sizeof(w),nullptr,sink};
    check(d->save_state(b->state,&w)==WS2_INVALID,"pending release cannot claim exact carry");
    sb.accept=true;run(*b,nullptr);
    check(d->save_state(b->state,&w)==WS2_OK,"delivered release permits carry");
    sb.accept=false;run(*b,&on1);
    check(d->save_state(b->state,&w)==WS2_INVALID,"unemitted attack cannot claim exact carry");
}

static void e7c_vco_vca_roundtrip() {
    const auto* d=ws::Module<ws2ports::Vco>::descriptor();
    auto a=std::make_unique<DspFixture>(d,48000.,1);
    auto b=std::make_unique<DspFixture>(d,48000.,1);
    auto rate=std::make_unique<DspFixture>(d,44100.,1);
    for(auto*f:{a.get(),b.get(),rate.get()}){
        f->ins[0].flags=WS2_CONSTANT;f->ins[0].channels=0; // note-driven frequency
    }
    auto on=note({WS2_NOTE_ON,74,21,0,9});on.offset=19;
    a->process.events={sizeof(ws2_event_list),1,&on};
    a->run(128);
    a->process.events={sizeof(ws2_event_list),0,nullptr};
    a->run(128);
    const auto saved=e7c_save(*a);
    check(e7c_load(*b,saved)==WS2_OK,"VCO fresh-state phase and frequency");
    auto compare=[&](){
        a->run(128);b->run(128);
        check(std::memcmp(a->output[0][0],b->output[0][0],128*sizeof(float))==0,
              "VCO continuation phase/event pitch bit-identical");
    };
    compare();
    auto choke=note({WS2_NOTE_CHOKE,74,21,0,9});choke.offset=33;
    a->process.events=b->process.events={sizeof(ws2_event_list),1,&choke};
    compare();
    const auto current=e7c_save(*b);
    auto bad=saved;bad[0]=99;
    check(e7c_load(*b,bad)==WS2_UNSUPPORTED&&e7c_save(*b)==current,"VCO unknown version atomic");
    bad=saved;bad.pop_back();
    check(e7c_load(*b,bad)==WS2_INVALID&&e7c_save(*b)==current,"VCO truncated atomic");
    bad=saved;bad[24]=2; // first voice's encoded note_active
    check(e7c_load(*b,bad)==WS2_INVALID&&e7c_save(*b)==current,"VCO malformed active flag atomic");
    check(e7c_load(*rate,saved)==WS2_INVALID,"VCO C2 mismatch refused");

    d=ws::Module<ws2ports::Vca>::descriptor();
    auto gain=std::make_unique<DspFixture>(d,48000.,1);
    auto gain_fresh=std::make_unique<DspFixture>(d,44100.,1);
    gain->run(128);
    const auto vca=e7c_save(*gain);
    check(vca.size()==4&&e7c_load(*gain_fresh,vca)==WS2_OK,"VCA version-only C2 adaptation");
    gain->run(128);gain_fresh->run(128);
    check(std::memcmp(gain->output[0][0],gain_fresh->output[0][0],128*sizeof(float))==0,
          "VCA stateless cross-rate sample equality");
    bad=vca;bad[0]=99;
    check(e7c_load(*gain_fresh,bad)==WS2_UNSUPPORTED&&e7c_save(*gain_fresh)==vca,"VCA version refused");
    bad=vca;bad.pop_back();
    check(e7c_load(*gain_fresh,bad)==WS2_INVALID&&e7c_save(*gain_fresh)==vca,"VCA truncated refused");
}


static void e7c_keyboard_differing_controls() {
    const auto* d=ws::Module<ws2ports::HostKeyboard>::descriptor();
    for(bool change_mode:{false,true}){
        auto a=std::make_unique<DspFixture>(d,48000.,1);
        auto b=std::make_unique<DspFixture>(d,48000.,1);
        std::vector<ws2_event> la,lb;
        ws2_event_out oa[4]{},ob[4]{};
        auto push=[](void* ctx,uint32_t,const ws2_event* e){
            static_cast<std::vector<ws2_event>*>(ctx)->push_back(*e);
            return int(WS2_OK);
        };
        oa[3]={sizeof(ws2_event_out),&la,3,push};
        ob[3]={sizeof(ws2_event_out),&lb,3,push};
        a->process.event_outputs=oa;b->process.event_outputs=ob;
        a->parameter[0][0][0]=4.f;a->parameter[1][0][0]=0.f;
        std::array<ws2_event,4> chord{};
        for(int c=0;c<4;++c){
            chord[c]=note({WS2_NOTE_ON,60+4*c,100+c,0,7});
            chord[c].offset=uint32_t(c*8);
        }
        a->process.events={sizeof(ws2_event_list),4,chord.data()};
        a->run(128);
        check(la.size()==4,"A73 live oracle four delivered notes");
        a->process.events={sizeof(ws2_event_list),0,nullptr};
        a->run(128);
        const auto saved=e7c_save(*a);
        // The fresh instance has never processed, but its actual first-block
        // controls already differ. Load can see only the staged saved config.
        for(auto*f:{a.get(),b.get()}){
            f->parameter[0][0][0]=change_mode?4.f:2.f;
            f->parameter[1][0][0]=change_mode?3.f:0.f;
        }
        check(e7c_load(*b,saved)==WS2_OK,"A73 fresh differing-controls load");
        la.clear();
        check(lb.empty(),"A73 load emits nothing");
        auto compare=[&](){
            a->run(128);b->run(128);
            for(uint32_t p=0;p<3;++p){
                const uint32_t width=change_mode?1u:2u;
                check(a->outs[p].channels==width&&b->outs[p].channels==width,
                      "A73 live and fresh configured widths");
                for(uint32_t c=0;c<width;++c)
                    check(std::memcmp(a->output[p][c],b->output[p][c],128*sizeof(float))==0,
                          "A73 differing controls match live CV samples");
            }
            check(la.size()==lb.size(),"A73 same live release count");
            for(size_t i=0;i<la.size();++i){
                const auto& x=la[i];const auto& y=lb[i];
                check(x.type==y.type&&x.channel==y.channel&&x.key==y.key&&
                      x.note_id==y.note_id&&x.source_id==y.source_id&&
                      x.offset==y.offset&&x.value==y.value,
                      "A73 same live event identity and timing");
            }
        };
        compare();
        check(lb.size()==(change_mode?4u:2u),"A73 exact released voice count");
        for(size_t i=0;i<lb.size();++i){
            const int lane=int(i)+(change_mode?0:2);
            const auto& e=lb[i];
            check(e.type==(change_mode?WS2_NOTE_CHOKE:WS2_NOTE_OFF)&&
                  e.channel==lane&&e.key==60+4*lane&&e.note_id==100+lane&&
                  e.source_id==7&&e.offset==0,"A73 only expected live releases, no attack");
        }
        for(uint32_t c=0;c<b->outs[1].channels;++c)
            for(uint32_t i=0;i<128;++i)
                check(b->output[1][c][i]==(change_mode?0.f:1.f),"A73 retained/cleared gate");
        la.clear();lb.clear();
        compare();
        check(lb.empty(),"A73 settled block no duplicate release or attack");
        auto off=note({WS2_NOTE_OFF,60,100,0,7});off.offset=41;
        a->process.events=b->process.events={sizeof(ws2_event_list),1,&off};
        compare();
        if(change_mode)check(lb.empty(),"A73 old note no longer active after mode change");
        else check(lb.size()==1&&lb[0].type==WS2_NOTE_OFF&&lb[0].note_id==100&&
                   lb[0].offset==41,"A73 retained voice releases its original identity");
    }
}

int main() {
    try {
        e7c_keyboard_roundtrip();
        e7c_keyboard_differing_controls();
        e7c_keyboard_anonymous_and_pending();
        e7c_vco_vca_roundtrip();
        reclamation_burst();
        shared_tests();
        live_voice_resize();
        graph_tests();
        std::cout << "keyboard/core matrices pass\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
