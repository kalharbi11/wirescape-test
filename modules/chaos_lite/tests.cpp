// SPDX-License-Identifier: GPL-3.0-only
#include "../../engine/engine2.h"
#include "../../sdk/examples/test_support.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include "dsp.h"
#include <chrono>
#include <complex>
#include <cstring>
#include <string>
#include <vector>

struct Arena {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(4 * 1024 * 1024);
    size_t used = 0;
    static void* WS2_CALL alloc(void* context, size_t bytes, size_t alignment, uint32_t) {
        auto& a = *static_cast<Arena*>(context);
        const uintptr_t base = reinterpret_cast<uintptr_t>(a.bytes.data());
        const uintptr_t next = (base + a.used + alignment - 1) & ~(uintptr_t(alignment) - 1);
        const size_t end = next - base + bytes;
        if (end > a.bytes.size()) return nullptr;
        a.used = end;
        return reinterpret_cast<void*>(next);
    }
};
struct Rig {
    Arena arena;
    ws2_host host{sizeof(host), &arena, Arena::alloc};
    const ws2_descriptor* descriptor = ws::Module<chaos_lite::ChaosLite>::descriptor();
    void* instance = nullptr;
    static constexpr uint32_t max_frames = 256, max_channels = 16, parameters = 27;
    struct Buffers { float out[2][max_channels][max_frames]{}; };
    std::unique_ptr<Buffers> buffers = std::make_unique<Buffers>();
    float input_values[4][max_channels]{};
    const float* input_rows[4][max_channels]{};
    ws2_signal inputs[4]{};
    float values[parameters]{};
    const float* value_rows[parameters]{};
    ws2_signal params[parameters]{};
    float* output_rows[2][max_channels]{};
    ws2_signal_out outputs[2]{};
    ws2_process context{};
    explicit Rig(double rate = 48000, uint32_t channels = 4, uint64_t seed = 42) {
        const ws2_config config{sizeof(config), rate, max_frames, max_channels, seed, nullptr, 0};
        instance = descriptor->create(&host);
        REQUIRE(instance && descriptor->prepare(instance, &config, &host) == WS2_OK);
        for (uint32_t p = 0; p < 4; ++p) {
            for (uint32_t c = 0; c < max_channels; ++c) {
                input_values[p][c] = p == 1 || p == 2 ? 1.f : 0.f;
                input_rows[p][c] = &input_values[p][c];
            }
            inputs[p] = {sizeof(ws2_signal), input_rows[p], channels, WS2_CONSTANT | WS2_CONNECTED};
        }
        for (uint32_t p = 0; p < parameters; ++p) {
            values[p] = chaos_lite::ChaosLite::params[p].initial;
            value_rows[p] = &values[p]; params[p] = {sizeof(ws2_signal), &value_rows[p], 1, WS2_CONSTANT};
        }
        for (uint32_t p = 0; p < 2; ++p) {
            for (uint32_t c = 0; c < max_channels; ++c) output_rows[p][c] = buffers->out[p][c];
            outputs[p] = {sizeof(ws2_signal_out), output_rows[p], channels, 0, max_channels};
        }
        context.struct_size = sizeof(context); context.sample_rate = rate; context.channels = channels;
        context.input_count = 4; context.output_count = 2; context.param_count = parameters;
        context.inputs = inputs; context.outputs = outputs; context.params = params;
    }
    ~Rig() { descriptor->destroy(instance); }
    void run(uint32_t frames) {
        context.frames = frames;
        int result;
        { allocation_watch::Scope watch; result = descriptor->process(instance, &context); REQUIRE(watch.allocations() == 0); }
        REQUIRE(result == WS2_OK);
        REQUIRE(!(outputs[0].flags & WS2_CONSTANT) && !(outputs[1].flags & WS2_CONSTANT));
        context.frame_position += frames;
        for (uint32_t p = 0; p < 2; ++p) for (uint32_t c = 0; c < context.channels; ++c)
            for (uint32_t i = 0; i < frames; ++i) {
                const float value = buffers->out[p][c][i];
                REQUIRE(std::isfinite(value) && std::abs(value) <= .8912509f);
                if (c >= 8) REQUIRE(value == 0);
            }
    }
    Bytes save() { Bytes bytes; ws2_writer writer{sizeof(writer), &bytes, sink}; REQUIRE(descriptor->save_state(instance, &writer) == WS2_OK); return bytes; }
    int load(const Bytes& bytes) { Source source_context{&bytes, 0}; ws2_reader reader{sizeof(reader), &source_context, source, static_cast<uint32_t>(bytes.size())}; return descriptor->load_state(instance, &reader); }
};
static std::vector<float> render(Rig& r, uint32_t block) {
    std::vector<float> result(2 * r.context.channels * 256);
    for (uint32_t first = 0; first < 256; first += block) {
        const uint32_t n = std::min(block, 256u - first); r.run(n);
        for (uint32_t p = 0; p < 2; ++p) for (uint32_t c = 0; c < r.context.channels; ++c)
            for (uint32_t i = 0; i < n; ++i) result[(p * r.context.channels + c) * 256 + first + i] = r.buffers->out[p][c][i];
    }
    return result;
}
static void matrix() {
    for (double rate : {44100., 48000., 96000.}) for (uint32_t channels : {1u, 4u, 16u}) {
        Rig whole(rate, channels); const auto reference = render(whole, 256);
        REQUIRE(*std::max_element(reference.begin(), reference.end()) > 0);
        for (uint32_t block : {1u, 17u, 64u}) { Rig cut(rate, channels); REQUIRE(render(cut, block) == reference); }
        whole.descriptor->reset(whole.instance); REQUIRE(render(whole, 17) == reference);
        if (channels > 1) REQUIRE(!std::equal(reference.begin(), reference.begin() + 256, reference.begin() + 256));
    }
}
static void state_and_extremes() {
    Rig a, b; a.run(256); const auto bytes = a.save(); REQUIRE(b.load(bytes) == WS2_OK); REQUIRE(b.save() == bytes);
    Bytes bad = bytes; bad[0] = 99; REQUIRE(b.load(bad) == WS2_UNSUPPORTED); REQUIRE(b.save() == bytes);
    bad = bytes; bad.pop_back(); REQUIRE(b.load(bad) == WS2_INVALID); REQUIRE(b.save() == bytes);
    for (uint32_t mode = 0; mode < 3; ++mode) for (uint32_t extreme = 0; extreme < 2; ++extreme) {
        Rig r(96000, 16);
        for (uint32_t p = 0; p < Rig::parameters; ++p) r.values[p] = extreme ? chaos_lite::ChaosLite::params[p].maximum : chaos_lite::ChaosLite::params[p].minimum;
        r.values[9] = static_cast<float>(mode);
        for (uint32_t c = 0; c < 16; ++c) { r.input_values[0][c] = c & 1 ? 1e6f : -1e6f; r.input_values[3][c] = 1e6f; }
        for (uint32_t block = 0; block < 32; ++block) r.run(256);
    }
    Rig muted; for (auto& x : muted.input_values[1]) x = 0; muted.run(256);
    for (float x : muted.buffers->out[0][0]) REQUIRE(x == 0);
    Rig mono(48000, 4); mono.inputs[0].channels = 1; mono.input_values[0][0] = .5f; mono.run(256);
    REQUIRE(std::strcmp(chaos_lite::ChaosLite::outputs[0].stereo_pair, "stereo") == 0);
    REQUIRE(std::strcmp(chaos_lite::ChaosLite::outputs[1].stereo_pair, "stereo") == 0);
}
static void components() {
    // Parallel coupling is a plain sum at every depth; zero-depth AM preserves it.
    float memory[512]{}; chaos_lite::Coupler coupler; coupler.attach(memory, 512); coupler.reset();
    REQUIRE(coupler.process(.25f, -.5f, 0, 1, 48000) == -.25f);
    REQUIRE(coupler.process(.25f, -.5f, 1, 0, 48000) == -.25f);
    REQUIRE(coupler.process(.25f, -.5f, 1, 1, 48000) == -.625f);
    // A zero-feedback impulse has no response before the shortest comb delay,
    // and eventually leaves the buffer entirely (no self-sustaining feedback).
    float comb_memory[3 * 128]{}; chaos_lite::CombBody body; body.attach(comb_memory, 128); body.reset();
    REQUIRE(body.process(1, 60, 0, .5f) == 0);
    for (int i = 1; i < 29; ++i) REQUIRE(body.process(0, 60, 0, .5f) == 0);
    bool heard = false;
    for (int i = 29; i < 120; ++i) heard = heard || body.process(0, 60, 0, .5f) != 0;
    REQUIRE(heard);
    for (int i = 0; i < 256; ++i) body.process(0, 60, 0, .5f);
    REQUIRE(body.process(0, 60, 0, .5f) == 0);
    Rig r; const auto prior = r.save(); Bytes bad = prior;
    bad[20] = 0; bad[21] = 0; bad[22] = 0xc0; bad[23] = 0x7f;
    REQUIRE(r.load(bad) == WS2_INVALID); REQUIRE(r.save() == prior);
}
static void lfo_shapes_and_legacy_state() {
    // Every waveform must be independent of the host's process block cuts.
    // Use fast enough modulation to cross multiple seeded random cycles.
    for (double rate : {44100., 48000., 96000.}) {
        for (uint32_t shape = 0; shape < 4; ++shape) {
            Rig whole(rate, 4), cut(rate, 4);
            whole.values[26] = cut.values[26] = static_cast<float>(shape);
            whole.values[20] = cut.values[20] = 12.f;
            whole.values[21] = cut.values[21] = .6f;
            REQUIRE(render(whole, 256) == render(cut, 17));
            for (uint32_t k = 0; k < 24; ++k) {
                whole.run(256);
                cut.run(256);
            }
            REQUIRE(whole.save() == cut.save());
        }
    }
    // A live switch returns to sine without clicks from stale random state;
    // default zero still renders through the unchanged sine path.
    Rig switched;
    switched.run(256);
    for (uint32_t shape : {3u, 2u, 1u, 0u}) {
        switched.values[26] = static_cast<float>(shape);
        for (uint32_t k = 0; k < 12; ++k) switched.run(256);
    }
    // Load an authentic v1 byte layout (64 bytes per voice) extracted from
    // the preserved prefix of v2. v1 has no LFO random generator/shape data.
    Rig original;
    original.run(256);
    const Bytes modern = original.save();
    REQUIRE(modern.size() == 4 + 8 * (64 + 40));
    Bytes legacy;
    for (uint8_t b : {uint8_t(1), uint8_t(0), uint8_t(0), uint8_t(0)}) legacy.push_back(b);
    for (uint32_t c = 0; c < 8; ++c) {
        const size_t offset = 4 + c * (64 + 40);
        for (size_t i = 0; i < 64; ++i) legacy.push_back(modern[offset + i]);
    }
    Rig restored;
    REQUIRE(restored.load(legacy) == WS2_OK);
    const Bytes new_state = restored.save();
    for (size_t c = 0; c < 8; ++c) {
        REQUIRE(std::equal(legacy.begin() + 4 + c * 64, legacy.begin() + 4 + (c + 1) * 64,
                           new_state.begin() + 4 + c * (64 + 40)));
    }
    restored.run(256); // accepts a migrated old patch without changing old ID semantics
    // A state captured mid-shape-switch at 96 kHz may have 959 samples
    // of its 10 ms fade left; loading at 44.1 kHz must not reject it.
    Rig high_rate(96000, 4);
    high_rate.values[26] = 3.f;
    high_rate.run(1);
    const Bytes high_rate_state = high_rate.save();
    Rig same_rate(96000, 4);
    REQUIRE(same_rate.load(high_rate_state) == WS2_OK);
    REQUIRE(same_rate.save() == high_rate_state);
    Rig lower_rate(44100, 4);
    REQUIRE(lower_rate.load(high_rate_state) == WS2_OK);
    lower_rate.values[26] = 3.f;
    lower_rate.run(256);
}

