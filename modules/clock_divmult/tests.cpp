// SPDX-License-Identifier: GPL-3.0-only
// essentials.clock_divmult (MOD-4) tests, written before the DSP: ÷N and ×N counts over 64 edges and
// after reset, ×N under a jittered and an accelerating clock, width at 0.01 and 0.99, a ratio change
// mid-period, transport mode playing and stopped, a 16-channel host, state round-trip, zero
// allocations, finite output at extremes, and the beat_clock.h copy matching essentials.random's.
#include "dsp.h"
#include "../../tests/native/abi2/dsp_fixture.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Mod = ws::Module<wsmod::ClockDivMult>;
static constexpr int kRatio[19] = {-16, -12, -8, -7, -6, -5, -4, -3, -2, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16};

// ---- beat_clock.h is a byte copy of modules/random/beat_clock.h (0024 §4.0 C6)
static std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string out;
    for (char c : s) if (c != '\r') out += c;  // a Windows checkout may add CRs
    return out;
}
static uint64_t fnv1a(const std::string &s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
    return h;
}
static void beat_clock_copy_matches_random() {
    const std::string mine = read_file(std::string(WSDM_SOURCE_DIR) + "/beat_clock.h");
    VERIFY(!mine.empty());
    // FNV-1a 64 of modules/random/beat_clock.h at MOD-3a (fe4b974), CRs removed. Change both copies and
    // this constant together, or not at all.
    VERIFY(fnv1a(mine) == 0xc350780f5a66cdeeull && mine.size() == 7633);
    // When both folders are staged (one scratch tree with random too), compare them directly.
    const std::string theirs = read_file(std::string(WSDM_SOURCE_DIR) + "/../random/beat_clock.h");
    if (!theirs.empty()) VERIFY(theirs == mine);
}

// ---- harness
struct Rig {
    std::unique_ptr<DspFixture> f;
    std::vector<float> out[4];
    explicit Rig(uint32_t channels = 1) : f(std::make_unique<DspFixture>(Mod::descriptor(), 48000, channels)) {
        for (uint32_t c = 0; c < 16; ++c) std::fill_n(f->input[0][c], 256, 0.f), std::fill_n(f->input[1][c], 256, 0.f);
        f->ins[0].flags = WS2_CONNECTED;
        f->ins[1].flags = WS2_CONNECTED;
    }
    void ratio(uint32_t o, float index) { f->parameter[o][0][0] = index; }
    void width(float w) { f->parameter[4][0][0] = w; }
    // Runs `clock` and `reset` (one value per sample) through the module in 256-sample blocks.
    void run(const std::vector<float> &clock, const std::vector<float> &reset = {}) {
        for (size_t at = 0; at < clock.size(); at += 256) {
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(256, clock.size() - at));
            for (uint32_t i = 0; i < n; ++i) {
                f->input[0][0][i] = clock[at + i];
                f->input[1][0][i] = at + i < reset.size() ? reset[at + i] : 0.f;
            }
            f->run(n);
            for (uint32_t o = 0; o < 4; ++o) out[o].insert(out[o].end(), f->output[o][0], f->output[o][0] + n);
        }
    }
};
// A clock with 10-sample pulses at the given rising-edge samples.
static std::vector<float> pulses(const std::vector<uint32_t> &edges, size_t length) {
    std::vector<float> v(length, 0.f);
    for (uint32_t e : edges) for (uint32_t k = 0; k < 10 && e + k < length; ++k) v[e + k] = 1.f;
    return v;
}
static std::vector<uint32_t> steady(uint32_t first, uint32_t period, uint32_t count) {
    std::vector<uint32_t> e;
    for (uint32_t k = 0; k < count; ++k) e.push_back(first + k * period);
    return e;
}
// Rising edges of an output (gate starts) in [from, to).
static uint32_t starts(const std::vector<float> &o, size_t from, size_t to) {
    uint32_t n = 0;
    for (size_t i = from; i < to && i < o.size(); ++i) n += o[i] > .5f && (i == 0 || o[i - 1] < .5f);
    return n;
}
static std::vector<size_t> start_samples(const std::vector<float> &o) {
    std::vector<size_t> s;
    for (size_t i = 0; i < o.size(); ++i) if (o[i] > .5f && (i == 0 || o[i - 1] < .5f)) s.push_back(i);
    return s;
}

