// SPDX-License-Identifier: GPL-3.0-only
#include "../../engine/engine2.h"
#include "../../sdk/examples/test_support.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include "../../tests/native/abi2/test_modules.h"
#include "../adsr/dsp.h"
#include "../chord/dsp.h"
#include "../host_keyboard/dsp.h"
#include "../output/dsp.h"
#include "../vca/dsp.h"
#include "../vco/dsp.h"
#include "dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>

using notes_to_cv::NotesToCv;
static constexpr uint32_t kFrames = 256;

// Named fields prevent accidental reordering of similarly typed event values.
struct NoteSpec {
    uint32_t type;
    int key;
    int32_t id = 0;
    uint32_t source = 1;
    int channel = 0;
    uint32_t offset = 0;
};
static ws2_event note(const NoteSpec &spec) {
    ws2_event e{};
    e.struct_size = sizeof(e);
    e.type = spec.type;
    e.port = 0;
    e.key = spec.key;
    e.note_id = spec.id;
    e.source_id = spec.source;
    e.channel = spec.channel;
    e.offset = spec.offset;
    e.value = .75f;
    return e;
}
struct Rig {
    Pool pool{};
    ws2_host host{sizeof(host), &pool, allocate};
    const ws2_descriptor *desc = ws::Module<NotesToCv>::descriptor();
    void *state = nullptr;
    struct Buffers {
        float output[4][16][kFrames]{};
    };
    std::unique_ptr<Buffers> storage = std::make_unique<Buffers>();
    float *out_rows[4][16]{};
    ws2_signal_out outs[4]{};
    ws2_signal input[1]{};
    float voice_param = 4;
    const float *voice_rows[1]{&voice_param};
    ws2_signal params[1]{{sizeof(ws2_signal), voice_rows, 1, WS2_CONSTANT}};
    ws2_event_list incoming[1]{};
    ws2_process ctx{};

    explicit Rig(double rate = 48000, uint32_t host_channels = 16) {
        const ws2_config cfg{sizeof(cfg), rate, kFrames, host_channels, 42, nullptr, 0};
        state = desc->create(&host);
        REQUIRE(state && desc->prepare(state, &cfg, &host) == WS2_OK);
        for (int p = 0; p < 4; ++p) {
            for (uint32_t c = 0; c < 16; ++c) out_rows[p][c] = storage->output[p][c];
            outs[p] = {sizeof(ws2_signal_out), out_rows[p], 0, 0, host_channels};
        }
        input[0] = {sizeof(ws2_signal), nullptr, 0, 0};
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = rate;
        ctx.channels = 1;
        ctx.input_count = 1;
        ctx.output_count = 4;
        ctx.param_count = 1;
        ctx.inputs = input;
        ctx.outputs = outs;
        ctx.params = params;
        ctx.event_inputs = incoming;
    }
    ~Rig() { desc->destroy(state); }

