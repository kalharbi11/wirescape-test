// SPDX-License-Identifier: GPL-3.0-only OR MIT
// beat_clock.h: the shared beat clock (lab proposals/0024 §4.0 C2) and division table (C3).
// Dual-licensed: GPL-3.0-only for the lab modules tree (wsmod C2), MIT for the lab SDK (0024 §4.0 C6).
// Written for essentials.random (wirescape-test MOD-3). Clock Div/Mult copies it, and SDK-1 moves it
// to the lab SDK unchanged in behaviour (§4.0 C6). Header-only, no allocation, no OS calls.
// A named guard, not #pragma once: identical copies in two module folders (MOD-4) and the later SDK
// header must collapse to one definition when one catalogue includes them all.
#ifndef WSBEAT_BEAT_CLOCK_H
#define WSBEAT_BEAT_CLOCK_H
#include "wirescape_sdk.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wsbeat {

// C3: divisions in beats, slowest first (index 0..17). A bar is 4 beats until the transport carries a
// time signature.
inline constexpr double kDivisionBeats[18] = {
    16, 8, 4, 3, 2, 1.5, 4. / 3, 1, .75, 2. / 3, .5, .375, 1. / 3, .25, .1875, 1. / 6, .125, 1. / 12};
inline double division_beats(float index) noexcept {
    const float i = std::isfinite(index) ? std::clamp(index, 0.f, 17.f) : 7.f;
    return kDivisionBeats[static_cast<int>(std::lround(i))];
}

// Index of the division period that contains `beat`. The tiny bias keeps a beat that is an exact
// multiple of a non-binary division (1/3, 1/6, ...) on the boundary despite rounding.
inline int64_t division_index(double beat, double length) noexcept {
    return static_cast<int64_t>(std::floor(beat / length + 1e-9));
}

// C2 beat clock, one per voice. Call step() once per sample.
// - With a clock cable (`connected`): one beat per `ppq` rising edges (Schmitt .5/.25). After two
//   edges, the beat is interpolated from the last measured period between edges, but it never reaches
//   the next edge's beat before that edge arrives. Before the second edge (and after a hold) it moves
//   only at edges. After two periods with no edge it holds, and the period is measured afresh.
// - Otherwise the transport drives it: locked to transport.beat per sample while playing, and
//   free-running at transport.bpm (120 without a transport) from its last position while stopped.
//   While playing, beat = transport beat - origin; the origin is the transport beat at the last reset
//   (0 until the first reset).
// - A rising `reset` sets the beat origin to now; a clock edge in the same sample is that origin.
class BeatClock {
public:
    // Just below one edge interval, so interpolation never touches the next edge's beat.
    static constexpr double kMaxFraction = 1. - 1. / 1048576.;

    void reset() noexcept { *this = BeatClock{}; }

    // Returns the beat after this sample. `reset_edge` is true on the sample of a rising reset.
    double step(bool connected, float clock, bool reset_edge, float ppq, const ws2_transport *t,
                uint32_t frame, double rate) noexcept {
        const double per_edge = 1. / std::clamp(std::isfinite(ppq) ? static_cast<double>(ppq) : 1., .5, 96.);
        if (connected != connected_) {  // switching source keeps the beat continuous
            connected_ = connected;
            base_ = beat_;
            since_ = period_ = 0;
            held_ = false;
        }
        if (reset_edge) {
            base_ = beat_ = 0;
            origin_ = transport_beat(t, frame, rate);
        }
        if (connected) {
            const bool high = schmitt_.process(clock);
            const bool edge = high && !high_;
            high_ = high;
            if (since_ < UINT64_MAX) ++since_;
            if (edge) {
                // The period is the time between two edges with no hold in between.
                period_ = (edges_ > 0 && !held_) ? since_ : 0;
                if (!reset_edge) base_ += per_edge;
                since_ = 0;
                held_ = false;
                ++edges_;
            } else if (period_ && since_ >= 2 * period_) {
                held_ = true;
            }
            double fraction = 0;
            if (held_) fraction = held_fraction_;
            else if (period_) fraction = std::min(static_cast<double>(since_) / static_cast<double>(period_), kMaxFraction);
            held_fraction_ = fraction;
            beat_ = base_ + fraction * per_edge;
            samples_per_beat_ = period_ ? static_cast<double>(period_) / per_edge : samples_per_beat_;
        } else {
            const bool playing = t && (t->flags & WS2_PLAYING) && t->bpm > 0;
            const double bpm = t && t->bpm > 0 ? t->bpm : 120.;
            if (playing) beat_ = transport_beat(t, frame, rate) - origin_;
            else if (!reset_edge) beat_ += bpm / (60. * rate);
            samples_per_beat_ = 60. * rate / bpm;
        }
        return beat_;
    }

    double beat() const noexcept { return beat_; }
    // The current tempo estimate in samples per beat (the measured clock, else the transport).
    double samples_per_beat() const noexcept { return samples_per_beat_; }
    bool held() const noexcept { return held_; }

    // Portable state: a fixed list of fields for save/load (the owner writes them little-endian).
    struct State {
        double base, beat, origin, held_fraction, samples_per_beat;
        uint64_t since, period, edges;
        uint32_t flags;  // bit 0 connected, 1 high, 2 held, 3 Schmitt latch
    };
    State save() const noexcept {
        ws::Schmitt probe = schmitt_;  // probe the latch inside the hysteresis band
        const bool latch = probe.process(.375f);
        return {base_, beat_, origin_, held_fraction_, samples_per_beat_, since_, period_, edges_,
                (connected_ ? 1u : 0u) | (high_ ? 2u : 0u) | (held_ ? 4u : 0u) | (latch ? 8u : 0u)};
    }
    // Validates before committing; returns false and changes nothing on a bad state.
    bool restore(const State &s) noexcept {
        const bool finite = std::isfinite(s.base) && std::isfinite(s.beat) && std::isfinite(s.origin) &&
                            std::isfinite(s.held_fraction) && std::isfinite(s.samples_per_beat);
        if (!finite || s.flags > 15 || s.held_fraction < 0 || s.held_fraction > kMaxFraction ||
            s.samples_per_beat <= 0)
            return false;
        BeatClock c;
        c.base_ = s.base; c.beat_ = s.beat; c.origin_ = s.origin; c.held_fraction_ = s.held_fraction;
        c.samples_per_beat_ = s.samples_per_beat; c.since_ = s.since; c.period_ = s.period; c.edges_ = s.edges;
        c.connected_ = s.flags & 1u; c.high_ = (s.flags & 2u) != 0; c.held_ = (s.flags & 4u) != 0;
        if (s.flags & 8u) c.schmitt_.process(1.f);
        *this = c;
        return true;
    }

private:
    // The transport's beat at this sample (it advances only while playing); 0 without a transport.
    static double transport_beat(const ws2_transport *t, uint32_t frame, double rate) noexcept {
        if (!t || !std::isfinite(t->beat)) return 0;
        const bool playing = (t->flags & WS2_PLAYING) && t->bpm > 0;
        return t->beat + (playing ? frame * t->bpm / (60. * rate) : 0.);
    }
    double base_ = 0, beat_ = 0, origin_ = 0, held_fraction_ = 0, samples_per_beat_ = 24000;
    uint64_t since_ = 0, period_ = 0, edges_ = 0;
    bool connected_ = false, high_ = false, held_ = false;
    ws::Schmitt schmitt_;
};

}  // namespace wsbeat
#endif  // WSBEAT_BEAT_CLOCK_H
