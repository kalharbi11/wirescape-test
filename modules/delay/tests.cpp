// SPDX-License-Identifier: GPL-3.0-only
// Callback-level tests for fx.delay. Expected values come from plain arithmetic on sample positions, not from dsp.h.
#include "../../sdk/examples/test_support.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include "dsp.h"
#include "settled_reference.h"
#include <cstring>
#include <cmath>
#include <functional>
#include <vector>

using fxdelay::Delay;
enum : uint8_t { kTime, kDivision, kFeedback, kTone, kMode, kMix };
using Wave = std::vector<float>;
using DelayBytes = std::vector<uint8_t>;
struct DelaySource { const DelayBytes* bytes; size_t pos; };
static int WS2_CALL delay_sink(void* ctx,const void* p,uint32_t n) {
    auto& b=*static_cast<DelayBytes*>(ctx);const auto* v=static_cast<const uint8_t*>(p);
    b.insert(b.end(),v,v+n);return WS2_OK;
}
static int WS2_CALL delay_source(void* ctx,void* p,uint32_t n) {
    auto& s=*static_cast<DelaySource*>(ctx);
    if(n>s.bytes->size()-s.pos)return WS2_INVALID;
    std::memcpy(p,s.bytes->data()+s.pos,n);s.pos+=n;return WS2_OK;
}


struct Arena {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(size_t(4) * 1024 * 1024);
    size_t used = 0;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- fixed host allocator ABI.
    static void *WS2_CALL alloc(void *context, size_t bytes, size_t alignment, uint32_t) {
        auto &a = *static_cast<Arena *>(context);
        const uintptr_t base = reinterpret_cast<uintptr_t>(a.bytes.data());
        const uintptr_t next = (base + a.used + alignment - 1) & ~(uintptr_t(alignment) - 1);
        const size_t end = next - base + bytes;
        if (end > a.bytes.size()) return nullptr;
        a.used = end;
        return a.bytes.data() + (next - base);
    }
};

// One input port: `channels` waves of equal length, patched or not.
struct In {
    std::vector<Wave> ch{Wave{}};
    bool connected = false;
};
struct Result {
    Wave l, r;
};

struct Rig {
    Arena arena;
    ws2_host host{sizeof(host), &arena, Arena::alloc};
    const ws2_descriptor *d = ws::Module<Delay>::descriptor();
    void *instance = nullptr;
    double rate;
    float values[6];
    In in[3];
    Wave zeros = Wave(256, 0.f);