    void run(std::initializer_list<ws2_event> events = {}, uint32_t frames = 128) {
        REQUIRE(frames >= 1 && frames <= kFrames && events.size() <= 32);
        std::array<ws2_event, 32> buffer{};
        std::copy(events.begin(), events.end(), buffer.begin());
        incoming[0] = {sizeof(ws2_event_list), uint32_t(events.size()), buffer.data()};
        ctx.events = incoming[0];
        ctx.frames = frames;
        REQUIRE(desc->process(state, &ctx) == WS2_OK);
        ctx.frame_position += frames;
    }
    float value(uint32_t p, uint32_t voice, uint32_t frame) const { return storage->output[p][voice][frame]; }
};
static void poly_triads_and_offs() {
    Rig a;
    a.run({note({WS2_NOTE_ON, 60, 0, 3, 2, 11}), note({WS2_NOTE_ON, 64, 1, 3, 2, 11}),
           note({WS2_NOTE_ON, 67, 2, 3, 2, 11})});
    REQUIRE(a.outs[0].channels == 4);
    REQUIRE(a.value(0, 0, 11) == 0.f);
    REQUIRE(std::abs(a.value(0, 1, 11) - 4.f / 12.f) < .00001f);
    REQUIRE(std::abs(a.value(0, 2, 11) - 7.f / 12.f) < .00001f);
    for (uint32_t c = 0; c < 3; ++c)
        REQUIRE(a.value(1, c, 10) == 0.f && a.value(1, c, 12) == 1.f && a.value(2, c, 12) == .75f);
    a.run({note({WS2_NOTE_OFF, 64, 1, 3, 2, 19})});
    REQUIRE(a.value(1, 0, 20) == 1 && a.value(1, 1, 20) == 0 && a.value(1, 2, 20) == 1);
    a.run({note({WS2_NOTE_ON, 71, 3, 3, 2, 17})});
    REQUIRE(std::abs(a.value(0, 3, 20) - 11.f / 12.f) < .00001f);
    REQUIRE(a.value(1, 3, 20) == 1.f);
}
static void source_identity_and_choke() {
    Rig a;
    a.run({note({WS2_NOTE_ON, 60, 0, 10}), note({WS2_NOTE_ON, 64, 0, 11})});
    a.run({note({WS2_NOTE_OFF, 60, 0, 10})});
    REQUIRE(a.value(1, 0, 1) == 0 && a.value(1, 1, 1) == 1);
    a.run({note({WS2_NOTE_ON, 67, 9, 10})});
    a.run({note({WS2_NOTE_CHOKE, 0, -1, 11})});
    REQUIRE(a.value(1, 1, 1) == 0 && a.value(1, 2, 1) == 1);
    a.run({note({WS2_NOTE_CHOKE, 0, -1, UINT32_MAX})});
    for (int c = 0; c < 4; ++c) REQUIRE(a.value(1, c, 1) == 0);
}
static void steal_and_reset() {
    Rig a;
    a.run({note({WS2_NOTE_ON, 60, 0}), note({WS2_NOTE_ON, 64, 1}), note({WS2_NOTE_ON, 67, 2}),
           note({WS2_NOTE_ON, 71, 3})});
    a.run({note({WS2_NOTE_ON, 72, 4, 1, 0, 9})});
    REQUIRE(a.value(1, 0, 9) == 0 && a.value(1, 0, 10) == 1); // one-sample retrigger dip
    REQUIRE(a.value(0, 0, 10) == 1.f);
    a.run({note({WS2_NOTE_OFF, 60, 0})});
    REQUIRE(a.value(1, 0, 1) == 1); // old off cannot close the stolen voice
    a.desc->reset(a.state);
    a.run();
    for (uint32_t c = 0; c < 4; ++c) REQUIRE(a.value(1, c, 0) == 0);
}
static void anonymous_fifo_and_repeated_roots() {
    Rig a;
    auto older = note({WS2_NOTE_ON, 60, -1});
    auto newer = note({WS2_NOTE_ON, 60, -1});
    older.value = .25f;
    newer.value = .75f;
    a.run({older, newer});
    REQUIRE(a.outs[0].channels == 4);
    REQUIRE(a.value(1, 0, 1) == 1.f && a.value(1, 1, 1) == 1.f);
    REQUIRE(a.value(2, 0, 1) == .25f && a.value(2, 1, 1) == .75f);

    a.run({note({WS2_NOTE_OFF, 60, -1})});
    REQUIRE(a.value(1, 0, 1) == 0.f && a.value(1, 1, 1) == 1.f);
    REQUIRE(a.value(2, 1, 1) == .75f);
    a.run({note({WS2_NOTE_OFF, 60, -1})});
    REQUIRE(a.value(1, 0, 1) == 0.f && a.value(1, 1, 1) == 0.f);
}

