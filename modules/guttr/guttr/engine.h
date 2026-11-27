// SPDX-License-Identifier: GPL-3.0-only
// GUTTR core (owner's GUTTUR package, src/engine.cpp), restructured for the Wirescape SDK: one block of
// prepared memory, per-channel parameter snapshots for per-voice controls, a fixed 128-frame control
// tick that ignores host block size, singer-major rendering inside a tick, and an idle bus.
#pragma once
#include "room.h"
#include "singer.h"

namespace wsmod::guttr {

// Where everything lives inside the one WS2_LARGE allocation; a pure function of the sample rate.
struct Layout {
    uint32_t line_len = 0, pre_len = 0;
    size_t lines = 0, pre = 0, scratch = 0, total = 0;
    static constexpr size_t align = 64;
    static size_t up(size_t x) noexcept { return (x + align - 1) / align * align; }
    static Layout of(double rate) noexcept;
};

struct Core {
    struct Note {
        int64_t id = 0;  // 0: free record
        uint32_t source = 0;
        int32_t note_id = -1, key = 0;
        uint8_t channel = 0;
        uint64_t seq = 0;
        int size = 0;
        bool released = false;
    };

    double sr = 48000;
    uint64_t seed = 17, frame = 0;
    uint32_t ctlLeft = 0;
    Pulse pulses{};
    Tube grid{};
    Room room{};
    Calibration cal{};
    // G: every parameter at channel 0 (globals come from here). chP[c]: per-voice values from voice
    // channel c with the globals copied in; sampled at control ticks for the channels in use.
    Params G{};
    std::array<Params, kChannels> chP{};
    std::array<bool, kChannels> chChanged{};
    bool globalChanged = false;
    uint32_t usedMask = 0;
    std::array<Singer, kMaxThroats> singers{};
    std::array<Note, kMaxNotes> notes{};
    uint64_t noteSeq = 0;
    int64_t nextId = 1;
    double time = 0, sjT = 0, sjA = 0, sjB = 0, sharedJit = 0, lnRef = 0;
    uint32_t sjN = 1;
    std::array<Biquad, 2> lowSh{}, highSh{}, lmEq{};
    Biquad scLM{}, scPR{};
    double eLM = 1e-9, ePR = 1e-9, lmCut = 0, lmC = 0, lmCStep = 0, lmRatio = 0, dc[4]{}, limG = 1;
    std::array<double, 1024> limBL{}, limBR{}, limQv{};
    std::array<int64_t, 1024> limQ{};
    uint32_t limW = 0, limLook = 120;
    int64_t limIdx = 0, limQh = 0, limQt = 0;
    bool gritAfter = false;
    double ampSmooth = 0, lowMidEnvelopeCoeff = 0, limiterReleaseCoeff = 0, limiterAttackCoeff = 0;
    double masterGain = 1, masterStep = 0, wetGain = .392, wetStep = 0, gritPre = 1, gritTrim = 1;
    double roomSize = -1, roomDecay = -1, body = 999, air = 999;
    // Idle bus: with no throat active and the bus below -140 dBFS for `idleNeeded` ticks, the bus memory
    // is cleared and segments write exact zeros until a throat starts.
    bool busIdle = true;
    uint32_t idleTicks = 0, idleNeeded = 64;
    double busPeak = 0;
    double dryL[kTick]{}, dryR[kTick]{}, send[kTick]{};

    // ------------------------------------------------------------------ prepare and reset
    // `memory` holds Layout::of(rate).total bytes (64-byte aligned); the Core sits at its start.
    void prepare(double rate, uint64_t seed_, unsigned char *memory, const Layout &layout) noexcept {
        sr = rate;
        seed = seed_;
        for (uint32_t k = 0; k < kParamCount; ++k) G[k] = kParams[k].initial;
        for (auto &p : chP) p = G;
        auto *scratch = reinterpret_cast<double *>(memory + layout.scratch);
        pulses.build(scratch, scratch + M, scratch + 2 * M, scratch + 2 * M + MAX_LEVEL);
        grid.build();
        lnRef = expected_power(160, L_REF, 1.2);
        room.prepare(rate, reinterpret_cast<float *>(memory + layout.lines), layout.line_len,
                     reinterpret_cast<float *>(memory + layout.pre), layout.pre_len);
        scLM.set(Biquad::BandPass, 280, .6, 0, rate);
        scPR.set(Biquad::BandPass, 2000, .55, 0, rate);
        for (int i = 0; i < 2; i++) lmEq[i].set(Biquad::BandPass, 310, .8, 0, rate);
        limLook = uint32_t(std::floor(.0025 * rate + .5));
        ampSmooth = 1 - std::exp(-1 / (.0015 * rate));
        lowMidEnvelopeCoeff = 1 - std::exp(-1 / (.35 * rate));
        limiterReleaseCoeff = 1 - std::exp(-1 / (.08 * rate));
        limiterAttackCoeff = 1 - std::exp(-1 / (limLook * .25));
        idleNeeded = (room.len + limLook) / kTick + 4;
        set_tone();
        reset();
    }
    void reset() noexcept {
        for (int i = 0; i < kMaxThroats; i++) singers[i].reset(i);
        notes = {};
        noteSeq = 0;
        nextId = 1;
        room.reset();
        clear_bus();
        frame = 0; time = 0; ctlLeft = 0; sjT = sjA = sjB = sharedJit = 0; sjN = 1;
        lmCut = lmC = lmCStep = lmRatio = 0;
        masterGain = db(get(G, level)); masterStep = 0;
        wetGain = get(G, wet) * 1.4; wetStep = 0;
        usedMask = 0;
        chChanged.fill(false);
        globalChanged = false;
    }
    void clear_bus() noexcept {
        room.clear();
        for (int i = 0; i < 2; i++) { lowSh[i].reset(); highSh[i].reset(); lmEq[i].reset(); }
        scLM.reset(); scPR.reset();
        eLM = ePR = 1e-9;
        std::fill_n(dc, 4, 0.);
        limBL.fill(0); limBR.fill(0); limQ.fill(0); limQv.fill(0);
        limW = 0; limIdx = limQh = limQt = 0; limG = 1;
        busIdle = true; idleTicks = 0; busPeak = 0;
    }