static void descriptor() {
    const auto *d = Mod::descriptor();
    VERIFY(d->param_count == 5 && d->input_count == 2 && d->output_count == 4);
    const char *ids[] = {"ratio1", "ratio2", "ratio3", "ratio4", "width"};
    const float defaults[] = {10, 8, 6, 2, .5f};
    for (uint32_t p = 0; p < 5; ++p) {
        VERIFY(!std::strcmp(d->params[p].id, ids[p]) && d->params[p].legacy_index == p);
        VERIFY(d->params[p].initial == defaults[p] && d->params[p].flags & WS2_MODULATABLE);
    }
    for (uint32_t p = 0; p < 4; ++p) VERIFY(d->params[p].flags & WS2_STEPPED && d->params[p].minimum == 0 && d->params[p].maximum == 18);
    VERIFY(!(d->params[4].flags & WS2_STEPPED) && d->params[4].minimum == .01f && d->params[4].maximum == .99f);
    VERIFY(!std::strcmp(d->inputs[0].id, "clock") && d->inputs[0].hint == WS2_GATE);
    VERIFY(!std::strcmp(d->inputs[1].id, "reset") && d->inputs[1].hint == WS2_TRIGGER);
    const char *outs[] = {"out1", "out2", "out3", "out4"};
    for (uint32_t o = 0; o < 4; ++o) VERIFY(!std::strcmp(d->outputs[o].id, outs[o]) && d->outputs[o].hint == WS2_GATE);
}

// ÷N fires on edges 0, N, 2N, ... from power-on (and from reset); ×N gives N gates per input period
// once the period is measured (one gate in the first period, before the second edge).
static void counts_over_64_edges_and_after_reset() {
    for (int r = 0; r < 19; r += 4) {
        Rig rig;
        const int idx[4] = {r, std::min(r + 1, 18), std::min(r + 2, 18), std::min(r + 3, 18)};
        for (uint32_t o = 0; o < 4; ++o) rig.ratio(o, float(idx[o]));
        // 64 edges, period 100; a reset at 6350 (mid-period); then 64 more edges from 6400.
        std::vector<float> reset(12800, 0.f);
        reset[6350] = 1.f;
        rig.run(pulses(steady(0, 100, 128), 12800), reset);
        for (uint32_t o = 0; o < 4; ++o) {
            const int n = kRatio[idx[o]];
            const uint32_t before = starts(rig.out[o], 0, 6350), during = starts(rig.out[o], 6350, 6400),
                           after = starts(rig.out[o], 6400, 12800);
            if (n < 0) {
                const uint32_t want = 63 / uint32_t(-n) + 1;
                VERIFY(before == want && during == 0 && after == want);
                // On the edges themselves (one sample late only when the previous gate is still high).
                for (size_t s : start_samples(rig.out[o])) {
                    VERIFY(s % 100 <= 1);
                    VERIFY((s < 6400 ? s / 100 : (s - 6400) / 100) % uint32_t(-n) == 0);
                }
            } else {
                // Before the reset: 1 in the first period, then N per period for edges 1..62, and the
                // gates of period 63 that start before 6350.
                uint32_t want = 1 + 62 * uint32_t(n);
                for (int j = 0; j < n; ++j) want += std::ceil(j * 100. / n) < 50;
                VERIFY(before == want);
                VERIFY(during == 0);  // armed: nothing until the next edge
                VERIFY(after == 64 * uint32_t(n));
            }
        }
        // Reset: every output fires on the next edge (6400, or 6401 after a gate still high).
        for (uint32_t o = 0; o < 4; ++o) VERIFY(starts(rig.out[o], 6400, 6402) == 1);
    }
}

// ×N never gives more than N gates in one input period, whatever the clock does.
static void multiply_never_exceeds_n() {
    std::vector<uint32_t> jitter, accel;
    uint32_t x = 12345, at = 0;
    for (int k = 0; k < 80; ++k) {
        jitter.push_back(at);
        x = x * 1103515245u + 12345u;
        at += 50 + (x >> 16) % 101;  // 50..150
    }
    at = 0;
    for (int k = 0; k < 60; ++k) {
        accel.push_back(at);
        at += 400 - 6 * k;  // 400 down to 46
    }
    for (const auto *edges : {&jitter, &accel}) {
        for (int r : {10, 12, 14, 16, 17, 18}) {
            for (float w : {.5f, .99f}) {
                Rig rig;
                rig.width(w);
                for (uint32_t o = 0; o < 4; ++o) rig.ratio(o, float(r));
                rig.run(pulses(*edges, edges->back() + 1));
                const uint32_t n = uint32_t(kRatio[r]);
                for (size_t k = 0; k + 1 < edges->size(); ++k) {
                    const uint32_t c = starts(rig.out[0], (*edges)[k], (*edges)[k + 1]);
                    VERIFY(c <= n && c >= 1);
                }
            }
        }
    }
}

