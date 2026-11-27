// SPDX-License-Identifier: GPL-3.0-only
#include "dsp.h"
#include "../../host/midi/module_test_support.h"
#include "../../engine/engine2.h"
#include "../../tests/native/abi2/test_modules.h"
#include <cstring>
using namespace wirescape::midi;

static void audit_channel_filter_exclusion() {
    const auto *d = ws::Module<MidiIn>::descriptor();
    REQUIRE(d->param_count == 1 && d->input_count == 0 && d->output_count == 1);
    REQUIRE(std::strcmp(d->outputs[0].id, "midi") == 0 && d->outputs[0].kind == WS2_EVENT);
    const auto &p = d->params[0];
    REQUIRE(std::strcmp(p.id, "channel_filter") == 0 && p.legacy_index == 0);
    REQUIRE(p.flags == (WS2_STEPPED | WS2_AUTOMATABLE));
    REQUIRE(p.minimum == 0 && p.maximum == 16 && p.initial == 0 && p.step == 1);
    wirescape::engine2::Engine engine(wirescape::engine2::Config{});
    REQUIRE(engine.add_node(1, ws::Module<ws2test::Rows>::descriptor()));
    REQUIRE(engine.add_node(2, d));
    REQUIRE(engine.set_parameter(2, 0, 16.f));
    REQUIRE(!engine.modulate(1, 0, 2, 0, 1.f)); // valid signal source; deliberately excluded target

    MidiRig<MidiIn> r;
    // Omni forwards independent MIDI 1/2 sources and preserves sample offsets.
    r.run({midi1(0x90,60,100,3,10), midi2(0x9f,64,0x40000000,9,20)});
    REQUIRE(r.emitted.size() == 2 && r.emitted[0].source_id == 10 && r.emitted[1].source_id == 20);
    r.emitted.clear();
    r.event_limit = 0;
    r.values[0] = 16; // control-plane value change, no input event needed
    r.run();
    REQUIRE(r.emitted.empty()); // source chokes must survive backpressure

    r.event_limit = 20000;
    r.run({midi1(0x90,60,100,4,10), midi1(0x9f,65,100,17,30), midi1(0xf8,0,0,19,40)});
    REQUIRE(r.emitted.size() == 4);
    for (uint32_t i = 0; i < 2; ++i) {
        const auto &e = r.emitted[i];
        REQUIRE(e.type == WS2_NOTE_CHOKE && e.source_id == (i == 0 ? 10u : 20u));
        REQUIRE(e.offset == 0 && e.channel == -1 && e.note_id == -1);
    }
    REQUIRE(r.emitted[2].ump[0] == 0x209f4164u && r.emitted[2].offset == 17);
    REQUIRE(r.emitted[3].source_id == 40 && r.emitted[3].offset == 19); // system messages pass

    r.emitted.clear();
    r.values[0] = 1;
    r.run({midi1(0x90,60,100,4,10), midi1(0x9f,65,100,17,30)});
    REQUIRE(r.emitted.size() == 3);
    REQUIRE(r.emitted[0].type == WS2_NOTE_CHOKE && r.emitted[0].source_id == 30);
    REQUIRE(r.emitted[1].type == WS2_NOTE_CHOKE && r.emitted[1].source_id == 40);
    REQUIRE(r.emitted[2].ump[0] == 0x20903c64u && r.emitted[2].offset == 4);

    Bytes bytes{};
    ws2_writer writer{sizeof(writer), &bytes, sink};
    REQUIRE(r.d->save_state(r.instance, &writer) == WS2_OK && bytes.size() == 4);
    Source src{&bytes, 0};
    ws2_reader reader{sizeof(reader), &src, source, uint32_t(bytes.size())};
    {
        allocation_watch::Scope watch;
        REQUIRE(r.d->load_state(r.instance, &reader) == WS2_OK);
        REQUIRE(watch.allocations() == 0);
    }
    r.emitted.clear();
    r.values[0] = 16;
    r.run({midi1(0x9f,67,100,6,50)});
    REQUIRE(r.emitted.size() == 1 && r.emitted[0].source_id == 50); // no stale source chokes
    {
        allocation_watch::Scope watch;
        r.d->reset(r.instance);
        REQUIRE(watch.allocations() == 0);
    }
    r.emitted.clear();
    r.run();
    REQUIRE(r.emitted.empty());
    std::puts("PASS MOD-7 MIDI In setParam-only channel filter, source choke ordering, retry and state");
}

int main() {
    audit_channel_filter_exclusion();
    MidiRig<MidiIn> r; r.values[0] = 1;
    r.run({midi1(0x90, 60, 127, 6), midi1(0x91, 62, 127, 9), midi1(0xf8, 0, 0, 10)});
    REQUIRE(r.emitted.size() == 2 && r.emitted[0].offset == 6 && r.emitted[1].offset == 10);
    REQUIRE(r.emitted[0].ump[0] == 0x20903c7fu); r.emitted.clear(); r.values[0] = 0;
    const auto second = midi2(0x91, 60, 0, 9, 2);
    r.run({midi1(0x90, 60, 0, 6, 1), second}); REQUIRE(r.emitted.size() == 3 && r.emitted[0].type == WS2_NOTE_CHOKE && r.emitted[0].source_id == 1);
    REQUIRE(r.emitted[1].ump_words == 1 && r.emitted[2].ump_words == 2 && r.emitted[2].source_id == 2);
    r.emitted.clear(); r.event_limit = 0; r.run({midi1(0x80, 60, 0, 6)}); REQUIRE(r.emitted.empty());
    r.event_limit = 20000; r.run(); REQUIRE(r.emitted.size() == 1 && r.emitted[0].offset == 0 && ((r.emitted[0].ump[0] >> 20) & 15) == 8);
    r.emitted.clear(); r.run({source_choke(2, 7)}); REQUIRE(r.emitted.size() == 1 && r.emitted[0].type == WS2_NOTE_CHOKE && r.emitted[0].source_id == 2);
}