    double expected_power(double f0, double L, double rd) const noexcept {
        double F[NRES]{}, A[NRES]{}, B[NRES]{};
        const double minA = grid.lookup(.5, 0, .5, .3, F, A), sc = L_REF / L;
        const double rf = clamp((rd - RD_MIN) / RD_STEP, 0, NR - 1.001);
        const int r0 = int(rf);
        const double rw = rf - r0;
        for (int k = 0; k < NRES; k++) { F[k] *= sc; B[k] = bandwidth(F[k], k, rd, minA); }
        double P = 0;
        for (int h = 1; h <= NH; h++) {
            const double f = h * f0;
            if (f > 7000 || f > .45 * sr) break;
            const double c = (1 - rw) * pulses.harm[r0 * NH + h - 1] + rw * pulses.harm[(r0 + 1) * NH + h - 1];
            double hr = 0, hi = 0;
            for (int k = 0; k < NRES; k++) {
                const double wk = F[k], q = B[k], dr = wk * wk - f * f, di = f * q, den = dr * dr + di * di, g = A[k] * wk * wk / den;
                hr += g * dr;
                hi -= g * di;
            }
            const double f2 = f * f, kw = (1 + 1.5119 * f2 / (f2 + 2.25e6)) * (f2 * f2 / (f2 * f2 + 2.1e6));
            P += c * c * (hr * hr + hi * hi) * kw;
        }
        return P;
    }
    void set_tone() noexcept {
        const double b = get(G, Param::body), a = get(G, Param::air);
        if (b != body || a != air) {
            for (int i = 0; i < 2; i++) { lowSh[i].set(Biquad::LowShelf, 240, .7, b, sr); highSh[i].set(Biquad::HighShelf, 2400, .7, a, sr); }
            body = b; air = a;
        }
        const double rs = get(G, Param::room), rd = get(G, Param::decay);
        if (rs != roomSize || rd != roomDecay) { room.set(rs, rd); roomSize = rs; roomDecay = rd; }
        gritAfter = get(G, grit_after) > .5;
    }

    // ------------------------------------------------------------------ parameters
    // Channels whose snapshot the next tick must refresh: owners of active or fading throats.
    uint32_t channels_in_use() const noexcept {
        uint32_t mask = 0;
        for (const auto &s : singers)
            if (s.active) mask |= (1u << s.channel) | (s.killFade ? 1u << s.nextChannel : 0u);
        for (const auto &n : notes)
            if (n.id) mask |= 1u << n.channel;
        return mask;
    }
    // read(k, c) returns parameter k on voice channel c at the tick frame (already sanitised).
    template <class Read> void sample_parameters(Read read) noexcept {
        globalChanged = false;
        for (uint32_t k = 0; k < kParamCount; ++k) {
            const double v = read(k, 0u);
            if (!kParams[k].per_voice && v != G[k]) globalChanged = true;
            G[k] = v;
        }
        usedMask = channels_in_use();
        for (uint32_t c = 0; c < kChannels; ++c) {
            chChanged[c] = false;
            if (!(usedMask & (1u << c))) continue;
            auto &P = chP[c];
            for (uint32_t k = 0; k < kParamCount; ++k) {
                const double v = kParams[k].per_voice ? (c ? read(k, c) : G[k]) : G[k];
                if (v != P[k]) chChanged[c] = true;
                P[k] = v;
            }
        }
    }