// Width is the gate length as a fraction of the output period.
static void width_extremes() {
    for (float w : {.01f, .99f}) {
        Rig rig;
        rig.width(w);
        rig.ratio(0, 9);   // ×1: period 480
        rig.ratio(1, 8);   // ÷2: period 960
        rig.ratio(2, 12);  // ×4: period 120
        rig.ratio(3, 18);  // ×16: period 30
        rig.run(pulses(steady(0, 480, 20), 480 * 20));
        const double periods[4] = {480, 960, 120, 30};
        for (uint32_t o = 0; o < 4; ++o) {
            const auto s = start_samples(rig.out[o]);
            VERIFY(s.size() >= 4);
            // A steady period after the first edge: check one gate in the middle.
            const size_t g = s[s.size() / 2];
            size_t high = 0;
            while (g + high < rig.out[o].size() && rig.out[o][g + high] > .5f) ++high;
            const double want = std::max(1., std::min(std::round(w * periods[o]), periods[o] - 1));
            VERIFY(double(high) == want);
            for (size_t k = s.size() / 2; k + 1 < s.size(); ++k) VERIFY(double(s[k + 1] - s[k]) == periods[o]);
        }
    }
}

// A ratio change applies at that output's next period start: the gate in progress is not cut.
static void ratio_change_mid_period() {
    Rig rig;
    rig.ratio(0, 6);  // ÷4: gates of 200 samples every 400 (edges every 100)
    const auto clock = pulses(steady(0, 100, 26), 2600);
    const auto slice = [&](size_t from, size_t to) {
        rig.run(std::vector<float>(clock.begin() + from, clock.begin() + to));
    };
    slice(0, 450);
    rig.ratio(0, 12);  // ×4, mid-gate (the ÷4 gate 400..599): applies at the next ÷4 start, 800
    slice(450, 1610);
    rig.ratio(0, 6);   // back to ÷4, mid ×4 gate (1600..1612)
    slice(1610, 2600);
    const auto &o = rig.out[0];
    VERIFY(o[400] > .5f && o[599] > .5f && o[600] < .5f);  // not cut
    VERIFY(starts(o, 401, 800) == 0);
    VERIFY(starts(o, 800, 900) == 4);                      // ×4 from 800
    VERIFY(o[1600] > .5f && o[1612] > .5f && o[1613] < .5f);  // the running ×4 gate completes
    // The ×4 period ends at 1625; ÷4 resumes on its own grid: 2000 and 2400.
    VERIFY(starts(o, 1601, 2600) == 2 && starts(o, 2000, 2001) == 1 && starts(o, 2400, 2401) == 1);
}

// Without a clock cable the transport drives it: locked while playing, free-running while stopped.
static void transport_playing_and_stopped() {
    Rig rig;
    rig.f->ins[0].flags = 0;  // no clock cable
    ws2_transport t{sizeof(t), WS2_PLAYING, 1440, 0.0, 0, 4, 4, 0, 0};  // 2000 samples per beat
    rig.f->process.transport = &t;
    // Advance the transport by hand per block (the host's job): 64 blocks of 250 per half.
    for (int block = 0; block < 2; ++block) {
        for (int k = 0; k < 64; ++k) {
            rig.run(std::vector<float>(250, 0.f));
            t.beat += 250 * 1440. / (60 * 48000.);
        }
        if (block == 0) t.flags = 0;  // stopped for the second half: free-run at its bpm
    }
    // out1 ×2: every 1000 samples from 0; out2 ÷2: every 4000; out3 ÷4: every 8000; out4 ÷8: every 16000.
    const size_t every[4] = {1000, 4000, 8000, 16000};
    for (uint32_t o = 0; o < 4; ++o) {
        const auto s = start_samples(rig.out[o]);
        VERIFY(s.size() == 32000 / every[o]);
        for (size_t k = 0; k < s.size(); ++k) VERIFY(std::llabs((long long)s[k] - (long long)(k * every[o])) <= 1);
    }
    // A reset in transport mode is beat 0: every output fires there.
    Rig r2;
    r2.f->ins[0].flags = 0;
    ws2_transport t2{sizeof(t2), WS2_PLAYING, 1440, 0.3, 0, 4, 4, 0, 0};  // no boundary at 0.3
    r2.f->process.transport = &t2;
    std::vector<float> reset(256, 0.f);
    reset[100] = 1;
    r2.run(std::vector<float>(256, 0.f), reset);
    for (uint32_t o = 0; o < 4; ++o) VERIFY(r2.out[o][100] > .5f && r2.out[o][99] < .5f);
}

