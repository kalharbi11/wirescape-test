// SPDX-License-Identifier: GPL-3.0-only
#include "../../engine/engine2.h"
#include "../../host/midi/module_test_support.h"
#include "../adsr/dsp.h"
#include "../midi_cc_to_cv/data_validation.h"
#include "../midi_in/dsp.h"
#include "../output/dsp.h"
#include "../vca/dsp.h"
#include "../vcf/dsp.h"
#include "../vco/dsp.h"
#include "trigger_test_support.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <tuple>
using namespace wirescape; using namespace midi;
static void basic_and_steal() {
    MidiRig<MidiToCv> r; r.values[0] = 3;
    r.run({midi1(0x90, 60, 127), midi1(0x90, 69, 64), midi1(0x90, 72, 1)});
    REQUIRE(r.sample(0, 0) == 0 && r.sample(0, 1) == .75f && r.sample(0, 2) == 1);
    REQUIRE(r.sample(2, 0) == 1 && r.sample(2, 1) == 64.f / 127 && r.sample(2, 2) == 1.f / 127);
    REQUIRE(r.emitted.size() == 3 && r.emitted[0].note_id != r.emitted[1].note_id);
    MidiRig<MidiToCv> steal; steal.values[0] = 2;
    steal.run({midi1(0x90, 60, 127, 0), midi1(0x90, 64, 127, 5), midi1(0x90, 67, 127, 10), midi1(0x80, 60, 0, 20)});
    REQUIRE(steal.emitted.size() == 4 && steal.emitted[2].type == WS2_NOTE_CHOKE && steal.emitted[3].type == WS2_NOTE_ON);
    REQUIRE(steal.emitted[2].offset == 10 && steal.emitted[3].offset == 10 && steal.sample(1, 0, 20) == 1);
    for (uint32_t voices : {1u, 4u}) {
        MidiRig<MidiToCv> same; same.values[0] = float(voices);
        same.run({midi1(0x90, 60, 127, 0), midi1(0x90, 60, 127, 1), midi1(0x80, 60, 0, 2)});
        REQUIRE(same.sample(1, voices == 1 ? 0 : 1) == 1); same.run({midi1(0x80, 60, 0)});
        for (uint32_t c = 0; c < voices; ++c) REQUIRE(same.sample(1, c) == 0);
    }
}
static void live_voice_resize() {
    MidiRig<MidiToCv> r;
    r.values[0] = 4;
    r.run({midi1(0x90, 60, 100), midi1(0x90, 64, 90), midi1(0x90, 67, 80),
           midi1(0x90, 71, 70), midi1(0xb0, 64, 127, 1), midi1(0x80, 60, 0, 2),
           midi1(0xa0, 64, 100, 3)});
    REQUIRE(r.emitted.size() == 5);
    const auto a = r.emitted[0], b = r.emitted[1], c = r.emitted[2], d = r.emitted[3];
    const auto kept = [&] {
        for (uint32_t frame = 0; frame < 32; ++frame) {
            REQUIRE(r.sample(1, 0, frame) == 1 && r.sample(1, 1, frame) == 1);
            REQUIRE(r.sample(0, 0, frame) == 0 && r.sample(0, 1, frame) == 4.f / 12);
            REQUIRE(r.sample(2, 0, frame) == 100.f / 127 && r.sample(2, 1, frame) == 90.f / 127);
            REQUIRE(r.sample(4, 1, frame) == 100.f / 127);
        }
    };
    r.emitted.clear();
    r.values[0] = 2;
    r.run();
    REQUIRE(r.outputs[0].channels == 2);
    kept();
    REQUIRE(r.emitted.size() == 2);
    for (uint32_t i = 0; i < 2; ++i) {
        const auto &off = r.emitted[i];
        const auto &on = i == 0 ? c : d;
        REQUIRE((off.type == WS2_NOTE_OFF || off.type == WS2_NOTE_CHOKE) &&
                off.channel == on.channel && off.key == on.key && off.note_id == on.note_id &&
                off.source_id == on.source_id && off.offset == 0);
    }
    r.emitted.clear();
    r.run();
    kept();
    REQUIRE(r.emitted.empty());
    r.values[0] = 4;
    r.run();
    REQUIRE(r.outputs[0].channels == 4);
    kept();
    REQUIRE(r.emitted.empty());
    for (uint32_t ch = 2; ch < 4; ++ch)
        for (uint32_t frame = 0; frame < 32; ++frame) REQUIRE(r.sample(1, ch, frame) == 0);
    r.run({midi1(0x80, 67, 0), midi1(0x80, 71, 0)});
    kept();
    REQUIRE(r.emitted.empty());
    r.run({midi1(0xb0, 64, 0)});
    REQUIRE(r.emitted.size() == 1 && r.emitted[0].type == WS2_NOTE_OFF &&
            r.emitted[0].note_id == a.note_id && r.sample(1, 0) == 0 && r.sample(1, 1) == 1);
    r.emitted.clear();
    r.run({midi1(0x80, 64, 0)});
    REQUIRE(r.emitted.size() == 1 && r.emitted[0].type == WS2_NOTE_OFF &&
            r.emitted[0].note_id == b.note_id && r.sample(1, 1) == 0);

    MidiRig<MidiToCv> mono;
    mono.values[1] = 3;
    mono.run({midi1(0x90, 60, 127), midi1(0x90, 64, 127)});
    mono.emitted.clear();
    mono.values[0] = 2;
    mono.run();
    REQUIRE(mono.outputs[0].channels == 1 && mono.sample(1) == 1 &&
            mono.sample(0) == 4.f / 12 && mono.emitted.empty());
    mono.values[0] = 4;
    mono.run();
    REQUIRE(mono.outputs[0].channels == 1 && mono.sample(1) == 1 && mono.emitted.empty());
    mono.run({midi1(0x80, 64, 0)});
    REQUIRE(mono.sample(1) == 1 && mono.sample(0) == 0);
}