    // ------------------------------------------------------------------ notes
    Note *find_note(int64_t id) noexcept {
        if (!id) return nullptr;
        for (auto &n : notes)
            if (n.id == id) return &n;
        return nullptr;
    }
    bool has_note(int64_t id) const noexcept {
        if (!id) return false;
        for (const auto &n : notes)
            if (n.id == id) return true;
        return false;
    }
    void release_note(int64_t id) noexcept {
        for (auto &n : notes)
            if (id && n.id == id && !n.released) {
                n.released = true;
                for (auto &s : singers)
                    if (s.ownerId == id) {
                        const double lag = s.index == 0 ? 0 : s.lagMs * .6 / 1000;
                        if (s.pendingAt >= 0) s.pendingStop = time + lag + .03;
                        else s.pendingStop = time + lag;
                    }
            }
    }
    // A choked note's throats fade over one control tick and are not reassigned.
    void orphan(int64_t id) noexcept {
        for (auto &s : singers)
            if (id && s.ownerId == id) {
                if (!s.killFade) s.killFade = 1;
                s.ownerId = 0;
                s.pendingAt = -2;
                s.pendingStop = -1;
            }
    }
    void choke_note(int64_t id) noexcept {
        orphan(id);
        if (auto *n = find_note(id)) *n = {};
    }
    // Bounded internal group allocation: up to 16 note records and 16 throats (old engine addNote).
    bool add_note(uint32_t source, int32_t note_id, uint8_t channel, int key, double vel, const Params &desired) noexcept {
        if (!(usedMask & (1u << channel))) {  // an idle channel's snapshot starts at this note's values
            chP[channel] = desired;
            usedMask |= 1u << channel;
        }
        Note *target = nullptr;
        for (auto &n : notes)
            if (!n.id) { target = &n; break; }
        if (!target)  // steal a record: the oldest released note, else the oldest held one
            for (int pass = 0; pass < 2 && !target; ++pass)
                for (auto &n : notes)
                    if ((pass || n.released) && (!target || n.seq < target->seq)) target = &n;
        if (!target) return false;
        if (target->id) { orphan(target->id); *target = {}; }
        const int G_ = std::clamp(int(std::lround(get(desired, throats))), 1, kMaxThroatsPerNote);
        const int64_t id = nextId++;
        *target = {id, source, note_id, key, channel, ++noteSeq, G_, false};
        const auto lt = loose_table(get(desired, looseness));
        std::array<bool, kMaxThroats> chosen{};
        for (int i = 0; i < G_; i++) {
            Singer *s = nullptr;
            for (auto &x : singers)
                if (!chosen[x.slot] && !x.active && x.ownerId == 0 && !x.killFade) { s = &x; break; }
            if (!s)  // a throat already fading out with no owner
                for (auto &x : singers)
                    if (!chosen[x.slot] && x.killFade && x.ownerId == 0) { s = &x; break; }
            if (!s) {  // steal: released notes first, then the oldest
                uint64_t oldest = UINT64_MAX;
                for (auto &x : singers)
                    if (!chosen[x.slot] && x.ownerId) {
                        const Note *own = find_note(x.ownerId);
                        const uint64_t priority = (own && own->released ? 0ull : 1ull) << 62 | (own ? own->seq : 0);
                        if (priority < oldest) { oldest = priority; s = &x; }
                    }
            }
            if (!s) break;
            chosen[s->slot] = true;
            if (s->ownerId) { if (!s->killFade) s->killFade = 1; s->pendingStop = -1; }  // fade, then replace
            const int slot = s->slot;
            const uint32_t oldrng = s->rng;
            if (!s->killFade) { s->reset(slot); s->rng = oldrng; }
            s->ownerId = id;
            if (s->killFade) {
                s->pendingAt = -2; s->seq = target->seq; s->midi = key; s->index = i; s->group = G_;
                s->pendingLevel = vel; s->nextChannel = channel;
            } else {
                const uint32_t placeSeed = uint32_t(seed * 131 + uint64_t(i) * 7);
                s->channel = s->nextChannel = channel;
                s->assign(id, i, G_, key, vel, placeSeed, desired);
                s->gainExtra = std::pow(G_, -(.44 + .08 * (1 - get(desired, looseness))));
                const double slip = i == 0 ? 0 : h11(int32_t(target->seq), i, 21, 0) * lt.slip;
                s->pendingAt = time + std::max(0., (s->lagMs + slip) / 1000.);
                s->pendingScoop = i == 0 ? lt.scoop * .3 : lt.scoop * (.4 + .6 * h01(int32_t(target->seq), i, 22, 0));
                s->pendingSettle = lt.settle;
            }
        }
        return true;
    }
    void finish_steal(Singer &s) noexcept {
        const int slot = s.slot, index = s.index, midi = s.midi, group = s.group;
        const uint64_t sq = s.seq;
        const int64_t id = s.ownerId;
        const double vel = s.pendingLevel;
        const uint8_t channel = s.nextChannel;
        s.reset(slot);
        if (!has_note(id)) return;
        const Params &P = chP[channel];
        const auto lt = loose_table(get(P, looseness));
        const uint32_t placeSeed = uint32_t(seed * 131 + uint64_t(index) * 7);
        s.channel = s.nextChannel = channel;
        s.assign(id, index, group, midi, vel, placeSeed, P);
        s.gainExtra = std::pow(group, -(.44 + .08 * (1 - get(P, looseness))));
        const double slip = index == 0 ? 0 : h11(int32_t(sq), index, 21, 0) * lt.slip;
        s.pendingAt = time + std::max(0., (s.lagMs + slip) / 1000.);
        s.pendingScoop = lt.scoop * .5;
        s.pendingSettle = lt.settle;
        for (const auto &n : notes)
            if (n.id == id && n.released) { s.pendingStop = s.pendingAt + .05; break; }
    }

