// SPDX-License-Identifier: GPL-3.0-only
// OB-g5 standalone device-off regression harness; build from this module's CMakeLists.txt.
// The old Gate math below is a frozen, independent source copy, not a wrapper over Adsr::process.
#include "wide_fixture.h"  // lab dsp_fixture.h holds 6 parameters; see DESIGN.md "Limitations"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

struct OriginalGate {
    float level = 0, start = 0;
    uint64_t elapsed = 0, duration = 1;
    int stage = 0;
    bool gated = false;
    ws::Schmitt edge;
    void begin(int next, float seconds, double rate) {
        stage = next;
        start = level;
        elapsed = 0;
        duration = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(seconds * rate)));
    }
    float sample(float in, float attack, float decay, float sustain, float release, double rate) {
        bool high = edge.process(in);
        if (high && !gated) { gated = true; begin(1, attack, rate); }
        else if (!high && gated) { gated = false; begin(4, release, rate); }
        if (stage == 1) {
            level = start + (1 - start) * static_cast<float>(++elapsed) / static_cast<float>(duration);
            if (elapsed >= duration) { level = 1; begin(2, decay, rate); }
        } else if (stage == 2) {
            level = start + (sustain - start) * static_cast<float>(++elapsed) / static_cast<float>(duration);
            if (elapsed >= duration) { level = sustain; stage = 3; }
        } else if (stage == 3) level = sustain;
        else if (stage == 4) {
            level = start * (1 - static_cast<float>(++elapsed) / static_cast<float>(duration));
            if (elapsed >= duration) { level = 0; stage = 0; }
        }
        return level;
    }
};
static ws2_event note(uint32_t type, uint32_t offset, uint32_t source = 7, int32_t id = 21) {
    ws2_event e{};
    e.struct_size = sizeof(e);
    e.type = type;
    e.offset = offset;
    e.channel = 0;
    e.source_id = source;
    e.note_id = id;
    e.key = 60;
    e.value = 1.f;
    return e;
}
static std::unique_ptr<DspFixture> rig(double rate = 48000.) {
    auto p = std::make_unique<DspFixture>(ws::Module<ws2ports::Adsr>::descriptor(), rate, 1);
    VERIFY(p->d->param_count == 7 && p->d->output_count == 1);
    VERIFY(std::strcmp(p->d->params[4].id, "mode") == 0);
    VERIFY(std::strcmp(p->d->params[5].id, "hold") == 0);
    VERIFY(p->d->params[4].initial == 0.f && p->d->params[5].initial == 0.f);
    // MOD-2 (0024 §4.1): delay appended after hold, default 0, the same flags as attack.
    VERIFY(std::strcmp(p->d->params[6].id, "delay") == 0);
    VERIFY(p->d->params[6].initial == 0.f && p->d->params[6].minimum == 0.f && p->d->params[6].maximum == 30.f);
    VERIFY(p->d->params[6].flags == p->d->params[0].flags && p->d->params[6].legacy_index == 6);
    return p;
}
static void default_gate_bit_identical() {
    for (double rate : {44100., 48000., 96000.}) {
        auto f = rig(rate);
        // A nonzero hold is inert while the default mode is Gate.
        f->parameter[0][0][0] = .001f;
        f->parameter[1][0][0] = .0015f;
        f->parameter[2][0][0] = .63f;
        f->parameter[3][0][0] = .002f;
        f->parameter[5][0][0] = .2f;
        OriginalGate old;
        uint32_t frame = 0;
        for (uint32_t count : {1u, 17u, 64u, 128u, 17u, 256u, 64u, 128u}) {
            for (uint32_t i = 0; i < count; ++i) {
                const auto absolute = frame + i;
                const bool on = (absolute >= 5 && absolute < 68) ||
                                (absolute >= 110 && absolute < 177) ||
                                (absolute >= 343 && absolute < 455);
                f->input[0][0][i] = on ? 1.f : 0.f;
            }
            f->run(count);
            for (uint32_t i = 0; i < count; ++i) {
                const float expected = old.sample(f->input[0][0][i], .001f, .0015f,
                                                  .63f, .002f, rate);
                const float actual = f->output[0][0][i];
                VERIFY(std::memcmp(&expected, &actual, sizeof(float)) == 0);
            }
            frame += count;
        }
    }
}
static void triggered_gate_timed_hold() {
    const double rate = 48000.;
    auto f = rig(rate);
    f->parameter[0][0][0] = .001f; // 48 samples
    f->parameter[1][0][0] = .001f; // 48 samples
    f->parameter[2][0][0] = .6f;
    f->parameter[3][0][0] = .001f; // 48 samples
    f->parameter[4][0][0] = 1.f;  // Trigger
    f->parameter[5][0][0] = .002f; // 96 samples at Sustain
    std::array<float, 256> samples{};
    for (uint32_t block = 0; block < 2; ++block) {
        for (uint32_t i = 0; i < 128; ++i) f->input[0][0][i] = block == 0 && i < 64 ? 1.f : 0.f;
        f->run(128);
        std::copy_n(f->output[0][0], 128, samples.data() + 128 * block);
    }
    VERIFY(std::abs(samples[47] - 1.f) < 1e-6f);
    VERIFY(std::abs(samples[95] - .6f) < 1e-6f);
    for (uint32_t i = 96; i < 192; ++i) VERIFY(samples[i] == samples[95]);
    VERIFY(samples[192] < .6f && samples[192] > 0);
    VERIFY(samples[239] == 0 && samples[240] == 0);
    // Falling edge at 64 never begins Release in Trigger mode.
    VERIFY(samples[65] > .6f);

    // Zero hold: Release starts as soon as Decay reaches Sustain.
    auto zero = rig(rate);
    zero->parameter[0][0][0] = .001f;
    zero->parameter[1][0][0] = .001f;
    zero->parameter[2][0][0] = .6f;
    zero->parameter[3][0][0] = .001f;
    zero->parameter[4][0][0] = 1.f;
    zero->parameter[5][0][0] = 0.f;
    for (uint32_t i = 0; i < 128; ++i) zero->input[0][0][i] = i < 64 ? 1.f : 0.f;
    zero->run(128);
    VERIFY(std::abs(zero->output[0][0][95] - .6f) < 1e-6f);
    VERIFY(zero->output[0][0][96] < zero->output[0][0][95]);
}
static void note_retrigger_from_current_level_and_choke() {
    auto f = rig(48000.);
    f->ins[0].flags = 0; // Notes events are authoritative without a gate cable.
    f->parameter[0][0][0] = .001f;
    f->parameter[1][0][0] = .001f;
    f->parameter[2][0][0] = .6f;
    f->parameter[3][0][0] = .001f;
    f->parameter[4][0][0] = 1.f;
    f->parameter[5][0][0] = .01f;
    std::array<ws2_event, 3> events{note(WS2_NOTE_ON, 0), note(WS2_NOTE_OFF, 64),
                                    note(WS2_NOTE_ON, 120, 7, 22)};
    f->process.events = {sizeof(ws2_event_list), uint32_t(events.size()), events.data()};
    f->run(256);
    VERIFY(std::abs(f->output[0][0][119] - .6f) < 1e-6f);
    VERIFY(f->output[0][0][120] > f->output[0][0][119]);
    VERIFY(f->output[0][0][120] < .7f); // no jump to full level or back to zero
    VERIFY(f->output[0][0][255] > .5f); // release from Note Off was ignored
    // An obsolete Note Off must not release the new note.
    auto obsolete = note(WS2_NOTE_OFF, 0, 7, 21);
    f->process.events = {sizeof(ws2_event_list), 1, &obsolete};
    f->run(1);
    VERIFY(f->output[0][0][0] > .5f);
    // S7.9 intended sound change: matching CHOKE clears the note identity
    // immediately, then releases the old level to zero in exactly 5 ms.
    const float level = f->output[0][0][0];
    auto choke = note(WS2_NOTE_CHOKE, 0, 7, 22);
    f->process.events = {sizeof(ws2_event_list), 1, &choke};
    f->run(64);
    for (uint32_t i = 0; i < 64; ++i)
        VERIFY(std::abs(f->output[0][0][i] - level * (1.f - float(i+1)/240.f)) < 1e-6f);
    // An old repeated CHOKE cannot prolong this tail.
    f->run(176);
    for (uint32_t i = 0; i < 176; ++i)
        VERIFY(std::abs(f->output[0][0][i] - level * (1.f - float(i+65)/240.f)) < 1e-6f);
    VERIFY(f->output[0][0][175] == 0.f);
    f->reset();
    f->process.events = {sizeof(ws2_event_list), 0, nullptr};
    f->run(64);
    for (uint32_t i = 0; i < 64; ++i) VERIFY(f->output[0][0][i] == 0);
}
static void cable_trigger_restarts_from_release_tail() {
    auto f = rig(48000.);
    f->parameter[0][0][0] = .001f;
    f->parameter[1][0][0] = .001f;
    f->parameter[2][0][0] = .6f;
    f->parameter[3][0][0] = .002f;
    f->parameter[4][0][0] = 1.f;
    f->parameter[5][0][0] = 0.f;
    // First pulse fully traverses A, D, and part of R.
    for (uint32_t i = 0; i < 256; ++i)
        f->input[0][0][i] = i < 64 || i >= 130 && i < 145 ? 1.f : 0.f;
    f->run(256);
    VERIFY(f->output[0][0][129] > 0 && f->output[0][0][129] < .6f);
    VERIFY(f->output[0][0][130] > f->output[0][0][129]);
    VERIFY(f->output[0][0][130] < .7f);
}

