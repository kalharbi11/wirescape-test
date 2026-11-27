// SPDX-License-Identifier: GPL-3.0-only
// MOD-7: effective parameter streams at nonzero sample offsets.
#include "dsp.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Groove MOD-7 line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

using Seq = groove::Sequencer;
constexpr uint32_t N = 16;
struct Rig {
    Seq seq{};
    groove::Config pattern{};
    ws2_config config{};
    ws2_host host{};
    ws2_process process{};
    float param[10][N]{}, input[3][N]{}, output[18][N]{};
    const float* pp[10]{}, *ip[3]{};
    float* op[18]{};
    ws2_signal pars[10]{}, ins[3]{};
    ws2_signal_out outs[18]{};
    ws2_event_out events[18]{};

    static int WS2_CALL accept(void*, uint32_t, const ws2_event*) { return WS2_OK; }
    explicit Rig(uint32_t channels) {
        pattern.authored_version = 2;
        pattern.track_count = 2;
        for (uint32_t t = 0; t < 2; ++t) {
            pattern.tracks[t].length = 8;
            for (auto& s : pattern.tracks[t].steps) {
                s.kind = groove::Note;
                s.velocity = 1.f;
            }
        }
        config = {sizeof(config), 48000., N, channels, 42,
                  reinterpret_cast<const uint8_t*>(&pattern), sizeof(pattern)};
        CHECK(seq.prepare(ws::Setup{config, host}));
        for (uint32_t p = 0; p < 10; ++p) {
            pp[p] = param[p];
            pars[p] = {sizeof(ws2_signal), &pp[p], 1, 0};
        }
        for (uint32_t p = 0; p < 3; ++p) {
            ip[p] = input[p];
            ins[p] = {sizeof(ws2_signal), &ip[p], 1, p == 0 ? uint32_t(WS2_CONNECTED) : 0u};
        }
        for (uint32_t p = 0; p < 18; ++p) {
            op[p] = output[p];
            outs[p] = {sizeof(ws2_signal_out), &op[p], 1, 0, 1};
            events[p] = {sizeof(ws2_event_out), nullptr, p, accept};
        }
        process.struct_size = sizeof(process);
        process.frames = N;
        process.channels = channels;
        process.sample_rate = 48000.;
        process.input_count = 3;
        process.output_count = 18;
        process.param_count = 10;
        process.inputs = ins;
        process.outputs = outs;
        process.params = pars;
        process.event_outputs = events;
    }
    void run() {
        ws::Block block(process);
        seq.process(block);
        process.frame_position += N;
        for (auto& p : param) std::fill_n(p, N, 0.f);
        for (auto& p : input) std::fill_n(p, N, 0.f);
    }
    // Host-provided effective stream, not a setParam or constant buffer.
    void pulse(uint32_t p) {
        param[p][5] = param[p][6] = param[p][7] = 1.f;
        run();
    }
    void tick() { input[0][8] = 1.f; run(); }
};

