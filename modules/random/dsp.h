// SPDX-License-Identifier: GPL-3.0-only
// essentials.random (wirescape-test MOD-3, lab proposals/0024 §4.2): a new random value at every
// division boundary of the C2 beat, held (`stepped`), glided with an S-curve (`smooth`) and announced
// (`step`). Starts from lab sdk/examples/random_source (MIT): ws::Random per value, Schmitt edges.
#pragma once
#include "wirescape_sdk.h"
#include "beat_clock.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wsmod {

struct Random {
    static constexpr ws::Info info{"essentials.random", "Random", 1};

    // Mono in v1: the first channel of each input is used.
    static constexpr std::array inputs{
        ws::port("clock", "Clock", WS2_GATE, WS2_FIRST),
        ws::port("reset", "Reset", WS2_TRIGGER, WS2_FIRST)};
    static constexpr std::array outputs{
        ws::port("stepped", "Stepped", WS2_CV_BI),
        ws::port("smooth", "Smooth", WS2_CV_BI),
        ws::port("step", "Step", WS2_TRIGGER)};

    // Parameter IDs are API. division is STEPPED|MODULATABLE (it sets timing, not structure); seed and
    // clock_ppq are STEPPED only: they pick the sequence and the clock's meaning (0023 R3.3 rule 2).
    static constexpr std::array params{
        ws::param("division", "Division", 0, 17, 7, "", WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 0, 4, 1),
        ws::param("depth", "Depth", 0, 1, 1, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 1),
        ws::param("slew", "Slew", 0, 1, 1, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 2),
        ws::param("seed", "Seed", 0, 65535, 0, "", WS2_STEPPED | WS2_AUTOMATABLE, WS2_LINEAR, 3, 4, 1),
        ws::param("clock_ppq", "Clock PPQ", 1, 24, 1, "", WS2_STEPPED | WS2_AUTOMATABLE, WS2_LINEAR, 4, 4, 1)};

    wsbeat::BeatClock beat;
    ws::Random rng;
    ws::Schmitt reset_in;
    bool reset_high = false;
    bool started = false;          // false until the first value after a module reset
    int64_t index = INT64_MIN;     // division period of the last value (INT64_MIN: none yet)
    uint32_t seed_used = 0;        // the seed parameter the sequence started from
    float held = 0;                // current value in [-1, 1)
    float from = 0, to = 0;        // S-curve glide endpoints
    uint32_t glide_pos = 0, glide_len = 0, trig_left = 0;
    uint64_t instance_seed = 0;
    double rate = 48000;