// S7.9 draft-ahead: exact callback-level 2 s CHOKE/no-CHOKE pair. Reuse
// the T7.0 causal Butterworth 2 kHz, +/-25 ms window at 48 kHz. The
// historic T7.0/T7.0b files contain no ADSR or VCO CHOKE rows, so the
// unchanged source's diagnostic before-numbers must be captured first.
static double choke_peak_dbfs(const std::vector<float>& pcm, size_t at) {
    constexpr double rate = 48000., tau = 6.28318530717958647692;
    const double w = tau * 2000. / rate, c = std::cos(w);
    const double alpha = std::sin(w) / std::sqrt(2.);
    const double b0 = (1. + c) / (2. * (1. + alpha));
    const double b1 = -2. * b0, b2 = b0;
    const double a1 = -2. * c / (1. + alpha), a2 = (1. - alpha) / (1. + alpha);
    double x1 = 0., x2 = 0., y1 = 0., y2 = 0., peak = 0.;
    for (size_t i = 0; i < at + 1200; ++i) {
        const double x = pcm[i], y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        if (i >= at - 1200) peak = std::max(peak, std::abs(y));
    }
    return 20. * std::log10(std::max(peak, 1e-15));
}
static std::vector<float> choke_capture(bool oscillator, bool trigger, bool choke) {
    const auto* d = oscillator ? ws::Module<ws2ports::Vco>::descriptor() :
                                 ws::Module<ws2ports::Adsr>::descriptor();
    auto f = std::make_unique<DspFixture>(d, 48000., 1);
    f->ins[0].flags = 0;
    if (oscillator) {
        f->ins[1].flags = 0;
        std::fill_n(f->input[1][0], DspFixture::frames, 0.f);
        std::fill_n(f->parameter[0][0], DspFixture::frames, 220.f);
    } else {
        f->parameter[0][0][0] = .001f;
        f->parameter[1][0][0] = .001f;
        f->parameter[2][0][0] = .6f;
        f->parameter[4][0][0] = trigger ? 1.f : 0.f;
        f->parameter[5][0][0] = 30.f;
    }
    std::vector<float> pcm(96000);
    for (uint32_t first = 0; first < pcm.size(); first += 128) {
        ws2_event e{};
        uint32_t count = 0;
        if (first == 0) { e = note(WS2_NOTE_ON, 17); e.key = 69; count = 1; }
        if (choke && first == 48000) { e = note(WS2_NOTE_CHOKE, 0); e.key = 69; count = 1; }
        f->process.events = {sizeof(ws2_event_list), count, count ? &e : nullptr};
        f->run(128);
        std::copy_n(f->output[0][0], 128, pcm.data() + first);
    }
    return pcm;
}
static void s79_choke_metric() {
    bool all_pass = true;
    for (int mode : {0, 1, 2}) {
        const bool oscillator = mode == 2, trigger = mode == 1;
        const auto ref = choke_capture(oscillator, trigger, false);
        const auto switched = choke_capture(oscillator, trigger, true);
        VERIFY(std::equal(ref.begin(), ref.begin()+48000, switched.begin()));
        VERIFY(std::all_of(switched.begin(), switched.end(), [](float x){ return std::isfinite(x); }));
        if (!oscillator) {
            // Ordinary note-on attack must still reach 1 in 48 samples.
            const auto first = std::find(ref.begin(), ref.begin()+256, 1.f);
            VERIFY(first - ref.begin() == 17+47);
        } else {
            // NOTE_ON preserves phase; no retrigger reset at frame 17.
            constexpr double tau = 6.28318530717958647692;
            VERIFY(std::abs(ref[17] - std::sin(tau * 220. * 17. / 48000.)) < 1e-6);
        }
        const double after = choke_peak_dbfs(switched, 48000);
        const double floor = choke_peak_dbfs(ref, 48000);
        // Fail-first source dfc3736595839875f2c9be7c1bd29cfd38ea0f2d,
        // hosted 45478523246 (both architectures, same windows).
        const double before = oscillator ? -7.429097628 : -6.046020153;
        const double reduction = before - after;
        const bool pass = reduction >= 20. || after < -60. || std::abs(after - floor) <= .1;
        if (oscillator) {
            // CHOKE preserves the instantaneous phase sample and reverts to
            // the free-running 220 Hz frequency (the event note was 440 Hz).
            VERIFY(switched[48000] == ref[48000]);
            VERIFY(std::abs(switched[48200] - switched[48000]) > .01f);
            constexpr double tau = 6.28318530717958647692;
            const double phase = tau*(220.*17. + 440.*(48000.-17.))/48000.;
            for (size_t i=0; i<240; ++i)
                VERIFY(std::abs(switched[48000+i] - std::sin(phase+tau*220.*i/48000.)) < 1e-6);
        } else {
            const float start = switched[47999];
            for (size_t i=0; i<240; ++i)
                VERIFY(std::abs(switched[48000+i]-start*(1.f-float(i+1)/240.f)) < 1e-6f);
            VERIFY(std::all_of(switched.begin()+48239, switched.end(), [](float x){return x==0.f;}));
        }
        std::printf("S7.9 %s CHOKE peak_dbfs=%.9f reduction_db=%.9f no_switch_floor_dbfs=%.9f %s\n",
                    oscillator ? "VCO" : trigger ? "ADSR-trigger" : "ADSR-gate",
                    after, reduction, floor, pass ? "PASS" : "FAIL");
        all_pass = all_pass && pass;
    }
    std::fflush(stdout);
    VERIFY(all_pass);
}


