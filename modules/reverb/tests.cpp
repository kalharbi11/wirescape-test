// SPDX-License-Identifier: GPL-3.0-only
#include "../../sdk/examples/test_support.h"
#include "../../tests/native/abi2/allocation_watch.h"
#define REVERB_TEST_GAIN_METER 1
#include "dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <numeric>
#include <vector>

using reverb::Reverb;
namespace {
constexpr uint32_t kFrames = 256, kMaxChannels = 16;
using Samples = std::vector<float>;
struct Stereo {
    Samples l, r;
};
struct Arena {
    std::unique_ptr<unsigned char[]> bytes = std::make_unique<unsigned char[]>(size_t(2) * 1024 * 1024);
    size_t used = 0;
};
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- the host allocation ABI fixes this signature.
void *WS2_CALL heap_alloc(void *ptr, size_t size, size_t align, uint32_t) {
    auto &arena = *static_cast<Arena *>(ptr);
    const auto start = reinterpret_cast<uintptr_t>(arena.bytes.get());
    const size_t offset = ((start + arena.used + align - 1) & ~(uintptr_t(align) - 1)) - start;
    if (offset + size > size_t(2) * 1024 * 1024) return nullptr;
    arena.used = offset + size;
    return arena.bytes.get() + offset;
}
struct Rig {
    Arena arena;
    ws2_host host{sizeof(host), &arena, heap_alloc};
    const ws2_descriptor *d = ws::Module<Reverb>::descriptor();
    void *instance = nullptr;
    float rows[2][kMaxChannels][kFrames]{};
    float *out_rows[2][kMaxChannels]{};
    const float *in_rows[2][1]{};
    ws2_signal inputs[2]{};
    float values[reverb::COUNT]{};
    const float *param_rows[reverb::COUNT]{};
    ws2_signal params[reverb::COUNT]{};
    ws2_signal_out outputs[2]{};
    ws2_process ctx{};
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- sample rate and voice count are distinct.
    explicit Rig(double rate = 48000, uint32_t channels = 1) {
        const ws2_config config{sizeof(config), rate, kFrames, kMaxChannels, 42, nullptr, 0};
        ws2_memory memory{sizeof(memory), 0, 0, 0};
        REQUIRE(d->memory_requirements(&config, &memory) == WS2_OK && memory.large_bytes > 100000);
        instance = d->create(&host);
        REQUIRE(instance && d->prepare(instance, &config, &host) == WS2_OK);
        for (uint32_t p = 0; p < 2; ++p) {
            for (uint32_t c = 0; c < kMaxChannels; ++c) out_rows[p][c] = rows[p][c];
            outputs[p] = {sizeof(ws2_signal_out), out_rows[p], 1, 0, kMaxChannels};
        }
        for (uint32_t p = 0; p < reverb::COUNT; ++p) {
            values[p] = Reverb::params[p].initial;
            param_rows[p] = &values[p];
            params[p] = {sizeof(ws2_signal), &param_rows[p], 1, WS2_CONSTANT};
        }
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = rate;
        ctx.channels = channels;
        ctx.input_count = 2;
        ctx.output_count = 2;
        ctx.param_count = reverb::COUNT;
        ctx.inputs = inputs;
        ctx.outputs = outputs;
        ctx.params = params;
    }
    ~Rig() { d->destroy(instance); }
    Rig &set(uint32_t p, float v) {
        values[p] = v;
        return *this;
    }
    Stereo run(const Samples &l, const Samples &r, uint32_t block = kFrames, bool right_connected = true,
               const Samples *freeze = nullptr) {
        REQUIRE(l.size() == r.size() && block > 0 && block <= kFrames);
        REQUIRE(!freeze || freeze->size() == l.size());
        Stereo result;
        result.l.reserve(l.size());
        result.r.reserve(r.size());
        for (size_t pos = 0; pos < l.size(); pos += block) {
            const uint32_t n = uint32_t(std::min<size_t>(block, l.size() - pos));
            in_rows[0][0] = l.data() + pos;
            in_rows[1][0] = r.data() + pos;
            inputs[0] = {sizeof(ws2_signal), in_rows[0], 1, WS2_CONNECTED};
            inputs[1] = {sizeof(ws2_signal), in_rows[1], 1, right_connected ? WS2_CONNECTED : 0u};
            ctx.frames = n;
            if (freeze) {
                param_rows[reverb::FREEZE] = freeze->data() + pos;
                params[reverb::FREEZE].flags = n == 1 ? WS2_CONSTANT : 0u;
            }
            int status = 0;
            {
                allocation_watch::Scope watch;
                status = d->process(instance, &ctx);
                REQUIRE(watch.allocations() == 0);
            }
            if (freeze) {
                param_rows[reverb::FREEZE] = &values[reverb::FREEZE];
                params[reverb::FREEZE].flags = WS2_CONSTANT;
            }
            REQUIRE(status == WS2_OK && outputs[0].channels == 1 && outputs[1].channels == 1);
            REQUIRE(!(outputs[0].flags & WS2_CONSTANT) && !(outputs[1].flags & WS2_CONSTANT));
            for (uint32_t i = 0; i < n; ++i) {
                const float a = rows[0][0][i], b = rows[1][0][i];
                REQUIRE(std::isfinite(a) && std::isfinite(b));
                result.l.push_back(a);
                result.r.push_back(b);
            }
            ctx.frame_position += n;
        }
        return result;
    }
    Bytes save() {
        Bytes b;
        ws2_writer w{sizeof(w), &b, sink};
        REQUIRE(d->save_state(instance, &w) == WS2_OK);
        return b;
    }
    int load(const Bytes &bytes) {
        Source s{&bytes, 0};
        ws2_reader r{sizeof(r), &s, source, uint32_t(bytes.size())};
        return d->load_state(instance, &r);
    }
};
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- sample count and amplitude are distinct.
Samples noise(size_t n, float amplitude = .3f) {
    Samples x(n);
    uint32_t state = 0x52cau;
    for (auto &sample : x) {
        state = state * 1664525u + 1013904223u;
        sample = amplitude * (float(state >> 8) / float(0x00ffffffu) * 2.f - 1.f);
    }
    return x;
}
Samples silence(size_t n) { return Samples(n, 0); }
double rms(const Samples &s, size_t start, size_t end) {
    double energy = 0;
    for (size_t i = start; i < end; ++i) energy += double(s[i]) * s[i];
    return std::sqrt(energy / double(end - start));
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- symmetric source channels are intentional.
double correlation(const Samples &a, const Samples &b, size_t start) {
    double aa = 0, bb = 0, ab = 0, ma = 0, mb = 0;
    const double n = double(a.size() - start);
    for (size_t i = start; i < a.size(); ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= n;
    mb /= n;
    for (size_t i = start; i < a.size(); ++i) {
        const double x = a[i] - ma, y = b[i] - mb;
        aa += x * x;
        bb += y * y;
        ab += x * y;
    }
    return ab / std::sqrt(aa * bb);
}
void test_lengths_and_rates() {
    // Native legacy indices must match the stable UI slot map, not default to zero.
    for (uint32_t p = 0; p < reverb::COUNT; ++p) REQUIRE(Reverb::params[p].legacy_index == p);
    for (double rate : {44100., 48000., 96000.}) {
        Reverb dsp;
        dsp.rate = float(rate);
        Reverb at_48k;
        at_48k.rate = 48000.f;
        for (uint32_t section = 0; section < 4; ++section) {
            const double at_rate_seconds = double(Reverb::diffusion_count(rate, section)) / rate;
            const double reference_seconds = double(Reverb::diffusion_count(48000., section)) / 48000.;
            REQUIRE(std::abs(at_rate_seconds - reference_seconds) <= 1.01 / rate);
        }
        for (float size : {0.f, .2f, .5f, 1.f}) {
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t a = dsp.scaled_length(i, size);
                REQUIRE(Reverb::prime(a) && a < Reverb::line_count(rate) - 2);
                for (uint32_t j = 0; j < i; ++j) REQUIRE(a != dsp.scaled_length(j, size));
                const double physical_seconds = double(dsp.physical_length(i, size)) / rate;
                const double original_seconds = double(at_48k.scaled_length(i, size)) / 48000.;
                REQUIRE(std::abs(physical_seconds - original_seconds) < 1e-6);
            }
        }
        Rig rig(rate, 16);
        rig.set(reverb::MIX, 1).set(reverb::DAMPING, 0).set(reverb::MOTION, .4f);
        const auto y = rig.run(noise(size_t(rate * .15)), silence(size_t(rate * .15)), 17, false);
        REQUIRE(rms(y.l, 2000, y.l.size()) > 0);
    }
}
void test_mix_loudness() {
    // Owner v1 report: high Mix got very quiet. Full wet must stay within 6 dB of dry.
    const Samples x = noise(48000 * 2, .25f);
    Rig dry, wet;
    dry.set(reverb::MIX, 0);
    wet.set(reverb::MIX, 1);
    const double d = rms(dry.run(x, x).l, 48000, 96000), w = rms(wet.run(x, x).l, 48000, 96000);
    const double db = 20 * std::log10(w / d);
    std::printf("full-wet vs dry: %.2f dB\n", db);
    REQUIRE(std::abs(db) < 6);
}
void test_mix_zero_continuity() {
    // The original positive-Mix headroom stepped 12% below exact dry at
    // zero. Exercise the moving, sample-accurate control in BOTH output
    // paths, including a change from a substantial wet setting back to 0.
    const Samples left(1, .5f), right(1, -.25f);
    for (double rate : {44100., 48000., 96000.}) {
        Rig moving(rate);
        const Samples warm = noise(4096, .25f);
        moving.set(reverb::MIX, .3f);
        moving.run(warm, warm, 17);
        for (float mix : {0.f, .00001f, .0001f, 0.f,
                          .5f, .0001f, .00001f, 0.f}) {
            moving.set(reverb::MIX, mix);
            const Stereo out = moving.run(left, right, 1);
            if (mix == 0.f) {
                REQUIRE(out.l[0] == left[0] && out.r[0] == right[0]);
            } else if (mix <= .0001f) {
                // The pre-correction 0.88 dry multiplier fails by ~0.06 FS.
                REQUIRE(std::abs(out.l[0] - left[0]) < .0005f);
                REQUIRE(std::abs(out.r[0] - right[0]) < .0005f);
            }
        }
    }
}
void test_s7_2_gain_staging_and_no_clamp() {
    // Direct mono VCO-style sine excitation under the same default parameters
    // as the module-output contract; hosted MO remains the official oracle.
    constexpr uint32_t rate = 48000;
    Samples sine(rate * 2);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin(float(i) * (2.f * reverb::kPi * 440.f / rate));
    Reverb::test_preclamp_peak = 0.f;
    Reverb::test_raw_mix_peak = 0.f;
    Reverb::test_clamp_hits = 0;
    Reverb::test_soft_knee_hits = 0;
    Rig default_rig;
    const Stereo normal = default_rig.run(sine, sine, 128);
    float default_peak = 0.f;
    for (float v : normal.l) default_peak = std::max(default_peak, std::abs(v));
    for (float v : normal.r) default_peak = std::max(default_peak, std::abs(v));
    std::printf("S7.2 direct VCO-style default peak %.8f FS; preclamp %.8f FS\n",
                default_peak, Reverb::test_preclamp_peak);
    REQUIRE(default_peak <= 1.f);
    REQUIRE(Reverb::test_raw_mix_peak <= 1.f);
    REQUIRE(Reverb::test_clamp_hits == 0);
    REQUIRE(Reverb::test_soft_knee_hits == 0);

    // Full-scale broadband and coherent cases at Mix=1, Decay=20 s.
    // The fixture observes the *unclipped* output to prevent a tautological
    // pass from the existing +/-2 final safety clamp.
    const Samples full = noise(size_t(rate) * 8, 1.f);
    Samples anti = full;
    for (auto &v : anti) v = -v;
    for (bool inverted : {false, true}) {
        Rig hot;
        hot.set(reverb::MIX, 1.f)
           .set(reverb::DECAY, 20.f)
           .set(reverb::DAMPING, 0.f)
           .set(reverb::SIZE, 1.f)
           .set(reverb::MOTION, .2f);
        Reverb::test_preclamp_peak = 0.f;
        Reverb::test_raw_mix_peak = 0.f;
        Reverb::test_clamp_hits = 0;
        Reverb::test_soft_knee_hits = 0;
        const Stereo out = hot.run(full, inverted ? anti : full, 128);
        for (float v : out.l) REQUIRE(std::isfinite(v));
        for (float v : out.r) REQUIRE(std::isfinite(v));
        std::printf("S7.2 mix1/decay20 full-scale %s: raw %.8f FS, preclamp %.8f FS, soft %llu, hard %llu\n",
                    inverted ? "antiphase" : "in-phase",
                    Reverb::test_raw_mix_peak, Reverb::test_preclamp_peak,
                    static_cast<unsigned long long>(Reverb::test_soft_knee_hits),
                    static_cast<unsigned long long>(Reverb::test_clamp_hits));
        REQUIRE(Reverb::test_preclamp_peak < 2.f);
        REQUIRE(Reverb::test_clamp_hits == 0);
        REQUIRE(Reverb::test_soft_knee_hits > 0);
    }
}
void test_frame_and_reset() {
    const Samples x = noise(12037), y = noise(12037, .2f);
    Rig one;
    one.set(reverb::MIX, 1).set(reverb::MOTION, .5f);
    const Stereo reference = one.run(x, y);
    for (uint32_t frames : {1u, 17u, 64u, 256u}) {
        Rig other;
        other.set(reverb::MIX, 1).set(reverb::MOTION, .5f);
        const Stereo got = other.run(x, y, frames);
        REQUIRE(got.l == reference.l && got.r == reference.r);
    }
    one.d->reset(one.instance);
    const Stereo fresh = one.run(x, y, 17);
    REQUIRE(fresh.l == reference.l && fresh.r == reference.r);
}
void test_dry_and_width() {
    const Samples x = noise(48000), y = noise(48000, .5f);
    Rig dry;
    dry.set(reverb::MIX, 0);
    const Stereo exact = dry.run(x, y, 17);
    REQUIRE(exact.l == x && exact.r == y);
    Rig normal;
    normal.set(reverb::MIX, 0);
    const Stereo mirrored = normal.run(x, y, 64, false);
    REQUIRE(mirrored.l == x && mirrored.r == x);
    Rig mono;
    mono.set(reverb::MIX, 1).set(reverb::WIDTH, 0).set(reverb::DAMPING, 0).set(reverb::MOTION, 0);
    const Stereo a = mono.run(x, x);
    REQUIRE(a.l == a.r && rms(a.l, 12000, a.l.size()) > 0);
    Rig wide;
    wide.set(reverb::MIX, 1).set(reverb::WIDTH, 1).set(reverb::DAMPING, 0).set(reverb::MOTION, 0);
    const Stereo b = wide.run(x, x);
    const double corr = correlation(b.l, b.r, 12000);
    std::printf("stereo correlation: %.4f\n", corr);
    REQUIRE(corr < .5);
}
double measured_rt60(float size) {
    constexpr uint32_t rate = 48000, window = 2400;
    Samples impulse(size_t(rate) * 3, 0);
    impulse[0] = 1;
    Rig rig;
    rig.set(reverb::SIZE, size)
        .set(reverb::DECAY, 2)
        .set(reverb::DAMPING, 0)
        .set(reverb::MOTION, 0)
        .set(reverb::PREDELAY, 0)
        .set(reverb::MIX, 1);
    const Stereo result = rig.run(impulse, impulse);
    // Least-squares fit to independent RMS-envelope measurements from 0.2 to 2.6 s.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const int begin = 4, end = 52;
    for (int k = begin; k < end; ++k) {
        const double t = (k + .5) * double(window) / rate;
        const double energy = .5 * (std::pow(rms(result.l, size_t(k) * window, size_t(k + 1) * window), 2) +
                                    std::pow(rms(result.r, size_t(k) * window, size_t(k + 1) * window), 2));
        const double ln = std::log(std::sqrt(energy) + 1e-30);
        sx += t;
        sy += ln;
        sxx += t * t;
        sxy += t * ln;
    }
    const double slope = (double(end - begin) * sxy - sx * sy) / (double(end - begin) * sxx - sx * sx);
    return -std::log(1000.0) / slope;
}
// Reproduce the production R2 impulse metric with the same one-second
// pre-silence and two-second, single-sample impulse response. The first frame
// after the final sample above -60 dB of the measured absolute peak is the
// suffix-maximum crossing; a zero crossing within the tail is not decay.
double measured_r2_minus60_ms(uint32_t rate, float damping = .4f) {
    Rig rig(rate);
    rig.set(reverb::DAMPING, damping);
    const Samples idle = silence(rate);
    rig.run(idle, idle, 128);
    Samples impulse(size_t(rate) * 2, 0.f);
    impulse[0] = 1.f;
    const Stereo response = rig.run(impulse, impulse, 128);
    double peak = 0;
    size_t peak_frame = 0;
    for (size_t i = 0; i < response.l.size(); ++i) {
        const double level = std::abs(double(response.l[i]));
        if (level > peak) {
            peak = level;
            peak_frame = i;
        }
    }
    REQUIRE(peak > 0);
    const double threshold = peak * .001;
    size_t last_above = peak_frame;
    for (size_t i = peak_frame; i < response.l.size(); ++i)
        if (std::abs(double(response.l[i])) > threshold) last_above = i;
    REQUIRE(last_above + 1 < response.l.size());
    return 1000.0 * double(last_above + 1) / double(rate);
}
void test_rate_independent_decay() {
    // Same decay target as M7r1 S7.1, no redefinition of the R2 metric.
    // These values are emitted by the existing three-platform folder-test CI.
    const double low = measured_r2_minus60_ms(44100);
    const double reference = measured_r2_minus60_ms(48000);
    const double high = measured_r2_minus60_ms(96000);
    const double spread_percent = 100.0 * (std::max({low, reference, high}) -
                                            std::min({low, reference, high})) / reference;
    std::printf("S7.1 R2 reverb: 44.1 %.5f ms; 48 %.5f ms; 96 %.5f ms; spread %.3f%%\n",
                low, reference, high, spread_percent);
    // The source baseline at 48 kHz was 245.35417 ms; a difference here
    // would invalidate the bit-identical golden and requires escalation.
    REQUIRE(std::abs(reference - 245.35416666666669) < 1.0);
    REQUIRE(spread_percent <= 5.0);
    // M7 r1.2: preserve the identical suffix-maximum method at each damping.
    // This 1% guard is stronger than even the smallest Architect diagnostic
    // base spread; hosted base/head numbers remain required for acceptance.
    for (float damping : {.1f, .7f, .9f}) {
        const double a = measured_r2_minus60_ms(44100, damping);
        const double b = measured_r2_minus60_ms(48000, damping);
        const double c = measured_r2_minus60_ms(96000, damping);
        const double spread = 100.0 * (std::max({a, b, c}) - std::min({a, b, c})) / b;
        std::printf("S7.1 R2 damping %.1f: 44.1 %.5f; 48 %.5f; 96 %.5f ms; spread %.3f%%\n",
                    damping, a, b, c, spread);
        REQUIRE(spread <= 1.0);
    }
}
void test_decay() {
    const double small = measured_rt60(.2f), large = measured_rt60(1.f);
    std::printf("RT60 size .2: %.3fs; size 1: %.3fs\n", small, large);
    REQUIRE(small > 1.6 && small < 2.4);
    REQUIRE(large > 1.6 && large < 2.4);
    REQUIRE(std::abs(small - large) / std::max(small, large) < .2);
}
void test_freeze() {
    constexpr size_t rate = 48000;
    const Samples first = noise(rate, .25f), second = noise(rate * 2, .25f);
    Rig r;
    r.set(reverb::MIX, 1).set(reverb::DAMPING, 0).set(reverb::MOTION, 0).set(reverb::DECAY, 5);
    const Stereo before = r.run(first, first);
    const double original = rms(before.l, rate * 3 / 4, rate);
    r.set(reverb::FREEZE, 1);
    const Stereo frozen = r.run(second, second);
    const double held = rms(frozen.l, rate / 5, rate * 2);
    const double db = 20 * std::log10(held / original);
    std::printf("freeze RMS change: %.3f dB\n", db);
    REQUIRE(std::abs(db) < 1.5);
    // Non-default sizes must converge to integer taps and hold frozen energy.
    for (float size : {.2f, 1.f}) {
        Rig hold;
        hold.set(reverb::SIZE, size).set(reverb::MIX, 1);
        hold.run(first, first);
        hold.set(reverb::FREEZE, 1);
        const auto tail = hold.run(silence(rate * 5), silence(rate * 5));
        for (const auto *channel : {&tail.l, &tail.r}) {
            const double drift = 20 * std::log10(rms(*channel, rate * 4, rate * 5) / rms(*channel, rate / 2, rate));
            REQUIRE(std::abs(drift) < 1.5);
        }
    }
    // Once the 20ms transition has finished, two distinct new inputs cannot enter.
    Rig a, b;
    for (Rig *rig : {&a, &b}) {
        rig->set(reverb::MIX, 1).set(reverb::DAMPING, 0).set(reverb::MOTION, 0);
        rig->run(first, first);
        rig->set(reverb::FREEZE, 1);
        rig->run(silence(rate / 5), silence(rate / 5));
    }
    const Stereo ax = a.run(noise(rate / 2, 1), noise(rate / 2, 1));
    const Stereo bx = b.run(silence(rate / 2), silence(rate / 2));
    REQUIRE(ax.l == bx.l && ax.r == bx.r);
}
void test_cached_parameter_changes() {
    const Samples x = noise(4096), y = noise(4096, .1f);
    Rig a, b;
    a.set(reverb::MIX, .1f);
    b.set(reverb::MIX, .9f);
    a.run(x, y);
    b.run(x, y); // Mix must not alter the internal feedback history.
    a.set(reverb::MIX, .9f);
    const Stereo same_a = a.run(x, y), same_b = b.run(x, y);
    REQUIRE(same_a.l == same_b.l && same_a.r == same_b.r);
    for (float size : {1.f, 0.f, .5f}) {
        a.set(reverb::SIZE, size);
        b.set(reverb::SIZE, size);
        const Stereo part_a = a.run(x, y, 17), part_b = b.run(x, y, 256);
        REQUIRE(part_a.l == part_b.l && part_a.r == part_b.r);
    }
}
void test_state_extremes_and_sweep() {
    const Samples x = noise(12000);
    Rig used;
    used.set(reverb::MIX, 1);
    used.run(x, x);
    const Bytes state = used.save();
    REQUIRE(state.size() == 4);
    Rig reset;
    reset.set(reverb::MIX, 1);
    REQUIRE(reset.load(state) == WS2_OK);
    const Stereo fresh = reset.run(x, x);
    used.d->reset(used.instance);
    const Stereo again = used.run(x, x);
    REQUIRE(fresh.l == again.l && fresh.r == again.r);
    Bytes bad = state;
    bad[0] = 99;
    REQUIRE(reset.load(bad) == WS2_UNSUPPORTED);
    bad.clear();
    REQUIRE(reset.load(bad) == WS2_INVALID);
    for (bool maximum : {false, true}) {
        Rig r(96000, 16);
        for (uint32_t p = 0; p < reverb::COUNT; ++p)
            r.values[p] = maximum ? Reverb::params[p].maximum : Reverb::params[p].minimum;
        const Stereo hot = r.run(noise(96000, 1), noise(96000, 1), 1);
        for (float v : hot.l) REQUIRE(std::abs(v) <= 2);
        for (float v : hot.r) REQUIRE(std::abs(v) <= 2);
    }
    Rig sweep;
    sweep.set(reverb::MIX, 1).set(reverb::DECAY, 20).set(reverb::DAMPING, 0);
    for (float size : {0.f, .25f, .5f, .75f, 1.f, .75f, .5f, .25f, 0.f}) {
        sweep.set(reverb::SIZE, size);
        const auto out = sweep.run(noise(12000, .25f), noise(12000, .25f));
        // Same tank-step bound as before the wet makeup: at Mix 1 the output is the
        // tank scaled by exactly (1 + kWetMakeup), so compare in tank units.
        constexpr float tank = 1.f + Reverb::kWetMakeup;
        for (size_t i = 1; i < out.l.size(); ++i) REQUIRE(std::abs(out.l[i] - out.l[i - 1]) / tank < .5f);
    }
    Rig stable;
    stable.set(reverb::DECAY, 20).set(reverb::DAMPING, 0).set(reverb::MOTION, 0).set(reverb::MIX, 1);
    const auto out = stable.run(noise(size_t(48000) * 30, 1), noise(size_t(48000) * 30, 1));
    for (float v : out.l) REQUIRE(std::abs(v) <= 2);
    for (float v : out.r) REQUIRE(std::abs(v) <= 2);
}

void test_batch_b_freeze_effective_rows() {
    const auto *d = ws::Module<Reverb>::descriptor();
    REQUIRE(d->param_count == reverb::COUNT);
    REQUIRE(d->params[reverb::FREEZE].flags ==
            (WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE));
    const char *ids[] = {"size", "decay", "damping", "predelay", "width", "motion", "freeze", "mix"};
    for (unsigned p = 0; p < reverb::COUNT; ++p) {
        REQUIRE(std::strcmp(d->params[p].id, ids[p]) == 0);
        REQUIRE(d->params[p].legacy_index == p);
        if (p != reverb::FREEZE)
            REQUIRE(d->params[p].flags == (WS2_AUTOMATABLE | WS2_MODULATABLE));
    }
    // White-box witness: input drive follows each original 48 kHz tank tick,
    // not a host block boundary or the first parameter sample of the block.
    for (double host_rate : {44100., 48000., 96000.}) {
        Rig fixture(host_rate);
        Reverb dsp;
        const ws2_config cfg{sizeof(cfg), host_rate, kFrames, kMaxChannels, 42, nullptr, 0};
        REQUIRE(dsp.prepare(ws::Setup{cfg, fixture.host}));
        dsp.reset();
        Samples left(256, .25f), right(256, -.125f), freeze(256);
        for (unsigned i = 0; i < 256; ++i)
            freeze[i] = i < 64 ? .49f : i < 192 ? .5f : 0.f;
        fixture.in_rows[0][0] = left.data();
        fixture.in_rows[1][0] = right.data();
        for (unsigned p = 0; p < 2; ++p)
            fixture.inputs[p] = {sizeof(ws2_signal), fixture.in_rows[p], 1, WS2_CONNECTED};
        fixture.param_rows[reverb::FREEZE] = freeze.data();
        fixture.params[reverb::FREEZE].flags = 0;
        fixture.ctx.frames = 256;
        { allocation_watch::Scope watch;
          ws::Block block(fixture.ctx);
          dsp.process(block);
          REQUIRE(watch.allocations() == 0); }
        const double ratio = 48000. / host_rate;
        double phase = ratio < 1. ? 1. - ratio : 0.;
        unsigned tick = 0;
        float amount = 0.f;
        const float step = 1.f / (48000.f * .02f);
        for (unsigned i = 0; i < 256; ++i) {
            phase += ratio;
            while (phase >= 1.) {
                const float target = freeze[i] >= .5f ? 1.f : 0.f;
                amount += std::clamp(target - amount, -step, step);
                REQUIRE(std::abs(dsp.pre[0][tick] - left[i] * (1.f - amount)) < 1e-6f);
                REQUIRE(std::abs(dsp.pre[1][tick] - right[i] * (1.f - amount)) < 1e-6f);
                ++tick;
                phase -= 1.;
            }
        }
        REQUIRE(dsp.sample_counter == tick && dsp.pre_write == tick);
        REQUIRE(std::abs(dsp.freeze_amount - amount) < 1e-6f);
        { allocation_watch::Scope watch; dsp.reset(); REQUIRE(watch.allocations() == 0); }
        REQUIRE(dsp.freeze_amount == 0.f && dsp.pre_write == 0 && dsp.sample_counter == 0);
    }
    // Registered descriptor callback: varying effective rows must match a
    // one-host-sample constant-row oracle at the same absolute frame schedule.
    // WS2_SUM input is already summed by the host; outputs stay one voice.
    const Samples x = noise(8192, .25f);
    Samples freeze(x.size(), 0.f);
    std::fill(freeze.begin() + 67, freeze.begin() + 4099, 1.f);
    for (double rate : {44100., 48000., 96000.}) {
        for (unsigned channels : {1u, 16u}) {
            Rig varying(rate, channels), reference(rate, channels), unfrozen(rate, channels);
            for (Rig *rig : {&varying, &reference, &unfrozen})
                rig->set(reverb::MIX, 1.f);
            const Stereo a = varying.run(x, x, 127, true, &freeze);
            const Stereo b = reference.run(x, x, 1, true, &freeze);
            const Stereo c = unfrozen.run(x, x, 127);
            REQUIRE(a.l == b.l && a.r == b.r);
            REQUIRE(a.l != c.l || a.r != c.r);
            for (float v : a.l) REQUIRE(std::abs(v) <= 2.f);
            for (float v : a.r) REQUIRE(std::abs(v) <= 2.f);
            const Bytes state = varying.save();
            REQUIRE(state == reference.save() && state.size() == 4);
            REQUIRE(varying.load(state) == WS2_OK);
            { allocation_watch::Scope watch; reference.d->reset(reference.instance);
              REQUIRE(watch.allocations() == 0); }
            const Stereo after = varying.run(x, x, 127);
            const Stereo fresh = reference.run(x, x, 127);
            REQUIRE(after.l == fresh.l && after.r == fresh.r);
        }
    }
    std::puts("PASS reverb batch B freeze effective rows and original tank cadence");
}
} // namespace
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_batch_b_freeze_effective_rows();
    test_lengths_and_rates();
    test_frame_and_reset();
    test_mix_loudness();
    test_mix_zero_continuity();
    test_s7_2_gain_staging_and_no_clamp();
    test_dry_and_width();
    test_decay();
    test_rate_independent_decay();
    test_freeze();
    test_cached_parameter_changes();
    test_state_extremes_and_sweep();
    std::puts("abi2_reverb_tests passed");
}