static void voice_count_change_and_reset() {
    Rig a;
    a.run({note({WS2_NOTE_ON, 60, 0}), note({WS2_NOTE_ON, 64, 1}),
           note({WS2_NOTE_ON, 67, 2}), note({WS2_NOTE_ON, 71, 3})});
    REQUIRE(a.outs[0].channels == 4);
    const auto check_kept = [&] {
        for (uint32_t frame = 0; frame < 128; ++frame) {
            REQUIRE(a.value(1, 0, frame) == 1.f && a.value(1, 1, frame) == 1.f);
            REQUIRE(a.value(0, 0, frame) == 0.f && a.value(0, 1, frame) == 4.f / 12.f);
            REQUIRE(a.value(2, 0, frame) == .75f && a.value(2, 1, frame) == .75f);
        }
    };
    a.voice_param = 2.f;
    {
        allocation_watch::Scope scope;
        a.run();
        REQUIRE(scope.allocations() == 0);
    }
    REQUIRE(a.outs[0].channels == 2);
    check_kept();
    a.run(); // same count is a no-op
    check_kept();

    a.voice_param = 4.f;
    a.run();
    REQUIRE(a.outs[0].channels == 4);
    check_kept();
    for (uint32_t c = 2; c < 4; ++c)
        for (uint32_t frame = 0; frame < 128; ++frame)
            REQUIRE(a.value(1, c, frame) == 0.f);
    a.run({note({WS2_NOTE_OFF, 67, 2}), note({WS2_NOTE_OFF, 71, 3})});
    check_kept(); // retired notes must not close the retained voices
    a.run({note({WS2_NOTE_OFF, 60, 0})});
    REQUIRE(a.value(1, 0, 0) == 0.f && a.value(1, 1, 0) == 1.f);
    a.desc->reset(a.state);
    a.run();
    for (uint32_t c = 0; c < 4; ++c) REQUIRE(a.value(1, c, 0) == 0.f);
}
static void voice_count_bounds_and_host_cap() {
    struct Case {
        float requested;
        uint32_t host_channels;
        uint32_t expected;
    };
    const Case cases[] = {{1.f, 16, 1}, {16.f, 16, 16}, {0.f, 16, 1}, {17.f, 16, 16}, {2.6f, 16, 3}, {16.f, 3, 3}};
    for (const Case test : cases) {
        Rig a(48000, test.host_channels);
        a.voice_param = test.requested;
        a.run({note({WS2_NOTE_ON, 60, 9})});
        for (const auto &output : a.outs)
            REQUIRE(output.channels == test.expected && output.channels <= test.host_channels);
        REQUIRE(a.value(1, 0, 1) == 1.f && a.value(2, 0, 1) == .75f);
        for (uint32_t voice = 1; voice < test.expected; ++voice) REQUIRE(a.value(1, voice, 1) == 0.f);
        a.desc->reset(a.state);
        a.run();
        for (const auto &output : a.outs) REQUIRE(output.channels == test.expected);
        for (uint32_t voice = 0; voice < test.expected; ++voice) REQUIRE(a.value(1, voice, 1) == 0.f);
    }
}
static void block_matrix_and_allocations() {
    for (double rate : {44100., 48000., 96000.})
        for (uint32_t n : {1u, 17u, 128u, 256u}) {
            Rig a(rate);
            size_t allocations = 0;
            {
                allocation_watch::Scope scope;
                a.run({note({WS2_NOTE_ON, 60})}, n);
                a.run({note({WS2_NOTE_OFF, 60})}, n);
                allocations = scope.allocations();
            }
            REQUIRE(allocations == 0 && a.outs[0].channels == 4);
        }
}
static void native_keyboard_graph(const std::array<int, 4> &keys, uint32_t count, bool generated = false) {
    using namespace wirescape::engine2;
    REQUIRE(count == 3 || count == 4);
    Config cfg;
    cfg.sample_rate = 48000;
    cfg.max_block = 256;
    Engine e(cfg);
    const auto producer = e.register_producer();
    REQUIRE(e.add_node(1, ws::Module<ws2ports::HostKeyboard>::descriptor()));
    if (generated) {
        // Exercise the shipped Chord and Notes-to-CV descriptors together,
        // not the test-only NotesToCv adapter used by Chord's own unit tests.
        REQUIRE(e.add_node(7, ws::Module<chord::Chord>::descriptor(), {7, 0, 0, 0}));
        if (count == 4) {
            REQUIRE(e.set_parameter(7, 4, 1.f));  // Enable a seventh interval.
            REQUIRE(e.set_parameter(7, 5, 11.f)); // C4 -> B4, not the default octave.
        }
    }
    REQUIRE(e.add_node(2, ws::Module<NotesToCv>::descriptor()));
    REQUIRE(e.add_node(3, ws::Module<ws2ports::Vco>::descriptor()));
    REQUIRE(e.add_node(4, ws::Module<ws2ports::Adsr>::descriptor()));
    REQUIRE(e.add_node(5, ws::Module<ws2ports::Vca>::descriptor()));
    REQUIRE(e.add_node(6, ws::Module<ws2ports::Output>::descriptor()));
    if (generated) {
        REQUIRE(e.connect(1, 3, 7, 0) && e.connect(7, 0, 2, 0));
    } else {
        REQUIRE(e.connect(1, 3, 2, 0));
    }
    REQUIRE(e.connect(2, 0, 3, 0) && e.connect(2, 1, 4, 0) && e.connect(3, 0, 5, 0) && e.connect(4, 0, 5, 1) &&
            e.connect(5, 0, 6, 0) && e.output(6, 0, 0) && e.prepare());
    for (uint32_t i = 0; i < (generated ? 1u : count); ++i)
        REQUIRE(e.enqueue(uint32_t(producer), 1, WS2_DIRECT_PORT, note({WS2_NOTE_ON, keys[i], keys[i]}), 0) == WS2_OK);

    float samples[256]{};
    float *out[]{samples};
    {
        allocation_watch::Scope scope;
        REQUIRE(e.render(out, 1, 256) == WS2_OK);
        REQUIRE(scope.allocations() == 0);
    }
    REQUIRE(e.output_channels(2, 0) == 4 && e.output_channels(5, 0) == 4);

    std::array<uint32_t, 4> voice_for{};
    std::array<bool, 16> assigned{};
    for (uint32_t i = 0; i < count; ++i) {
        const float expected_pitch = static_cast<float>(keys[i] - 60) / 12.f;
        bool found = false;
        for (uint32_t voice = 0; voice < 4; ++voice) {
            if (assigned[voice] || e.output_sample(2, 1, voice, 128) != 1.f ||
                e.output_sample(2, 2, voice, 128) != .75f ||
                std::abs(e.output_sample(2, 0, voice, 128) - expected_pitch) > .00001f)
                continue;
            assigned[voice] = true;
            voice_for[i] = voice;
            found = true;

            // Inspect each VCA channel before the Output node sums voices to its bus.
            double energy = 0;
            for (uint32_t frame = 0; frame < 256; ++frame) {
                const float sample = e.output_sample(5, 0, voice, frame);
                REQUIRE(std::isfinite(sample));
                energy += static_cast<double>(sample) * sample;
            }
            REQUIRE(std::sqrt(energy / 256.0) > .0001);
            break;
        }
        REQUIRE(found);
    }

    float peak = 0;
    for (const float sample : samples) {
        REQUIRE(std::isfinite(sample));
        peak = std::max(peak, std::abs(sample));
    }
    REQUIRE(peak > .00001f); // retain the real summed production audio assertion

    if (generated) {
        // A single root release must independently close all generated voices.
        const uint64_t frame = e.meter().frame;
        REQUIRE(e.enqueue(uint32_t(producer), 1, WS2_DIRECT_PORT, note({WS2_NOTE_OFF, keys[0], keys[0]}), frame) ==
                WS2_OK);
        REQUIRE(e.render(out, 1, 256) == WS2_OK);
        for (uint32_t i = 0; i < count; ++i) REQUIRE(e.output_sample(2, 1, voice_for[i], 128) == 0.f);
    } else {
        // Direct four-note MIDI-like baseline: each off leaves other gates open.
        for (uint32_t released = 0; released < count; ++released) {
            const uint64_t frame = e.meter().frame;
            REQUIRE(e.enqueue(uint32_t(producer), 1, WS2_DIRECT_PORT,
                              note({WS2_NOTE_OFF, keys[released], keys[released]}), frame) == WS2_OK);
            REQUIRE(e.render(out, 1, 256) == WS2_OK);
            for (uint32_t i = 0; i < count; ++i)
                REQUIRE(e.output_sample(2, 1, voice_for[i], 128) == (i <= released ? 0.f : 1.f));
        }
    }
    REQUIRE(e.meter().faults == 0);
}