// State-byte offsets for voice 0 (state v2: 4-byte header, 104 bytes per voice).
static float state_float(const Bytes &state, size_t offset) {
    float v;
    std::memcpy(&v, state.items + offset, 4);
    return v;
}
static void lfo_switch_steps_and_triangle_phase() {
    constexpr size_t phase_offset = 4 + 16 + 2 * 4, previous_offset = 4 + 16 + 3 * 4;
    constexpr size_t last_shape_offset = 4 + 64 + 32, fade_offset = last_shape_offset + 4;
    // Switching lfo_shape mid-note crossfades (10 ms): the LFO value moves by at most
    // |difference| / fade length plus the target shape's own slope per sample. Sampled
    // one sample at a time at 48 kHz, 12 Hz, no self-modulation, before the first wrap.
    for (uint32_t from = 0; from < 4; ++from) {
        for (uint32_t to = 0; to < 4; ++to) {
            if (from == to) continue;
            Rig r(48000, 1);
            r.values[20] = 12.f;
            r.values[21] = .5f;
            r.values[23] = 0.f;
            r.values[26] = static_cast<float>(from);
            r.run(256);
            r.run(256);
            r.run(88); // Initial crossfade is over; phase is far from a wrap (4000 samples).
            float last = state_float(r.save(), previous_offset);
            float worst = 0;
            r.values[26] = static_cast<float>(to);
            for (int i = 0; i < 500; ++i) {
                r.run(1);
                const float now = state_float(r.save(), previous_offset);
                worst = std::max(worst, std::abs(now - last));
                last = now;
            }
            REQUIRE(worst < 2.f / 480.f + .002f); // 0.0062: crossfade step + shape slope
        }
    }
    // Triangle phase: at phase 0 the triangle is +1 (peak), 4|phase - .5| - 1.
    Rig t(48000, 1);
    t.run(1);
    Bytes state = t.save();
    const float zero = 0.f;
    const uint32_t one = 1, none = 0;
    std::memcpy(state.items + phase_offset, &zero, 4);
    std::memcpy(state.items + last_shape_offset, &one, 4);
    std::memcpy(state.items + fade_offset, &none, 4);
    t.values[26] = 1.f;
    t.values[23] = 0.f;
    REQUIRE(t.load(state) == WS2_OK);
    t.run(1);
    REQUIRE(std::abs(state_float(t.save(), previous_offset) - 1.f) < 1e-6f);
}

