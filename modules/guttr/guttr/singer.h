// SPDX-License-Identifier: GPL-3.0-only
// One GUTTR throat (owner's GUTTUR package, src/internal.h and src/engine.cpp): LF pulse source with
// closure-time jitter, shimmer, growl, creak, fry and register breaks; breath, rasp and flutter noise;
// ten parallel resonators; pitch-following high-pass; anti-aliased drive; distance, pan, ITD and head
// shadow. `channel` is the voice channel of the note that owns the throat: its per-voice parameters.
#pragma once
#include "tables.h"

namespace wsmod::guttr {

struct Loose { double lag, slip, scoop, settle, detune, skip, coupling, vowel, level; };
inline Loose loose_table(double L) noexcept {
    return {lerp(6, 116, L), lerp(2, 47, L), lerp(6, 76, L), lerp(.06, .28, L), lerp(2, 22, L), lerp(0, .12, L),
            lerp(.5, 0, L), lerp(.02, .12, L), lerp(.5, 3.5, L)};
}

struct Singer {
    bool active = false, voicing = false, gate = false, closureFirst = false, coefInit = false, hpInit = false,
         driveOn = false, carrier = true, farR = false;
    int slot = 0, index = 0, midi = 60, group = 1, lod = NRES, first = 0, breakN = 0, parity = 0, gap = 0, lev = 0,
        rdA = 0, tabRd = 0;
    uint8_t channel = 0, nextChannel = 0;
    // 0: none; 1: marked for a 128-frame fade at the next control tick; 2: fading in this tick.
    uint8_t killFade = 0;
    double breakR = 1;
    int64_t ownerId = 0;
    uint64_t seq = 0;
    uint32_t rng = 1, noiseRng = 1, cycN = 0, itdW = 0, silentBlocks = 0, driveFadeLeft = 0;
    double pendingAt = -1, pendingStop = -1, pendingScoop = 0, pendingSettle = .1, pendingLevel = 1;
    double L = 17.5, az = 0, dist = .15, lagMs = 0, detune = 0, levelDb = 0, vowelOff = 0, coupling = .5, looseWander = 0;
    double baseHz = 261.6256, fLow = 92, fCur = 110, scoop = 0, scoopTau = .1, style = .45, pT = 0, p = 0, V = 0, pLevel = 0;
    double trem[3]{}, tremR[3]{}, vibPh = 0, wanderS = 0, wanderV = 0;
    double ph = 0, inc = 0, amp = 0, ampT = 0, rdW = 0, rdNow = 1.2, raspC = 0, reg = 0;
    double noiseA = 0, noiseAT = 0, noiseStep = 0, breathFloor = .12, asp1 = 0, fl1 = 0, fl2 = 0, flg = 0, flk = 0, flNorm = 0, flDepth = 0;
    double norm = 1, normT = 1, lnF = 0, lnRd = 0, lnL = 0;
    double ic1[NRES]{}, ic2[NRES]{}, ca1[NRES]{}, ca2[NRES]{}, ca3[NRES]{}, ta1[NRES]{}, ta2[NRES]{}, ta3[NRES]{}, ca[NRES]{}, ta[NRES]{};
    double Fbuf[NRES]{}, Abuf[NRES]{};
    double cStep1[NRES]{}, cStep2[NRES]{}, cStep3[NRES]{}, aStep[NRES]{};
    double hp1 = 0, hp2 = 0, hp_1 = 0, hp_2 = 0, hp_3 = 0, hpT1 = 0, hpT2 = 0, hpT3 = 0, hpS1 = 0, hpS2 = 0, hpS3 = 0;
    double driveT = 0, xPrev = 0, adPrev = 0, gainT = 0, gainCur = 0, gainStep = 0, gainExtra = 1;
    double gLT = 0, gRT = 0, gL = 0, gR = 0, gLStep = 0, gRStep = 0, sendT = 0, revS = 0, sendStep = 0;
    bool spaceInit = false;
    double dlp = 0, flp = 0, dlpC = 0, shadowC = 0, itdT = 0, pdel = 0, itdStep = 0;
    std::array<float, 128> itd{};
    double energy = 0;

