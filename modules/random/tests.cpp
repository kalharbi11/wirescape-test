// SPDX-License-Identifier: GPL-3.0-only
// essentials.random (MOD-3) tests, written before the DSP: the C2 beat clock (clock, transport playing
// and stopped, reset, hold), trigger-to-step at defaults, seeds, glide, depth, 16-channel hosts, state,
// zero allocations and finite output at extremes.
#include "dsp.h"
#include "../../tests/native/abi2/dsp_fixture.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using wsbeat::BeatClock;
using Mod = ws::Module<wsmod::Random>;

// ---- beat clock
static void clock_edges_and_interpolation() {
    BeatClock c;
    const double rate = 48000;
    const uint32_t period = 100;
    std::vector<double> beats;
    for (uint32_t s = 0; s < 1000; ++s) {
        const float gate = (s % period) < 10 && s < 500 ? 1.f : 0.f;  // edges at 0,100,..,400
        beats.push_back(c.step(true, gate, false, 1, nullptr, 0, rate));
    }
    VERIFY(beats[0] == 1);                         // first edge: one beat
    for (uint32_t s = 1; s < 100; ++s) VERIFY(beats[s] == 1);  // before the 2nd edge: moves only at edges
    VERIFY(beats[100] == 2);
    VERIFY(std::abs(beats[150] - 2.5) < 1e-12);    // interpolated from the measured period
    VERIFY(beats[199] < 3 && beats[200] == 3);
    // After the last edge (400): interpolation never reaches the next edge's beat...
    for (uint32_t s = 400; s < 1000; ++s) VERIFY(beats[s] < 6);
    VERIFY(beats[499] > 5.98);
    // ...and holds after two periods without an edge.
    VERIFY(c.held() && beats[999] == beats[600]);
    // The next edge resumes at the next beat; the period is measured afresh (moves only at edges).
    double b = c.step(true, 1, false, 1, nullptr, 0, rate);
    VERIFY(b == 6);
    for (int s = 0; s < 50; ++s) b = c.step(true, 0, false, 1, nullptr, 0, rate);
    VERIFY(b == 6);
}
static void clock_ppq() {
    BeatClock c;
    double b = 0;
    for (uint32_t s = 0; s < 24 * 10; ++s) b = c.step(true, s % 10 < 5 ? 1.f : 0.f, false, 24, nullptr, 0, 48000);
    VERIFY(std::abs(b - (1 + .9 / 24)) < 1e-9);  // 24 edges = 1 beat, then 9/10 of an edge interval
}
static void transport_playing_and_stopped() {
    BeatClock c;
    const double rate = 48000;
    ws2_transport t{sizeof(t), WS2_PLAYING, 120, 3.0, 0, 4, 4, 0, 0};
    double b = 0;
    for (uint32_t i = 0; i < 64; ++i) b = c.step(false, 0, false, 1, &t, i, rate);
    VERIFY(std::abs(b - (3.0 + 63 * 120. / (60 * rate))) < 1e-12);  // locked per sample
    t.flags = 0;                                                     // stopped: free-run from here
    const double at = b;
    for (uint32_t i = 0; i < 48000; ++i) b = c.step(false, 0, false, 1, &t, i % 128, rate);
    VERIFY(std::abs(b - (at + 2.0)) < 1e-6);                         // 120 bpm for one second
    // No transport at all: 120 bpm free-run.
    BeatClock d;
    for (uint32_t i = 0; i < 24000; ++i) b = d.step(false, 0, false, 1, nullptr, 0, rate);
    VERIFY(std::abs(b - 1.0) < 1e-6);
}
static void reset_sets_the_origin() {
    BeatClock c;
    ws2_transport t{sizeof(t), WS2_PLAYING, 120, 10.0, 0, 4, 4, 0, 0};
    VERIFY(c.step(false, 0, false, 1, &t, 0, 48000) == 10.0);
    VERIFY(c.step(false, 0, true, 1, &t, 0, 48000) == 0.0);          // reset: origin now
    VERIFY(std::abs(c.step(false, 0, false, 1, &t, 24000, 48000) - 1.0) < 1e-12);
    // Clock mode: an edge in the reset sample is the origin, not a beat.
    BeatClock k;
    k.step(true, 1, false, 1, nullptr, 0, 48000);
    k.step(true, 0, false, 1, nullptr, 0, 48000);
    VERIFY(k.step(true, 1, true, 1, nullptr, 0, 48000) == 0.0);
}
static void division_table() {
    const double expect[18] = {16, 8, 4, 3, 2, 1.5, 4. / 3, 1, .75, 2. / 3, .5, .375, 1. / 3, .25, .1875, 1. / 6, .125, 1. / 12};
    for (int i = 0; i < 18; ++i) {
        VERIFY(wsbeat::division_beats(float(i)) == expect[i]);
        if (i) VERIFY(expect[i] < expect[i - 1]);  // a sweep is monotonic
    }
    for (int k = 0; k < 600; ++k) VERIFY(wsbeat::division_index(k * (1. / 3), 1. / 3) == k);
    for (int k = 0; k < 600; ++k) VERIFY(wsbeat::division_index(k * (1. / 12), 1. / 12) == k);
}