static int benchmark() {
    using namespace wirescape::engine2;
    Config config; config.max_block = 128; config.max_channels = 4; config.seed = 42;
    Engine engine(config);
    for (uint32_t node : {1u, 2u}) {
        REQUIRE(engine.add_node(node, ws::Module<chaos_lite::ChaosLite>::descriptor()));
        REQUIRE(engine.set_channels(node, 4)); REQUIRE(engine.output(node, 0, (node - 1) * 2)); REQUIRE(engine.output(node, 1, (node - 1) * 2 + 1));
    }
    REQUIRE(engine.prepare());
    float samples[4][128]{}; float* rows[]{samples[0], samples[1], samples[2], samples[3]};
    for (int i = 0; i < 256; ++i) REQUIRE(engine.render(rows, 4, 128) == WS2_OK);
    std::vector<double> times(10000);
    for (auto& time : times) {
        const auto before = std::chrono::steady_clock::now();
        { allocation_watch::Scope watch; REQUIRE(engine.render(rows, 4, 128) == WS2_OK); REQUIRE(watch.allocations() == 0); }
        time = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before).count();
    }
    REQUIRE(engine.meter().faults == 0); std::sort(times.begin(), times.end());
    std::printf("{\"graph\":\"chaos_lite_2x4\",\"rate\":48000,\"block\":128,\"blocks\":10000,\"median_us\":%.3f,\"p95_us\":%.3f,\"worst_us\":%.3f,\"setup_bytes\":%zu}\n", (times[4999] + times[5000]) * .5, times[9499], times.back(), engine.meter().setup_bytes);
    return times.back() < 1333.333333 ? 0 : 1;
}
// Owner v1 report: with the gate unpatched (constant 1) a fall never restarted,
// so the pitch stayed at the floor after Fall returned to zero, until panic.
static void fall_recovers_and_retriggers() {
    constexpr float rate = 48000;
    chaos_lite::Drift d; d.reset(42, 0);
    float drop = 0;
    for (int i = 0; i < 48000; ++i) drop = -d.process(1, 0, 0, 1, rate);
    REQUIRE(drop > .99f && drop < 1.01f);
    // Constant gate, fall knob back to zero: the drop returns (16 oct/s), no edge needed.
    for (int i = 0; i < 2999; ++i) drop = -d.process(1, 0, 0, 0, rate);
    REQUIRE(drop > 0);
    for (int i = 0; i < 4; ++i) drop = -d.process(1, 0, 0, 0, rate);
    REQUIRE(drop == 0);
    // Held gate, fall on: a new note restarts the drop.
    for (int i = 0; i < 24000; ++i) drop = -d.process(1, 0, 0, 1, rate);
    REQUIRE(drop > .49f);
    drop = -d.process(1, 0, 0, 1, rate, true);
    REQUIRE(drop > 0 && drop < 2.f / rate);
    // The maximum drop stays bounded at four octaves.
    for (int i = 0; i < 5 * 48000; ++i) drop = -d.process(1, 0, 0, 1, rate);
    REQUIRE(drop == 4.f);
}