// A single trigger (the tempo never measured) still ends its gates: at the 120 BPM estimate.
static void single_trigger_does_not_stick() {
    Rig rig;
    rig.run(pulses({0}, 200000));
    for (uint32_t o = 0; o < 4; ++o) {
        VERIFY(starts(rig.out[o], 0, 200000) == 1 && rig.out[o][0] > .5f);
        VERIFY(rig.out[o].back() < .5f);
    }
    // ×2 at width .5 of half a beat at 24000 samples per beat: 6000 samples.
    VERIFY(rig.out[0][5999] > .5f && rig.out[0][6000] < .5f);
}
static void sixteen_channel_host_and_extremes() {
    Rig rig(16);
    rig.run(pulses(steady(0, 64, 8), 512));
    VERIFY(rig.f->outs[0].channels == 1 && rig.f->outs[3].channels == 1);  // mono v1
    const float extremes[][5] = {{0, 0, 0, 0, .01f}, {18, 18, 18, 18, .99f}, {-5, 40, 9, 9, -1}, {9, 9, 9, 9, 2}};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto &e : extremes) {
        for (uint32_t p = 0; p < 5; ++p) rig.f->parameter[p][0][0] = e[p];
        for (float in : {0.f, 1.f, nan, 1e30f, -1e30f}) {
            std::vector<float> c(256), r(256);
            for (uint32_t i = 0; i < 256; ++i) c[i] = r[i] = (i & 1) ? in : 0.f;
            rig.run(c, r);
        }
    }
    for (uint32_t p = 0; p < 5; ++p) rig.f->parameter[p][0][0] = nan;
    rig.run(pulses(steady(0, 32, 8), 256));
    for (uint32_t o = 0; o < 4; ++o)
        for (float v : rig.out[o]) VERIFY(v == 0.f || v == 1.f);
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
    Rig a, b;
    for (auto *r : {&a, &b}) r->ratio(0, 18), r->ratio(1, 3), r->width(.3f);
    std::vector<float> reset(1000, 0.f);
    reset[640] = 1;
    a.run(pulses(steady(0, 100, 10), 1000), reset);
    a.ratio(1, 12);  // pending ratio change, mid-gate, mid-period, armed edge not yet seen
    b.ratio(1, 12);
    a.run(std::vector<float>(30, 0.f));
    const auto saved = save(*a.f);
    VERIFY(load(*b.f, saved) == WS2_OK && save(*b.f) == saved);
    for (auto &o : a.out) o.clear();
    const auto more = pulses(steady(70, 100, 20), 2048);
    a.run(more);
    b.run(more);
    for (uint32_t o = 0; o < 4; ++o) VERIFY(a.out[o] == b.out[o]);
    const auto now = save(*b.f);
    auto bad = saved;
    bad[0] = 2;
    VERIFY(load(*b.f, bad) == WS2_UNSUPPORTED);
    bad = saved;
    bad.pop_back();
    VERIFY(load(*b.f, bad) == WS2_INVALID);
    bad = saved;
    bad.back() = 7;  // the last output's last_out flag must be 0 or 1
    VERIFY(load(*b.f, bad) == WS2_INVALID);
    VERIFY(save(*b.f) == now);  // a refused load changes nothing
}
static void zero_allocations() {
    Rig rig;
    for (uint32_t o = 0; o < 4; ++o) rig.ratio(o, float(o * 6));
    for (uint32_t i = 0; i < 256; ++i) rig.f->input[0][0][i] = i % 7 == 0 ? 1.f : 0.f;
    allocation_watch::Scope watch;
    for (int k = 0; k < 8; ++k) rig.f->run(256);
    rig.f->reset();
    rig.f->run(17);
    VERIFY(watch.allocations() == 0);
}

int main() {
    beat_clock_copy_matches_random();
    descriptor();
    counts_over_64_edges_and_after_reset();
    multiply_never_exceeds_n();
    width_extremes();
    ratio_change_mid_period();
    transport_playing_and_stopped();
    single_trigger_does_not_stick();
    sixteen_channel_host_and_extremes();
    state_round_trip();
    zero_allocations();
    std::puts("PASS essentials.clock_divmult (MOD-4): beat_clock copy, ÷N/×N counts, reset, jitter, width, ratio change, transport, state, no allocations");
    return 0;
}
