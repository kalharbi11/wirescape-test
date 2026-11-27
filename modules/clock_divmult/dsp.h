// SPDX-License-Identifier: GPL-3.0-only
// essentials.clock_divmult (wirescape-test MOD-4, lab proposals/0024 §4.3): four gate outputs that
// divide or multiply the C2 beat (one beat per input edge), from a `clock` cable or the transport.
#pragma once
#include "wirescape_sdk.h"
#include "beat_clock.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wsmod {

struct ClockDivMult {
    static constexpr ws::Info info{"essentials.clock_divmult", "Clock Div/Mult", 1};

    // Mono in v1: the first channel of each input is used.
    static constexpr std::array inputs{
        ws::port("clock", "Clock", WS2_GATE, WS2_FIRST),
        ws::port("reset", "Reset", WS2_TRIGGER, WS2_FIRST)};
    static constexpr std::array outputs{
        ws::port("out1", "Out 1", WS2_GATE), ws::port("out2", "Out 2", WS2_GATE),
        ws::port("out3", "Out 3", WS2_GATE), ws::port("out4", "Out 4", WS2_GATE)};

    // Parameter IDs are API. Ratios are STEPPED|MODULATABLE (timing, not structure; 0023 R3.3).
    static constexpr uint32_t kRatioFlags = WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE;
    static constexpr std::array params{
        ws::param("ratio1", "Ratio 1", 0, 18, 10, "", kRatioFlags, WS2_LINEAR, 0, 4, 1),
        ws::param("ratio2", "Ratio 2", 0, 18, 8, "", kRatioFlags, WS2_LINEAR, 1, 4, 1),
        ws::param("ratio3", "Ratio 3", 0, 18, 6, "", kRatioFlags, WS2_LINEAR, 2, 4, 1),
        ws::param("ratio4", "Ratio 4", 0, 18, 2, "", kRatioFlags, WS2_LINEAR, 3, 4, 1),
        ws::param("width", "Width", .01f, .99f, .5f, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 4)};

    // Index 0..18: ÷16 ÷12 ÷8 ÷7 ÷6 ÷5 ÷4 ÷3 ÷2 ×1 ×2 ×3 ×4 ×5 ×6 ×7 ×8 ×12 ×16 (negative divides).
    static constexpr int kRatio[19] = {-16, -12, -8, -7, -6, -5, -4, -3, -2, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16};
    static constexpr uint32_t kDefault[4] = {10, 8, 6, 2};
    static constexpr double kMaxGate = 2147483647.;

    struct Out {
        uint32_t ratio = 0;     // the latched ratio index (applies to the period in progress)
        uint32_t pending = 0;   // the ratio parameter now; latched at the next period start
        int64_t index = 0;      // the period of the latched ratio that the position is in
        uint32_t left = 0;      // samples of gate left (tempo known)
        uint32_t delayed = 0;   // a start delayed one sample because the gate was still high: its length
        double end_beat = -1;   // tempo unknown: the gate also ends at this position (beats); -1 none
        bool high = false;      // the last sample's output
    };

    wsbeat::BeatClock beat;
    ws::Schmitt clock_in, reset_in;
    bool clock_high = false, reset_high = false;
    bool armed = true;       // clock mode: the next edge fires every output and sets the grid
    uint32_t mode = 0;       // 0 not yet run, 1 clock cable, 2 transport
    uint32_t edges = 0;      // clock edges seen (saturates at 2): the tempo is known from the second
    double grid = 0;         // the beat that position 0 is (clock mode: the beat at the armed edge)
    std::array<Out, 4> outs{};
    double rate = 48000;

    void prepare(const ws::Setup &s) {
        rate = s.sample_rate();
        reset();
    }
    void reset() {
        beat.reset();
        clock_in = reset_in = ws::Schmitt{};
        clock_high = reset_high = false;
        armed = true;
        mode = 0;
        edges = 0;
        grid = 0;
        previous_position_ = 0;
        for (uint32_t o = 0; o < 4; ++o) outs[o] = Out{kDefault[o], kDefault[o]};
    }