// S7.3: T7.0's restricted alias bins, not the total non-harmonic residual.
// The exact 2 s render, first-fifth discard, Hann taper and +/-4 bin masks
// mirror bench/offline/metrics.py. This folder oracle uses real callback PCM;
// the separate hosted Offline facts run remains the authoritative graph render.
using Complex = std::complex<double>;
static void alias_fft(const double* x, size_t stride, size_t n, Complex* out,
                      const std::vector<Complex>& roots) {
    if (n == 1) { out[0] = *x; return; }
    size_t radix = 2;
    while (n % radix != 0) ++radix;
    REQUIRE(radix <= 7);
    const size_t m = n / radix, root_step = roots.size() / n;
    for (size_t j = 0; j < radix; ++j)
        alias_fft(x + j * stride, stride * radix, m, out + j * m, roots);
    for (size_t k = 0; k < m; ++k) {
        Complex v[7]{};
        for (size_t j = 0; j < radix; ++j) v[j] = out[j * m + k];
        for (size_t l = 0; l < radix; ++l) {
            Complex sum{};
            const size_t bin = k + l * m;
            for (size_t j = 0; j < radix; ++j)
                sum += v[j] * roots[(j * bin * root_step) % roots.size()];
            out[bin] = sum;
        }
    }
}
static void s73_alias_rows() {
    struct Row { int rate, hz, shape; double before_db, before_rms; size_t bins; };
    // Committed x64 T7.0 baseline, capture 44029050283, source 1b36751e44d99536166962094d52a713dfb989a2.
    const Row rows[] = {
        {44100, 110, 0, -39.39643340467205, 0.20381768415729856, 5409},
        {44100, 1760, 0, -27.315589379901336, 0.21030228610159382, 342},
        {44100, 3520, 0, -24.500051695630376, 0.20858527275462563, 171},
        {44100, 7040, 0, -21.956223763668348, 0.2052060723603866, 81},
        {44100, 110, 1, -36.60500138899152, 0.3166975775392479, 5409},
        {44100, 1760, 1, -24.13029008862906, 0.3316093429064836, 342},
        {44100, 3520, 1, -21.07888147297292, 0.3294285114725236, 171},
        {44100, 7040, 1, -19.888377942469493, 0.3248892471845933, 81},
        {48000, 110, 0, -39.90765924414583, 0.2024452874794181, 5886},
        {48000, 1760, 0, -27.630974505341573, 0.2100480129702842, 369},
        {48000, 3520, 0, -24.413627774288305, 0.2080670900025708, 189},
        {48000, 7040, 0, -21.847113082463853, 0.2041565668366347, 90},
        {48000, 110, 1, -37.141149494444676, 0.3138802881042255, 5886},
        {48000, 1760, 1, -24.83447441647449, 0.3313071401181518, 369},
        {48000, 3520, 1, -21.008560872683848, 0.3287749212994898, 189},
        {48000, 7040, 1, -19.545816585486374, 0.3234972451608996, 90},
        {96000, 110, 0, -44.61498432383293, 0.18201376394554902, 11781},
        {96000, 1760, 0, -32.34513837537304, 0.20746400168463947, 738},
        {96000, 3520, 0, -29.27639670024594, 0.20297237518255015, 369},
        {96000, 7040, 0, -26.0564945504346, 0.1938544362112383, 189},
        {96000, 110, 1, -42.29083453051146, 0.27358143360187076, 11781},
        {96000, 1760, 1, -29.398718330975168, 0.3279765354648648, 738},
        {96000, 3520, 1, -26.481878135549643, 0.3222348082830644, 369},
        {96000, 7040, 1, -22.826503650907615, 0.30996179653345124, 189},
    };
    constexpr double tau = 6.28318530717958647692;
    bool all_pass = true;
    for (const auto& row : rows) {
        Rig r(row.rate, 1, 7);
        const double octaves = std::log2(row.hz / 261.6255653);
        const int octave = std::clamp(static_cast<int>(std::floor(octaves)), -3, 3);
        r.input_values[0][0] = static_cast<float>(octaves - octave);
        r.values[0] = static_cast<float>(row.shape);
        r.values[3] = static_cast<float>(octave);
        r.values[5] = 1.f; r.values[6] = 0.f; r.values[7] = 0.f;
        for (int p : {14, 15, 17, 18, 19, 21}) r.values[p] = 0.f;
        r.values[24] = 20000.f; r.values[25] = 1.f;
        const size_t frames = 2u * row.rate, discard = frames / 5, n = frames - discard;
        std::vector<double> x(n);
        double squares = 0., taper_sum = 0.;
        for (size_t first = 0; first < frames; first += 128) {
            const auto count = static_cast<uint32_t>(std::min<size_t>(128, frames - first));
            r.run(count);
            for (size_t i = 0; i < count; ++i) {
                const double sample = r.buffers->out[0][0][i];
                squares += sample * sample;
                if (first + i >= discard) {
                    const size_t k = first + i - discard;
                    const double taper = .5 - .5 * std::cos(tau * k / (n - 1));
                    x[k] = sample * taper; taper_sum += taper;
                }
            }
        }
        std::vector<Complex> roots(n), spectrum(n);
        for (size_t k = 0; k < n; ++k) roots[k] = std::polar(1., -tau * k / n);
        alias_fft(x.data(), 1, n, spectrum.data(), roots);
        std::vector<bool> harmonic(n / 2 + 1), aliases(n / 2 + 1);
        const auto mark = [&](std::vector<bool>& mask, int hz) {
            const auto center = static_cast<size_t>(std::llround(double(hz) * n / row.rate));
            for (size_t k = center > 4 ? center - 4 : 1;
                 k < std::min(mask.size(), center + 5); ++k) mask[k] = true;
        };
        const int base = row.rate / (2 * row.hz) + 1;
        for (int k = 1; k < base; ++k) mark(harmonic, k * row.hz);
        for (int k = base; k <= std::min(4096, 2 * row.rate / row.hz); ++k) {
            const int remainder = (k * row.hz) % row.rate;
            const int folded = std::min(remainder, row.rate - remainder);
            if (folded == 0 || 2 * folded >= row.rate || folded % row.hz == 0) continue;
            mark(aliases, folded);
        }
        size_t bins = 0; double power = 0.;
        for (size_t k = 1; k < aliases.size(); ++k) if (aliases[k] && !harmonic[k]) {
            ++bins; power += 2. * std::norm(spectrum[k]) / (taper_sum * taper_sum);
        }
        REQUIRE(bins == row.bins);
        const double db = 10. * std::log10(std::max(power, 1e-30));
        const double rms = std::sqrt(squares / frames);
        const double rms_delta = 20. * std::log10(rms / row.before_rms);
        const double reduction = row.before_db - db;
        const bool alias_ok = row.hz == 110 ? reduction >= -1. :
            row.rate == 96000 || db < -80. || reduction >= (row.rate == 48000 ? 20. : 15.);
        const bool rms_ok = std::abs(rms_delta) <= .5;
        std::printf("S7.3 rate=%d hz=%d shape=%s bins=%zu alias_dbfs=%.6f reduction_db=%.6f rms=%.9f rms_delta_db=%.6f %s\n",
                    row.rate, row.hz, row.shape ? "square" : "saw", bins, db, reduction, rms, rms_delta,
                    alias_ok && rms_ok ? "PASS" : "FAIL");
        all_pass = all_pass && alias_ok && rms_ok;
    }
    std::fflush(stdout); // Preserve all row diagnostics even on assertion failure.
    REQUIRE(all_pass);
}


