// SPDX-License-Identifier: GPL-3.0-only
#include "../../sdk/examples/test_support.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include "dsp.h"
#include "../../engine/engine2.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
constexpr uint32_t kFrames = 256, kVoices = 16;
struct RigSettings {
    double rate = 48000;
    uint32_t voices = 1;
};
struct Rig {
    Pool pool{};
    ws2_host host{sizeof(host), &pool, allocate};
    const ws2_descriptor *descriptor = ws::Module<quad_vca::QuadVca>::descriptor();
    void *state = nullptr;
    struct Data {
        float input[8][kVoices][kFrames]{};
        float output[5][kVoices][kFrames]{};
    };
    std::unique_ptr<Data> data = std::make_unique<Data>();
    const float *input_rows[8][kVoices]{};
    float *output_rows[5][kVoices]{};
    ws2_signal inputs[8]{};
    ws2_signal_out outputs[5]{};
    float values[9]{};
    const float *param_rows[9]{};
    ws2_signal params[9]{};
    ws2_process ctx{};

    explicit Rig(RigSettings settings = {}) {
        const ws2_config cfg{sizeof(cfg), settings.rate, kFrames, kVoices, 123, nullptr, 0};
        state = descriptor->create(&host);
        REQUIRE(state && descriptor->prepare(state, &cfg, &host) == WS2_OK);
        for (uint32_t port = 0; port < 8; ++port) {
            for (uint32_t c = 0; c < kVoices; ++c) input_rows[port][c] = data->input[port][c];
            inputs[port] = {sizeof(ws2_signal), input_rows[port], settings.voices, WS2_CONSTANT};
        }
        for (uint32_t port = 0; port < 5; ++port) {
            for (uint32_t c = 0; c < kVoices; ++c) output_rows[port][c] = data->output[port][c];
            outputs[port] = {sizeof(ws2_signal_out), output_rows[port], settings.voices, 0, kVoices};
        }
        for (uint32_t p = 0; p < 9; ++p) {
            values[p] = quad_vca::QuadVca::params[p].initial;
            param_rows[p] = &values[p];
            params[p] = {sizeof(ws2_signal), &param_rows[p], 1, WS2_CONSTANT};
        }
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = settings.rate;
        ctx.channels = settings.voices;
        ctx.input_count = 8;
        ctx.output_count = 5;
        ctx.param_count = 9;
        ctx.inputs = inputs;
        ctx.outputs = outputs;
        ctx.params = params;
    }
    ~Rig() { descriptor->destroy(state); }
    void connect(uint32_t port, bool constant = true) {
        inputs[port].flags = WS2_CONNECTED | (constant ? WS2_CONSTANT : 0u);
    }
    void run(uint32_t frames) {
        ctx.frames = frames;
        int code = WS2_INVALID;
        {
            allocation_watch::Scope no_alloc;
            code = descriptor->process(state, &ctx);
            REQUIRE(no_alloc.allocations() == 0);
        }
        REQUIRE(code == WS2_OK);
        for (auto &output : outputs) REQUIRE(output.channels == ctx.channels);
        ctx.frame_position += frames;
    }
    float sample(uint32_t port, uint32_t voice = 0, uint32_t frame = 0) const {
        return data->output[port][voice][(outputs[port].flags & WS2_CONSTANT) ? 0 : frame];
    }
};

void test_legacy_slots() {
    const auto &params = quad_vca::QuadVca::params;
    REQUIRE(params.size() == 9);
    for (uint32_t p = 0; p < params.size(); ++p) REQUIRE(params[p].legacy_index == p);
}

