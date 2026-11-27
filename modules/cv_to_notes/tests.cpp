// SPDX-License-Identifier: GPL-3.0-only
#include "../../sdk/examples/test_support.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include "dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string_view>

using cv_to_notes::CvToNotes;
static constexpr uint32_t kFrames = 256;

struct Recorder {
    std::array<ws2_event, 256> events{};
    uint32_t count = 0;
    bool refuse_on = false, refuse_off = false;
    void clear() { count = 0; }
};
static int WS2_CALL push(void *ctx, uint32_t port, const ws2_event *e) {
    auto &r = *static_cast<Recorder *>(ctx);
    REQUIRE(port == 0 && e->port == 0 && e->struct_size == sizeof(ws2_event));
    if ((e->type == WS2_NOTE_ON && r.refuse_on) || (e->type == WS2_NOTE_OFF && r.refuse_off))
        return WS2_CAPACITY;
    REQUIRE(r.count < r.events.size());
    r.events[r.count++] = *e;
    return WS2_OK;
}
struct Rig {
    Pool pool{};
    ws2_host host{sizeof(host), &pool, allocate};
    const ws2_descriptor *desc = ws::Module<CvToNotes>::descriptor();
    void *state = nullptr;
    struct Buffers {
        float signal[3][16][kFrames]{};
        float output[16][kFrames]{};
        Recorder recorder{};
    };
    std::unique_ptr<Buffers> mem = std::make_unique<Buffers>();
    Recorder &r = mem->recorder;
    const float *signal_rows[3][16]{};
    float *output_rows[16]{};
    ws2_signal inputs[3]{};
    ws2_signal_out outs[1]{};
    ws2_event_out event_outs[1]{};
    float legato = 0;
    const float *param_rows[1]{&legato};
    ws2_signal params[1]{{sizeof(ws2_signal), param_rows, 1, WS2_CONSTANT}};
    ws2_process ctx{};