    // ------------------------------------------------------------------ control tick
    void control() noexcept {
        constexpr int n = int(kTick);
        time = double(frame) / sr;
        if (globalChanged) set_tone();
        for (auto &s : singers)
            if (s.active && s.ownerId && !s.killFade && (globalChanged || chChanged[s.channel])) {
                const Params &P = chP[s.channel];
                s.place(P, uint32_t(seed * 131 + uint64_t(s.index) * 7));
                s.gainExtra = std::pow(s.group, -(.44 + .08 * (1 - get(P, looseness))));
            }
        const double dt = n / sr;
        sjT += dt;
        if (sjT > .008) {
            sjT = 0; sjA = sjB; sjN = hash(int32_t(sjN), 5, 9, 1);
            sjB = (h01(int32_t(sjN), 1, 0, 0) + h01(int32_t(sjN), 2, 0, 0) + h01(int32_t(sjN), 3, 0, 0) + h01(int32_t(sjN), 4, 0, 0) - 2) * 1.732;
        }
        sharedJit = lerp(sjA, sjB, sjT / .008);
        for (auto &s : singers) {
            if (s.killFade && s.pendingAt == -2) {
                s.killFade = 2;
                s.hold_ramps();
                s.gainT = 0;
                s.gainStep = (s.gainT - s.gainCur) / n;
                continue;
            }
            if (s.pendingAt >= 0 && time >= s.pendingAt) { s.pendingAt = -1; s.start(s.pendingLevel, s.pendingScoop, s.pendingSettle); }
            if (s.pendingStop >= 0 && time >= s.pendingStop) { s.pendingStop = -1; s.stop(); }
            if (!s.active) continue;
            control_singer(s, chP[s.channel], dt);
        }
        lmCStep = ((1 - std::pow(10., -lmCut / 20)) - lmC) / n;
        // Initial configuration is immediate; later automation ramps over the tick.
        const double targetMaster = db(get(G, level)), targetWet = get(G, wet) * 1.4;
        if (frame == 0) { masterGain = targetMaster; wetGain = targetWet; }
        masterStep = (targetMaster - masterGain) / n;
        wetStep = (targetWet - wetGain) / n;
        // Post-grit drive follows the channel-0 texture values (it acts on the mixed bus).
        gritPre = (2.2 + 6 * (get(G, growl) + get(G, rasp) + get(G, shimmer) / 12 + get(G, jitter) / 3 + get(G, creak)) / 3);
        gritTrim = db(cal.afterTrim) / std::sqrt(gritPre);
        room.control(kTick);
    }
    void control_singer(Singer &s, const Params &P, double dt) noexcept {
        constexpr int n = int(kTick);
        s.style = get(P, style);
        const double st = s.style, att = std::max(st > .85 ? .004 : .012, get(P, attack) / 1000. * (1.2 - st));
        const double rel = get(P, release) / 1000.;
        const double tc = s.pT > s.p ? att : rel;
        s.p += (s.pT - s.p) * (1 - std::exp(-dt / tc));
        if (s.p < 1e-5 && s.pT == 0) s.p = 0;
        const double th = st < .33 ? lerp(.12, .5, st / .33) : st < .67 ? lerp(.5, .08, (st - .33) / .34) : lerp(.08, 0, (st - .67) / .33);
        const double lead = st < .33 ? lerp(.25, .9, st / .33) : st < .67 ? lerp(.9, .1, (st - .33) / .34) : lerp(.1, 0, (st - .67) / .33);
        s.V = smooth(th, th + .22, s.p);
        s.pLevel = std::pow(s.p, .7);
        if (!s.voicing && s.V > .002) { s.voicing = true; s.ph = 0; s.gap = 0; s.parity = 0; s.closureFirst = true; }
        const double asp = get(P, breath) * .55 * (1 + .5 * s.reg);
        const double pre = lead * s.p * (1 - s.V) * .9;
        s.noiseAT = (asp * s.V + pre + get(P, whisper) * s.p * .95) * (gritAfter ? .6 : 1);
        s.breathFloor = .12 + .5 * get(P, breath);
        s.scoop *= std::exp(-dt / s.scoopTau);
        const double tremAmp = get(P, tremor) * (.55 + .45 * std::sin(time * .37 + s.trem[0]));
        const double trem = tremAmp * (.5 * std::sin(TAU * s.tremR[0] * time + s.trem[0]) + .3 * std::sin(TAU * s.tremR[1] * time + s.trem[1]) +
                                       .2 * std::sin(TAU * s.tremR[2] * time + s.trem[2]));
        s.wanderV += (gauss(s.rng) * .9 - s.wanderV) * (1 - std::exp(-dt / .6));
        s.wanderS += (s.wanderV - s.wanderS) * (1 - std::exp(-dt / 1.4));
        const double wand = (get(P, wander) + (s.index > 0 ? s.looseWander : 0)) * s.wanderS * 1.6;
        s.vibPh += TAU * 5.4 * dt;
        const double vib = get(P, vibrato) * std::sin(s.vibPh) * smooth(.25, .9, s.p);
        const double cents = s.detune + s.scoop + trem + wand + vib;
        const double target = s.baseHz * std::pow(2, cents / 1200);
        const double gl = std::max(.002, get(P, glide) / 1000.);
        s.fCur += (target - s.fCur) * (1 - std::exp(-dt / gl));
        const double vowelv = get(P, vowel) + s.vowelOff, mouthv = get(P, mouth);
        const double minA = grid.lookup(vowelv, get(P, pharynx), mouthv, get(P, ring), s.Fbuf, s.Abuf);
        const double scale = L_REF / s.L, rd = s.rdNow > 0 ? s.rdNow : lerp(2.3, .62, get(P, pressure));
        for (int k = 0; k < s.lod; k++) {
            const double F = std::min(s.Fbuf[k] * scale, .42 * sr);
            const double B = bandwidth(F, k, rd, minA), g = std::tan(PI * F / sr), kk = std::min(1.9, B / F);
            const double a1 = 1 / (1 + g * (g + kk)), a2 = g * a1, a3 = g * a2;
            if (!s.coefInit) { s.ca1[k] = a1; s.ca2[k] = a2; s.ca3[k] = a3; s.ca[k] = s.Abuf[k]; }
            s.ta[k] = s.Abuf[k]; s.ta1[k] = a1; s.ta2[k] = a2; s.ta3[k] = a3;
            s.cStep1[k] = (a1 - s.ca1[k]) / n; s.cStep2[k] = (a2 - s.ca2[k]) / n; s.cStep3[k] = (a3 - s.ca3[k]) / n; s.aStep[k] = (s.ta[k] - s.ca[k]) / n;
        }
        s.coefInit = true;
        const double fLowest = s.fCur * (get(P, growl) > .1 ? .5 : 1) * (1 - .5 * clamp(get(P, creak), 0, 1));
        double hpf = .6 * fLowest;
        if (!s.carrier) hpf = std::max(hpf, .9 * s.fCur);
        hpf = clamp(hpf * (1 + 1.2 * s.dist), 25, 900);
        {
            const double g = std::tan(PI * hpf / sr), k = 1.41421356, a1 = 1 / (1 + g * (g + k));
            s.hpT1 = a1; s.hpT2 = g * a1; s.hpT3 = g * g * a1;
            if (!s.hpInit) { s.hp_1 = s.hpT1; s.hp_2 = s.hpT2; s.hp_3 = s.hpT3; s.hpInit = true; }
            s.hpS1 = (s.hpT1 - s.hp_1) / n; s.hpS2 = (s.hpT2 - s.hp_2) / n; s.hpS3 = (s.hpT3 - s.hp_3) / n;
        }
        const double ffc = clamp(30 + .3 * s.fCur, 30, 150), fq = 1.6;
        s.flg = std::tan(PI * ffc / sr);
        s.flk = 1 / fq;
        s.flNorm = 1 / std::sqrt((1. / 3) * PI * (ffc / fq) / sr) * .5;
        s.flDepth = (gritAfter ? 0 : get(P, flutter)) * .32 * (.75 + .5 * (1 - s.reg));
        s.driveT = get(P, drive);
        if (s.lnF == 0 || std::abs(s.fCur / s.lnF - 1) >= .02 || std::abs(rd - s.lnRd) >= .06 || s.lnL != s.L) {
            s.lnF = s.fCur; s.lnRd = rd; s.lnL = s.L;
            const double power = expected_power(clamp(s.fCur, 30, 1500), s.L, rd);
            const double corr = std::pow(10., -3.6 * std::max(0., 1.5 - rd) * std::max(0., std::log2(std::max(1., s.fCur) / 110)) / 20);
            s.normT = clamp(std::sqrt(lnRef / (power + 1e-12)) * corr, .08, 12);
            if (s.lnF == s.fCur && s.norm == 1 && s.cycN == 0) s.norm = s.normT;
        }
        s.norm += (s.normT - s.norm) * (1 - std::exp(-dt / .12));
        s.noiseAT *= std::pow(std::max(0., s.norm), .6 * (1 - get(P, whisper)));
        s.noiseStep = (s.noiseAT - s.noiseA) / n;
        s.gainT = db(s.levelDb) * s.gainExtra * cal.voice / cal.preDrive;
        s.gainStep = (s.gainT - s.gainCur) / n;
        const double d = s.dist, dirG = 1 / (1 + 4.5 * d * d + 1.5 * d), azr = s.az * PI / 180, lat = std::sin(azr), back = smooth(95, 160, std::abs(s.az));
        const double pan = (lat + 1) * PI / 4;
        s.gLT = std::cos(pan) * dirG * (1 - .18 * back);
        s.gRT = std::sin(pan) * dirG * (1 - .18 * back);
        s.sendT = (.32 + .5 * d) * (.85 + .3 * back);
        const double lpf = clamp(17000 * std::exp(-1.9 * d) * (1 - .45 * back), 1800, 18000);
        s.dlpC = 1 - std::exp(-TAU * lpf / sr);
        s.itdT = std::min(60., std::abs(lat) * .00062 * sr);
        s.farR = lat < 0;
        s.shadowC = 1 - std::exp(-TAU * clamp(18000 - 13500 * std::abs(lat), 2500, 18000) / sr);
        if (!s.spaceInit) { s.gL = s.gLT; s.gR = s.gRT; s.spaceInit = true; }
        s.gLStep = (s.gLT - s.gL) / n; s.gRStep = (s.gRT - s.gR) / n; s.sendStep = (s.sendT - s.revS) / n; s.itdStep = (s.itdT - s.pdel) / n;
        s.noiseRng = s.rng;
        s.energy = 0;
    }