void test_vca_and_normalling() {
    Rig r;
    r.connect(0);
    r.data->input[0][0][0] = 1.f;
    r.values[0] = .5f;
    r.run(256);
    for (auto &o : r.outputs) REQUIRE(o.flags & WS2_CONSTANT);
    REQUIRE(r.sample(0) == .5f);
    for (uint32_t k = 1; k < 4; ++k) REQUIRE(r.sample(k) == 1.f);
    REQUIRE(r.sample(4) == 3.5f);
    REQUIRE(std::abs(20.0 * std::log10(double(r.sample(0))) + 6.020599913) < .05);

    // The second channel normals from in1, not the attenuated out1.
    r.values[0] = 0.f;
    r.run(17);
    REQUIRE(r.sample(0) == 0.f && r.sample(1) == 1.f && r.sample(4) == 3.f);
    r.connect(1);
    r.data->input[1][0][0] = .25f;
    r.run(17);
    REQUIRE(r.sample(1) == .25f && r.sample(2) == .25f && r.sample(3) == .25f);

    r.data->input[0][0][0] = 2.f;
    r.inputs[1].flags = WS2_CONSTANT; // Restore all-input normalling.
    r.values[0] = 1.f;
    r.run(17);
    for (uint32_t k = 0; k < 4; ++k) REQUIRE(r.sample(k) == 2.f);
    REQUIRE(r.sample(4) == 4.f); // Only the mix is clamped.
}
void test_varying_control() {
    Rig r;
    r.connect(0, false);
    r.connect(4, false);
    r.values[0] = .8f;
    for (uint32_t i = 0; i < 256; ++i) {
        r.data->input[0][0][i] = float(i) / 256.f;
        r.data->input[4][0][i] = float(i) / 512.f;
    }
    r.run(256);
    REQUIRE(!(r.outputs[0].flags & WS2_CONSTANT));
    for (uint32_t i = 0; i < 256; ++i)
        REQUIRE(std::abs(r.sample(0, 0, i) - float(i) / 256.f * .8f * float(i) / 512.f) < 1e-6f);
}
void test_frame_rate_voice_matrix() {
    for (double rate : {32000., 48000., 96000.})
        for (uint32_t voices : {1u, 16u})
            for (uint32_t frames : {1u, 17u, 256u}) {
                Rig r({rate, voices});
                r.connect(0);
                for (uint32_t c = 0; c < voices; ++c) r.data->input[0][c][0] = .2f;
                r.run(frames);
                for (uint32_t c = 0; c < voices; ++c) {
                    REQUIRE(std::abs(r.sample(0, c) - .2f) < 1e-6f);
                    REQUIRE(std::abs(r.sample(4, c) - .8f) < 1e-6f);
                }
                r.values[4] = 1.f;
                r.run(frames);
                for (uint32_t k = 0; k < 5; ++k) {
                    for (uint32_t c = 0; c < voices; ++c) {
                        for (uint32_t i = 0; i < frames; ++i) {
                            REQUIRE(std::isfinite(r.sample(k, c, i)));
                        }
                    }
                }
            }
}
std::vector<float> release_trace(float response) {
    Rig r;
    r.connect(0);
    r.connect(4);
    r.data->input[0][0][0] = .8f;
    r.values[4] = 1.f;
    r.values[8] = response;
    r.data->input[4][0][0] = 1.f;
    for (int i = 0; i < 80; ++i) r.run(256); // settle 0.427 s
    r.data->input[4][0][0] = 0.f;
    std::vector<float> trace;
    for (int i = 0; i < 120; ++i) {
        r.run(256);
        trace.push_back(std::abs(r.sample(0, 0, 255)));
    }
    r.descriptor->reset(r.state);
    r.run(256);
    REQUIRE(std::abs(r.sample(0, 0, 0)) < 1e-7f);
    return trace;
}
void test_lpg_decay() {
    const auto fast = release_trace(0.f), slow = release_trace(1.f);
    REQUIRE(fast[0] > fast[40] && fast[40] > fast[100]);
    REQUIRE(slow[0] > slow[40] && slow[40] > slow[100]);
    REQUIRE(slow[40] > fast[40] * 2.f);
    // The release envelope is monotonic after the initial filter transition.
    for (size_t i = 4; i < fast.size(); ++i) REQUIRE(fast[i] <= fast[i - 1] + 1e-5f);
}
void test_lpg_closing_darkens() {
    constexpr double rate = 48000., pi = 3.141592653589793;
    Rig low({rate}), high({rate});
    for (Rig *r : {&low, &high}) {
        r->connect(0, false);
        r->connect(4);
        r->values[4] = 1.f;
        r->values[8] = .5f;
        r->data->input[4][0][0] = 1.f;
    }
    double held_lo = 0, held_hi = 0, tail_lo = 0, tail_hi = 0;
    auto block = [&](uint32_t block_index, bool held) {
        for (uint32_t i = 0; i < 256; ++i) {
            const double t = (block_index * 256.0 + i) / rate;
            low.data->input[0][0][i] = float(.4 * std::sin(2 * pi * 100 * t));
            high.data->input[0][0][i] = float(.4 * std::sin(2 * pi * 4000 * t));
        }
        low.run(256);
        high.run(256);
        if (held && block_index > 35) {
            for (uint32_t i = 0; i < 256; ++i) {
                held_lo += std::pow(low.sample(0, 0, i), 2);
                held_hi += std::pow(high.sample(0, 0, i), 2);
            }
        }
        if (!held && block_index > 40) {
            for (uint32_t i = 0; i < 256; ++i) {
                tail_lo += std::pow(low.sample(0, 0, i), 2);
                tail_hi += std::pow(high.sample(0, 0, i), 2);
            }
        }
    };
    for (uint32_t n = 0; n < 80; ++n) block(n, true);
    low.data->input[4][0][0] = high.data->input[4][0][0] = 0;
    for (uint32_t n = 0; n < 80; ++n) block(n, false);
    REQUIRE(held_lo > 0 && held_hi > 0 && tail_lo > 0 && tail_hi > 0);
    REQUIRE(tail_hi / held_hi < tail_lo / held_lo);
}
void test_mode_switch_crossfade() {
    // Exercise all four stepped modes at native and off-native rates, across blocks.
    // A constant input must produce nonconstant audio while the fade is active.
    for (double rate : {44100., 48000., 96000.})
        for (uint32_t k = 0; k < 4; ++k) {
            Rig r({rate});
            r.connect(0);
            r.data->input[0][0][0] = .5f;
            r.values[k] = .6f;
            r.run(256);
            const float vca = r.sample(k);
            REQUIRE(vca == .3f || std::abs(vca - .3f) < 1e-6f);
            const uint32_t fade_frames = uint32_t(rate * .015) + 1; // >10 ms

            r.values[4 + k] = 1.f;
            float previous = vca;
            for (uint32_t frame = 0; frame < fade_frames; ++frame) {
                r.run(1);
                const float current = r.sample(k);
                REQUIRE(std::isfinite(current));
                REQUIRE(std::abs(current - previous) < .02f);
                previous = current;
            }
            for (int n = 0; n < 8; ++n) r.run(256); // Settled LPG.
            previous = r.sample(k, 0, 255);

            r.values[4 + k] = 0.f;
            r.run(1);
            REQUIRE(!(r.outputs[k].flags & WS2_CONSTANT));
            REQUIRE(std::abs(r.sample(k) - previous) < .02f);
            previous = r.sample(k);
            for (uint32_t frame = 1; frame < fade_frames; ++frame) {
                r.run(1);
                const float current = r.sample(k);
                REQUIRE(std::isfinite(current));
                REQUIRE(std::abs(current - previous) < .02f);
                previous = current;
            }
            r.run(256);
            REQUIRE(r.outputs[k].flags & WS2_CONSTANT);
            REQUIRE(std::abs(r.sample(k) - vca) < 1e-6f);
        }
}
// H1 oracle: the pre-S7.5 LPG equations at base 24c41de, with independent
// follower/filter state and coefficient tables. No switch occurs in this test.
void test_lpg_at_load_and_reset() {
    for (double rate : {44100., 48000., 96000.}) {
        Rig r({rate, 2});
        std::array<float, 257> g{}, decay{};
        const double ratio = std::min(18000.0, .45 * rate) / 40.0;
        const float attack = float(std::exp(-1.0 / (.002 * rate)));
        for (uint32_t i = 0; i <= 256; ++i) {
            const double pos = double(i) / 256.0;
            const double cutoff = 40.0 * std::pow(ratio, pos);
            g[i] = float(std::tan(3.14159265358979323846 * cutoff / rate));
            const double release = .060 * std::pow(25.0, pos);
            decay[i] = float(std::exp(-1.0 / (release * rate)));
        }
        auto lookup = [](const auto &table, float x) {
            const float scaled = std::clamp(x, 0.f, 1.f) * 256.f;
            const uint32_t index = std::min<uint32_t>(uint32_t(scaled), uint32_t(255));
            return table[index] + (table[index + 1] - table[index]) * (scaled - float(index));
        };
        r.connect(0, false);
        r.connect(4, false);
        for (uint32_t k = 0; k < 4; ++k) r.values[4 + k] = 1.f;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            if (pass) r.descriptor->reset(r.state);
            float follower[4][2]{}, ic1[4][2]{}, ic2[4][2]{};
            for (uint32_t block = 0; block < 5; ++block) {
                for (uint32_t c = 0; c < 2; ++c)
                    for (uint32_t i = 0; i < kFrames; ++i) {
                        r.data->input[0][c][i] = float(.5 * std::cos(.13 * (block * kFrames + i + c)));
                        r.data->input[4][c][i] = (i < 128 ? .75f : .2f);
                    }
                r.run(kFrames);
                for (uint32_t c = 0; c < 2; ++c)
                    for (uint32_t i = 0; i < kFrames; ++i) {
                        float sum = 0.f;
                        for (uint32_t k = 0; k < 4; ++k) {
                            const float control = k == 0 ? r.data->input[4][c][i] : 1.f;
                            const float x = r.data->input[0][c][i];
                            auto &f = follower[k][c];
                            auto &a = ic1[k][c];
                            auto &b = ic2[k][c];
                            const float alpha = control > f ? attack : lookup(decay, r.values[8]);
                            f = alpha * f + (1.f - alpha) * control;
                            const float coeff = lookup(g, f);
                            constexpr float q = 1.f / .6f;
                            const float a1 = 1.f / (1.f + coeff * (coeff + q));
                            const float v1 = (a + coeff * (x - b)) * a1;
                            const float v2 = b + coeff * v1;
                            a = 2.f * v1 - a;
                            b = 2.f * v2 - b;
                            const float expected = v2 * f;
                            if (r.sample(k, c, i) != expected) {
                                std::printf("LPG startup rate=%.0f pass=%u frame=%u channel=%u voice=%u got=%.9g expected=%.9g\n",
                                    rate, pass, block * kFrames + i, k, c,
                                    double(r.sample(k,c,i)), double(expected));
                                std::fflush(stdout);
                            }
                            REQUIRE(r.sample(k, c, i) == expected);
                            sum += expected;
                        }
                        REQUIRE(r.sample(4, c, i) == std::clamp(sum, -4.f, 4.f));
                    }
            }
        }
    }
}