static void trigger_pulse_voice_routing_and_reset() {
    for (double rate : {44100., 48000., 96000.}) {
        const uint32_t width = uint32_t(std::lround(rate / 1000.0));
        Rig poly(rate);
        REQUIRE(poly.desc->output_count == 4);
        REQUIRE(std::strcmp(poly.desc->outputs[3].id, "trig") == 0);
        poly.run({note({WS2_NOTE_ON, 60, 1, 7, 0, 3}),
                  note({WS2_NOTE_ON, 64, 2, 7, 0, 19})}, width + 32);
        REQUIRE(poly.outs[3].channels == 4);
        for (uint32_t i = 0; i < width + 32; ++i) {
            REQUIRE(poly.value(3, 0, i) == float(i >= 3 && i < 3 + width));
            REQUIRE(poly.value(3, 1, i) == float(i >= 19 && i < 19 + width));
            REQUIRE(poly.value(3, 2, i) == 0 && poly.value(3, 3, i) == 0);
            REQUIRE(poly.value(1, 0, i) == float(i >= 3));
            REQUIRE(poly.value(1, 1, i) == float(i >= 19));
        }

        // A voice's pulse survives a block boundary, including a short note.
        Rig tail(rate);
        tail.run({note({WS2_NOTE_ON, 60, 1, 1, 0, 127}),
                  note({WS2_NOTE_OFF, 60, 1, 1, 0, 127})}, 128);
        REQUIRE(tail.value(3, 0, 126) == 0 && tail.value(3, 0, 127) == 1);
        tail.run({}, width + 1);
        for (uint32_t i = 0; i < width + 1; ++i)
            REQUIRE(tail.value(3, 0, i) == float(i < width - 1));
        REQUIRE(tail.value(1, 0, 0) == 0);
        tail.run({note({WS2_NOTE_ON, 72, 2})});
        REQUIRE(tail.value(3, 1, 0) == 1);
        tail.desc->reset(tail.state);
        tail.run();
        for (uint32_t c = 0; c < 4; ++c) REQUIRE(tail.value(3, c, 0) == 0);

        Rig resized(rate);
        resized.run({note({WS2_NOTE_ON, 60, 1, 1, 0, 31}),
                     note({WS2_NOTE_ON, 64, 2, 1, 0, 31}),
                     note({WS2_NOTE_ON, 67, 3, 1, 0, 31}),
                     note({WS2_NOTE_ON, 71, 4, 1, 0, 31})}, 32);
        resized.voice_param = 2;
        resized.run({}, 8);
        REQUIRE(resized.outs[3].channels == 2);
        for (uint32_t c = 0; c < 2; ++c)
            for (uint32_t i = 0; i < 8; ++i)
                REQUIRE(resized.value(3, c, i) == 1 && resized.value(1, c, i) == 1);
        resized.voice_param = 4;
        resized.run({}, width);
        REQUIRE(resized.outs[3].channels == 4);
        for (uint32_t i = 0; i < width; ++i) {
            for (uint32_t c = 0; c < 2; ++c)
                REQUIRE(resized.value(3, c, i) == float(i < width - 9) && resized.value(1, c, i) == 1);
            for (uint32_t c = 2; c < 4; ++c)
                REQUIRE(resized.value(3, c, i) == 0 && resized.value(1, c, i) == 0);
        }

        // A steal/retrigger restarts only that voice's pulse.
        Rig steal(rate);
        steal.voice_param = 1;
        const uint32_t retrigger = width + 14;
        steal.run({note({WS2_NOTE_ON, 60, 1, 1, 0, 9}),
                   note({WS2_NOTE_ON, 64, 2, 1, 0, retrigger})}, retrigger + width + 2);
        for (uint32_t i = 0; i < retrigger + width + 2; ++i)
            REQUIRE(steal.value(3, 0, i) ==
                    float((i >= 9 && i < 9 + width) || (i >= retrigger && i < retrigger + width)));
        REQUIRE(steal.value(1, 0, retrigger) == 0); // existing one-sample retrigger gate dip
        REQUIRE(steal.value(1, 0, retrigger + 1) == 1);
    }
}