    static double period_beats(uint32_t ratio) noexcept {
        const int n = kRatio[ratio];
        return n < 0 ? double(-n) : 1. / n;
    }
    static int64_t period_index(double position, uint32_t ratio) noexcept {
        const int n = kRatio[ratio];
        return n < 0 ? wsbeat::division_index(position, double(-n))
                     : static_cast<int64_t>(std::floor(position * n + 1e-9));
    }
    static bool ratio_of(float v, uint32_t &out) noexcept {
        if (!std::isfinite(v)) return false;
        out = static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 18.f)));
        return true;
    }

    // A period start: the gate fires, one sample late if the last sample was still high.
    void start(Out &o, double position, float width_value) noexcept {
        const double w = std::clamp(std::isfinite(width_value) ? double(width_value) : .5, double(.01f), double(.99f));
        const double beats = period_beats(o.ratio);
        // Samples per beat: measured (or the transport's); before the second edge, the beat clock's
        // estimate (120 BPM until measured), and the gate also ends where its width runs out in beats.
        const double period = beats * beat.samples_per_beat();
        const double cap = std::max(1., std::floor(period) - 1.);
        const uint32_t len = static_cast<uint32_t>(std::min(std::clamp(std::round(w * period), 1., cap), kMaxGate));
        o.end_beat = tempo_known() ? -1. : position + w * beats;
        if (o.high) {
            o.left = 0;
            o.delayed = len;
        } else {
            o.left = len;
            o.delayed = 0;
        }
    }
    bool tempo_known() const noexcept { return mode == 2 || edges >= 2; }

    void process(ws::Block &b) {
        for (uint32_t p = 0; p < 4; ++p) {
            b.out(p).set_channels(1);
            b.out(p).mark_constant(false);
        }
        const ws2_transport *t = b.transport();
        const bool connected = b.in(0).connected();
        for (uint32_t i = 0; i < b.frames(); ++i) {
            for (uint32_t o = 0; o < 4; ++o) ratio_of(b.param(o).value(0, i), outs[o].pending);
            const float width = b.param(4).value(0, i);
            const bool r = reset_in.process(b.in(1).value(0, i));
            const bool reset_edge = r && !reset_high;
            reset_high = r;
            const float clock = b.in(0).value(0, i);
            bool edge = false;
            if (connected) {  // the same Schmitt edges as the beat clock's
                const bool high = clock_in.process(clock);
                edge = high && !clock_high;
                clock_high = high;
            }
            const double now = beat.step(connected, clock, reset_edge, 1, t, i, rate);
            const uint32_t want = connected ? 1u : 2u;
            const bool entered = mode != want;
            mode = want;

            bool fire_all = false;
            double position = 0;
            if (connected) {
                if (entered) armed = true, edges = 0;  // (re)patched: re-measure the tempo
                if (reset_edge) armed = true;
                if (edge && edges < 2) ++edges;
                if (edge && armed) {
                    armed = false;
                    grid = now;
                    fire_all = true;
                }
                position = now - grid;
            } else {
                grid = 0;
                position = now;
                fire_all = reset_edge;  // a reset is beat 0: every output starts there
                if (entered && !reset_edge)
                    for (auto &o : outs) o.ratio = o.pending, o.index = period_index(position - 1e-6, o.ratio);
            }

            for (uint32_t k = 0; k < 4; ++k) {
                Out &o = outs[k];
                if (o.delayed) {  // the start delayed by one sample begins now
                    o.left = o.delayed;
                    o.delayed = 0;
                }
                if (o.end_beat >= 0 && o.left && position >= o.end_beat - 1e-9) o.left = 0;  // tempo unknown
                if (fire_all) {
                    o.ratio = o.pending;
                    o.index = period_index(position, o.ratio);
                    start(o, position, width);
                } else if (!(connected && armed)) {
                    const int64_t at = period_index(position, o.ratio);
                    if (at != o.index) {  // the latched period ended
                        o.index = at;
                        bool fires = true;
                        if (o.pending != o.ratio) {  // the change applies here, on its own grid
                            const int64_t was = period_index(previous_position_, o.pending);
                            o.ratio = o.pending;
                            o.index = period_index(position, o.ratio);
                            fires = o.index != was;
                        }
                        if (fires) start(o, position, width);
                    }
                }
                const bool on = o.left > 0;
                if (on) --o.left;
                o.high = on;
                b.out(k).ch(0)[i] = on ? 1.f : 0.f;
            }
            previous_position_ = position;
        }
    }

    // State v1, little-endian u32 words: the beat clock, the module fields, then the four outputs.
    static bool put64(ws::Writer w, uint64_t v) { return w.u32(uint32_t(v)) && w.u32(uint32_t(v >> 32)); }
    static bool get64(ws::Reader r, uint64_t &v) {
        uint32_t lo, hi;
        if (!r.u32(lo) || !r.u32(hi)) return false;
        v = uint64_t(lo) | (uint64_t(hi) << 32);
        return true;
    }
    static bool putd(ws::Writer w, double d) { uint64_t v; std::memcpy(&v, &d, 8); return put64(w, v); }
    static bool getd(ws::Reader r, double &d) { uint64_t v; if (!get64(r, v)) return false; std::memcpy(&d, &v, 8); return true; }
    int save_state(ws::Writer w) {
        const auto c = beat.save();
        bool ok = w.u32(1) && putd(w, c.base) && putd(w, c.beat) && putd(w, c.origin) && putd(w, c.held_fraction) &&
                  putd(w, c.samples_per_beat) && put64(w, c.since) && put64(w, c.period) && put64(w, c.edges) &&
                  w.u32(c.flags) && putd(w, grid) && putd(w, previous_position_) && w.u32(clock_high ? 1u : 0u) &&
                  w.u32(reset_high ? 1u : 0u) && w.u32(armed ? 1u : 0u) && w.u32(mode) && w.u32(edges);
        for (const auto &o : outs)
            ok = ok && w.u32(o.ratio) && w.u32(o.pending) && put64(w, uint64_t(o.index)) && w.u32(o.left) &&
                 w.u32(o.delayed) && putd(w, o.end_beat) && w.u32(o.high ? 1u : 0u);
        return ok ? WS2_OK : WS2_CAPACITY;
    }
    int load_state(ws::Reader r) {
        uint32_t version = 0;
        if (!r.u32(version)) return WS2_INVALID;
        if (version != 1) return WS2_UNSUPPORTED;
        wsbeat::BeatClock::State c{};
        double g, prev;
        uint32_t ch, rh, arm, m, e;
        if (!getd(r, c.base) || !getd(r, c.beat) || !getd(r, c.origin) || !getd(r, c.held_fraction) ||
            !getd(r, c.samples_per_beat) || !get64(r, c.since) || !get64(r, c.period) || !get64(r, c.edges) ||
            !r.u32(c.flags) || !getd(r, g) || !getd(r, prev) || !r.u32(ch) || !r.u32(rh) || !r.u32(arm) ||
            !r.u32(m) || !r.u32(e))
            return WS2_INVALID;
        if (!std::isfinite(g) || !std::isfinite(prev) || ch > 1 || rh > 1 || arm > 1 || m > 2 || e > 2)
            return WS2_INVALID;
        std::array<Out, 4> staged{};
        for (auto &o : staged) {
            uint64_t index;
            uint32_t high;
            if (!r.u32(o.ratio) || !r.u32(o.pending) || !get64(r, index) || !r.u32(o.left) || !r.u32(o.delayed) ||
                !getd(r, o.end_beat) || !r.u32(high))
                return WS2_INVALID;
            if (o.ratio > 18 || o.pending > 18 || high > 1 || !std::isfinite(o.end_beat) || o.end_beat < -1)
                return WS2_INVALID;
            o.index = int64_t(index);
            o.high = high != 0;
        }
        wsbeat::BeatClock clock;
        if (!clock.restore(c)) return WS2_INVALID;
        beat = clock;
        grid = g;
        previous_position_ = prev;
        clock_high = ch != 0;
        reset_high = rh != 0;
        armed = arm != 0;
        mode = m;
        edges = e;
        outs = staged;
        clock_in = reset_in = ws::Schmitt{};
        if (clock_high) clock_in.process(1.f);
        if (reset_high) reset_in.process(1.f);
        return WS2_OK;
    }

private:
    double previous_position_ = 0;  // the position at the last sample (for a ratio change's grid)
};

}  // namespace wsmod