void test_mod9_modes() {
    const auto *d = ws::Module<quad_vca::QuadVca>::descriptor();
    REQUIRE(d->param_count == 9 && d->input_count == 8 && d->output_count == 5);
    for (uint32_t k = 0; k < 4; ++k) {
        const auto &p = d->params[4 + k];
        REQUIRE(p.flags == (WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE));
        REQUIRE(p.minimum == 0.f && p.maximum == 1.f && p.initial == 0.f && p.step == 1.f);
        REQUIRE(d->params[k].flags == (WS2_MODULATABLE | WS2_PER_VOICE | WS2_AUDIO_RATE_OK | WS2_AUTOMATABLE));
    }
    REQUIRE(d->params[8].flags == (WS2_MODULATABLE | WS2_AUTOMATABLE));
    for (uint32_t voices : {1u, 16u})
        for (uint32_t k = 0; k < 4; ++k) {
            Rig block({48000., voices}), single({48000., voices});
            for (Rig *r : {&block, &single}) {
                r->connect(0);
                for (uint32_t c = 0; c < voices; ++c)
                    r->data->input[0][c][0] = .25f + .01f * float(c);
                r->run(kFrames); // Prime all channels in VCA.
            }
            std::array<float, kFrames> effective{};
            for (uint32_t i = 64; i < 192; ++i) effective[i] = 1.f;
            block.param_rows[4 + k] = effective.data();
            block.params[4 + k].flags &= ~uint32_t(WS2_CONSTANT);
            block.run(kFrames);
            REQUIRE(!(block.outputs[k].flags & WS2_CONSTANT));
            // One-frame oracle exercises the same selected mode at each frame.
            for (uint32_t i = 0; i < kFrames; ++i) {
                single.values[4 + k] = effective[i];
                single.run(1);
                for (uint32_t c = 0; c < voices; ++c)
                    for (uint32_t p = 0; p < 5; ++p) {
                        const float actual = block.sample(p, c, i);
                        REQUIRE(std::isfinite(actual));
                        REQUIRE(std::abs(actual - single.sample(p, c)) < 1e-6f);
                        if (p < 4 && p != k)
                            REQUIRE(actual == block.data->input[0][c][0]);
                    }
            }
            REQUIRE(block.sample(k, 0, 63) == .25f);
            REQUIRE(block.sample(k, 0, 64) < .25f);
            block.param_rows[4 + k] = &block.values[4 + k];
            block.params[4 + k].flags |= WS2_CONSTANT;
            for (int n = 0; n < 3; ++n) block.run(kFrames);
            REQUIRE(block.outputs[k].flags & WS2_CONSTANT);
            for (uint32_t c = 0; c < voices; ++c)
                REQUIRE(block.sample(k, c) == block.data->input[0][c][0]);
            block.descriptor->reset(block.state);
            block.run(kFrames);
            REQUIRE(block.outputs[k].flags & WS2_CONSTANT);
            REQUIRE(block.sample(k) == .25f);
        }
}