    void prepare(const ws::Setup &s) {
        instance_seed = s.config.seed;
        rate = s.sample_rate();
        reset();
    }
    // C4: seed 0 uses the engine's per-instance seed; 1..65535 fixes the sequence.
    void reseed(uint32_t seed) noexcept {
        seed_used = seed;
        rng.seed(seed ? static_cast<uint64_t>(seed) : instance_seed, 0);
    }
    void reset() {
        beat.reset();
        reset_in = ws::Schmitt{};
        reset_high = false;
        started = false;
        index = INT64_MIN;
        held = from = to = 0;
        glide_pos = glide_len = trig_left = 0;
        reseed(seed_used);
    }
    static uint32_t seed_of(float v) noexcept {
        return std::isfinite(v) ? static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 65535.f))) : 0u;
    }

    void process(ws::Block &b) {
        for (uint32_t p = 0; p < 3; ++p) {
            b.out(p).set_channels(1);
            b.out(p).mark_constant(false);
        }
        float *stepped = b.out(0).ch(0), *smooth = b.out(1).ch(0), *step = b.out(2).ch(0);
        const ws2_transport *t = b.transport();
        const bool connected = b.in(0).connected();
        const uint32_t trig_samples = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(rate / 1000.)));
        for (uint32_t i = 0; i < b.frames(); ++i) {
            const uint32_t seed = seed_of(b.param(3).value(0, i));
            if (seed != seed_used) reseed(seed);
            const bool r = reset_in.process(b.in(1).value(0, i));
            const bool reset_edge = r && !reset_high;
            reset_high = r;
            if (reset_edge) {  // origin now, and the sequence restarts (seed reproducibility)
                reseed(seed);
                index = INT64_MIN;
            }
            const double length = wsbeat::division_beats(b.param(0).value(0, i));
            const double now = beat.step(connected, b.in(0).value(0, i), reset_edge, b.param(4).value(0, i),
                                         t, i, rate);
            const int64_t at = wsbeat::division_index(now, length);
            if (at != index) {  // a division boundary: a new value, a glide and a trigger
                index = at;
                held = rng.bipolar();
                from = started ? glide_value() : held;  // the power-on value neither glides nor fires
                to = held;
                const float slew = std::clamp(finite_or(b.param(2).value(0, i), 1.f), 0.f, 1.f);
                const double span = started ? slew * length * beat.samples_per_beat() : 0.;
                glide_len = static_cast<uint32_t>(std::min(span, 30. * rate));
                glide_pos = 0;
                if (started) trig_left = trig_samples;
                started = true;
            }
            if (glide_pos < glide_len) ++glide_pos;
            const float depth = std::clamp(finite_or(b.param(1).value(0, i), 1.f), 0.f, 1.f);
            stepped[i] = held * depth;
            smooth[i] = glide_value() * depth;
            step[i] = trig_left ? 1.f : 0.f;
            if (trig_left) --trig_left;
        }
    }
    static float finite_or(float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; }
    // S-curve (smoothstep) from `from` to `to` over glide_len samples; `to` once the glide is done.
    float glide_value() const noexcept {
        if (glide_pos >= glide_len) return to;
        const float u = static_cast<float>(glide_pos) / static_cast<float>(glide_len);
        return from + (to - from) * (u * u * (3.f - 2.f * u));
    }

    // State v1, little-endian: beat clock fields, Random state, then the module fields.
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
        const auto s = rng.save();
        const bool ok = w.u32(1) && putd(w, c.base) && putd(w, c.beat) && putd(w, c.origin) &&
                        putd(w, c.held_fraction) && putd(w, c.samples_per_beat) && put64(w, c.since) &&
                        put64(w, c.period) && put64(w, c.edges) && w.u32(c.flags) && put64(w, s.state) &&
                        put64(w, s.sequence) && put64(w, uint64_t(index)) && w.f32(held) && w.f32(from) &&
                        w.f32(to) && w.u32(glide_pos) && w.u32(glide_len) && w.u32(trig_left) &&
                        w.u32(seed_used) && w.u32(started ? 1u : 0u) && w.u32(reset_high ? 1u : 0u);
        return ok ? WS2_OK : WS2_CAPACITY;
    }
    int load_state(ws::Reader r) {
        uint32_t version = 0;
        if (!r.u32(version)) return WS2_INVALID;
        if (version != 1) return WS2_UNSUPPORTED;
        wsbeat::BeatClock::State c{};
        ws::Random::State s{};
        uint64_t at = 0;
        float h, f, to_value;
        uint32_t pos, len, trig, seed, began, high;
        if (!getd(r, c.base) || !getd(r, c.beat) || !getd(r, c.origin) || !getd(r, c.held_fraction) ||
            !getd(r, c.samples_per_beat) || !get64(r, c.since) || !get64(r, c.period) || !get64(r, c.edges) ||
            !r.u32(c.flags) || !get64(r, s.state) || !get64(r, s.sequence) || !get64(r, at) || !r.f32(h) ||
            !r.f32(f) || !r.f32(to_value) || !r.u32(pos) || !r.u32(len) || !r.u32(trig) || !r.u32(seed) ||
            !r.u32(began) || !r.u32(high))
            return WS2_INVALID;
        const auto bounded = [](float x) { return std::isfinite(x) && std::abs(x) <= 1.f; };
        if (!bounded(h) || !bounded(f) || !bounded(to_value) || pos > len || len > 30 * 384000u ||
            trig > 384 || seed > 65535 || began > 1 || high > 1 || (s.sequence & 1) != 1)
            return WS2_INVALID;
        wsbeat::BeatClock clock;
        if (!clock.restore(c)) return WS2_INVALID;
        beat = clock;
        rng.restore(s);
        index = int64_t(at);
        held = h; from = f; to = to_value;
        glide_pos = pos; glide_len = len; trig_left = trig; seed_used = seed;
        started = began != 0;
        reset_high = high != 0;
        reset_in = ws::Schmitt{};
        if (reset_high) reset_in.process(1.f);
        return WS2_OK;
    }
};

}  // namespace wsmod