static void descriptor() {
    const auto* d = ws::Module<Seq>::descriptor();
    const char* ids[] = {"run","stop","reset","mute","t1_run","t1_stop","t1_mute","t2_run","t2_stop","t2_mute"};
    CHECK(d->param_count == 10 && d->input_count == 3 && d->output_count == 18);
    CHECK(d->data_slot_count == 1 && d->data_slots != nullptr);
    CHECK(std::strcmp(d->data_slots[0].id, "pattern") == 0);
    CHECK(d->data_slots[0].bytes == sizeof(groove::LivePattern));
    for (uint32_t p = 0; p < 10; ++p) {
        const auto& x = d->params[p];
        CHECK(std::strcmp(x.id, ids[p]) == 0 && x.legacy_index == p);
        CHECK(x.flags == (WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE));
        CHECK(x.minimum == 0 && x.maximum == 1 && x.initial == 0 && x.step == 1);
    }
}
// The pin's live-pattern data must survive the flags-only workshop override.
static void live_pattern_controls(uint32_t channels) {
    Rig r(channels);
    auto pattern = groove::materialize_legacy_pattern(r.pattern);
    pattern.steps_1[0].key = pattern.steps_1[0].keys[0] = 72;
    ws2_data view{sizeof(ws2_data), &pattern, sizeof(pattern), 1};
    r.process.data_count = 1;
    r.process.data = &view;
    r.run();
    r.pulse(0);
    CHECK(r.seq.running && r.output[0][5] == 1.f && r.output[1][5] == 1.f);
    // A new data serial changes the next step, without restarting the transport.
    auto edited = pattern;
    edited.steps_1[1].key = edited.steps_1[1].keys[0] = 48;
    view.data = &edited;
    view.serial = 2;
    r.tick();
    CHECK(r.seq.running && r.seq.step == 1 && r.output[0][8] == -1.f && r.output[1][8] == 1.f);
}
static void global_controls(uint32_t channels) {
    Rig r(channels);
    // Saved/initial high is a baseline, not a command.
    std::fill_n(r.param[0], N, 1.f);
    r.run();
    CHECK(!r.seq.running);
    r.run();
    r.pulse(0);
    CHECK(r.seq.running && r.output[1][4] == 0 && r.output[1][5] == 1 && r.output[6][5] == 1);
    r.pulse(3);
    CHECK(r.seq.muted && r.output[1][4] == 1 && r.output[1][5] == 0 && r.output[6][5] == 0);
    r.pulse(3);
    CHECK(!r.seq.muted);
    r.tick();
    CHECK(r.seq.step == 1 && r.output[1][8] == 1);
    r.pulse(2);
    CHECK(r.seq.step == 0 && r.seq.reset_wait && r.output[1][4] == 1 && r.output[1][5] == 0);
    r.tick();
    CHECK(!r.seq.reset_wait && r.seq.step == 0 && r.output[1][8] == 1);
    r.pulse(1);
    CHECK(r.seq.stop_queued && r.seq.running);
    for (uint32_t i = 0; i < 8; ++i) r.tick();
    CHECK(!r.seq.running && r.output[10][8] == -1 && r.output[11][8] == -1);
    r.seq.reset();
    std::fill_n(r.param[0], N, 1.f);
    r.run();
    CHECK(!r.seq.running);
}
static void track_controls(uint32_t channels) {
    for (uint32_t t = 0; t < 2; ++t) {
        Rig r(channels);
        const uint32_t p = 4 + t * 3;
        r.run();
        r.pulse(p + 1);
        CHECK(!r.seq.players[t].armed && r.seq.players[1-t].armed);
        r.pulse(p);
        CHECK(r.seq.players[t].armed);
        r.pulse(0);
        CHECK(r.seq.players[t].playing && r.seq.players[1-t].playing);
        r.pulse(p + 2);
        CHECK(r.seq.players[t].muted && !r.seq.players[1-t].muted);
        CHECK(r.output[t*5+1][4] == 1 && r.output[t*5+1][5] == 0);
        CHECK(r.output[(1-t)*5+1][5] == 1);
        r.pulse(p + 2);
        CHECK(!r.seq.players[t].muted);
        r.tick();
        CHECK(r.output[t*5+1][8] == 1);
        r.pulse(p + 1);
        CHECK(r.seq.players[t].stop && !r.seq.players[1-t].stop);
        r.pulse(p);
        CHECK(!r.seq.players[t].stop);
        r.pulse(p + 1);
        for (uint32_t i = 0; i < 7; ++i) r.tick();
        CHECK(!r.seq.players[t].playing && r.seq.players[1-t].playing);
        r.pulse(p);
        CHECK(r.seq.players[t].enter);
        for (uint32_t i = 0; i < 8; ++i) r.tick();
        CHECK(r.seq.players[t].playing && !r.seq.players[t].enter);
    }
}
struct Bytes {
    std::array<uint8_t,20> data{};
    size_t pos = 0;
    static int WS2_CALL write(void* ctx, const void* p, uint32_t n) {
        auto& b = *static_cast<Bytes*>(ctx);
        if (n > b.data.size()-b.pos) return WS2_CAPACITY;
        std::memcpy(b.data.data()+b.pos, p, n); b.pos += n; return WS2_OK;
    }
    static int WS2_CALL read(void* ctx, void* p, uint32_t n) {
        auto& b = *static_cast<Bytes*>(ctx);
        if (n > b.data.size()-b.pos) return WS2_INVALID;
        std::memcpy(p, b.data.data()+b.pos, n); b.pos += n; return WS2_OK;
    }
};
static void state_roundtrip() {
    Rig a(1), b(1);
    a.run(); a.pulse(0);
    for (uint32_t i = 0; i < 8; ++i) a.tick();
    CHECK(a.seq.players[0].loop == 2);
    Bytes bytes;
    ws2_writer writer{sizeof(writer), &bytes, Bytes::write};
    CHECK(a.seq.save_state(ws::Writer(&writer)) == WS2_OK && bytes.pos == bytes.data.size());
    bytes.pos = 0;
    ws2_reader reader{sizeof(reader), &bytes, Bytes::read, uint32_t(bytes.data.size())};
    CHECK(b.seq.load_state(ws::Reader(&reader)) == WS2_OK);
    for (uint32_t t = 0; t < 2; ++t) {
        CHECK(a.seq.players[t].loop == b.seq.players[t].loop);
        CHECK(a.seq.players[t].rng == b.seq.players[t].rng);
    }
}
int main() {
    descriptor();
    for (uint32_t channels : {1u,16u}) {
        live_pattern_controls(channels);
        global_controls(channels);
        track_controls(channels);
    }
    state_roundtrip();
}