static void sustain_bend_expression() {
    MidiRig<MidiToCv> s;
    s.run({midi1(0x90, 60, 127, 0, 1), midi1(0x90, 64, 127, 0, 2), midi1(0x91, 67, 127, 0, 1), midi1(0xb0, 64, 127, 3, 1), midi1(0x80, 60, 0, 5, 1), midi1(0x80, 64, 0, 5, 2), midi1(0x81, 67, 0, 5, 1), midi1(0xb0, 64, 0, 12, 1)});
    REQUIRE(s.sample(1, 0, 11) == 1 && s.sample(1, 0, 12) == 0 && s.sample(1, 1, 11) == 0 && s.sample(1, 2, 11) == 0);
    MidiRig<MidiToCv> bend;
    bend.run({midi1(0x90, 60, 127), midi1(0xe0, 0, 64, 1), midi1(0xe0, 0, 0, 2), midi1(0xe0, 127, 127, 3)});
    REQUIRE(bend.sample(0, 0, 1) == 0 && bend.sample(0, 0, 2) == -1.f / 6 && bend.sample(0, 0, 3) == 1.f / 6);
    REQUIRE(bend.sample(5, 0, 2) == -1 && bend.sample(5, 0, 3) == 1);
    bend.values[7] = 0; bend.run({midi1(0xe0, 0, 0)}); REQUIRE(bend.sample(0) == 0 && bend.sample(5) == -1);
    MidiRig<MidiToCv> expression;
    expression.run({midi1(0x91, 60, 127), midi1(0xd1, 64, 0, 1), midi1(0xa1, 60, 100, 2), midi1(0xb1, 1, 127, 3)});
    REQUIRE(expression.sample(3) == 64.f / 127 && expression.sample(4) == 100.f / 127 && expression.sample(6) == 1);
    MidiRig<MidiToCv> zero;
    zero.run({midi1(0x90, 60, 127), midi1(0x90, 60, 0, 1), midi2(0x90, 60, 0, 2)});
    REQUIRE(zero.sample(1, 0, 1) == 0 && zero.sample(1, 1, 2) == 1 && zero.sample(2, 1, 2) == 0);
    MidiRig<MidiToCv> channel;
    channel.run({midi1(0x90, 60, 127), midi1(0x91, 64, 127), midi1(0xb0, 64, 127, 1), midi1(0xb0, 123, 0, 2)});
    REQUIRE(channel.sample(1, 0) == 0 && channel.sample(1, 1) == 1);
}
static void modes_state_and_bounds() {
    for (uint32_t mode = 0; mode < 4; ++mode) for (uint32_t priority = 0; priority < 3; ++priority) {
        MidiRig<MidiToCv> r; r.values[1] = float(mode); r.values[3] = float(priority);
        voice::Config cfg; cfg.max_voices = 4; cfg.mode = static_cast<voice::Mode>(mode); cfg.mono_priority = static_cast<voice::Priority>(priority); voice::Allocator oracle(cfg);
        for (int key : {60, 62, 64}) oracle.note_on({0, key, 0, int16_t(key), 1}); oracle.note_off({0, 62, 0, 62, 0}); oracle.note_on({0, 66, 0, 66, 1});
        r.run({midi1(0x90, 60, 127), midi1(0x90, 62, 127), midi1(0x90, 64, 127), midi1(0x80, 62, 0), midi1(0x90, 66, 127)});
        for (int c = 0; c < oracle.voice_count(); ++c) { REQUIRE(r.sample(0, c) == oracle.voice(c).pitch); REQUIRE(r.sample(1, c) == (oracle.voice(c).gate ? 1.f : 0.f)); }
    }
    MidiRig<MidiToCv> legato; legato.values[1] = 3; legato.values[4] = 1;
    legato.run({midi1(0x90, 60, 127), midi1(0x90, 64, 127, 8), midi1(0x80, 64, 0, 16)});
    REQUIRE(legato.sample(0, 0, 0) == 0 && legato.sample(0, 0, 8) == 1.f / 3 && legato.sample(0, 0, 16) == 0);
    for (uint32_t i = 0; i < 32; ++i) REQUIRE(legato.sample(1, 0, i) == 1);
    for (double rate : {44100., 48000., 96000.}) for (uint32_t voices : {1u, 4u, 16u}) for (uint32_t frames : {1u, 17u, 128u, 256u}) {
        MidiRig<MidiToCv> r(nullptr, 0, rate); r.values[0] = float(voices); r.run({midi1(0x90, 60, 127, 0), source_choke(UINT32_MAX, frames - 1)}, frames);
        REQUIRE(r.outputs[0].channels == voices); for (uint32_t c = 0; c < voices; ++c) REQUIRE(r.sample(1, c, frames - 1) == 0);
    }
    MidiRig<MidiToCv> r; r.run({midi1(0x90, 60, 127), midi1(0xb0, 64, 127, 1)});
    Bytes bytes; ws2_writer writer{sizeof(writer), &bytes, sink}; REQUIRE(r.d->save_state(r.instance, &writer) == WS2_OK);
    Source source_context{&bytes, 0}; ws2_reader reader{sizeof(reader), &source_context, source, uint32_t(bytes.size())}; REQUIRE(r.d->load_state(r.instance, &reader) == WS2_OK); r.run(); for (uint32_t c = 0; c < 4; ++c) REQUIRE(r.sample(1, c) == 0);
    bytes[0] = 99; source_context.pos = 0; REQUIRE(r.d->load_state(r.instance, &reader) == WS2_UNSUPPORTED);
    bytes.clear(); source_context.pos = 0; reader.bytes = 0; REQUIRE(r.d->load_state(r.instance, &reader) == WS2_INVALID);
}
static void graph_fixtures() {
    using namespace engine2;
    for (uint32_t voices : {1u, 4u, 16u}) {
        Config config; config.max_block = 128; Engine e(config); const int producer = e.register_producer();
        REQUIRE(e.add_node(1, ws::Module<MidiIn>::descriptor()) && e.add_node(2, ws::Module<MidiToCv>::descriptor()));
        REQUIRE(e.add_node(3, ws::Module<ws2ports::Vco>::descriptor()) && e.add_node(4, ws::Module<ws2ports::Adsr>::descriptor()) && e.add_node(5, ws::Module<ws2ports::Vca>::descriptor()) && e.add_node(6, ws::Module<ws2ports::Output>::descriptor()));
        REQUIRE(e.connect(1, 0, 2, 0) && e.connect(2, 0, 3, 0) && e.connect(2, 1, 4, 0) && e.connect(3, 0, 5, 0) && e.connect(4, 0, 5, 1) && e.connect(5, 0, 6, 0) && e.output(6, 0, 0));
        REQUIRE(e.set_parameter(2, 0, float(voices)) && e.prepare());
        for (uint8_t key : {60, 64, 67, 72}) REQUIRE(e.enqueue(uint32_t(producer), 1, WS2_DIRECT_PORT, midi1(0x90, key, 127), 0) == WS2_OK);
        float data[128]{}; float* out[]{data}; { allocation_watch::Scope scope; REQUIRE(e.render(out, 1, 128) == WS2_OK); REQUIRE(scope.allocations() == 0); }
        REQUIRE(e.output_channels(2, 0) == voices && e.output_channels(6, 0) == 1);
        REQUIRE(e.enqueue(uint32_t(producer), 1, WS2_DIRECT_PORT, source_choke(), e.meter().frame) == WS2_OK); REQUIRE(e.render(out, 1, 128) == WS2_OK);
        for (uint32_t c = 0; c < voices; ++c) REQUIRE(e.output_sample(2, 1, c, 127) == 0);
    }
}