// ---- module
static std::unique_ptr<DspFixture> rig(uint32_t channels = 1) {
    auto f = std::make_unique<DspFixture>(Mod::descriptor(), 48000, channels);
    const auto *d = f->d;
    VERIFY(d->param_count == 5 && d->input_count == 2 && d->output_count == 3);
    const char *ids[] = {"division", "depth", "slew", "seed", "clock_ppq"};
    for (uint32_t p = 0; p < 5; ++p) VERIFY(!std::strcmp(d->params[p].id, ids[p]) && d->params[p].legacy_index == p);
    VERIFY(d->params[0].flags & WS2_STEPPED && d->params[0].flags & WS2_MODULATABLE && d->params[0].initial == 7);
    VERIFY(!(d->params[1].flags & WS2_STEPPED) && d->params[1].flags & WS2_MODULATABLE && d->params[1].initial == 1);
    VERIFY(!(d->params[2].flags & WS2_STEPPED) && d->params[2].flags & WS2_MODULATABLE && d->params[2].initial == 1);
    VERIFY(d->params[3].flags & WS2_STEPPED && !(d->params[3].flags & WS2_MODULATABLE) && d->params[3].maximum == 65535);
    VERIFY(d->params[4].flags & WS2_STEPPED && !(d->params[4].flags & WS2_MODULATABLE) && d->params[4].initial == 1);
    for (uint32_t c = 0; c < 16; ++c) std::fill_n(f->input[0][c], 256, 0.f), std::fill_n(f->input[1][c], 256, 0.f);
    f->ins[0].flags = WS2_CONNECTED;
    f->ins[1].flags = WS2_CONNECTED;
    return f;
}
// Defaults: every rising edge on `clock` is one step (sample and hold), even when irregular.
static void trigger_to_step_at_defaults() {
    auto f = rig();
    f->parameter[2][0][0] = 0;  // slew 0: smooth equals stepped
    const uint32_t edges[] = {3, 40, 47, 130, 131, 200};
    for (uint32_t e : edges) f->input[0][0][e] = 1;  // 1-sample triggers (Schmitt sees each)
    f->run(256);
    const float *s = f->output[0][0], *m = f->output[1][0], *g = f->output[2][0];
    std::vector<uint32_t> changes;
    for (uint32_t i = 1; i < 256; ++i) if (s[i] != s[i - 1]) changes.push_back(i);
    // 131 follows 130 with no fall below .25 in between, so it is not a new edge.
    const std::vector<uint32_t> expected{3, 40, 47, 130, 200};
    VERIFY(changes == expected);
    for (uint32_t i = 0; i < 256; ++i) {
        VERIFY(std::isfinite(s[i]) && s[i] >= -1 && s[i] <= 1 && m[i] == s[i]);
    }
    VERIFY(g[2] == 0 && g[3] == 1);
    VERIFY(g[200] == 1 && g[200 + 47] == 1 && g[200 + 48] == 0);  // 1 ms trigger at 48 kHz
}
// Seed 1..65535 fixes the sequence; seed 0 uses the engine's per-instance seed. Reset reproduces it.
static std::vector<float> sequence(float seed, uint64_t config_seed = 42) {
    auto f = rig();
    if (config_seed != 42) {
        f->config.seed = config_seed;
        VERIFY(f->d->prepare(f->state, &f->config, &f->host) == WS2_OK);
    }
    f->parameter[3][0][0] = seed;
    std::vector<float> out;
    for (int block = 0; block < 4; ++block) {
        for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = i % 32 < 4 ? 1.f : 0.f;
        f->run(256);
        for (uint32_t i = 0; i < 256; i += 32) out.push_back(f->output[0][0][i]);
    }
    return out;
}
static void seeds() {
    VERIFY(sequence(123) == sequence(123));
    VERIFY(sequence(123) != sequence(124));
    VERIFY(sequence(0) == sequence(0));
    VERIFY(sequence(0) != sequence(0, 43));     // 0 follows the instance seed
    VERIFY(sequence(123) == sequence(123, 43));  // a fixed seed ignores it
    // Module reset and the reset input both restart the sequence.
    auto f = rig();
    f->parameter[3][0][0] = 9;
    for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = i % 32 < 4 ? 1.f : 0.f;
    f->run(256);
    std::vector<float> first(f->output[0][0], f->output[0][0] + 256);
    f->reset();
    f->run(256);
    VERIFY(std::memcmp(first.data(), f->output[0][0], 256 * sizeof(float)) == 0);
    f->input[1][0][0] = 1;  // reset input at the same sample as a clock edge
    f->run(256);
    VERIFY(std::memcmp(first.data(), f->output[0][0], 256 * sizeof(float)) == 0);
}
// smooth glides with an S-curve over slew x the step length; depth scales both outputs.
static void glide_and_depth() {
    auto f = rig();
    f->parameter[1][0][0] = .5f;  // depth
    for (int block = 0; block < 4; ++block) {
        for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = i % 64 == 0 ? 1.f : 0.f;  // period 64
        f->run(256);
    }
    const float *s = f->output[0][0], *m = f->output[1][0];
    // Step at 0: glide from the previous value over 64 samples (slew 1 x one beat of 64 samples).
    VERIFY(m[62] != s[0] && m[63] == s[0]);      // arrives exactly at the next step
    VERIFY(std::abs(s[0]) <= .5f);               // depth .5
    // S-curve: slow at both ends, fastest in the middle.
    const float a = std::abs(m[2] - m[1]), mid = std::abs(m[33] - m[32]), z = std::abs(m[62] - m[61]);
    VERIFY(mid > a && mid > z);
}
static void division_ratchets_between_edges() {
    auto f = rig();
    f->parameter[0][0][0] = 13;  // 1/16: four values per beat
    f->parameter[2][0][0] = 0;
    uint32_t changes = 0;
    for (int block = 0; block < 4; ++block) {
        for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = i % 128 == 0 ? 1.f : 0.f;  // beat = 128
        f->run(256);
        if (block == 3)
            for (uint32_t i = 1; i < 256; ++i) changes += f->output[0][0][i] != f->output[0][0][i - 1];
    }
    VERIFY(changes == 7);  // 8 values in two beats; the first lands at sample 0
}
static void sixteen_channel_host_and_extremes() {
    auto f = rig(16);
    for (uint32_t i = 0; i < 256; ++i) for (uint32_t c = 0; c < 16; ++c) f->input[0][c][i] = i % 16 < 2 ? 1.f : 0.f;
    f->run(256);
    VERIFY(f->outs[0].channels == 1 && f->outs[1].channels == 1 && f->outs[2].channels == 1);  // mono v1
    const float extremes[][5] = {{0, 0, 0, 0, 1}, {17, 1, 1, 65535, 24}, {0, 1, 1, 1, 24}, {17, 0, 0, 65535, 1}};
    for (const auto &e : extremes) {
        for (uint32_t p = 0; p < 5; ++p) f->parameter[p][0][0] = e[p];
        for (float in : {0.f, 1.f, std::numeric_limits<float>::quiet_NaN(), 1e30f, -1e30f}) {
            for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = f->input[1][0][i] = (i & 1) ? in : 0.f;
            f->run(256);
            for (uint32_t p = 0; p < 3; ++p) for (uint32_t i = 0; i < 256; ++i) VERIFY(std::isfinite(f->output[p][0][i]));
        }
    }
}
static std::vector<uint8_t> save(DspFixture &f) {
    std::vector<uint8_t> bytes;
    ws2_writer w{sizeof(w), &bytes, [](void *ctx, const void *p, uint32_t n) {
        auto &b = *static_cast<std::vector<uint8_t> *>(ctx);
        b.insert(b.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        return int(WS2_OK);
    }};
    VERIFY(f.d->save_state(f.state, &w) == WS2_OK);
    return bytes;
}
static int load(DspFixture &f, const std::vector<uint8_t> &bytes) {
    struct Cursor { const std::vector<uint8_t> &b; size_t pos = 0; } c{bytes};
    ws2_reader r{sizeof(r), &c, [](void *ctx, void *p, uint32_t n) {
        auto &c = *static_cast<Cursor *>(ctx);
        if (n > c.b.size() - c.pos) return int(WS2_INVALID);
        std::memcpy(p, c.b.data() + c.pos, n);
        c.pos += n;
        return int(WS2_OK);
    }, uint32_t(bytes.size())};
    return f.d->load_state(f.state, &r);
}
static void state_round_trip() {
    auto a = rig(), b = rig();
    for (auto *f : {a.get(), b.get()}) f->parameter[0][0][0] = 13;
    for (uint32_t i = 0; i < 256; ++i) a->input[0][0][i] = i % 100 < 3 ? 1.f : 0.f;
    a->run(250);  // mid-period, mid-glide, mid-trigger
    const auto saved = save(*a);
    VERIFY(load(*b, saved) == WS2_OK && save(*b) == saved);
    for (int block = 0; block < 4; ++block) {
        for (auto *f : {a.get(), b.get()}) for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = (i + 50) % 100 < 3 ? 1.f : 0.f;
        a->run(256); b->run(256);
        for (uint32_t p = 0; p < 3; ++p) VERIFY(std::memcmp(a->output[p][0], b->output[p][0], 256 * sizeof(float)) == 0);
    }
    auto bad = saved;
    bad[0] = 2;
    VERIFY(load(*b, bad) == WS2_UNSUPPORTED);
    bad = saved;
    bad.pop_back();
    VERIFY(load(*b, bad) == WS2_INVALID);
    bad = saved;
    bad.back() = 7;  // reset_high must be 0 or 1
    VERIFY(load(*b, bad) == WS2_INVALID);
}
static void zero_allocations() {
    auto f = rig();
    f->parameter[0][0][0] = 17;
    for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = i % 7 == 0 ? 1.f : 0.f;
    allocation_watch::Scope watch;
    for (int k = 0; k < 8; ++k) f->run(256);
    f->reset();
    f->run(17);
    VERIFY(watch.allocations() == 0);
}
int main() {
    clock_edges_and_interpolation();
    clock_ppq();
    transport_playing_and_stopped();
    reset_sets_the_origin();
    division_table();
    trigger_to_step_at_defaults();
    seeds();
    glide_and_depth();
    division_ratchets_between_edges();
    sixteen_channel_host_and_extremes();
    state_round_trip();
    zero_allocations();
    std::puts("PASS essentials.random (MOD-3): beat clock, trigger-to-step, seeds, glide, state, extremes, no allocations");
    return 0;
}