static void audit_voices_exclusion() {
    const auto *d = ws::Module<NotesToCv>::descriptor();
    REQUIRE(d->param_count == 1 && d->input_count == 1 && d->output_count == 4);
    REQUIRE(std::strcmp(d->inputs[0].id, "notes") == 0 && d->inputs[0].kind == WS2_EVENT);
    const char *ids[]{"pitch","gate","velocity","trig"};
    for (uint32_t p = 0; p < 4; ++p)
        REQUIRE(std::strcmp(d->outputs[p].id, ids[p]) == 0 && d->outputs[p].kind == WS2_SIGNAL);
    const auto &p = d->params[0];
    REQUIRE(std::strcmp(p.id, "voices") == 0 && p.legacy_index == 0);
    REQUIRE(p.flags == (WS2_STEPPED | WS2_AUTOMATABLE));
    REQUIRE(p.minimum == 1 && p.maximum == 16 && p.initial == 4 && p.step == 1);
    wirescape::engine2::Engine engine(wirescape::engine2::Config{});
    REQUIRE(engine.add_node(1, ws::Module<ws2test::Rows>::descriptor()));
    REQUIRE(engine.add_node(2, d));
    REQUIRE(engine.set_parameter(2, 0, 16.f));
    REQUIRE(!engine.modulate(1, 0, 2, 0, 1.f)); // structural count is not a modulation target

    Rig r;
    r.voice_param = 16;
    {
        allocation_watch::Scope watch;
        r.run({note({WS2_NOTE_ON, 60, 0, 7, 0, 3}),
           note({WS2_NOTE_ON, 61, 1, 7, 0, 3}),
           note({WS2_NOTE_ON, 62, 2, 7, 0, 3}),
           note({WS2_NOTE_ON, 63, 3, 7, 0, 3}),
           note({WS2_NOTE_ON, 64, 4, 7, 0, 3}),
           note({WS2_NOTE_ON, 65, 5, 7, 0, 3}),
           note({WS2_NOTE_ON, 66, 6, 7, 0, 3}),
           note({WS2_NOTE_ON, 67, 7, 7, 0, 3}),
           note({WS2_NOTE_ON, 68, 8, 7, 0, 3}),
           note({WS2_NOTE_ON, 69, 9, 7, 0, 3}),
           note({WS2_NOTE_ON, 70, 10, 7, 0, 3}),
           note({WS2_NOTE_ON, 71, 11, 7, 0, 3}),
           note({WS2_NOTE_ON, 72, 12, 7, 0, 3}),
           note({WS2_NOTE_ON, 73, 13, 7, 0, 3}),
           note({WS2_NOTE_ON, 74, 14, 7, 0, 3}),
           note({WS2_NOTE_ON, 75, 15, 7, 0, 3})}, 32);
        for (const auto &out : r.outs) REQUIRE(out.channels == 16);
        for (uint32_t c = 0; c < 16; ++c) {
            REQUIRE(r.value(1,c,2) == 0.f && r.value(1,c,3) == 1.f);
            REQUIRE(r.value(0,c,31) == float(c) / 12.f && r.value(3,c,31) == 1.f);
        }
        // A host value change resizes before processing events in the block.
        // Retired lane's late Note Off must not release the retained lane.
        r.voice_param = 1;
        r.run({note({WS2_NOTE_OFF, 75, 15, 7, 0, 17})}, 32);
        for (const auto &out : r.outs) REQUIRE(out.channels == 1);
        for (uint32_t i = 0; i < 32; ++i)
            REQUIRE(r.value(0,0,i) == 0.f && r.value(1,0,i) == 1.f && r.value(2,0,i) == .75f);

        // Expand on the no-event/no-pulse path; retired notes and triggers
        // must not reappear. The still-owned lane must survive.
        r.voice_param = 16;
        r.run({}, 32);
        for (const auto &out : r.outs) REQUIRE(out.channels == 16);
        REQUIRE(r.outs[3].flags & WS2_CONSTANT);
        for (uint32_t c = 0; c < 16; ++c) {
            REQUIRE(r.value(1,c,31) == (c == 0 ? 1.f : 0.f));
            REQUIRE(r.value(3,c,0) == 0.f);
        }
        REQUIRE(watch.allocations() == 0);
    }
    Bytes bytes{};
    ws2_writer writer{sizeof(writer), &bytes, sink};
    REQUIRE(r.desc->save_state(r.state, &writer) == WS2_OK && bytes.size() == 4);
    Source src{&bytes, 0};
    ws2_reader reader{sizeof(reader), &src, source, uint32_t(bytes.size())};
    {
        allocation_watch::Scope watch;
        REQUIRE(r.desc->load_state(r.state, &reader) == WS2_OK);
        r.run({}, 32);
        REQUIRE(r.outs[0].channels == 16);
        for (uint32_t c = 0; c < 16; ++c)
            REQUIRE(r.value(1,c,0) == 0.f && r.value(3,c,0) == 0.f);
        r.desc->reset(r.state);
        REQUIRE(watch.allocations() == 0);
    }
    std::puts("PASS MOD-7 Notes to CV setParam-only voices, 16-to-1-to-16 resize, state and allocation");
}