static void partitioned_graph_fixtures() {
    using namespace engine2;
    using Observed = std::tuple<uint32_t, uint32_t, uint64_t, uint32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t>;
    std::array<std::vector<Observed>, 3> reference;
    for (uint32_t fixture = 0; fixture < 3; ++fixture)
    for (double rate : {44100., 48000., 96000.}) for (uint32_t block : {1u, 17u, 128u}) {
        Config config; config.sample_rate = rate; config.max_block = 128; config.playing = false;
        Engine e(config); const auto producer = uint32_t(e.register_producer());
        REQUIRE(e.add_node(1, ws::Module<MidiIn>::descriptor()) && e.add_node(2, ws::Module<MidiToCv>::descriptor()));
        REQUIRE(e.add_node(3, ws::Module<ws2ports::Vco>::descriptor()) && e.add_node(4, ws::Module<ws2ports::Adsr>::descriptor()));
        REQUIRE(e.add_node(5, ws::Module<ws2ports::Vca>::descriptor()) && e.add_node(6, ws::Module<ws2ports::Output>::descriptor()));
        REQUIRE(e.connect(1, 0, 2, 0) && e.connect(2, 0, 3, 0) && e.connect(2, 1, 4, 0));
        REQUIRE(e.connect(3, 0, 5, 0) && e.connect(4, 0, 5, 1) && e.connect(5, 0, 6, 0) && e.output(6, 0, 0));
        const uint32_t voices = fixture == 1 ? 2 : 1;
        REQUIRE(e.set_parameter(2, 0, float(voices)) && e.prepare());
        auto send = [&](ws2_event event, uint64_t frame) { REQUIRE(e.enqueue(producer, 1, WS2_DIRECT_PORT, event, frame) == WS2_OK); };
        // Synthetic endpoints A/B provide immutable source IDs; backend acquisition
        // and physical hot-plug are independently exercised by fake Service tests.
        if (fixture == 1) {
            send(midi1(0xb0, 64, 127), 0);
            for (uint8_t key : {60, 64, 67}) send(midi1(0x90, key, 127), 1);
            send(midi1(0x90, 72, 127, 0, 2), 17); send(midi1(0x80, 67, 0), 20);
            send(source_choke(1), 35); send(midi1(0x80, 72, 0, 0, 2), 77);
        } else {
            send(midi1(0x90, 60, 127), 7);
            if (fixture == 2) {
                uint32_t rejected = 0;
                for (uint32_t n = 0; n < 400; ++n) {
                    const int status = e.enqueue(producer, 1, WS2_DIRECT_PORT, midi1(0xb0, 1, uint8_t(n % 128)), 0);
                    REQUIRE(status == WS2_OK || status == WS2_CAPACITY); rejected += status == WS2_CAPACITY;
                }
                REQUIRE(rejected > 0);
                // A saturated ordinary ingress must still reserve these releases.
                for (int n = 0; n < 4; ++n) send(midi1(0x80, 60, 0), 127);
            } else send(midi1(0x80, 60, 0), 77);
        }
        std::vector<Observed> observed; uint32_t attacks = 0, releases = 0, chokes = 0;
        float peak = 0; float data[128]{}; float* output[]{data};
        for (uint32_t first = 0; first < 160; first += block) {
            const auto frames = std::min(block, 160u - first);
            { allocation_watch::Scope scope; REQUIRE(e.render(output, 1, frames) == WS2_OK); REQUIRE(scope.allocations() == 0); }
            for (uint32_t i = 0; i < frames; ++i) {
                REQUIRE(std::isfinite(data[i])); peak = std::max(peak, std::abs(data[i]));
                const auto frame = first + i;
                if (fixture != 1) {
                    REQUIRE(e.output_sample(2, 0, 0, i) == 0);
                    REQUIRE(e.output_sample(2, 1, 0, i) == float(frame >= 7 && frame < (fixture == 2 ? 127u : 77u)));
                } else if (frame >= 35) {
                    uint32_t active = 0; bool b_alive = false;
                    for (uint32_t c = 0; c < voices; ++c) if (e.output_sample(2, 1, c, i) == 1) {
                        ++active; b_alive |= e.output_sample(2, 0, c, i) == 1;
                    }
                    REQUIRE(active == (frame < 77 ? 1u : 0u)); if (frame < 77) REQUIRE(b_alive);
                }
            }
            Outgoing item;
            while (e.pop_egress(item)) {
                const auto& event = item.event;
                // Compare accepted source timestamps even under bounded output loss;
                // normal fixture notes additionally require exact CHOKE/OFF ordering.
                if (fixture != 2 || item.node == 1)
                    observed.emplace_back(item.node, item.port, item.frame, event.type, event.channel,
                                          event.key, event.note_id, event.source_id, event.ump[0]);
                if (fixture != 2 && item.node == 2 && item.port == 7) {
                    REQUIRE(event.channel >= 0 && uint32_t(event.channel) < voices);
                    if (event.type == WS2_NOTE_ON) ++attacks;
                    else { REQUIRE(event.type == WS2_NOTE_CHOKE || event.type == WS2_NOTE_OFF);
                        ++releases; chokes += event.type == WS2_NOTE_CHOKE; }
                }
                if (fixture == 0 && item.node == 1) REQUIRE(item.frame == (((event.ump[0] >> 20) & 15) == 9 ? 7u : 77u));
            }
        }
        REQUIRE(peak > 0 && e.meter().faults == 0);
        for (uint32_t c = 0; c < voices; ++c) REQUIRE(e.output_sample(2, 1, c, (160u - 1) % block) == 0);
        if (fixture != 2) {
            REQUIRE(attacks == releases && attacks == (fixture == 1 ? 4u : 1u));
            for (const auto& attack : observed) if (std::get<0>(attack) == 2 && std::get<3>(attack) == WS2_NOTE_ON) {
                REQUIRE(std::count_if(observed.begin(), observed.end(), [&](const auto& release) {
                    return std::get<0>(release) == 2 && (std::get<3>(release) == WS2_NOTE_OFF || std::get<3>(release) == WS2_NOTE_CHOKE)
                        && std::get<4>(release) == std::get<4>(attack) && std::get<5>(release) == std::get<5>(attack)
                        && std::get<6>(release) == std::get<6>(attack) && std::get<7>(release) == std::get<7>(attack);
                }) == 1);
            }
            if (fixture == 1) REQUIRE(chokes >= 3);
        }
        // Egress interleaves per-node queues; compare logical absolute timestamps,
        // not dispatcher drain order. basic_and_steal asserts local release-before-attack.
        std::sort(observed.begin(), observed.end(), [](const auto& a, const auto& b) {
            return std::tie(std::get<2>(a), a) < std::tie(std::get<2>(b), b);
        });
        if (reference[fixture].empty()) reference[fixture] = observed;
        else REQUIRE(observed == reference[fixture]);
    }
}