    explicit Rig(double rate = 48000., uint32_t cap = 16, uint64_t seed = 42) {
        const ws2_config cfg{sizeof(cfg), rate, kFrames, cap, seed, nullptr, 0};
        state = desc->create(&host);
        REQUIRE(state && desc->prepare(state, &cfg, &host) == WS2_OK);
        for (uint32_t c = 0; c < 16; ++c) {
            output_rows[c] = mem->output[c];
            for (uint32_t p = 0; p < 3; ++p) signal_rows[p][c] = mem->signal[p][c];
        }
        for (uint32_t p = 0; p < 3; ++p)
            inputs[p] = {sizeof(ws2_signal), signal_rows[p], 1, WS2_CONNECTED | WS2_CONSTANT};
        inputs[2].flags = WS2_CONSTANT; // no cable: velocity is 0.8, not a zero-CV connection
        outs[0] = {sizeof(ws2_signal_out), output_rows, 0, 0, cap};
        event_outs[0] = {sizeof(ws2_event_out), &r, 0, push};
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = rate;
        ctx.channels = 1;
        ctx.input_count = 3;
        ctx.output_count = 1;
        ctx.param_count = 1;
        ctx.inputs = inputs;
        ctx.outputs = outs;
        ctx.params = params;
        ctx.event_outputs = event_outs;
    }
    ~Rig() { desc->destroy(state); }
    void level(uint32_t port, uint32_t channel, float value) {
        mem->signal[port][channel][0] = value;
        inputs[port].flags |= WS2_CONSTANT;
    }
    void curve(uint32_t port, uint32_t channel, uint32_t from, uint32_t to,
               uint32_t at, uint32_t frames) {
        REQUIRE(frames <= kFrames && at <= frames);
        for (uint32_t i = 0; i < frames; ++i)
            mem->signal[port][channel][i] = i < at ? float(from) : float(to);
        inputs[port].flags &= ~uint32_t(WS2_CONSTANT);
    }
    void run(std::initializer_list<ws2_event> events = {}, uint32_t frames = 128) {
        REQUIRE(frames >= 1 && frames <= kFrames && events.size() <= 8);
        std::array<ws2_event, 8> list{};
        std::copy(events.begin(), events.end(), list.begin());
        ctx.events = {sizeof(ws2_event_list), uint32_t(events.size()), list.data()};
        ctx.frames = frames;
        REQUIRE(desc->process(state, &ctx) == WS2_OK);
        ctx.frame_position += frames;
    }
};
static void expect_event(const ws2_event &e, uint32_t type, int key,
                         int channel, uint32_t offset, float velocity) {
    REQUIRE(e.type == type && e.key == key && e.channel == channel && e.offset == offset);
    REQUIRE(std::abs(e.value - velocity) < 1e-6f);
    REQUIRE(e.note_id == channel);
}
static void edges_and_latched_pitch() {
    Rig a;
    a.curve(1, 0, 0, 1, 5, 32);
    a.run({}, 32);
    REQUIRE(a.r.count == 1);
    expect_event(a.r.events[0], WS2_NOTE_ON, 60, 0, 5, .8f);
    const uint32_t source = a.r.events[0].source_id;

    a.r.clear();
    a.level(0, 0, 1.f); // moving pitch does not retune a held note with legato Off
    a.level(1, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 0);

    a.r.clear();
    a.curve(1, 0, 1, 0, 18, 32);
    a.run({}, 32);
    REQUIRE(a.r.count == 1);
    expect_event(a.r.events[0], WS2_NOTE_OFF, 60, 0, 18, 0);
    REQUIRE(a.r.events[0].source_id == source);
}
static void legato_on_and_off() {
    Rig a;
    a.legato = 1.f;
    a.level(1, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 1);
    expect_event(a.r.events[0], WS2_NOTE_ON, 60, 0, 0, .8f);
    a.r.clear();
    a.level(0, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 2);
    expect_event(a.r.events[0], WS2_NOTE_OFF, 60, 0, 0, 0);
    expect_event(a.r.events[1], WS2_NOTE_ON, 72, 0, 0, .8f);
    REQUIRE(a.r.events[0].source_id == a.r.events[1].source_id);
    a.r.clear();
    a.level(1, 0, 0.f);
    a.run();
    REQUIRE(a.r.count == 1);
    expect_event(a.r.events[0], WS2_NOTE_OFF, 72, 0, 0, 0);

    a.legato = 0.f;
    a.r.clear();
    a.level(1, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 1);
    a.r.clear();
    a.level(0, 0, 0.f);
    a.run();
    REQUIRE(a.r.count == 0); // legato Off ignores changing pitch while held
    a.level(1, 0, 0.f);
    a.run();
    REQUIRE(a.r.count == 1 && a.r.events[0].key == 72);
}
static void poly_channels_and_velocity() {
    Rig a;
    a.inputs[0].channels = 3;
    a.inputs[1].channels = 3;
    a.level(0, 0, 0.f);
    a.level(0, 1, 4.f / 12.f);
    a.level(0, 2, 7.f / 12.f);
    for (uint32_t c = 0; c < 3; ++c) a.level(1, c, 1.f);
    a.run();
    REQUIRE(a.r.count == 3);
    for (uint32_t c = 0; c < 3; ++c)
        expect_event(a.r.events[c], WS2_NOTE_ON, int(60 + (c == 1 ? 4 : c == 2 ? 7 : 0)),
                     int(c), 0, .8f);
    for (uint32_t c = 1; c < 3; ++c) REQUIRE(a.r.events[c].source_id == a.r.events[0].source_id);
    a.r.clear();
    a.inputs[1].channels = 1; // mono gate broadcasts; no spurious edge for other voices
    a.run();
    REQUIRE(a.r.count == 0);
    a.level(1, 0, 0.f);
    a.run();
    REQUIRE(a.r.count == 3);
    for (uint32_t c = 0; c < 3; ++c)
        REQUIRE(a.r.events[c].type == WS2_NOTE_OFF && a.r.events[c].channel == int(c));
    a.r.clear();
    a.inputs[2].flags |= WS2_CONNECTED;
    a.inputs[2].channels = 1; // one connected velocity broadcasts
    a.level(2, 0, .25f);
    a.level(1, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 3);
    for (uint32_t c = 0; c < 3; ++c) REQUIRE(a.r.events[c].value == .25f);
    a.r.clear();
    a.level(2, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 0); // velocity is latched, not continuously reattacked
}
static void clamps_and_no_invalid_keys() {
    Rig a;
    a.inputs[0].channels = 2;
    a.inputs[1].channels = 2;
    a.level(0, 0, -9999.f);
    a.level(0, 1, 9999.f);
    a.level(1, 0, 1.f);
    a.level(1, 1, 1.f);
    a.run();
    REQUIRE(a.r.count == 2);
    expect_event(a.r.events[0], WS2_NOTE_ON, 0, 0, 0, .8f);
    expect_event(a.r.events[1], WS2_NOTE_ON, 127, 1, 0, .8f);
}
static void reset_and_choke_send_all_offs() {
    Rig a;
    a.inputs[0].channels = 2;
    a.inputs[1].channels = 2;
    a.level(0, 1, 1.f);
    a.level(1, 0, 1.f);
    a.level(1, 1, 1.f);
    a.run();
    REQUIRE(a.r.count == 2);
    a.desc->reset(a.state);
    a.r.clear();
    a.run(); // queued NOTE_OFF at frame 0; do not rearm high gates on reset
    REQUIRE(a.r.count == 2);
    for (uint32_t c = 0; c < 2; ++c)
        expect_event(a.r.events[c], WS2_NOTE_OFF, c ? 72 : 60, int(c), 0, 0);
    a.r.clear();
    a.run();
    REQUIRE(a.r.count == 0); // still-high gate remains inhibited
    a.level(1, 0, 0.f);
    a.level(1, 1, 0.f);
    a.run(); // arm fresh low-to-high edges
    a.level(1, 0, 1.f);
    a.level(1, 1, 1.f);
    a.r.clear();
    a.run();
    REQUIRE(a.r.count == 2);
    ws2_event choke{};
    choke.struct_size = sizeof(choke);
    choke.type = WS2_NOTE_CHOKE;
    choke.offset = 8;
    a.r.clear();
    a.run({choke}, 32);
    REQUIRE(a.r.count == 2);
    for (uint32_t c = 0; c < 2; ++c)
        expect_event(a.r.events[c], WS2_NOTE_OFF, c ? 72 : 60, int(c), 8, 0);
    a.r.clear();
    a.run();
    REQUIRE(a.r.count == 0);
}
static void rejected_release_retried_before_any_retrigger() {
    Rig a;
    a.legato = 1.f;
    a.level(1, 0, 1.f);
    a.run();
    REQUIRE(a.r.count == 1);
    a.r.clear();
    a.r.refuse_off = true;
    a.level(0, 0, 1.f); // legato key change tries OFF then ON
    a.run({}, 17);
    REQUIRE(a.r.count == 0);
    a.r.refuse_off = false;
    a.run({}, 17);
    REQUIRE(a.r.count == 2);
    expect_event(a.r.events[0], WS2_NOTE_OFF, 60, 0, 0, 0);
    expect_event(a.r.events[1], WS2_NOTE_ON, 72, 0, 0, .8f);

    a.r.clear();
    a.r.refuse_off = true;
    a.desc->reset(a.state);
    a.run();
    REQUIRE(a.r.count == 0);
    a.r.refuse_off = false;
    a.run();
    REQUIRE(a.r.count == 1);
    expect_event(a.r.events[0], WS2_NOTE_OFF, 72, 0, 0, 0);
    a.r.clear();
    a.run();
    REQUIRE(a.r.count == 0);
}
static void source_identity_and_state() {
    Rig a(48000., 16, 111), b(48000., 16, 222);
    a.level(1, 0, 1.f);
    b.level(1, 0, 1.f);
    a.run();
    b.run();
    REQUIRE(a.r.count == 1 && b.r.count == 1);
    REQUIRE(a.r.events[0].source_id != b.r.events[0].source_id);
    REQUIRE(a.r.events[0].note_id == b.r.events[0].note_id);
    Bytes bytes{};
    ws2_writer writer{sizeof(writer), &bytes, sink};
    REQUIRE(a.desc->save_state(a.state, &writer) == WS2_OK);
    a.r.clear();
    Source src{&bytes, 0};
    ws2_reader reader{sizeof(reader), &src, source, uint32_t(bytes.size())};
    REQUIRE(a.desc->load_state(a.state, &reader) == WS2_OK);
    a.run();
    REQUIRE(a.r.count == 1 && a.r.events[0].type == WS2_NOTE_OFF);
}
static void block_rate_cap_and_no_allocation() {
    for (double rate : {44100., 48000., 96000.})
        for (uint32_t size : {1u, 17u, 128u, 256u}) {
            Rig a(rate, 3);
            a.inputs[0].channels = 3;
            a.inputs[1].channels = 3;
            for (uint32_t c = 0; c < 3; ++c) a.level(1, c, 1.f);
            size_t allocations = 0;
            {
                allocation_watch::Scope scope;
                a.run({}, size);
                for (uint32_t c = 0; c < 3; ++c) a.level(1, c, 0.f);
                a.run({}, size);
                allocations = scope.allocations();
            }
            REQUIRE(allocations == 0 && a.r.count == 6);
        }
}