// Direct callback probe of the read site, not a supported modulation cable.
static void voice_count_frame_zero_audit() {
    Rig r;
    std::array<float, kFrames> stream{};
    stream.fill(16.f);
    stream[0] = 1.f;
    r.voice_rows[0] = stream.data();
    r.params[0].flags = 0;
    allocation_watch::Scope watch;
    r.run({note({WS2_NOTE_ON, 60, 1, 7, 0, 17})}, 64);
    for (const auto &out : r.outs) REQUIRE(out.channels == 1);
    REQUIRE(r.value(1,0,16) == 0.f && r.value(1,0,17) == 1.f);
    REQUIRE(r.value(3,0,63) == 1.f);
    stream.fill(1.f);
    stream[0] = 16.f;
    r.run({}, 64); // live trigger tail path, resize at frame zero
    for (const auto &out : r.outs) REQUIRE(out.channels == 16);
    REQUIRE(r.value(3,0,0) == 1.f && r.value(3,0,1) == 0.f);
    for (uint32_t c = 1; c < 16; ++c) REQUIRE(r.value(1,c,63) == 0.f);
    stream.fill(16.f);
    stream[0] = 1.f;
    r.run({}, 64); // idle/no-trigger fast path, same read cadence
    for (const auto &out : r.outs) REQUIRE(out.channels == 1);
    REQUIRE(r.outs[3].flags & WS2_CONSTANT);
    REQUIRE(r.value(1,0,63) == 1.f && r.value(3,0,0) == 0.f);
    REQUIRE(watch.allocations() == 0);
}