static void full_endpoint_channel_matrix() {
    MidiRig<MidiToCv> r;
    // All 16 enabled endpoints may send on all 16 channels without disconnecting.
    for (uint32_t source = 1; source <= 16; ++source)
        for (uint32_t channel = 0; channel < 16; ++channel) {
            r.run({midi1(uint8_t(0x90 | channel), 60, 127, 0, source)});
            REQUIRE(r.emitted.back().type == WS2_NOTE_ON && r.emitted.back().source_id == source);
            r.run({midi1(uint8_t(0x80 | channel), 60, 0, 0, source)});
            for (uint32_t voice = 0; voice < 4; ++voice) REQUIRE(r.sample(1, voice) == 0);
        }
    // Retirement frees only that source's state; another source keeps its sustain.
    r.run({midi1(0x90, 64, 127, 0, 16), midi1(0xb0, 64, 127, 1, 16), midi1(0x80, 64, 0, 2, 16)});
    for (uint32_t source = 1; source < 16; ++source) r.run({source_choke(source)});
    for (uint32_t source = 17; source < 32; ++source)
        for (uint32_t channel = 0; channel < 16; ++channel) {
            r.run(
                {midi1(uint8_t(0x90 | channel), 60, 127, 0, source), midi1(uint8_t(0x80 | channel), 60, 0, 1, source)});
            REQUIRE(r.emitted.back().type == WS2_NOTE_OFF && r.emitted.back().source_id == source);
        }
    uint32_t held = 0;
    for (uint32_t voice = 0; voice < 4; ++voice) held += r.sample(1, voice) > 0;
    REQUIRE(held == 1);
    r.run({midi1(0xb0, 64, 0, 0, 16)});
    for (uint32_t voice = 0; voice < 4; ++voice) REQUIRE(r.sample(1, voice) == 0);
}