    void reset(int singerSlot) noexcept {
        *this = Singer{};
        slot = singerSlot;
        rng = hash(slot, 991, 7, 3) | 1u;
        noiseRng = rng;
    }
    // Placement of follower `index` in a group of `group` (deliberately reproduces GUTTUR play-mode).
    void place(const Params &P, uint32_t placeSeed) noexcept {
        const auto lt = loose_table(get(P, looseness));
        const int32_t sid = int32_t(placeSeed);
        L = clamp(get(P, size) + (index == 0 ? 0 : h11(sid, 11, 0, 0) * 1.6 * get(P, size_spread)), 13, 20.5);
        fLow = 92 * std::pow(L_REF / L, 1.6);
        detune = index == 0 ? 0 : h11(sid, 12, 0, 0) * lt.detune;
        levelDb = index == 0 ? 0 : -h01(sid, 13, 0, 0) * lt.level;
        vowelOff = index == 0 ? 0 : h11(sid, 14, 0, 0) * lt.vowel;
        const double spreadDeg = get(P, spread) * 70;
        az = clamp(get(P, azimuth) + (group > 1 ? (double(index) / (group - 1) - .5) * 2 * spreadDeg : 0) +
                       h11(sid, 15, 0, 0) * 6 * get(P, spread), -180, 180);
        dist = clamp(get(P, distance) + h01(sid, 16, 0, 0) * .12 * get(P, spread), 0, 1);
        lagMs = index == 0 ? 0 : lt.lag * (.35 + .65 * h01(sid, 17, 0, 0));
        carrier = index < 3 || group <= 3;
        lod = (dist > .55 && index > 0) ? 6 : NRES;
        coupling = lt.coupling;
        looseWander = lt.detune * .3;
    }
    void assign(int64_t id, int singerIndex, int groupSize, int note, double velocity, uint32_t placeSeed, const Params &P) noexcept {
        ownerId = id; index = singerIndex; group = groupSize; midi = note; seq = placeSeed;
        baseHz = hz(note);
        fCur = baseHz;
        place(P, placeSeed);
        const int32_t sid = int32_t(placeSeed);
        for (int i = 0; i < 3; i++) trem[i] = h01(sid, i + 1, 0, 0) * TAU;
        tremR[0] = 4.1 + h01(sid, 4, 0, 0) * .8;
        tremR[1] = 5 + h01(sid, 5, 0, 0) * .7;
        tremR[2] = 5.9 + h01(sid, 6, 0, 0) * .9;
        vibPh = h01(sid, 7, 0, 0) * TAU;
        gainExtra = 1; style = get(P, Param::style); active = true; gate = false; voicing = false; p = 0; pT = 0;
        pendingLevel = velocity;
    }
    void start(double level, double scoop_, double settle) noexcept {
        gate = true; pT = level; pendingLevel = level;
        scoop = -scoop_;
        scoopTau = std::max(.02, settle / 2.3);
        if (!voicing) fCur = baseHz * std::pow(2, (detune - scoop_) / 1200.);
        first = voicing ? 0 : 2;
        active = true;
        silentBlocks = 0;
    }
    void stop() noexcept { gate = false; pT = 0; }
    // Ramps stop where they are: a fading throat keeps its last coefficients (the package kept
    // extrapolating the previous tick's ramps during the fade).
    void hold_ramps() noexcept {
        for (int k = 0; k < NRES; ++k) cStep1[k] = cStep2[k] = cStep3[k] = aStep[k] = 0;
        hpS1 = hpS2 = hpS3 = noiseStep = gLStep = gRStep = sendStep = itdStep = 0;
    }
    // One glottal closure: the next cycle's period, amplitude, Rd, mip level, gap and rasp.
    void closure(double sr, double sharedJit, bool gritAfter, const Params &P, const Calibration &cal, const Pulse &pt) noexcept {
        const double f = std::max(1., fCur), regNew = clamp(std::log2(f / fLow) / 2.5, 0, 1);
        reg = regNew;
        parity ^= 1;
        const double grit = gritAfter ? 0 : 1;
        const double growlv = get(P, growl) * grit, lowG = 1 - smooth(.2, .48, reg), dbl = growlv * lowG, gHi = growlv * (1 - lowG);
        const double releaseFry = (1 - V) * (gate ? .15 : 1) * get(P, tail_fry);
        const double creakv = clamp(get(P, creak) + releaseFry, 0, 1) * grit;
        const double lowC = 1 - smooth(.1, .38, reg), cr = creakv * lowC, crackP = creakv * (1 - lowC) * .05;
        const double c = coupling, sj = get(P, jitter) * grit * .01 * cal.jitter;
        double jit = sj * (c * sharedJit + std::sqrt(std::max(0., 1 - c * c)) * gauss(rng));
        jit += gHi * .012 * gauss(rng);
        double per = sr / f * (1 + clamp(jit, -.25, .25));
        const double ss = get(P, shimmer) * grit * .01 * cal.shimmer + gHi * .06;
        double a = 1 + ss * gauss(rng);
        if (dbl > 0) {
            if (parity) { a *= 1 - .62 * dbl; per *= 1 + .09 * dbl; }
            else per *= 1 - .09 * dbl;
        }
        double pulse = per, gapVal = 0;
        if (cr > 0) {
            pulse = per / (1 + 1.3 * cr);
            gapVal = per - pulse;
            if (parity) { gapVal += .9 * cr * per * (.45 + .55 * rnd(rng)); a *= 1 - .55 * cr * (.4 + .6 * rnd(rng)); }
            if (rnd(rng) < .08 * cr) a *= .08;
        }
        if (crackP > 0 && rnd(rng) < crackP) {
            if (rnd(rng) < .5) gapVal += per * (.3 + .6 * rnd(rng));
            else a *= .3;
            jit += .02 * gauss(rng);
        }
        if (breakN > 0) { --breakN; pulse /= breakR; gapVal /= breakR; a *= .55; }
        else if (get(P, breaks) > 0 && grit) {
            const double near = .25 + std::exp(-std::pow((reg - .55) / .12, 2));
            if (rnd(rng) < get(P, breaks) * .004 * near) { breakN = 2 + int(rnd(rng) * 6); breakR = rnd(rng) < .6 ? 1.5 : 2; }
        }
        const double voiced = V * (1 - get(P, whisper));
        double boost = 1;
        if (first > 0) { boost = 1 + .5 * smooth(.6, 1, style); first--; }
        ampT = std::max(0., a) * voiced * pLevel * boost * norm * (1 + cr);
        double rd = lerp(2.3, .62, get(P, pressure)) + get(P, breath) * .7 - cr * .55 - dbl * .25 - (boost - 1) * .8;
        if (breakN > 0) rd += .6;
        rd = clamp(rd + .05 * std::min(1., ss * 12) * gauss(rng), RD_MIN, RD_MAX - 1e-6);
        const double rf = (rd - RD_MIN) / RD_STEP;
        rdA = int(rf); rdW = rf - rdA; tabRd = rdW < .5 ? rdA : rdA + 1; rdNow = rd;
        const double fp = sr / std::max(pulse, 1.), lim = .475 * sr / fp;
        lev = 0;
        while (lev < NLEV - 1 && pt.hmax[lev] > lim) lev++;
        inc = 1 / std::max(pulse, 1.);
        gap = int(std::floor(gapVal + .5));
        raspC = (get(P, rasp) * grit + gHi * .5 + dbl * .25) * (.5 + rnd(rng));
        cycN++;
        if (voiced <= .002 && !gate) voicing = false;
    }
};

}  // namespace wsmod::guttr