struct ModeCv {
    static constexpr ws::Info info{"test.quad_vca_mode_cv", "Mode CV", 1};
    static constexpr std::array<ws2_port, 0> inputs{};
    static constexpr std::array outputs{ws::port("audio", "Audio", WS2_AUDIO),
                                        ws::port("mode", "Mode", WS2_CV_BI)};
    static constexpr std::array<ws2_param, 0> params{};
    void prepare(const ws::Setup &) {}
    void reset() {}
    void process(ws::Block &b) {
        for (uint32_t p = 0; p < 2; ++p) {
            b.out(p).set_channels(1);
            b.out(p).mark_constant(false);
        }
        for (uint32_t i = 0; i < b.frames(); ++i) {
            b.out(0).ch(0)[i] = .25f;
            b.out(1).ch(0)[i] = b.frame_position() + i >= 64 ? 1.f : 0.f;
        }
    }
};
void test_mod9_engine_cables() {
    for (uint32_t k = 0; k < 4; ++k) {
        wirescape::engine2::Config cfg;
        cfg.max_block = 128;
        wirescape::engine2::Engine e(cfg);
        REQUIRE(e.add_node(1, ws::Module<ModeCv>::descriptor()));
        REQUIRE(e.add_node(2, ws::Module<quad_vca::QuadVca>::descriptor()));
        REQUIRE(e.connect(1, 0, 2, 0));
        REQUIRE(e.modulate(1, 1, 2, 4 + k, 1.f));
        REQUIRE(e.output(2, k, 0) && e.prepare());
        float samples[128]{};
        float *rows[]{samples};
        {
            allocation_watch::Scope watch;
            REQUIRE(e.render(rows, 1, 128) == WS2_OK);
            REQUIRE(watch.allocations() == 0);
        }
        REQUIRE(e.output_sample(2, k, 0, 63) == .25f);
        REQUIRE(e.output_sample(2, k, 0, 64) < .25f);
        for (uint32_t i = 0; i < 128; ++i) {
            REQUIRE(std::isfinite(e.output_sample(2, k, 0, i)));
            for (uint32_t p = 0; p < 4; ++p)
                if (p != k) REQUIRE(e.output_sample(2, p, 0, i) == .25f);
        }
        REQUIRE(e.meter().faults == 0);
    }
}