static void last_note_mono_hold_and_restore() {
    MidiRig<MidiToCv> r;
    REQUIRE(r.d->output_count == 10);
    const char *ids[] = {"pitch", "gate", "velocity", "aftertouch", "pressure", "bend", "mod", "notes", "last_note", "trig"};
    for (uint32_t i = 0; i < 10; ++i) REQUIRE(std::strcmp(r.d->outputs[i].id, ids[i]) == 0);
    r.values[0] = 4;
    r.run();
    REQUIRE(r.outputs[8].channels == 1 && r.sample(8) == 0);
    r.run({midi1(0x90, 72, 127, 2), midi1(0x91, 67, 110, 8)});
    REQUIRE(r.sample(8, 0, 1) == 0);
    REQUIRE(r.sample(8, 0, 3) == 1.f);
    REQUIRE(r.sample(8, 0, 9) == float(67 - 60) / 12.f);
    REQUIRE(r.outputs[0].channels == 4); // mono history independent of poly outputs
    r.run({midi1(0xe1, 0, 0), midi1(0x81, 67, 0), source_choke(1)});
    REQUIRE(r.sample(8) == float(67 - 60) / 12.f); // note off, bend and source choke hold
    r.values[8] = 1;                               // channel 1 only
    r.run({midi1(0x91, 90, 127)});                 // channel 2 is rejected
    REQUIRE(r.sample(8) == float(67 - 60) / 12.f);
    r.run({midi1(0x90, 48, 127)});
    REQUIRE(r.sample(8) == -1.f);
    r.run({midi1(0x90, 48, 0), source_choke(UINT32_MAX)});
    REQUIRE(r.sample(8) == -1.f);
    Bytes bytes;
    ws2_writer writer{sizeof(writer), &bytes, sink};
    REQUIRE(r.d->save_state(r.instance, &writer) == WS2_OK);
    Source ctx{&bytes, 0};
    ws2_reader reader{sizeof(reader), &ctx, source, uint32_t(bytes.size())};
    REQUIRE(r.d->load_state(r.instance, &reader) == WS2_OK);
    r.run();
    REQUIRE(r.sample(8) == 0); // restart/restore never persists transient last note
}
static void last_note_acceptance_modes_and_limits() {
    for (uint32_t mode = 0; mode < 4; ++mode)
        for (uint32_t priority = 0; priority < 3; ++priority) {
            MidiRig<MidiToCv> r;
            r.values[1] = float(mode);
            r.values[3] = float(priority);
            r.run({midi1(0x90, 60, 127, 1), midi2(0x90, 72, 0xffff0000u, 3), midi1(0x90, 48, 127, 5)});
            REQUIRE(r.sample(8, 0, 2) == 0 && r.sample(8, 0, 4) == 1 && r.sample(8, 0, 6) == -1);
            if (mode == 3) REQUIRE(r.sample(0) == (priority == 2 ? 1.f : -1.f));
            r.values[8] = 1;
            r.run({midi1(0x91, 96, 127), midi1(0x90, 96, 0)});
            REQUIRE(r.sample(8) == -1); // filtered channel and MIDI1 velocity-zero off are not accepted attacks
            r.run({midi2(0x90, 84, 0)});
            REQUIRE(r.sample(8) == 2); // MIDI2 zero velocity remains a valid NoteOn
            r.run({source_choke()});
            REQUIRE(r.sample(8) == 2);
        }
    // An accepted held note need not sound, and output-queue refusal is not input refusal.
    MidiRig<MidiToCv> blocked;
    blocked.values[1] = 3;
    blocked.values[3] = 1;
    blocked.event_limit = 0;
    blocked.run({midi1(0x90, 60, 127), midi1(0x90, 72, 127, 1)});
    REQUIRE(blocked.emitted.empty() && blocked.sample(0) == 0 && blocked.sample(8) == 1);
    blocked.event_limit = 20000;
    blocked.run({source_choke()});
    // Mono retains all128 held notes. A rejected duplicate of the sounding key must not replace last_note.
    MidiRig<MidiToCv> mono;
    mono.values[1] = 3;
    mono.values[3] = 1;
    for (uint32_t i = 0; i < 127; ++i) mono.run({midi1(0x90, 60, 127)});
    mono.run({midi1(0x90, 72, 127)});
    REQUIRE(mono.sample(0) == 0 && mono.sample(8) == 1);
    mono.run({midi1(0x90, 60, 127)});
    REQUIRE(mono.sample(8) == 1);
    mono.run({midi1(0x80, 60, 0), midi1(0x90, 84, 127, 1)});
    REQUIRE(mono.sample(8) == 2);
    mono.run({source_choke()});
    // Rotate also bounds anonymous activations even though stolen voices leave only4 held records.
    MidiRig<MidiToCv> rotate;
    for (uint32_t i = 0; i < 124; ++i) rotate.run({midi1(0x90, 20, 127)});
    for (uint8_t key : {60, 64, 67, 72}) rotate.run({midi1(0x90, key, 127)});
    REQUIRE(rotate.sample(8) == 1);
    rotate.run({midi1(0x90, 60, 127)});
    REQUIRE(rotate.sample(8) == 1); // matching an active voice does not make a rejected attack accepted
    rotate.run({midi1(0x80, 20, 0), midi1(0x90, 84, 127, 1)});
    REQUIRE(rotate.sample(8) == 2);
    rotate.run({source_choke()});
}