static std::vector<uint8_t> e7c_save(DspFixture& f) {
    VERIFY(f.d->save_state && f.d->load_state);
    std::vector<uint8_t> bytes;
    ws2_writer w{sizeof(w), &bytes, [](void* ctx, const void* p, uint32_t n) {
        auto& b = *static_cast<std::vector<uint8_t>*>(ctx);
        auto* first = static_cast<const uint8_t*>(p);
        b.insert(b.end(), first, first+n);
        return int(WS2_OK);
    }};
    VERIFY(f.d->save_state(f.state, &w) == WS2_OK && !bytes.empty());
    return bytes;
}
static int e7c_load(DspFixture& f, const std::vector<uint8_t>& bytes) {
    struct Cursor { const std::vector<uint8_t>& b; size_t pos = 0; } c{bytes};
    ws2_reader r{sizeof(r), &c, [](void* ctx, void* p, uint32_t n) {
        auto& c = *static_cast<Cursor*>(ctx);
        if (n > c.b.size()-c.pos) return int(WS2_INVALID);
        std::memcpy(p, c.b.data()+c.pos, n);
        c.pos += n;
        return int(WS2_OK);
    }, uint32_t(bytes.size())};
    return f.d->load_state(f.state, &r);
}
static void e7c_roundtrip(const ws2_descriptor* d, bool oscillator, bool trigger, bool stateless) {
    auto a = std::make_unique<DspFixture>(d, 48000., 1);
    auto b = std::make_unique<DspFixture>(d, 48000., 1);
    auto rate = std::make_unique<DspFixture>(d, 44100., 1);
    for (auto* f : {a.get(), b.get(), rate.get()}) {
        if (!stateless) {
            f->ins[0].channels = 0; f->ins[0].flags = WS2_CONSTANT;
            if (oscillator) { f->ins[1].channels = 0; f->ins[1].flags = WS2_CONSTANT; }
            else { f->parameter[0][0][0] = .02f; f->parameter[4][0][0] = trigger ? 1.f : 0.f; }
        }
    }
    auto on = note(WS2_NOTE_ON, 17); on.key = 69;
    if (!stateless) a->process.events = {sizeof(ws2_event_list), 1, &on};
    a->run(173);
    a->process.events = {sizeof(ws2_event_list), 0, nullptr};
    a->run(91);
    const auto saved = e7c_save(*a);
    VERIFY(e7c_load(*b, saved) == WS2_OK);
    VERIFY(e7c_save(*b) == saved);
    for (int block = 0; block < 8; ++block) {
        auto off = note(WS2_NOTE_OFF, 19);
        if (block == 3 && !stateless) {
            a->process.events = b->process.events = {sizeof(ws2_event_list), 1, &off};
        }
        a->run(127); b->run(127);
        VERIFY(a->outs[0].flags == b->outs[0].flags);
        VERIFY(std::memcmp(a->output[0][0], b->output[0][0], 127*sizeof(float)) == 0);
        a->process.events = b->process.events = {sizeof(ws2_event_list), 0, nullptr};
    }
    auto current = e7c_save(*b), bad = saved;
    bad[0] = 99;
    VERIFY(e7c_load(*b, bad) == WS2_UNSUPPORTED);
    VERIFY(e7c_save(*b) == current);
    bad = saved; bad.pop_back();
    VERIFY(e7c_load(*b, bad) == WS2_INVALID);
    VERIFY(e7c_save(*b) == current);
    // VCA has no rate-dependent state: its version-only hook adapts trivially under C2.
    if (!stateless) VERIFY(e7c_load(*rate, saved) == WS2_INVALID);
    else VERIFY(e7c_load(*rate, saved) == WS2_OK);
}
static void e7c_cable_and_hold(bool trigger) {
    auto a=rig(),b=rig();
    for(auto*f:{a.get(),b.get()}){
        f->parameter[0][0][0]=.001f;
        f->parameter[1][0][0]=.001f;
        f->parameter[2][0][0]=.63f;
        f->parameter[3][0][0]=.002f;
        f->parameter[4][0][0]=trigger?1.f:0.f;
        f->parameter[5][0][0]=.01f;
        std::fill_n(f->input[0][0],256,1.f);
    }
    a->run(128); // attack + decay finished, trigger hold is counting down
    const auto saved=e7c_save(*a);
    VERIFY(e7c_load(*b,saved)==WS2_OK);
    for(int block=0;block<6;++block){
        // .375 is inside the Schmitt hysteresis band; a reset latch would
        // release a Gate envelope or spuriously retrigger at the next high.
        for(auto*f:{a.get(),b.get()})
            std::fill_n(f->input[0][0],256,block<2?.375f:block==2?1.f:0.f);
        a->run(127);b->run(127);
        VERIFY(std::memcmp(a->output[0][0],b->output[0][0],127*sizeof(float))==0);
    }
    VERIFY(a->output[0][0][126]==0.f);
}
static void e7c_state_tests() {
    e7c_cable_and_hold(false);
    e7c_cable_and_hold(true);
    e7c_roundtrip(ws::Module<ws2ports::Adsr>::descriptor(), false, false, false);
    e7c_roundtrip(ws::Module<ws2ports::Adsr>::descriptor(), false, true, false);
    e7c_roundtrip(ws::Module<ws2ports::Vco>::descriptor(), true, false, false);
    e7c_roundtrip(ws::Module<ws2ports::Vca>::descriptor(), false, false, true);
}