void test_mod9_stateless_contract() {
    // The pinned SDK explicitly reports WS2_UNSUPPORTED for modules without
    // custom state. Patch parameters are host-owned; no filter/fade history
    // is serialized. A fresh instance with restored parameter values must
    // match reset of the original instance.
    for (uint32_t voices : {1u, 16u}) {
        Rig original({48000., voices}), restored({48000., voices});
        original.values[0] = .6f;
        original.values[4] = 1.f;
        original.values[6] = 1.f;
        original.values[8] = .7f;
        original.connect(0);
        restored.connect(0);
        for (uint32_t c = 0; c < voices; ++c) {
            original.data->input[0][c][0] = .25f;
            restored.data->input[0][c][0] = .25f;
        }
        for (int n = 0; n < 4; ++n) original.run(kFrames);
        Bytes bytes{};
        ws2_writer writer{sizeof(writer), &bytes, sink};
        REQUIRE(original.descriptor->save_state(original.state, &writer) == WS2_UNSUPPORTED);
        REQUIRE(bytes.size() == 0);
        Source src{&bytes, 0};
        ws2_reader reader{sizeof(reader), &src, uint32_t(bytes.size())};
        REQUIRE(restored.descriptor->load_state(restored.state, &reader) == WS2_UNSUPPORTED);
        REQUIRE(src.pos == 0);
        std::copy(std::begin(original.values), std::end(original.values), std::begin(restored.values));
        original.descriptor->reset(original.state);
        original.run(kFrames);
        restored.run(kFrames);
        for (uint32_t c = 0; c < voices; ++c)
            for (uint32_t p = 0; p < 5; ++p)
                for (uint32_t i = 0; i < kFrames; ++i)
                    REQUIRE(original.sample(p, c, i) == restored.sample(p, c, i));
    }
}

} // namespace

int main() {
    test_mod9_stateless_contract();
    test_mod9_modes();
    test_mod9_engine_cables();
    test_legacy_slots();
    test_vca_and_normalling();
    test_varying_control();
    test_frame_rate_voice_matrix();
    test_lpg_decay();
    test_lpg_closing_darkens();
    test_mode_switch_crossfade();
    test_lpg_at_load_and_reset();
    std::puts("quad VCA: matrix, VCA, normalling, mix, LPG decay/spectrum, rate-aware mode crossfade, reset and no allocations PASS");
}