static void trigger_pulse_exact_head_device_off() {
    for (double rate : {44100., 48000., 96000.}) {
        const uint32_t width = uint32_t(std::lround(rate / 1000.0));
        MidiRig<MidiToCv> poly(nullptr, 0, rate);
        poly.values[0] = 3;
        poly.run({midi1(0x90, 60, 127, 3), midi1(0x90, 64, 127, 19),
                  midi1(0x90, 67, 127, 34)}, width + 40);
        REQUIRE(poly.outputs[9].channels == 3);
        const uint32_t offsets[]{3, 19, 34};
        for (uint32_t i = 0; i < width + 40; ++i) {
            for (uint32_t c = 0; c < 3; ++c) {
                REQUIRE(poly.sample(9, c, i) == float(i >= offsets[c] && i < offsets[c] + width));
                REQUIRE(poly.sample(1, c, i) == float(i >= offsets[c]));
            }
        }
        REQUIRE(poly.sample(0, 0, width + 39) == 0);
        REQUIRE(poly.sample(0, 1, width + 39) == 4.f / 12.f);
        REQUIRE(poly.sample(0, 2, width + 39) == 7.f / 12.f);

        MidiRig<MidiToCv> tail(nullptr, 0, rate);
        tail.run({midi1(0x90, 60, 127, 255), midi1(0x80, 60, 0, 255)}, 256);
        REQUIRE(tail.sample(9, 0, 254) == 0 && tail.sample(9, 0, 255) == 1);
        tail.run({}, width + 2);
        for (uint32_t i = 0; i < width + 2; ++i)
            REQUIRE(tail.sample(9, 0, i) == float(i < width - 1));
        REQUIRE(tail.sample(1, 0, 0) == 0);
        tail.d->reset(tail.instance);
        tail.run();
        for (uint32_t c = 0; c < 4; ++c) REQUIRE(tail.sample(9, c, 0) == 0);

        MidiRig<MidiToCv> mono(nullptr, 0, rate);
        mono.values[1] = 3;
        mono.values[4] = 1; // legato: the gate must never drop for the second Note On
        const uint32_t legato_at = width + 8;
        mono.run({midi1(0x90, 60, 127, 0), midi1(0x90, 64, 127, legato_at)},
                 legato_at + width + 2);
        REQUIRE(mono.outputs[9].channels == 1);
        for (uint32_t i = 0; i < legato_at + width + 2; ++i) {
            REQUIRE(mono.sample(9, 0, i) ==
                    float(i < width || (i >= legato_at && i < legato_at + width)));
            REQUIRE(mono.sample(1, 0, i) == 1);
            REQUIRE(mono.sample(0, 0, i) == (i < legato_at ? 0 : 4.f / 12.f));
        }

        // Resizing with live pulses retains only lanes that still fit.
        MidiRig<MidiToCv> resized(nullptr, 0, rate);
        resized.values[0] = 4;
        resized.run({midi1(0x90, 60, 127, 31), midi1(0x90, 64, 127, 31),
                     midi1(0x90, 67, 127, 31), midi1(0x90, 71, 127, 31)}, 32);
        resized.values[0] = 2;
        resized.run({}, 8);
        REQUIRE(resized.outputs[9].channels == 2);
        for (uint32_t c = 0; c < 2; ++c)
            for (uint32_t i = 0; i < 8; ++i)
                REQUIRE(resized.sample(9, c, i) == 1 && resized.sample(1, c, i) == 1);
        resized.values[0] = 4;
        resized.run({}, width);
        REQUIRE(resized.outputs[9].channels == 4);
        for (uint32_t i = 0; i < width; ++i) {
            for (uint32_t c = 0; c < 2; ++c)
                REQUIRE(resized.sample(9, c, i) == float(i < width - 9) && resized.sample(1, c, i) == 1);
            for (uint32_t c = 2; c < 4; ++c)
                REQUIRE(resized.sample(9, c, i) == 0 && resized.sample(1, c, i) == 0);
        }

        // Mono low-priority can hold a Note On without allocating it a lane.
        MidiRig<MidiToCv> held(nullptr, 0, rate);
        held.values[1] = 3;
        held.values[3] = 1; // lowest note
        held.run({midi1(0x90, 60, 127, 0), midi1(0x90, 72, 127, width + 9)},
                 width + 55);
        for (uint32_t i = width + 9; i < width + 55; ++i)
            REQUIRE(held.sample(9, 0, i) == 0);
        REQUIRE(held.sample(1, 0, width + 54) == 1);

        // MIDI 1 zero velocity is Note Off; MIDI 2 zero velocity remains Note On.
        MidiRig<MidiToCv> zero(nullptr, 0, rate);
        zero.run({midi1(0x90, 60, 0, 3), midi2(0x90, 64, 0, 10)}, width + 15);
        for (uint32_t i = 0; i < width + 15; ++i) {
            REQUIRE(zero.sample(9, 0, i) == float(i >= 10 && i < 10 + width));
            REQUIRE(zero.sample(9, 1, i) == 0);
        }
    }
}