// ---- MOD-2: delay (0024 §4.1). Tests written before the DSP.
static void delay_shifts_the_gate_bit_identically() {
    // From idle, a delayed rise is the original Gate engine fed a gate that rises n samples later.
    for (double rate : {44100., 48000., 96000.}) {
        auto f = rig(rate);
        f->parameter[0][0][0] = .001f;
        f->parameter[1][0][0] = .0015f;
        f->parameter[2][0][0] = .63f;
        f->parameter[3][0][0] = .002f;
        f->parameter[6][0][0] = .0005f;
        const uint32_t n = static_cast<uint32_t>(std::llround(.0005f * rate));
        OriginalGate old;
        uint32_t frame = 0;
        for (uint32_t count : {1u, 17u, 64u, 128u, 17u, 256u}) {
            for (uint32_t i = 0; i < count; ++i) {
                const auto t = frame + i;
                f->input[0][0][i] = t >= 5 && t < 300 ? 1.f : 0.f;
            }
            f->run(count);
            for (uint32_t i = 0; i < count; ++i) {
                const auto t = frame + i;
                const float shifted = t >= 5 + n && t < 300 ? 1.f : 0.f;
                const float expected = old.sample(shifted, .001f, .0015f, .63f, .002f, rate);
                VERIFY(std::memcmp(&expected, &f->output[0][0][i], sizeof(float)) == 0);
            }
            frame += count;
        }
    }
}
static void gate_falls_during_the_wait() {
    auto f = rig();
    f->parameter[0][0][0] = .001f;
    f->parameter[1][0][0] = .001f;
    f->parameter[2][0][0] = .6f;
    f->parameter[3][0][0] = .002f;  // 96 samples
    // Reach Sustain, release at 128, rise again at 150 with a 100-sample delay, fall at 180.
    for (uint32_t i = 0; i < 256; ++i) f->input[0][0][i] = i < 128 || (i >= 150 && i < 180) ? 1.f : 0.f;
    f->parameter[6][0][0] = 0.f;
    f->run(128);
    VERIFY(std::abs(f->output[0][0][127] - .6f) < 1e-6f);
    std::copy(f->input[0][0] + 128, f->input[0][0] + 256, f->input[0][0]);
    f->parameter[6][0][0] = 100.f / 48000.f;
    f->run(128);
    const float* o = f->output[0][0];
    // The release keeps falling through the wait: no attack starts.
    for (uint32_t i = 1; i < 52; ++i) VERIFY(o[i] < o[i - 1]);
    // The fall at 180 (index 52) restarts Release from the current level, never an attack.
    VERIFY(o[52] < o[51]);
    VERIFY(o[52] > 0.f);
    for (uint32_t i = 53; i < 128; ++i) VERIFY(o[i] <= o[i - 1]);
    // Release restarted at 52 with 96 samples: zero from 52+95 (index 19 of the next block).
    std::fill_n(f->input[0][0], 64, 0.f);
    f->run(64);
    VERIFY(f->output[0][0][18] > 0.f && f->output[0][0][19] == 0.f);
    VERIFY(f->output[0][0][63] == 0.f);
}
static void trigger_mode_delay_then_adr_and_retrigger_in_wait() {
    auto f = rig();
    f->parameter[0][0][0] = .001f;  // 48
    f->parameter[1][0][0] = .001f;  // 48
    f->parameter[2][0][0] = .6f;
    f->parameter[3][0][0] = .001f;  // 48
    f->parameter[4][0][0] = 1.f;    // Trigger
    f->parameter[5][0][0] = .0005f; // hold 24
    f->parameter[6][0][0] = .0005f; // delay 24
    // A 4-sample pulse at 0; another at 10 (inside the wait) restarts the wait: attack starts at 34.
    std::array<float, 512> out{};
    for (uint32_t block = 0; block < 2; ++block) {
        for (uint32_t i = 0; i < 256; ++i) {
            const uint32_t t = block * 256 + i;
            f->input[0][0][i] = t < 4 || (t >= 10 && t < 14) ? 1.f : 0.f;
        }
        f->run(256);
        std::copy_n(f->output[0][0], 256, out.data() + 256 * block);
    }
    for (uint32_t t = 0; t < 34; ++t) VERIFY(out[t] == 0.f);
    VERIFY(out[34] > 0.f);
    VERIFY(std::abs(out[34 + 47] - 1.f) < 1e-6f);             // attack: 48 samples
    VERIFY(std::abs(out[34 + 95] - .6f) < 1e-6f);             // decay: 48 samples
    for (uint32_t t = 34 + 96; t < 34 + 96 + 23; ++t) VERIFY(out[t] == out[34 + 95]); // hold 24
    VERIFY(out[34 + 96 + 24] < .6f);                          // release
    VERIFY(out[34 + 96 + 24 + 47] == 0.f);
}
static void notes_delay_and_note_off_in_wait() {
    auto f = rig();
    f->ins[0].flags = 0;
    f->parameter[0][0][0] = .001f;
    f->parameter[1][0][0] = .001f;
    f->parameter[2][0][0] = .6f;
    f->parameter[3][0][0] = .001f;
    f->parameter[6][0][0] = .001f;  // 48
    auto on = note(WS2_NOTE_ON, 10);
    f->process.events = {sizeof(ws2_event_list), 1, &on};
    f->run(64);
    for (uint32_t i = 0; i < 58; ++i) VERIFY(f->output[0][0][i] == 0.f);
    VERIFY(f->output[0][0][58] > 0.f);
    // A note that ends inside the wait never attacks.
    auto g = rig();
    g->ins[0].flags = 0;
    g->parameter[6][0][0] = .001f;
    std::array<ws2_event, 2> events{note(WS2_NOTE_ON, 0), note(WS2_NOTE_OFF, 20)};
    g->process.events = {sizeof(ws2_event_list), 2, events.data()};
    g->run(128);
    for (uint32_t i = 0; i < 128; ++i) VERIFY(g->output[0][0][i] == 0.f);
}
static void delay_sixteen_channels() {
    auto f = std::make_unique<DspFixture>(ws::Module<ws2ports::Adsr>::descriptor(), 48000., 16);
    f->pars[6].channels = 16;
    for (uint32_t c = 0; c < 16; ++c) {
        f->parameter[0][c][0] = .001f;
        f->parameter[6][c][0] = float(c) / 48000.f;  // channel c waits c samples
        for (uint32_t i = 0; i < 128; ++i) f->input[0][c][i] = 1.f;
    }
    f->run(128);
    for (uint32_t c = 0; c < 16; ++c) {
        const uint32_t first = c;  // c == 0: no wait, attack at sample 0
        for (uint32_t i = 0; i < first; ++i) VERIFY(f->output[0][c][i] == 0.f);
        VERIFY(f->output[0][c][first] > 0.f);
        VERIFY(std::abs(f->output[0][c][first + 47] - 1.f) < 1e-6f);
    }
}
static void state_v1_loads_and_v2_keeps_the_wait() {
    // Version 1 bytes (the pre-MOD-2 layout) still load: a voice with no wait.
    auto a = rig();
    std::vector<uint8_t> v1;
    auto u32 = [&](uint32_t x) { for (int k = 0; k < 4; ++k) v1.push_back(uint8_t(x >> (8 * k))); };
    auto f32 = [&](float x) { uint32_t b; std::memcpy(&b, &x, 4); u32(b); };
    const double rate = 48000.;
    uint64_t rb; std::memcpy(&rb, &rate, 8);
    u32(1); u32(uint32_t(rb)); u32(uint32_t(rb >> 32)); u32(0);
    for (int v = 0; v < 16; ++v) {
        u32(v == 0 ? 3 : 0); f32(v == 0 ? .7f : 0.f); f32(0.f);
        for (int k = 0; k < 6; ++k) u32(k == 2 ? 1 : 0);  // elapsed, duration=1, hold_left
        u32(v == 0 ? 1 : 0); u32(0); u32(0); u32(0); u32(v == 0 ? 1 : 0);
    }
    VERIFY(e7c_load(*a, v1) == WS2_OK);
    a->parameter[2][0][0] = .7f;
    std::fill_n(a->input[0][0], 16, 1.f);
    a->run(16);
    for (uint32_t i = 0; i < 16; ++i) VERIFY(a->output[0][0][i] == .7f);
    // Version 2 saves a voice in its wait; the copy attacks at the same sample.
    auto b = rig(), c = rig();
    for (auto* f : {b.get(), c.get()}) { f->parameter[6][0][0] = .002f; f->parameter[0][0][0] = .001f; }
    std::fill_n(b->input[0][0], 64, 1.f);
    b->run(40);
    const auto saved = e7c_save(*b);
    VERIFY(saved[0] == 2);
    VERIFY(e7c_load(*c, saved) == WS2_OK && e7c_save(*c) == saved);
    for (auto* f : {b.get(), c.get()}) std::fill_n(f->input[0][0], 128, 1.f);
    b->run(128); c->run(128);
    VERIFY(std::memcmp(b->output[0][0], c->output[0][0], 128 * sizeof(float)) == 0);
    VERIFY(b->output[0][0][55] == 0.f && b->output[0][0][56] > 0.f);  // 96 - 40 = 56
}
static void delay_tests() {
    delay_shifts_the_gate_bit_identically();
    gate_falls_during_the_wait();
    trigger_mode_delay_then_adr_and_retrigger_in_wait();
    notes_delay_and_note_off_in_wait();
    delay_sixteen_channels();
    state_v1_loads_and_v2_keeps_the_wait();
    std::puts("MOD-2 ADSR delay tests PASS");
}

int main() {
    e7c_state_tests();
    default_gate_bit_identical();
    triggered_gate_timed_hold();
    note_retrigger_from_current_level_and_choke();
    cable_trigger_restarts_from_release_tail();
    delay_tests();
    std::puts("OB-g5 ADSR device-off source tests PASS");
    s79_choke_metric();
}