    explicit Rig(double sample_rate = 48000, const ws2_descriptor *descriptor = nullptr)
        : d(descriptor ? descriptor : ws::Module<Delay>::descriptor()), rate(sample_rate) {
        const ws2_config config{sizeof(config), rate, 256, 16, 7, nullptr, 0};
        instance = d->create(&host);
        REQUIRE(instance && d->prepare(instance, &config, &host) == WS2_OK);
        for (int p = 0; p < 6; ++p) values[p] = Delay::params[p].initial;
    }
    ~Rig() { d->destroy(instance); }
    DelayBytes save() {
        DelayBytes bytes;
        ws2_writer writer{sizeof(writer), &bytes, delay_sink};
        REQUIRE(d->save_state(instance, &writer) == WS2_OK);
        return bytes;
    }
    int load(const DelayBytes &bytes, uint32_t reader_size = sizeof(ws2_reader)) {
        DelaySource ctx{&bytes, 0};
        ws2_reader reader{reader_size, &ctx, delay_source, static_cast<uint32_t>(bytes.size())};
        return d->load_state(instance, &reader);
    }
    // Runs `total` frames in blocks of `block`; `each` may change values before every block.
    Result run(size_t total, uint32_t block = 256, const std::function<void(size_t)> &each = {}, bool watch = false,
               const std::function<int(ws2_process &)> &direct = {}) {
        Result result{Wave(total), Wave(total)};
        float out_data[2][16][256]{};
        for (size_t first = 0; first < total; first += block) {
            if (each) each(first);
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, total - first));
            const float *rows[3][16]{};
            ws2_signal inputs[3]{};
            for (int p = 0; p < 3; ++p) {
                const In &port = in[p];
                const size_t count = port.connected ? port.ch.size() : 1;
                for (size_t c = 0; c < count; ++c)
                    rows[p][c] = port.connected ? port.ch[c].data() + first : zeros.data();
                inputs[p] = {sizeof(ws2_signal), rows[p], static_cast<uint32_t>(count),
                             static_cast<uint32_t>(port.connected ? WS2_CONNECTED : WS2_CONSTANT)};
            }
            const float *value_rows[6];
            ws2_signal params[6];
            for (int p = 0; p < 6; ++p) {
                value_rows[p] = &values[p];
                params[p] = {sizeof(ws2_signal), &value_rows[p], 1, WS2_CONSTANT};
            }
            float *out_rows[2][16];
            ws2_signal_out outputs[2];
            for (int p = 0; p < 2; ++p) {
                for (int c = 0; c < 16; ++c) out_rows[p][c] = out_data[p][c];
                outputs[p] = {sizeof(ws2_signal_out), out_rows[p], 1, 0, 16};
            }
            ws2_process ctx{};
            ctx.struct_size = sizeof(ctx);
            ctx.sample_rate = rate;
            ctx.channels = 1;
            ctx.frames = n;
            ctx.frame_position = first;
            ctx.input_count = 3;
            ctx.output_count = 2;
            ctx.param_count = 6;
            ctx.inputs = inputs;
            ctx.outputs = outputs;
            ctx.params = params;
            int status;
            if (watch) {
                allocation_watch::Scope scope;
                status = direct ? direct(ctx) : d->process(instance, &ctx);
                REQUIRE(scope.allocations() == 0);
            } else {
                status = direct ? direct(ctx) : d->process(instance, &ctx);
            }
            REQUIRE(status == WS2_OK);
            REQUIRE(outputs[0].channels == 1 && outputs[1].channels == 1);
            REQUIRE(!(outputs[0].flags & WS2_CONSTANT) && !(outputs[1].flags & WS2_CONSTANT));
            for (uint32_t i = 0; i < n; ++i) {
                result.l[first + i] = out_data[0][0][i];
                result.r[first + i] = out_data[1][0][i];
            }
        }
        return result;
    }
    void left(Wave w) {
        in[0].ch = {std::move(w)};
        in[0].connected = true;
    }
    void right(Wave w) {
        in[1].ch = {std::move(w)};
        in[1].connected = true;
    }
    void clock(Wave w) {
        in[2].ch = {std::move(w)};
        in[2].connected = true;
    }
};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- duration and onset are intentionally separate.
static Wave impulse(size_t length, size_t at = 0, float height = 1.f) {
    Wave w(length, 0.f);
    w[at] = height;
    return w;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- sample length, seed, and amplitude are distinct.
static Wave noise(size_t length, uint32_t seed, float amount = 1.f) {
    Wave w(length);
    uint32_t s = seed;
    for (auto &x : w) {
        s = s * 1664525u + 1013904223u;
        x = amount * (static_cast<float>(s >> 8) / 8388608.f - 1.f);
    }
    return w;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- half-open search bounds are intentional.
static size_t argmax(const Wave &w, size_t from, size_t to) {
    size_t best = from;
    for (size_t i = from; i < to; ++i)
        if (std::fabs(w[i]) > std::fabs(w[best])) best = i;
    return best;
}
static bool finite_within(const Wave &w, float limit) {
    for (float x : w)
        if (!std::isfinite(x) || std::fabs(x) > limit) return false;
    return true;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- delay time and loop feedback are different parameters.
static void wet_only(Rig &r, float time, float feedback) {
    r.values[kMix] = 1.f;
    r.values[kTime] = time;
    r.values[kFeedback] = feedback;
}

static void echo_position() {
    for (double rate : {44100., 48000., 96000.}) {
        Rig r(rate);
        wet_only(r, 0.1f, 0.f);
        const size_t length = static_cast<size_t>(rate * 0.3), at = static_cast<size_t>(std::lround(rate * 0.1));
        r.left(impulse(length));
        const Result out = r.run(length);
        for (const Wave *w : {&out.l, &out.r}) {
            const size_t peak = argmax(*w, 0, length);
            REQUIRE(peak + 1 >= at && peak <= at + 1);
            REQUIRE(std::fabs((*w)[peak]) > 0.99f);
            for (size_t i = 0; i < length; ++i)
                if (i + 3 < at || i > at + 3) REQUIRE(std::fabs((*w)[i]) < 0.01f); // the only echo
        }
    }
}
static void feedback_decay() {
    Rig r;
    wet_only(r, 0.1f, 0.5f);
    r.left(impulse(30000));
    const Result out = r.run(30000);
    float previous = 0;
    for (int k = 1; k <= 5; ++k) {
        const size_t centre = size_t(4800) * size_t(k);
        const float peak = std::fabs(out.l[argmax(out.l, centre - 5, centre + 5)]);
        REQUIRE(std::fabs(peak - std::pow(0.5f, float(k - 1))) < 0.02f * std::pow(0.5f, float(k - 1)));
        if (k > 1) REQUIRE(std::fabs(peak / previous - 0.5f) < 0.01f);
        previous = peak;
    }
}
static void ping_pong() {
    Rig r;
    wet_only(r, 0.1f, 0.5f);
    r.values[kMode] = 1.f;
    r.left(impulse(30000));
    const Result out = r.run(30000);
    for (int k = 1; k <= 5; ++k) {
        const Wave &hit = k % 2 ? out.l : out.r; // L, R, L, R, L
        const Wave &quiet = k % 2 ? out.r : out.l;
        const size_t centre = size_t(4800) * size_t(k);
        const size_t peak = argmax(hit, centre - 5, centre + 5);
        const float expected = std::pow(0.5f, float(k - 1));
        REQUIRE(std::fabs(std::fabs(hit[peak]) - expected) < 0.02f * expected);
        REQUIRE(std::fabs(quiet[peak]) < 0.01f);
    }
    // Stereo mode with a left-only input never reaches the right output.
    Rig s;
    wet_only(s, 0.1f, 0.5f);
    s.left(impulse(12000));
    s.right(Wave(12000, 0.f));
    const Result stereo = s.run(12000);
    REQUIRE(finite_within(stereo.r, 0.01f));
    REQUIRE(std::fabs(stereo.l[argmax(stereo.l, 4795, 4805)]) > 0.99f);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- duration and clock period are distinct.
static Wave pulses(size_t length, size_t period, size_t first = 0) {
    Wave w(length, 0.f);
    for (size_t at = first; at < length; at += period)
        for (size_t i = at; i < std::min(length, at + 100); ++i) w[i] = 1.f;
    return w;
}
static void clock_sync() {
    // 120 BPM quarter notes = 0.5 s; division 2 = x1/2 -> 0.25 s = 12000 frames, once the glide has settled.
    const size_t length = 96000 + 20000, at = 96000;
    Rig r;
    wet_only(r, 0.375f, 0.f);
    r.values[kDivision] = 2.f;
    r.left(impulse(length, at));
    r.clock(pulses(length, 24000));
    const Result out = r.run(length, 128);
    const size_t peak = argmax(out.l, at, length);
    REQUIRE(peak >= at + 12000 - 3 && peak <= at + 12000 + 3);
    REQUIRE(std::fabs(out.l[peak]) > 0.95f);
    // Dotted eighth at the same tempo: x3/4 = 0.375 s = 18000 frames (the time knob is ignored while patched).
    Rig q;
    wet_only(q, 0.1f, 0.f);
    q.values[kDivision] = 4.f;
    q.left(impulse(length, at));
    q.clock(pulses(length, 24000));
    const Result dotted = q.run(length);
    const size_t p2 = argmax(dotted.l, at, length);
    REQUIRE(p2 >= at + 18000 - 3 && p2 <= at + 18000 + 3);
    // A clock cable that never pulses leaves the time knob in charge.
    Rig idle;
    wet_only(idle, 0.1f, 0.f);
    idle.left(impulse(12000));
    idle.clock(Wave(12000, 0.f));
    const Result still = idle.run(12000);
    const size_t p3 = argmax(still.l, 0, 12000);
    REQUIRE(p3 >= 4799 && p3 <= 4801);
}
static void mix_law() {
    const size_t length = 20000;
    Rig dry;
    dry.values[kMix] = 0.f;
    dry.values[kFeedback] = 0.9f;
    dry.left(noise(length, 1));
    dry.right(noise(length, 2));
    const Result a = dry.run(length);
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) -- bit-exact dry passthrough includes signed zero.
    REQUIRE(std::memcmp(a.l.data(), dry.in[0].ch[0].data(), length * sizeof(float)) == 0);
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) -- bit-exact dry passthrough includes signed zero.
    REQUIRE(std::memcmp(a.r.data(), dry.in[1].ch[0].data(), length * sizeof(float)) == 0);
    Rig wet;
    wet_only(wet, 0.1f, 0.f);
    wet.left(noise(length, 1));
    const Result b = wet.run(length);
    for (size_t i = 0; i < 4790; ++i) REQUIRE(b.l[i] == 0.f); // no dry component
    for (size_t i = 4800; i < length; ++i) REQUIRE(std::fabs(b.l[i] - wet.in[0].ch[0][i - 4800]) < 1e-3f);
    // Equal power: a 0.5 mix of dry and the fully correlated echo has the independent gain cos+sin of 45 degrees.
    Rig half;
    wet_only(half, 0.1f, 0.f);
    half.values[kMix] = 0.5f;
    half.left(Wave(length, 0.25f));
    const Result c = half.run(length);
    REQUIRE(std::fabs(c.l[length - 1] - 0.25f * 2.f * std::sqrt(0.5f)) < 1e-3f);
}
static void unpatched_right() {
    Rig r;
    r.values[kMix] = 0.5f;
    r.left(noise(10000, 3));
    const Result out = r.run(10000);
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) -- left/right mirroring requires exact audio bits.
    REQUIRE(std::memcmp(out.l.data(), out.r.data(), size_t(10000) * sizeof(float)) == 0);
    REQUIRE(!finite_within(out.l, 0.f)); // not silent
}
static void poly_sum() {
    Rig one, two;
    for (Rig *r : {&one, &two}) wet_only(*r, 0.1f, 0.f);
    one.left(impulse(6000, 10, 0.5f));
    two.in[0].ch = {impulse(6000, 10, 0.25f), impulse(6000, 10, 0.25f)};
    two.in[0].connected = true;
    const Result a = one.run(6000), b = two.run(6000);
    REQUIRE(a.l == b.l && a.r == b.r);
    REQUIRE(std::fabs(a.l[argmax(a.l, 4800, 4820)] - 0.5f) < 0.01f);
}
static void loop_safety() {
    for (uint32_t mode = 0; mode < 2; ++mode) {
        Rig r;
        r.values[kFeedback] = 1.f;
        r.values[kMix] = 0.5f;
        r.values[kTime] = 0.02f;
        r.values[kMode] = static_cast<float>(mode);
        r.left(noise(480000, 5));
        r.right(noise(480000, 6));
        const Result out = r.run(480000);
        REQUIRE(finite_within(out.l, 2.f) && finite_within(out.r, 2.f));
        // The loop holds repeats up instead of dying away: late output is still substantial.
        float late = 0;
        for (size_t i = 470000; i < 480000; ++i) late = std::max(late, std::fabs(out.l[i]));
        REQUIRE(late > 0.2f);
    }
    // After the input stops, repeats at feedback 1 are sustained at a bounded level, never growing.
    Rig s;
    wet_only(s, 0.01f, 1.f);
    s.left([&] {
        Wave w = noise(4800, 8);
        w.resize(96000, 0.f);
        return w;
    }());
    const Result tail = s.run(96000);
    float last = 0;
    for (size_t i = 90000; i < 96000; ++i) last = std::max(last, std::fabs(tail.l[i]));
    REQUIRE(last > 0.05f && last <= 1.6f);
    // Sweep 0.001 -> 2 s and back while a sine plays: finite, no jump bigger than the limiter ceiling.
    Rig w;
    w.values[kFeedback] = 0.8f;
    w.values[kMix] = 0.6f;
    Wave sine(480000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = 0.5f * std::sin(6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
    w.left(sine);
    const Result out = w.run(480000, 64, [&](size_t at) {
        const double phase = static_cast<double>(at) / 480000.0;
        const double up = phase < 0.5 ? phase * 2 : 2 - phase * 2;
        w.values[kTime] = static_cast<float>(0.001 * std::pow(2000.0, up));
    });
    REQUIRE(finite_within(out.l, 2.f));
    for (size_t i = 1; i < out.l.size(); ++i) REQUIRE(std::fabs(out.l[i] - out.l[i - 1]) <= 1.5f);
}
static void tone_shapes_loop() {
    // Dark tone loses highs in the loop; thin tone loses lows. Compare the second echo of a 100 Hz and a 6 kHz tone.
    auto second_echo = [](float tone, float hz) {
        Rig r;
        wet_only(r, 0.1f, 1.f);
        r.values[kTone] = tone;
        Wave w(40000, 0.f);
        for (size_t i = 0; i < 4800; ++i) w[i] = 0.5f * std::sin(6.2831853f * hz * static_cast<float>(i) / 48000.f);
        r.left(w);
        const Result out = r.run(40000);
        float peak = 0;
        for (size_t i = 24000; i < 33000; ++i) peak = std::max(peak, std::fabs(out.l[i]));
        return peak;
    };
    REQUIRE(second_echo(-1.f, 6000.f) < 0.25f * second_echo(0.f, 6000.f));
    REQUIRE(second_echo(-1.f, 100.f) > 0.8f * second_echo(0.f, 100.f));
    REQUIRE(second_echo(1.f, 100.f) < 0.25f * second_echo(0.f, 100.f));
    REQUIRE(second_echo(1.f, 6000.f) > 0.5f * second_echo(0.f, 6000.f)); // five passes of a gentle 1.5 kHz high-pass
}
static Wave mixed(Rig &r, uint32_t block, size_t length) {
    r.values[kTime] = 0.013f;
    r.values[kFeedback] = 0.7f;
    r.values[kTone] = -0.4f;
    r.values[kMode] = 1.f;
    r.values[kMix] = 0.5f;
    r.left(noise(length, 11, 0.6f));
    r.right(noise(length, 12, 0.6f));
    r.clock(pulses(length, 1500));
    const Result out = r.run(length, block);
    Wave both = out.l;
    both.insert(both.end(), out.r.begin(), out.r.end());
    return both;
}
static void matrices() {
    for (double rate : {44100., 48000., 96000.}) {
        Rig whole(rate);
        const Wave reference = mixed(whole, 256, 5000);
        REQUIRE(!finite_within(reference, 0.f));
        for (uint32_t block : {1u, 17u, 64u}) {
            Rig cut(rate);
            REQUIRE(mixed(cut, block, 5000) == reference);
        }
        whole.d->reset(whole.instance);
        REQUIRE(mixed(whole, 17, 5000) == reference); // reset equals a fresh instance
    }
    // Channel matrix: 1, 4 and 16 voices of 1/n each sum to the same input; a mono cable behaves the same.
    Rig base;
    wet_only(base, 0.05f, 0.3f);
    base.left(noise(8000, 4));
    const Result expected = base.run(8000);
    for (size_t voices : {size_t(4), size_t(16)}) {
        Rig poly;
        wet_only(poly, 0.05f, 0.3f);
        poly.in[0].ch.assign(voices, Wave(8000));
        for (size_t c = 0; c < voices; ++c)
            for (size_t i = 0; i < 8000; ++i) poly.in[0].ch[c][i] = base.in[0].ch[0][i] / static_cast<float>(voices);
        poly.in[0].connected = true;
        const Result got = poly.run(8000);
        for (size_t i = 0; i < 8000; ++i) REQUIRE(std::fabs(got.l[i] - expected.l[i]) < 1e-4f);
    }
}
static void state_and_extremes() {
    Rig a;
    wet_only(a, 0.01f, 0.8f);
    a.left(noise(4000, 9));
    a.run(4000);
    const DelayBytes bytes = a.save();
    Rig b;
    REQUIRE(b.load(bytes) == WS2_OK && b.save() == bytes);
    DelayBytes bad = bytes;
    bad[0] = 99;
    REQUIRE(b.load(bad) == WS2_UNSUPPORTED);
    REQUIRE(b.save() == bytes);
    bad = bytes;
    bad.pop_back();
    // Refusal must be atomic even when the destination already has an audible tail.
    REQUIRE(b.load(bad) == WS2_INVALID);
    REQUIRE(b.save() == bytes);
    // Reject a non-finite final ring sample after staging almost the entire payload.
    DelayBytes nonfinite = bytes;
    REQUIRE(nonfinite.size() >= 4);
    nonfinite[nonfinite.size()-4]=0;
    nonfinite[nonfinite.size()-3]=0;
    nonfinite[nonfinite.size()-2]=0x80;
    nonfinite[nonfinite.size()-1]=0x7f; // +infinity, little endian
    REQUIRE(b.load(nonfinite) == WS2_INVALID);
    REQUIRE(b.save() == bytes);
    Rig malformed;
    REQUIRE(malformed.load(bad) == WS2_INVALID);
    malformed.values[kMix] = 1.f;
    malformed.left(Wave(2000, 0.f));
    const auto cleared = malformed.run(2000);
    REQUIRE(finite_within(cleared.l, 0.f) && finite_within(cleared.r, 0.f));
    // The failed-load instance is separate from the successful continuation twin.
    REQUIRE(b.save() == bytes);
    // A70 v2 must continue the audible tail, not merely round-trip bytes.
    wet_only(b, 0.01f, 0.8f);
    a.left(Wave(4000, 0.f)); b.left(Wave(4000, 0.f));
    const Result reference = a.run(4000), restored = b.run(4000);
    REQUIRE(!finite_within(reference.l, 0.f));
    REQUIRE(std::memcmp(reference.l.data(), restored.l.data(), reference.l.size()*sizeof(float)) == 0);
    REQUIRE(std::memcmp(reference.r.data(), restored.r.data(), reference.r.size()*sizeof(float)) == 0);
    Rig other_rate(44100.);
    REQUIRE(other_rate.load(bytes) == WS2_INVALID);
    // v1 legacy state explicitly clears the lines: retain the original oracle.
    const DelayBytes legacy{1,0,0,0};
    REQUIRE(a.load(legacy) == WS2_OK);
    a.left(Wave(4000, 0.f));
    const Result quiet = a.run(4000);
    REQUIRE(finite_within(quiet.l, 0.f) && finite_within(quiet.r, 0.f));
    // The existing 1 MiB per-node serialization cap is not a new preparation cap.
    // At 96 kHz the stereo two-second history alone is larger than the limit.
    {
        Rig too_large(96000.);
        DelayBytes rejected;
        ws2_writer writer{sizeof(writer), &rejected, delay_sink};
        REQUIRE(too_large.d->save_state(too_large.instance, &writer) == WS2_CAPACITY);
        REQUIRE(rejected.empty()); // preflight before emitting a partial blob
    }
    // A nonzero tone, feedback, clock follower and changing delay time are also
    // runtime state. Restore partway through a glide; continue the clock phase
    // from the exact prefix, with no replay of the preceding input.
    {
        Rig prefix, twin;
        for (Rig* r : {&prefix, &twin}) {
            wet_only(*r, .02f, .8f);
            r->values[kTone] = -.6f;
            r->values[kMode] = 1.f;
        }
        prefix.left(noise(3900, 71));
        prefix.clock(pulses(3900, 900));
        prefix.run(3900, 127);
        prefix.values[kDivision] = 7.f;
        prefix.left(noise(129, 73));
        // Keep clocked input present; next pulse is 600 frames away.
        prefix.clock(Wave(129, 0.f));
        prefix.run(129, 127);
        twin.values[kDivision] = prefix.values[kDivision];
        REQUIRE(twin.load(prefix.save()) == WS2_OK);
        Wave tail_clock(4800, 0.f);
        for (size_t at = 471; at < tail_clock.size(); at += 900)
            tail_clock[at] = 1.f;
        for (Rig* r : {&prefix, &twin}) {
            r->left(Wave(4800, 0.f));
            r->clock(tail_clock);
        }
        const auto expected = prefix.run(4800, 127), actual = twin.run(4800, 127);
        REQUIRE(!finite_within(expected.l, 0.f));
        REQUIRE(std::memcmp(expected.l.data(), actual.l.data(), expected.l.size()*sizeof(float)) == 0);
        REQUIRE(std::memcmp(expected.r.data(), actual.r.data(), expected.r.size()*sizeof(float)) == 0);
    }
    // Snapshot while mode and tone are changing and the clock latch is high.
    {
        Rig original, restored;
        wet_only(original, .01f, .7f);
        original.left(noise(3000, 181));
        original.clock(pulses(3000, 700));
        original.run(3000);
        original.values[kMode] = 1.f;
        original.values[kTone] = -.7f;
        original.left(noise(8, 182));
        original.clock(Wave(8, 1.f));
        original.run(8, 8);
        for(int p=0;p<6;++p) restored.values[p]=original.values[p];
        REQUIRE(restored.load(original.save()) == WS2_OK);
        Wave clock(2400, .375f);
        clock[32]=0.f; clock[33]=1.f;
        for(Rig* r : {&original,&restored}) {
            r->left(Wave(2400,0.f)); r->clock(clock);
        }
        const auto x=original.run(2400,31), y=restored.run(2400,31);
        REQUIRE(!finite_within(x.l,0.f));
        REQUIRE(x.l==y.l && x.r==y.r);
    }
    // A79: each malformed length must leave the next render unchanged, both
    // with a populated tail and during simultaneous mode/tone/clock transitions.
    for (bool midflight : {false, true}) {
        Rig source;
        wet_only(source, .01f, .7f);
        source.left(noise(3000, 281));
        source.clock(pulses(3000, 700));
        source.run(3000);
        if (midflight) {
            source.values[kMode] = 1.f;
            source.values[kTone] = -.7f;
            source.left(noise(8, 282));
            source.clock(Wave(8, 1.f));
            source.run(8, 8);
        }
        const auto snapshot = source.save();
        for (int kind = 0; kind < 4; ++kind) {
            Rig subject, twin;
            for (int p = 0; p < 6; ++p)
                subject.values[p] = twin.values[p] = source.values[p];
            REQUIRE(subject.load(snapshot) == WS2_OK);
            REQUIRE(twin.load(snapshot) == WS2_OK);
            auto malformed = snapshot;
            uint32_t reader_size = sizeof(ws2_reader);
            if (kind == 0) malformed.push_back(0);
            if (kind == 1) malformed.insert(malformed.end(), 64, 0);
            if (kind == 2) malformed.pop_back();
            if (kind == 3) reader_size = offsetof(ws2_reader, bytes);
            REQUIRE(subject.load(malformed, reader_size) == WS2_INVALID);
            for (Rig* r : {&subject, &twin}) {
                r->left(Wave(2400, 0.f));
                Wave clock(2400, .375f);
                clock[32] = 0.f; clock[33] = 1.f;
                r->clock(clock);
            }
            const auto x = subject.run(2400, 31), y = twin.run(2400, 31);
            REQUIRE(!finite_within(y.l, 0.f));
            REQUIRE(std::memcmp(x.l.data(), y.l.data(), x.l.size()*sizeof(float)) == 0);
            REQUIRE(std::memcmp(x.r.data(), y.r.data(), x.r.size()*sizeof(float)) == 0);
        }
    }
    // Extremes: every parameter at its minimum and maximum, hot inputs, tiny and large blocks.
    for (int extreme = 0; extreme < 2; ++extreme) {
        for (uint32_t mode = 0; mode < 2; ++mode) {
            Rig r(96000);
            for (int p = 0; p < 6; ++p) r.values[p] = extreme ? Delay::params[p].maximum : Delay::params[p].minimum;
            r.values[kMode] = static_cast<float>(mode);
            r.left(noise(30000, 13, 1e6f));
            r.right(noise(30000, 14, 1e6f));
            r.clock(pulses(30000, 400));
            const Result out = r.run(30000, 33);
            REQUIRE(finite_within(out.l, 1e7f) && finite_within(out.r, 1e7f));
        }
    }
    // No allocation in process, with a clock, ping-pong and tone active.
    Rig w;
    w.values[kMode] = 1.f;
    w.values[kTone] = 0.5f;
    w.left(noise(2048, 15));
    w.clock(pulses(2048, 500));
    w.run(2048, 128, {}, true);
    // Interface: IDs, stereo pairs, one-voice outputs.
    REQUIRE(std::strcmp(Delay::inputs[0].id, "left") == 0 && std::strcmp(Delay::inputs[1].id, "right") == 0);
    REQUIRE(std::strcmp(Delay::inputs[0].stereo_pair, "stereo") == 0 &&
            std::strcmp(Delay::outputs[1].stereo_pair, "stereo") == 0);
    REQUIRE(Delay::params.size() == 6 && std::strcmp(Delay::params[5].id, "mix") == 0);
    ws2_memory memory{sizeof(memory)};
    const ws2_config config{sizeof(config), 96000, 256, 16, 7, nullptr, 0};
    REQUIRE(w.d->memory_requirements(&config, &memory) == WS2_OK &&
            memory.large_bytes >= size_t(2) * 2 * 96000 * sizeof(float));
}

// Constant inputs isolate the mode-routing step from source transients and feedback.
// A real switch must transition smoothly in both directions at every supported rate;
// no-switch startup and the settled routes must retain their exact values.
static void mode_switch_crossfade() {
    constexpr size_t length = 32768, at = 8192;
    for (double rate : {44100., 48000., 96000.}) {
        for (uint32_t initial : {0u, 1u}) {
            Rig r(rate), control(rate);
            for (Rig *q : {&r, &control}) {
                wet_only(*q, .0625f, 0.f);
                q->values[kMode] = static_cast<float>(initial);
                q->left(Wave(length, .4f));
                q->right(Wave(length, .2f));
            }
            const Result before = control.run(length, 64);
            const Result after = r.run(length, 64, [&](size_t first) {
                if (first == at) r.values[kMode] = static_cast<float>(1u - initial);
            }, true);
            REQUIRE(finite_within(after.l, 1.f) && finite_within(after.r, 1.f));
            // No switch-related output change until the edit; the propagation
            // through the line is deliberately not assumed to land on a block.
            for (size_t i = 0; i < at; ++i) {
                REQUIRE(after.l[i] == before.l[i]);
                REQUIRE(after.r[i] == before.r[i]);
            }
            float peak_step = 0.f;
            for (size_t i = at; i < length; ++i) {
                peak_step = std::max(peak_step, std::fabs(after.l[i] - after.l[i - 1]));
                peak_step = std::max(peak_step, std::fabs(after.r[i] - after.r[i - 1]));
            }
            std::printf("delay mode switch rate=%.0f from=%u peak_step=%.9g\n",
                        rate, initial, static_cast<double>(peak_step));
            std::fflush(stdout);
            // With this DC stimulus the old step is .2 FS. A transition
            // reduces its adjacent-sample peak by at least 20 dB (.02 FS).
            REQUIRE(peak_step <= .02f);
            const float left = initial == 0 ? .3f : .4f;
            const float right = initial == 0 ? 0.f : .2f;
            for (size_t i = length - 64; i < length; ++i) {
                REQUIRE(std::fabs(after.l[i] - left) < 1e-6f);
                REQUIRE(std::fabs(after.r[i] - right) < 1e-6f);
            }
        }
    }
}

// With an isolated impulse at the first echo, the tone change reaches the
// output exactly one more delay later. Compare against a no-switch control
// to distinguish a feedback-filter jump from the original impulse.
static bool tone_switch_crossfade() {
    constexpr size_t delay = 3000, at = 8192, length = 16384;
    bool smooth = true;
    for (uint32_t initial : {0u, 1u}) {
        Rig r, control;
        for (Rig *q : {&r, &control}) {
            wet_only(*q, .0625f, .8f);
            q->values[kTone] = static_cast<float>(initial);
            q->left(impulse(length, at - delay, .5f));
        }
        const Result before = control.run(length, 64);
        const Result after = r.run(length, 64, [&](size_t first) {
            if (first == at) r.values[kTone] = static_cast<float>(1u - initial);
        }, true);
        REQUIRE(finite_within(after.l, 2.f) && finite_within(after.r, 2.f));
        for (size_t i = 0; i < at + delay; ++i)
            REQUIRE(after.l[i] == before.l[i] && after.r[i] == before.r[i]);
        const float step = std::fabs(after.l[at + delay] - before.l[at + delay]);
        std::printf("delay tone switch from=%u first_feedback_delta=%.9g\n",
                    initial, static_cast<double>(step));
        smooth = smooth && step <= .01f;
    }
    std::fflush(stdout);
    return smooth;
}

// A 6000-frame clock period selects an exact 6000-frame old tap and
// 9000-frame new tap. A repeating impulse-like source distinguishes them.
// The first switched sample must not jump to a fractional read position;
// once settled, the new tap must agree with its independent control.
static bool division_switch_crossfade() {
    constexpr size_t length = 65536, at = 32768;
    Wave source(length);
    for (size_t i = 0; i < length; ++i) source[i] = i % 7 == 0 ? -.4f : .4f;
    bool smooth = true;
    for (bool clocked : {false, true}) {
        Rig r, control, settled;
        for (Rig *q : {&r, &control, &settled}) {
            wet_only(*q, .125f, 0.f);
            q->values[kDivision] = 5.f;
            q->left(source);
            if (clocked) q->clock(pulses(length, 6000));
        }
        settled.values[kDivision] = 6.f;
        const Result before = control.run(length, 64);
        const Result endpoint = settled.run(length, 64);
        const Result after = r.run(length, 64, [&](size_t first) {
            if (first == at) r.values[kDivision] = 6.f;
        }, true);
        REQUIRE(finite_within(after.l, 1.f) && finite_within(after.r, 1.f));
        for (size_t i = 0; i < at; ++i)
            REQUIRE(after.l[i] == before.l[i] && after.r[i] == before.r[i]);
        if (!clocked) {
            REQUIRE(after.l == before.l && after.r == before.r);
        } else {
            const float step = std::fabs(after.l[at] - before.l[at]);
            std::printf("delay clocked division first_delta=%.9g\n",
                        static_cast<double>(step));
            smooth = smooth && step <= .02f;
            for (size_t i = length - 64; i < length; ++i) {
                REQUIRE(std::fabs(after.l[i] - endpoint.l[i]) < 1e-4f);
                REQUIRE(std::fabs(after.r[i] - endpoint.r[i]) < 1e-4f);
            }
        }
    }
    std::fflush(stdout);
    return smooth;
}

// Interrupt the 10 ms transitions with new targets before they settle.
// Same sample schedule must produce the same bits across block boundaries;
// resetting during a transition must discard all transient state.
static void interrupted_switches() {
    constexpr size_t length = 32768, at = 16384;
    for (double rate : {44100., 48000., 96000.}) {
        auto run = [&](Rig &r, uint32_t block, size_t count) {
            wet_only(r, .125f, .65f);
            r.values[kMode] = 0.f;
            r.values[kTone] = 0.f;
            r.values[kDivision] = 5.f;
            r.left(noise(length, 101, .5f));
            r.right(noise(length, 102, .5f));
            r.clock(pulses(length, 6000));
            return r.run(count, block, [&](size_t first) {
                if (first == at) {
                    r.values[kMode] = 1.f;
                    r.values[kTone] = 1.f;
                    r.values[kDivision] = 6.f;
                } else if (first == at + 128) {
                    r.values[kMode] = 0.f;
                    r.values[kTone] = -1.f;
                    r.values[kDivision] = 4.f;
                } else if (first == at + 256) {
                    r.values[kMode] = 1.f;
                    r.values[kTone] = 0.f;
                    r.values[kDivision] = 5.f;
                }
            }, true);
        };
        Rig a(rate), b(rate), c(rate);
        const Result reference = run(a, 128, length);
        REQUIRE(finite_within(reference.l, 1.6f) && finite_within(reference.r, 1.6f));
        const Result smaller = run(b, 16, length);
        REQUIRE(smaller.l == reference.l && smaller.r == reference.r);
        run(c, 64, at + 320); // reset with a division fade in progress
        c.d->reset(c.instance);
        const Result reset = run(c, 64, length);
        REQUIRE(reset.l == reference.l && reset.r == reference.r);
    }

    // Multiple division requests within one fade: the last request wins.
    // With zero feedback, the eventual output is exactly the selected tap.
    Rig changed, settled;
    for (Rig *r : {&changed, &settled}) {
        wet_only(*r, .125f, 0.f);
        r->left(noise(length, 103, .5f));
        r->clock(pulses(length, 6000));
    }
    settled.values[kDivision] = 4.f;
    const Result endpoint = settled.run(length, 64);
    const Result got = changed.run(length, 64, [&](size_t first) {
        if (first == at) changed.values[kDivision] = 6.f;
        if (first == at + 128) changed.values[kDivision] = 5.f;
        if (first == at + 256) changed.values[kDivision] = 4.f;
    }, true);
    // Division 4 is 3/4 of the measured 6000-frame period: 4500 samples.
    // The unchanged acquisition glide in the static reference starts at 6000
    // and still has ~.022 sample residual here. It is not an exact-tap oracle.
    // At feedback=0, wet=1, stereo, unpatched right and |source|<=.5,
    // the settled output on both channels is the original input at i-4500.
    // Keep the old reference as a diagnostic, not as the expected samples.
    constexpr size_t final_tap = 4500;
    float reference_error = 0.f, actual_error = 0.f;
    for (size_t i = length - 64; i < length; ++i) {
        const float expected = changed.in[0].ch[0][i - final_tap];
        reference_error = std::max(reference_error, std::fabs(endpoint.l[i] - expected));
        reference_error = std::max(reference_error, std::fabs(endpoint.r[i] - expected));
        actual_error = std::max(actual_error, std::fabs(got.l[i] - expected));
        actual_error = std::max(actual_error, std::fabs(got.r[i] - expected));
    }
    std::printf("delay retarget endpoint: static_reference_error=%.9g actual_error=%.9g\n",
                static_cast<double>(reference_error), static_cast<double>(actual_error));
    std::fflush(stdout);
    REQUIRE(actual_error < 1e-4f);
}

// The host ramps the non-stepped tone parameter over 5 ms. A change
// of filter kind starts one 10 ms transition; subsequent samples of that
// same-kind ramp must update its target without rearming its duration.
// White-box timing regression, complementary to the callback-level
// impulse, reset, allocation and block-invariance tests above.
static void tone_ramp_does_not_rearm() {
    for (double rate : {44100., 48000., 96000.}) {
        Delay d;
        d.rate = rate;
        const auto fade = static_cast<uint32_t>(std::ceil(.010 * rate));
        const auto ramp = static_cast<uint32_t>(std::ceil(.005 * rate));
        d.fade_frames = fade;
        d.update_tone(0.f);
        for (uint32_t i = 0; i < fade; ++i) {
            const float target = std::min(1.f, static_cast<float>(i + 1) / static_cast<float>(ramp));
            d.update_tone(target);
            d.advance_tone();
        }
        std::printf("delay tone ramp: rate=%.0f fade=%u remaining=%u\n", rate, fade, d.tone_left);
        std::fflush(stdout);
        REQUIRE(d.tone_left == 0);
        REQUIRE(d.tone_high == 1.f && d.tone_low == 0.f);
        REQUIRE(d.tone_current_a == d.tone_a);

        // An actual kind reversal while a fade is active does start a new
        // transition from the current blend, not a jump to the endpoint.
        d.update_tone(0.f);
        d.advance_tone();
        const float high = d.tone_high, low = d.tone_low;
        d.update_tone(-1.f);
        REQUIRE(d.tone_left == fade);
        REQUIRE(d.tone_high == high && d.tone_low == low);
        for (uint32_t i = 0; i < fade; ++i) d.advance_tone();
        REQUIRE(d.tone_left == 0);
        REQUIRE(d.tone_low == 1.f && d.tone_high == 0.f);
        REQUIRE(d.tone_current_a == d.tone_a);
    }
}

// SR6 first-divergence diagnostic against the pinned pre-S7.6 implementation.
// Compare raw float bits, not a broadened tolerance. First test the registered
// descriptor path, then trace direct DSP state after each block separately.
static size_t first_bits(const Wave &a, const Wave &b) {
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return i;
    return a.size();
}
struct SettledTrace {
    Wave writes[2];
    std::vector<std::array<float, 10>> state;
};
template<class DSP>
static Result trace_settled(Rig &rig, SettledTrace &trace, size_t length, uint32_t block) {
    DSP dsp;
    const ws2_config config{sizeof(config), rig.rate, 256, 16, 7, nullptr, 0};
    REQUIRE(dsp.prepare(ws::Setup{config, rig.host}));
    dsp.reset();
    for (auto &w : trace.writes) w.reserve(length);
    trace.state.reserve((length + block - 1) / block);
    return rig.run(length, block, {}, false, [&](ws2_process &ctx) {
        const uint32_t first = dsp.w;
        ws::Block b(ctx);
        dsp.process(b);
        for (unsigned ch = 0; ch < 2; ++ch)
            for (uint32_t i = 0; i < ctx.frames; ++i)
                trace.writes[ch].push_back(dsp.line[ch][(first + i) % dsp.size]);
        trace.state.push_back({dsp.tone_a, dsp.tone_k, dsp.lp[0], dsp.lp[1],
                               dsp.dc_x[0], dsp.dc_x[1], dsp.dc_y[0], dsp.dc_y[1],
                               static_cast<float>(dsp.cur), static_cast<float>(dsp.applied)});
        return WS2_OK;
    });
}

static void settled_reference_diagnosis() {
    constexpr size_t length = 65536;
    bool identical = true;
    struct Case { float time, feedback, tone, mode; bool clocked; };
    const Case cases[]{
        {.375f, 0.f, 0.f, 0.f, false},
        {.375f, .4f, 0.f, 0.f, false},
        {.375f, .4f, -.2f, 1.f, false},
        {.24f, .4f, -.18f, 1.f, false},
        {.125f, .4f, 0.f, 0.f, true},
    };
    const auto *ref_descriptor = ws::Module<fxdelay_reference::Delay>::descriptor();
    for (const auto &c : cases) {
        for (uint32_t block : {128u, 1u}) {
            auto setup = [&](Rig &rig) {
                rig.values[kTime] = c.time;
                rig.values[kFeedback] = c.feedback;
                rig.values[kTone] = c.tone;
                rig.values[kMode] = c.mode;
                rig.values[kMix] = 1.f;
                rig.left(noise(length, 301, .48f));
                rig.right(noise(length, 302, .48f));
                if (c.clocked) rig.clock(pulses(length, 6000));
            };
            Rig old(48000, ref_descriptor), now;
            setup(old); setup(now);
            const auto a = old.run(length, block);
            const auto b = now.run(length, block);
            const size_t first = std::min(first_bits(a.l, b.l), first_bits(a.r, b.r));
            std::printf("delay settled descriptor: time=%g feedback=%g tone=%g mode=%g clocked=%d block=%u first=%zu/%zu\n",
                        double(c.time), double(c.feedback), double(c.tone), double(c.mode), c.clocked,
                        block, first, length);
            identical = identical && first == length;
            // Traces use fresh objects, same fixture, and no product instrumentation.
            // Trace at 128-frame partition; the single-sample descriptor run
            // above remains a distinct block-partition check.
            if (block != 128u) continue;
            Rig direct_old(48000, ref_descriptor), direct_now;
            setup(direct_old); setup(direct_now);
            SettledTrace tr0, tr1;
            const auto x = trace_settled<fxdelay_reference::Delay>(direct_old, tr0, length, block);
            const auto y = trace_settled<Delay>(direct_now, tr1, length, block);
            const size_t out = std::min(first_bits(x.l, y.l), first_bits(x.r, y.r));
            const size_t write = std::min(first_bits(tr0.writes[0], tr1.writes[0]),
                                          first_bits(tr0.writes[1], tr1.writes[1]));
            size_t state = 0;
            while (state < tr0.state.size() &&
                   std::memcmp(tr0.state[state].data(), tr1.state[state].data(),
                               sizeof(tr0.state[state])) == 0) ++state;
            std::printf("delay settled direct: output=%zu ring_write=%zu state_block=%zu/%zu\n",
                        out, write, state, tr0.state.size());
            if (write < length)
                for (unsigned ch = 0; ch < 2; ++ch)
                    std::printf("  ring[%u] at %zu: %a -> %a\n", ch, write,
                                double(tr0.writes[ch][write]), double(tr1.writes[ch][write]));
            if (state < tr0.state.size()) {
                const char *labels[]{"tone_a", "tone_k", "lpL", "lpR", "dc_xL", "dc_xR", "dc_yL", "dc_yR", "cur", "applied"};
                for (unsigned k = 0; k < 10; ++k)
                    std::printf("  %s: %a -> %a\n", labels[k], double(tr0.state[state][k]), double(tr1.state[state][k]));
            }
            // The direct trace is diagnostic: compilation/inlining is not
            // the descriptor path. Only exact descriptor PCM gates here.
        }
    }
    std::fflush(stdout);
    REQUIRE(identical);
}


 // Effective parameter rows may change within a block. Test the original
 // sample cadence, rather than merely changing the stored value per block.
static void batch_b_effective_rows() {
    const auto *desc = ws::Module<Delay>::descriptor();
    const uint32_t expected = WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE;
    REQUIRE(desc->params[kDivision].flags == expected);
    REQUIRE(desc->params[kMode].flags == expected);
    for (unsigned channels : {1u, 16u}) {
        for (bool clocked : {false, true}) {
            Rig r;
            wet_only(r, .001f, 0.f);
            r.in[0].connected = true;
            r.in[0].ch.assign(channels, Wave(128, .01f));
            if (clocked) {
                Wave clock(128, 0.f);
                clock[0] = clock[64] = 1.f;
                r.clock(std::move(clock));
            }
            Delay dsp;
            const ws2_config config{sizeof(config), r.rate, 256, 16, 7, nullptr, 0};
            REQUIRE(dsp.prepare(ws::Setup{config, r.host}));
            float mode[128]{}, division[128]{};
            for (unsigned i = 0; i < 128; ++i) {
                mode[i] = i < 80 ? 0.f : 1.f;
                division[i] = i < 80 ? 5.f : 6.f;
            }
            const float *mode_row = mode, *division_row = division;
            const auto out = r.run(128, 128, {}, true, [&](ws2_process &ctx) {
                ws2_signal effective[6];
                std::copy_n(ctx.params, 6, effective);
                effective[kMode] = {sizeof(ws2_signal), &mode_row, 1, 0};
                effective[kDivision] = {sizeof(ws2_signal), &division_row, 1, 0};
                ctx.params = effective;
                ws::Block b(ctx);
                dsp.process(b);
                return WS2_OK;
            });
            REQUIRE(finite_within(out.l, 1.6f) && finite_within(out.r, 1.6f));
            REQUIRE(std::fabs(dsp.mode_blend - 48.f * dsp.mode_step) < 1e-6f);
            const float input = .01f * channels;
            REQUIRE(std::fabs(dsp.line[1][79] - input) < 1e-6f);
            REQUIRE(std::fabs(dsp.line[1][80] - input * (1.f - dsp.mode_step)) < 1e-6f);
            if (clocked) {
                REQUIRE(dsp.applied == 64.);
                REQUIRE(dsp.division_seen == 6);
                REQUIRE(dsp.division_to == 96.);
                REQUIRE(dsp.division_left == dsp.fade_frames - 48);
            } else {
                REQUIRE(dsp.division_seen == -1 && dsp.division_left == 0);
            }
            { allocation_watch::Scope watch; dsp.reset(); REQUIRE(watch.allocations() == 0); }
            REQUIRE(dsp.mode_blend == 0.f && dsp.division_seen == -1);
        }
    }
    std::puts("PASS delay batch B flags and sample-addressed mode/division");
}

int main() {
    batch_b_effective_rows();
    settled_reference_diagnosis();
    echo_position();
    feedback_decay();
    ping_pong();
    clock_sync();
    mix_law();
    unpatched_right();
    poly_sum();
    loop_safety();
    tone_shapes_loop();
    mode_switch_crossfade();
    const bool tone_smooth = tone_switch_crossfade();
    const bool division_smooth = division_switch_crossfade();
    REQUIRE(tone_smooth);
    REQUIRE(division_smooth);
    interrupted_switches();
    tone_ramp_does_not_rearm();
    matrices();
    state_and_extremes();
    std::puts("fx.delay tests passed");
    return 0;
}