static void metadata_contract() {
    REQUIRE(MidiToCv::params.size() == 9);
    REQUIRE(MidiToCv::outputs.size() == 10);
    REQUIRE(std::strcmp(MidiToCv::outputs[8].id, "last_note") == 0);
    REQUIRE(std::strcmp(MidiToCv::outputs[9].id, "trig") == 0);
    for (const auto& parameter : MidiToCv::params) REQUIRE(std::strcmp(parameter.id, "retrigger") != 0);
    REQUIRE(MidiToCv::params[5].legacy_index == 6);
    REQUIRE(MidiToCv::params[8].legacy_index == 9);
}
// This is a synthetic-MIDI fixture, not the oscillator-only poly8 soak. It
// exercises normal MIDI/event and CC/signal cables through the real wrappers.
static void coordinator_poly_soak(uint32_t seconds, bool paced) {
    using namespace engine2;
    constexpr uint32_t block = 128, rate = 48000, voices = 8;
    Config config;
    config.max_block = block;
    config.sample_rate = rate;
    Engine e(config);
    const auto producer = uint32_t(e.register_producer());
    REQUIRE(e.add_node(1, ws::Module<MidiIn>::descriptor()));
    REQUIRE(e.add_node(2, ws::Module<MidiToCv>::descriptor()));
    REQUIRE(e.add_node(3, ws::Module<ws2ports::Vco>::descriptor()));
    REQUIRE(e.add_node(4, ws::Module<ws2ports::Adsr>::descriptor()));
    REQUIRE(e.add_node(5, ws::Module<ws2ports::Vca>::descriptor()));
    REQUIRE(e.add_node(6, ws::Module<ws2ports::Output>::descriptor()));
    CcConfiguration cc;
    cc.rows[0].smoothing_ms = 0;
    std::vector<uint8_t> cc_bytes(sizeof(cc));
    std::memcpy(cc_bytes.data(), &cc, sizeof(cc));
    REQUIRE(e.add_node(7, ws::Module<CcToCv>::descriptor(), cc_bytes));
    CcRows mapping{};
    char error[128]{};
    REQUIRE(materialize_cc_rows(cc, mapping, error));
    const auto *slot = reinterpret_cast<const uint8_t *>(&mapping);
    REQUIRE(e.set_data_packed(7, 0, std::vector<uint8_t>(slot, slot + sizeof(mapping))) == WS2_OK);
    REQUIRE(e.add_node(8, ws::Module<ws2ports::Vca>::descriptor()));
    const uint32_t edges[][4] = {{1, 0, 2, 0}, {1, 0, 7, 0}, {2, 0, 3, 0}, {2, 1, 4, 0}, {3, 0, 5, 0},
                                 {4, 0, 5, 1}, {5, 0, 8, 0}, {7, 0, 8, 1}, {8, 0, 6, 0}};
    for (const auto &edge : edges) REQUIRE(e.connect(edge[0], edge[1], edge[2], edge[3]));
    REQUIRE(e.output(6, 0, 0) && e.set_parameter(2, 0, voices) && e.prepare());
    float data[block]{};
    float *output[]{data};
    uint64_t attacks = 0, releases = 0, chords = 0, allocations = 0;
    double peak = 0;
    uint8_t cc_value = 0;
    const uint32_t blocks = seconds * rate / block;
    const auto start = std::chrono::steady_clock::now();
    auto send = [&](const ws2_event &event) {
        REQUIRE(e.enqueue(producer, 1, WS2_DIRECT_PORT, event, e.meter().frame) == WS2_OK);
    };
    for (uint32_t b = 0; b < blocks; ++b) {
        const auto phase = b % 64;
        {
            allocation_watch::Scope watch;
            if (phase == 0) {
                ++chords;
                cc_value = uint8_t(chords % 2 ? 127 : 32);
                for (uint32_t c = 0; c < voices; ++c) send(midi1(0x90, uint8_t(48 + c), 100, 0, c % 2 + 1));
                send(midi1(0xb0, 1, cc_value));
            } else if (phase == 32) send(source_choke(1));
            else if (phase == 48)
                for (uint32_t c = 1; c < voices; c += 2) send(midi1(0x80, uint8_t(48 + c), 0, 0, 2));
            // The final block must leave no held note, including a partial cycle.
            if (b + 1 == blocks) send(source_choke(UINT32_MAX));
            REQUIRE(e.render(output, 1, block) == WS2_OK);
            allocations += watch.allocations();
        }
        REQUIRE(e.output_channels(2, 0) == voices && e.output_channels(5, 0) == voices);
        uint32_t held = 0;
        for (uint32_t c = 0; c < voices; ++c) held += e.output_sample(2, 1, c, block - 1) == 1;
        REQUIRE(held == (b + 1 == blocks ? 0u : phase < 32 ? 8u : phase < 48 ? 4u : 0u));
        REQUIRE(e.output_sample(7, 0, 0, block - 1) == float(cc_value) / 127.f);
        for (float sample : data) {
            REQUIRE(std::isfinite(sample));
            peak = std::max(peak, double(std::abs(sample)));
        }
        Outgoing item;
        while (e.pop_egress(item))
            if (item.node == 2 && item.port == 7) {
                attacks += item.event.type == WS2_NOTE_ON;
                releases += item.event.type == WS2_NOTE_OFF || item.event.type == WS2_NOTE_CHOKE;
            }
        if (paced)
            std::this_thread::sleep_until(start +
                                          std::chrono::nanoseconds(uint64_t(b + 1) * block * 1000000000ULL / rate));
    }
    const auto meter = e.meter();
    REQUIRE(attacks == chords * voices && releases == attacks && peak > 0);
    REQUIRE(allocations == 0 && meter.faults == 0 && meter.dropped == 0 && meter.ring_full == 0);
    REQUIRE(meter.frame == uint64_t(blocks) * block);
    std::printf(
        "synthetic-midi-poly: seconds=%u frames=%llu notes=%llu releases=%llu allocations=%llu faults=%llu peak=%g\n",
        seconds, static_cast<unsigned long long>(meter.frame), static_cast<unsigned long long>(attacks),
        static_cast<unsigned long long>(releases), static_cast<unsigned long long>(allocations),
        static_cast<unsigned long long>(meter.faults), peak);
}