static void mod9_stepped_streams() {
    const auto* d = ws::Module<chaos_lite::ChaosLite>::descriptor();
    REQUIRE(d->param_count == 27 && d->input_count == 4 && d->output_count == 2);
    constexpr uint32_t slots[] = {3, 4, 9, 22, 26};
    constexpr float low[] = {-3.f, -3.f, 0.f, 0.f, 0.f};
    constexpr float high[] = {3.f, 3.f, 2.f, 2.f, 3.f};
    constexpr float initial[] = {0.f, -1.f, 0.f, 0.f, 0.f};
    for (uint32_t k = 0; k < 5; ++k) {
        const auto& p = d->params[slots[k]];
        REQUIRE(p.flags == (WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE));
        REQUIRE(p.minimum == low[k] && p.maximum == high[k] &&
                p.initial == initial[k] && p.step == 1.f);
    }
    for (uint32_t channels : {1u, 16u}) for (uint32_t k = 0; k < 5; ++k) {
        Rig stream(48000, channels), scalar(48000, channels), baseline(48000, channels);
        for (Rig* r : {&stream, &scalar, &baseline}) {
            r->values[20] = 12.f;
            r->values[21] = 1.f;
            r->values[23] = 0.f;
            for (uint32_t c = 0; c < channels; ++c) r->input_values[0][c] = .02f * float(c);
            r->run(256);
        }
        const uint32_t slot = slots[k];
        float effective[Rig::max_frames]{};
        for (uint32_t i = 0; i < Rig::max_frames; ++i) {
            const uint32_t segment = i / 64;
            effective[i] = segment == 0 ? low[k] : segment == 1 ? high[k] :
                           segment == 2 ? low[k] + 1.f : high[k] - 1.f;
        }
        const float* row = effective;
        stream.params[slot] = {sizeof(ws2_signal), &row, 1, 0};
        stream.run(Rig::max_frames);
        baseline.run(Rig::max_frames);
        bool changed = false;
        for (uint32_t i = 0; i < Rig::max_frames; ++i) {
            scalar.values[slot] = effective[i];
            scalar.run(1);
            for (uint32_t side = 0; side < 2; ++side) for (uint32_t c = 0; c < channels; ++c) {
                const float actual = stream.buffers->out[side][c][i];
                REQUIRE(actual == scalar.buffers->out[side][c][0]);
                changed = changed || actual != baseline.buffers->out[side][c][i];
            }
        }
        REQUIRE(changed);
        REQUIRE(stream.save() == scalar.save());
        const auto saved = stream.save();
        REQUIRE(baseline.load(saved) == WS2_OK);
        REQUIRE(baseline.save() == saved);
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") return benchmark();
    mod9_stepped_streams();
    matrix();
    state_and_extremes();
    components();
    lfo_shapes_and_legacy_state();
    lfo_switch_steps_and_triangle_phase();
    fall_recovers_and_retriggers();
    std::puts("CHAOS-lite callback matrix/state/extremes/allocation checks passed");
    s73_alias_rows();
}