    // ------------------------------------------------------------------ per sample
    void singer_sample(Singer &s, double &outL, double &outR, double &sendOut) noexcept {
        const Params &P = chP[s.channel];
        if (s.closureFirst) { s.closureFirst = false; s.closure(sr, sharedJit, gritAfter, P, cal, pulses); s.noiseRng = s.rng; }
        uint32_t &x = s.noiseRng;
        const double w1 = xorshift(x) / 2147483648. - 1, w2 = xorshift(x) / 2147483648. - 1;
        const double fa1 = 1 / (1 + s.flg * (s.flg + s.flk)), fa2 = s.flg * fa1, fa3 = s.flg * fa2;
        const double fv3 = w2 - s.fl2, fv1 = fa1 * s.fl1 + fa2 * fv3, fv2 = s.fl2 + fa2 * s.fl1 + fa3 * fv3;
        s.fl1 = 2 * fv1 - s.fl1;
        s.fl2 = 2 * fv2 - s.fl2;
        const double fm = 1 + s.flDepth * s.flNorm * fv1 * s.flk;
        double source = 0, gateFlow = 0;
        if (s.voicing) {
            if (s.gap > 0) --s.gap;
            else {
                s.ph += s.inc;
                if (s.ph >= 1) {
                    s.ph -= 1;
                    s.closure(sr, sharedJit, gritAfter, P, cal, pulses);
                    if (s.gap > 0) { s.ph = 0; --s.gap; }
                }
                if (s.voicing && s.gap == 0) {
                    const int lev = s.lev, S = pulses.sizes[lev], o = pulses.base[lev];
                    const double fi = s.ph * S;
                    const int i0 = std::clamp(int(fi), 0, S - 1);
                    const double fr = fi - i0;
                    const int oa = o + s.rdA * (S + 1) + i0, ob = oa + (S + 1);
                    const double va = pulses.data[oa] + fr * (pulses.data[oa + 1] - pulses.data[oa]);
                    const double vb = s.rdA + 1 < NR ? pulses.data[ob] + fr * (pulses.data[ob + 1] - pulses.data[ob]) : va;
                    source = lerp(va, vb, s.rdW);
                    gateFlow = pulses.flow[s.tabRd * (FLOW + 1) + std::clamp(int(s.ph * FLOW), 0, FLOW)];
                }
            }
            s.amp += (s.ampT - s.amp) * ampSmooth;
            source *= s.amp * fm;
        }
        s.noiseA += s.noiseStep;
        const double aspC = .974, hp = w1 - s.asp1;
        s.asp1 = w1 * (1 - aspC) + s.asp1 * aspC;
        const double g = s.voicing ? (s.breathFloor + (1 - s.breathFloor) * gateFlow) : 1;
        const double noise = hp * s.noiseA * g * .35 + w2 * s.raspC * gateFlow * gateFlow * s.amp * .28;
        const double input = source + noise;
        // Ten parallel zero-delay SVFs with tube-M22 residue amplitudes.
        double filtered = 0;
        for (int k = 0; k < s.lod; k++) {
            s.ca1[k] += s.cStep1[k]; s.ca2[k] += s.cStep2[k]; s.ca3[k] += s.cStep3[k]; s.ca[k] += s.aStep[k];
            const double v3 = input - s.ic2[k], v1 = s.ca1[k] * s.ic1[k] + s.ca2[k] * v3, v2 = s.ic2[k] + s.ca2[k] * s.ic1[k] + s.ca3[k] * v3;
            s.ic1[k] = 2 * v1 - s.ic1[k] + 1e-25;
            s.ic2[k] = 2 * v2 - s.ic2[k];
            filtered += s.ca[k] * v2;
        }
        // Ramped pitch-following SVF high-pass.
        s.hp_1 += s.hpS1; s.hp_2 += s.hpS2; s.hp_3 += s.hpS3;
        const double v0 = filtered + 1e-18, v3 = v0 - s.hp2, v1 = s.hp_1 * s.hp1 + s.hp_2 * v3, v2 = s.hp2 + s.hp_2 * s.hp1 + s.hp_3 * v3;
        s.hp1 = 2 * v1 - s.hp1;
        s.hp2 = 2 * v2 - s.hp2;
        filtered = v0 - 1.41421356 * v1 - v2;
        filtered *= cal.preDrive;
        // Antiderivative anti-aliasing, not a plain tanh waveshaper.
        if (s.driveT > .002 || s.driveOn) {
            const double pre = 1 + 14 * s.driveT * s.driveT, post = 1 / std::sqrt(pre);
            const double xi = filtered * pre, dd = xi - s.xPrev, lx = antiderivative(xi);
            const double lp = s.driveOn ? s.adPrev : antiderivative(s.xPrev);  // cached: same value
            const double y = std::abs(dd) < 1e-6 ? std::tanh(.5 * (xi + s.xPrev)) : (lx - lp) / dd;
            s.xPrev = xi;
            s.adPrev = lx;
            if (!s.driveOn && s.driveFadeLeft == 0) s.driveFadeLeft = 128;
            const double faded = s.driveFadeLeft ? lerp(filtered, y * post, double(128 - s.driveFadeLeft) / 128) : y * post;
            filtered = faded;
            if (s.driveFadeLeft) --s.driveFadeLeft;
            s.driveOn = s.driveT > .002;
        }
        // Gain and stereo distance, angle, ITD and head shadow.
        s.gainCur += s.gainStep; s.gL += s.gLStep; s.gR += s.gRStep; s.revS += s.sendStep; s.pdel += s.itdStep;
        const double v = filtered * s.gainCur;
        s.energy += v * v;
        s.dlp += (v + 1e-18 - s.dlp) * s.dlpC;
        const int w = int(s.itdW), mask = 127;
        s.itd[w] = float(s.dlp);
        const double rp = w - s.pdel, ri = std::floor(rp), rf = rp - ri;
        const double dv = s.itd[int(ri) & mask] * (1 - rf) + s.itd[(int(ri) + 1) & mask] * rf;
        s.itdW = (s.itdW + 1) & mask;
        s.flp += (dv - s.flp) * s.shadowC;
        if (s.farR) { outL += s.dlp * s.gL; outR += s.flp * s.gR; }
        else { outL += s.flp * s.gL; outR += s.dlp * s.gR; }
        sendOut += v * s.revS;
    }
    void bus_sample(double dl_, double dr_, double sendIn, float &left, float &right) noexcept {
        double wetL = 0, wetR = 0;
        wetGain += wetStep;
        room.sample(sendIn, wetL, wetR, wetGain);
        double l = dl_ + wetL, r = dr_ + wetR;
        if (gritAfter) { l = std::tanh(l * gritPre) * gritTrim; r = std::tanh(r * gritPre) * gritTrim; }
        l = highSh[0].tick(lowSh[0].tick(l));
        r = highSh[1].tick(lowSh[1].tick(r));
        const double m = .5 * (l + r), a = scLM.tick(m), b = scPR.tick(m);
        eLM += (a * a - eLM) * lowMidEnvelopeCoeff;
        ePR += (b * b - ePR) * lowMidEnvelopeCoeff;
        lmC += lmCStep;
        l -= lmC * lmEq[0].tick(l);
        r -= lmC * lmEq[1].tick(r);
        const double dl = l - dc[0] + .9995 * dc[1];
        dc[0] = l; dc[1] = dl;
        const double dr = r - dc[2] + .9995 * dc[3];
        dc[2] = r; dc[3] = dr;
        masterGain += masterStep;
        l = dl * masterGain;
        r = dr * masterGain;
        busPeak = std::max(busPeak, std::max(std::abs(l), std::abs(r)));
        // Bounded monotone minimum-gain queue (2.5 ms look-ahead).
        constexpr double ceiling = .891;
        const double need = std::min(1., ceiling / (std::max(std::abs(l), std::abs(r)) + 1e-12));
        const int64_t idx = limIdx++;
        while (limQt > limQh && limQv[(limQt - 1) & 1023] >= need) --limQt;
        limQ[limQt & 1023] = idx; limQv[limQt & 1023] = need; limQt++;
        while (limQt > limQh && limQ[limQh & 1023] <= idx - int64_t(limLook)) limQh++;
        const double target = limQv[limQh & 1023];
        limG += (target - limG) * (target < limG ? limiterAttackCoeff : limiterReleaseCoeff);
        const auto w = limW, rd = (w - limLook + 1024) & 1023;
        limBL[w] = l; limBR[w] = r; limW = (w + 1) & 1023;
        const double ol = clamp(limBL[rd] * limG, -ceiling, ceiling), orr = clamp(limBR[rd] * limG, -ceiling, ceiling);
        left = std::isfinite(ol) ? float(ol) : 0;
        right = std::isfinite(orr) ? float(orr) : 0;
    }
    bool any_active() const noexcept {
        for (const auto &s : singers)
            if (s.active) return true;
        return false;
    }
    // Renders n <= ctlLeft frames of the current tick. Returns true when it wrote exact zeros (idle bus).
    bool render(float *left, float *right, uint32_t n) noexcept {
        const bool active = any_active();
        if (!active && busIdle) {
            std::fill_n(left, n, 0.f);
            std::fill_n(right, n, 0.f);
            lmC += lmCStep * n;
            return true;
        }
        busIdle = false;
        std::fill_n(dryL, n, 0.);
        std::fill_n(dryR, n, 0.);
        std::fill_n(send, n, 0.);
        // Throats are independent inside a tick, so each renders its run at once (same sums, same order).
        for (auto &s : singers)
            if (s.active)
                for (uint32_t i = 0; i < n; ++i) singer_sample(s, dryL[i], dryR[i], send[i]);
        for (uint32_t i = 0; i < n; ++i) bus_sample(dryL[i], dryR[i], send[i], left[i], right[i]);
        return false;
    }
    void finish_tick() noexcept {
        // Finalise RNG and voice lifecycle only after exactly 128 rendered samples.
        for (auto &s : singers)
            if (s.active) {
                s.rng = s.noiseRng;
                const double outLev = std::sqrt(s.energy / 128.);
                // A fading throat is handed over below, never reset here.
                if (!s.killFade && s.pendingAt < 0 && !s.voicing && s.p < 1e-4 && s.noiseA < 1e-5 && !s.gate) {
                    if (outLev < 1e-5) s.silentBlocks++;
                    else s.silentBlocks = 0;
                    if (s.silentBlocks > 6) s.reset(s.slot);
                } else s.silentBlocks = 0;
            }
        for (auto &s : singers)
            if (s.killFade == 2) finish_steal(s);
        // Release empty note records without blocking allocation of new notes.
        for (auto &n : notes)
            if (n.id) {
                bool found = false;
                for (const auto &s : singers)
                    if (s.ownerId == n.id) { found = true; break; }
                if (!found) n = {};
            }
        const double dt = double(kTick) / sr;
        lmRatio = 10 * std::log10((eLM + 1e-12) / (ePR + 1e-12));
        const double over = lmRatio - cal.lowMidTarget, cutT = over > 0 && eLM > 1e-8 ? std::min(6., over * .6) : 0;
        lmCut += (cutT - lmCut) * (1 - std::exp(-dt / .4));
        if (cutT == 0 && lmCut < .02) lmCut = 0;
        // Ramps finish on this boundary, so successive control ramps do not drift.
        masterGain = db(get(G, level));
        wetGain = get(G, wet) * 1.4;
        for (auto &s : singers)
            if (s.active) {
                s.noiseA = s.noiseAT; s.gainCur = s.gainT; s.gL = s.gLT; s.gR = s.gRT; s.revS = s.sendT; s.pdel = s.itdT;
                for (int k = 0; k < s.lod; k++) { s.ca1[k] = s.ta1[k]; s.ca2[k] = s.ta2[k]; s.ca3[k] = s.ta3[k]; s.ca[k] = s.ta[k]; }
                s.hp_1 = s.hpT1; s.hp_2 = s.hpT2; s.hp_3 = s.hpT3;
            }
        // Idle bus: no throat and nothing above -140 dBFS for long enough to empty the room and limiter.
        if (!busIdle) {
            if (!any_active() && busPeak < 1e-7 && room.peak < 1e-7) {
                if (++idleTicks >= idleNeeded) clear_bus();
            } else idleTicks = 0;
        }
        busPeak = 0;
        room.peak = 0;
    }
};

inline Layout Layout::of(double rate) noexcept {
    if (!(rate >= 1)) rate = 1;
    if (rate > 384000) rate = 384000;
    Layout l;
    l.line_len = Room::line_length(rate);
    l.pre_len = Room::pre_length(rate);
    l.lines = up(sizeof(Core));
    l.pre = l.lines + up(size_t(l.line_len) * 8 * sizeof(float));
    l.scratch = l.pre + up(size_t(l.pre_len) * sizeof(float));
    l.total = l.scratch + up(size_t(2 * M + 2 * MAX_LEVEL) * sizeof(double));
    return l;
}

}  // namespace wsmod::guttr