static void one_channel_host_voice_cap_audit() {
    Rig r(48000, 1);
    r.voice_param = 16;
    allocation_watch::Scope watch;
    r.run({note({WS2_NOTE_ON, 64, 1, 7, 0, 17})}, 64);
    for (const auto &out : r.outs) REQUIRE(out.channels == 1);
    REQUIRE(r.value(0,0,17) == 4.f / 12.f && r.value(1,0,17) == 1.f);
    r.voice_param = 1;
    r.run({}, 64);
    for (const auto &out : r.outs) REQUIRE(out.channels == 1);
    REQUIRE(r.value(1,0,63) == 1.f);
    REQUIRE(watch.allocations() == 0);
}

int main() {
    audit_voices_exclusion();
    voice_count_frame_zero_audit();
    one_channel_host_voice_cap_audit();
    trigger_pulse_voice_routing_and_reset();
    poly_triads_and_offs();
    source_identity_and_choke();
    anonymous_fifo_and_repeated_roots();
    voice_count_change_and_reset();
    voice_count_bounds_and_host_cap();
    steal_and_reset();
    block_matrix_and_allocations();
    native_keyboard_graph({60, 64, 67, 0}, 3);
    native_keyboard_graph({60, 64, 67, 71}, 4);
    native_keyboard_graph({60, 64, 67, 0}, 3, true);
    native_keyboard_graph({60, 64, 67, 71}, 4, true);
}