static void mod7_descriptor_and_effective_legato() {
    const auto *d = ws::Module<CvToNotes>::descriptor();
    REQUIRE(d->param_count == 1 && d->input_count == 3 && d->output_count == 1);
    const auto &p = d->params[0];
    REQUIRE(std::string_view(p.id) == "legato" && p.legacy_index == 0);
    REQUIRE(p.flags == (WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE));
    REQUIRE(p.minimum == 0 && p.maximum == 1 && p.initial == 0 && p.step == 1);
    for (uint32_t channels : {1u, 16u}) {
        Rig a(48000., channels);
        a.ctx.channels = channels;
        a.inputs[0].channels = channels;
        a.inputs[1].channels = channels;
        for (uint32_t c = 0; c < channels; ++c) a.level(1, c, 1.f);
        a.run({}, 17);
        REQUIRE(a.r.count == channels);
        for (uint32_t c = 0; c < channels; ++c)
            expect_event(a.r.events[c], WS2_NOTE_ON, 60, int(c), 0, .8f);
        const uint32_t owner = a.r.events[0].source_id;
        a.r.clear();
        // Effective host stream: not a constant/setParam value. While low,
        // pitch changes must not retrigger; enabling legato while high
        // immediately releases/reattacks the current quantized pitch.
        std::array<float, 32> effective{};
        for (uint32_t i = 0; i < effective.size(); ++i)
            effective[i] = (i >= 9 && i < 15) || i >= 23 ? 1.f : 0.f;
        a.param_rows[0] = effective.data();
        a.params[0].flags &= ~uint32_t(WS2_CONSTANT);
        a.inputs[0].flags &= ~uint32_t(WS2_CONSTANT);
        a.inputs[1].flags &= ~uint32_t(WS2_CONSTANT);
        for (uint32_t c = 0; c < channels; ++c)
            for (uint32_t i = 0; i < effective.size(); ++i) {
                a.mem->signal[0][c][i] = i < 5 ? 0.f : i < 19 ? 1.f : 2.f;
                a.mem->signal[1][c][i] = i < 31 ? 1.f : 0.f;
            }
        a.run({}, 32);
        REQUIRE(a.r.count == 5 * channels);
        for (uint32_t c = 0; c < channels; ++c) {
            expect_event(a.r.events[2*c], WS2_NOTE_OFF, 60, int(c), 9, 0.f);
            expect_event(a.r.events[2*c+1], WS2_NOTE_ON, 72, int(c), 9, .8f);
            expect_event(a.r.events[2*channels+2*c], WS2_NOTE_OFF, 72, int(c), 23, 0.f);
            expect_event(a.r.events[2*channels+2*c+1], WS2_NOTE_ON, 84, int(c), 23, .8f);
            expect_event(a.r.events[4*channels+c], WS2_NOTE_OFF, 84, int(c), 31, 0.f);
        }
        for (uint32_t i = 0; i < a.r.count; ++i) REQUIRE(a.r.events[i].source_id == owner);
    }
}

int main() {
    mod7_descriptor_and_effective_legato();
    edges_and_latched_pitch();
    legato_on_and_off();
    poly_channels_and_velocity();
    clamps_and_no_invalid_keys();
    reset_and_choke_send_all_offs();
    rejected_release_retried_before_any_retrigger();
    source_identity_and_state();
    block_rate_cap_and_no_allocation();
}