struct PerformanceCv {
    static constexpr ws::Info info{"test.midi_performance_cv", "Performance CV", 1};
    static constexpr std::array<ws2_port, 0> inputs{};
    static constexpr std::array outputs{ws::port("sustain", "Sustain", WS2_CV_BI),
                                        ws::port("range", "Range", WS2_CV_BI),
                                        ws::port("apply", "Apply", WS2_CV_BI)};
    static constexpr std::array<ws2_param, 0> params{};
    void prepare(const ws::Setup&) {}
    void reset() {}
    void process(ws::Block& b) {
        for (uint32_t p = 0; p < 3; ++p) {
            b.out(p).set_channels(1);
            b.out(p).mark_constant(false);
        }
        for (uint32_t i = 0; i < b.frames(); ++i) {
            b.out(0).ch(0)[i] = b.frame_position() + i >= 160 ? -1.f : 0.f;
            b.out(1).ch(0)[i] = i >= 64 ? 1.f : 0.f;
            b.out(2).ch(0)[i] = i >= 96 ? -1.f : 0.f;
        }
    }
};
static void performance_modulation() {
    const auto* d = ws::Module<MidiToCv>::descriptor();
    REQUIRE(d->param_count == 9);
    for (uint32_t p = 0; p < d->param_count; ++p) {
        const bool mod = p >= 5 && p <= 7;
        REQUIRE(d->params[p].flags == (WS2_STEPPED | WS2_AUTOMATABLE |
                                       (mod ? WS2_MODULATABLE : 0)));
    }
    for (uint32_t voices : {1u, 16u}) {
        engine2::Config cfg;
        cfg.max_block = 128;
        engine2::Engine e(cfg);
        const auto producer = uint32_t(e.register_producer());
        REQUIRE(e.add_node(1, ws::Module<MidiIn>::descriptor()));
        REQUIRE(e.add_node(2, d));
        REQUIRE(e.add_node(3, ws::Module<PerformanceCv>::descriptor()));
        REQUIRE(e.connect(1, 0, 2, 0));
        REQUIRE(e.set_parameter(2, 0, float(voices)));
        for (uint32_t p : {0u, 1u, 2u, 3u, 4u, 8u})
            REQUIRE(!e.modulate(3, 0, 2, p, 1.f));
        REQUIRE(e.modulate(3, 0, 2, 5, 1.f));
        REQUIRE(e.modulate(3, 1, 2, 6, 1.f));
        REQUIRE(e.modulate(3, 2, 2, 7, 1.f));
        REQUIRE(e.output(2, 0, 0) && e.prepare());
        auto send = [&](ws2_event event) {
            REQUIRE(e.enqueue(producer, 1, WS2_DIRECT_PORT, event, event.offset) == WS2_OK);
        };
        for (uint32_t c = 0; c < voices; ++c)
            send(midi1(0x90, uint8_t(60 + c), 127));
        send(midi1(0xe0, 127, 127, 1));
        send(midi1(0xb0, 64, 127, 2));
        for (uint32_t c = 0; c < voices; ++c)
            send(midi1(0x80, uint8_t(60 + c), 0, 3));
        float samples[128]{};
        float* rows[]{samples};
        auto render = [&] {
            allocation_watch::Scope watch;
            REQUIRE(e.render(rows, 1, 128) == WS2_OK);
            REQUIRE(watch.allocations() == 0);
        };
        render(); // Event/trigger path: full bend and held sustain.
        REQUIRE(e.output_channels(2, 0) == voices);
        auto check_pitch = [&] {
            for (uint32_t c = 0; c < voices; ++c)
                for (uint32_t i = 4; i < 128; ++i) {
                    const float expected = float(c) / 12.f +
                        (i < 64 ? 2.f / 12.f : i < 96 ? 4.f : 0.f);
                    REQUIRE(std::abs(e.output_sample(2, 0, c, i) - expected) < 1.e-6f);
                    REQUIRE(e.output_sample(2, 5, c, i) == 1.f);
                }
        };
        check_pitch();
        engine2::Outgoing event;
        while (e.pop_egress(event)) {}
        render(); // Idle path: midblock sustain CV change must not release early.
        check_pitch();
        for (uint32_t c = 0; c < voices; ++c)
            for (uint32_t i = 0; i < 128; ++i)
                REQUIRE(e.output_sample(2, 1, c, i) == 1.f);
        while (e.pop_egress(event))
            REQUIRE(event.node != 2 || event.port != 7);
        render(); // Next block samples sustain low and releases held notes at zero.
        for (uint32_t c = 0; c < voices; ++c)
            for (uint32_t i = 0; i < 128; ++i)
                REQUIRE(e.output_sample(2, 1, c, i) == 0.f);
        uint32_t releases = 0;
        while (e.pop_egress(event))
            if (event.node == 2 && event.port == 7) {
                REQUIRE(event.event.type == WS2_NOTE_OFF && event.event.offset == 0);
                ++releases;
            }
        REQUIRE(releases == voices && e.meter().faults == 0);
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--soak") == 0) {
        coordinator_poly_soak(60, true);
        return 0;
    }
    REQUIRE(argc == 1);
    performance_modulation();
    coordinator_poly_soak(1, false);
    full_endpoint_channel_matrix();
    metadata_contract();
    trigger_pulse_exact_head_device_off();
    basic_and_steal();
    live_voice_resize();
    last_note_mono_hold_and_restore();
    last_note_acceptance_modes_and_limits();
    sustain_bend_expression();
    modes_state_and_bounds();
    graph_fixtures();
    partitioned_graph_fixtures();
}
