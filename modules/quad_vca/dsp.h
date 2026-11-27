// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wirescape_sdk.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace quad_vca {
struct QuadVca {
    static constexpr ws::Info info{"essentials.quad_vca", "Quad VCA", 1};
    static constexpr std::array inputs{ws::port("in1", "Input 1", WS2_AUDIO),
                                       ws::port("in2", "Input 2", WS2_AUDIO),
                                       ws::port("in3", "Input 3", WS2_AUDIO),
                                       ws::port("in4", "Input 4", WS2_AUDIO),
                                       ws::port("cv1", "CV 1", WS2_CV_UNI, WS2_POLY, 1),
                                       ws::port("cv2", "CV 2", WS2_CV_UNI, WS2_POLY, 1),
                                       ws::port("cv3", "CV 3", WS2_CV_UNI, WS2_POLY, 1),
                                       ws::port("cv4", "CV 4", WS2_CV_UNI, WS2_POLY, 1)};
    static constexpr uint32_t kLevel = WS2_MODULATABLE | WS2_PER_VOICE | WS2_AUDIO_RATE_OK | WS2_AUTOMATABLE;
    static constexpr uint32_t kMode = WS2_STEPPED | WS2_AUTOMATABLE;
    static constexpr std::array outputs{ws::port("out1", "Out 1", WS2_AUDIO), ws::port("out2", "Out 2", WS2_AUDIO),
                                        ws::port("out3", "Out 3", WS2_AUDIO), ws::port("out4", "Out 4", WS2_AUDIO),
                                        ws::port("mix", "Mix", WS2_AUDIO)};
    static constexpr std::array params{
        ws::param("level1", "Level 1", 0, 1, 1, "", kLevel, WS2_LINEAR, 0),
        ws::param("level2", "Level 2", 0, 1, 1, "", kLevel, WS2_LINEAR, 1),
        ws::param("level3", "Level 3", 0, 1, 1, "", kLevel, WS2_LINEAR, 2),
        ws::param("level4", "Level 4", 0, 1, 1, "", kLevel, WS2_LINEAR, 3),
        ws::param("mode1", "Mode 1", 0, 1, 0, "", kMode, WS2_LINEAR, 4, 4, 1),
        ws::param("mode2", "Mode 2", 0, 1, 0, "", kMode, WS2_LINEAR, 5, 4, 1),
        ws::param("mode3", "Mode 3", 0, 1, 0, "", kMode, WS2_LINEAR, 6, 4, 1),
        ws::param("mode4", "Mode 4", 0, 1, 0, "", kMode, WS2_LINEAR, 7, 4, 1),
        ws::param("response", "Response", 0, 1, .4f, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 8)};

    void prepare(const ws::Setup &s) {
        const double rate = s.sample_rate();
        const double top = std::min(18000.0, .45 * rate);
        const double ratio = top / 40.0;
        attack_alpha_ = float(std::exp(-1.0 / (.002 * rate)));
        mode_fade_step_ = float(1.0 / (.010 * rate)); // 10 ms, rate independent.
        for (uint32_t i = 0; i <= 256; ++i) {
            const double position = double(i) / 256.0;
            const double cutoff = 40.0 * std::pow(ratio, position);
            cutoff_g_[i] = float(std::tan(3.14159265358979323846 * cutoff / rate));
            const double release = .060 * std::pow(25.0, position);
            release_alpha_[i] = float(std::exp(-1.0 / (release * rate)));
        }
        reset();
    }
    void reset() {
        primed_ = 0;
        for (auto &channel : state_)
            for (auto &voice : channel) voice = {};
    }
    void process(ws::Block &b) {
        const uint32_t voices = std::min<uint32_t>(b.channels(), uint32_t(16));
        bool constant = true;
        for (uint32_t k = 0; k < 4; ++k) {
            constant = constant && b.in(k).constant() && b.in(k + 4).constant() && b.param(k).constant() &&
                       b.param(k + 4).constant();
        }
        constant = constant && b.param(8).constant();
        for (uint32_t k = 0; k < 4 && constant; ++k)
            for (uint32_t voice = 0; voice < voices; ++voice)
                if (b.param(k + 4).value(voice) >= .5f || state_[k][voice].mode_blend > 0.f)
                    constant = false;
        for (uint32_t k = 0; k < 5; ++k) {
            b.out(k).set_channels(voices);
            b.out(k).mark_constant(constant);
        }
        const uint32_t frames = constant ? 1u : b.frames();
        for (uint32_t voice = 0; voice < voices; ++voice) {
            std::array<ws::Channel, 4> input{};
            std::array<ws::Channel, 4> cv{};
            std::array<float *, 5> output{};
            for (uint32_t k = 0; k < 4; ++k) {
                // Normal the *input* of the preceding channel, recursively.
                input[k] = b.in(k).connected() ? b.in(k).ch(voice) : (k ? input[k - 1] : ws::Channel{});
                cv[k] = b.in(k + 4).connected() ? b.in(k + 4).ch(voice) : ws::Channel{};
                output[k] = b.out(k).ch(voice);
            }
            output[4] = b.out(4).ch(voice);
            // Work on a local copy: output stores cannot alias it, so the per-sample mode
            // checks stay in registers. Written back once per voice per block.
            std::array<VoiceState, 4> local{};
            // Per block: a channel whose mode is constant and not fading is settled, so the
            // sample loop needs no mode read. kDynamic handles mode CV and running fades.
            std::array<uint8_t, 4> path{};
            const uint32_t voice_bit = uint32_t(1) << voice;
            const bool prime = !(primed_ & voice_bit);
            for (uint32_t k = 0; k < 4; ++k) {
                local[k] = state_[k][voice];
                // On load/reset, begin in the selected mode, not a VCA fade.
                if (prime) local[k].mode_blend = b.param(k + 4).value(voice, 0) >= .5f ? 1.f : 0.f;
                path[k] = kDynamic;
                if (b.param(k + 4).constant()) {
                    const bool want_lpg = b.param(k + 4).value(voice) >= .5f;
                    if (!want_lpg && local[k].mode_blend <= 0.f) path[k] = kVca;
                    else if (want_lpg && local[k].mode_blend >= 1.f) path[k] = kLpg;
                }
            }
            for (uint32_t i = 0; i < frames; ++i) {
                float sum = 0;
                for (uint32_t k = 0; k < 4; ++k) {
                    const float level = b.param(k).value(voice, i);
                    const float cv_value = b.in(k + 4).connected() ? cv[k][i] : 1.f;
                    const float control = std::clamp(level * cv_value, 0.f, 1.f);
                    const float x = input[k][i];
                    float y;
                    auto &s = local[k];
                    if (path[k] == kVca) {
                        y = x * control; // Settled VCA; reset and completed fades cleared state.
                    } else if (path[k] == kLpg) {
                        y = lpg(s, x, control, b.param(8).value(voice, i)); // Settled LPG.
                    } else {
                        const bool want_lpg = b.param(k + 4).value(voice, i) >= .5f;
                        if (!want_lpg && s.mode_blend <= 0.f) {
                            y = x * control;
                        } else if (want_lpg && s.mode_blend >= 1.f) {
                            y = lpg(s, x, control, b.param(8).value(voice, i));
                        } else {
                            // Mode change in progress: 10 ms linear crossfade, either direction.
                            s.mode_blend = want_lpg ? std::min(1.f, s.mode_blend + mode_fade_step_)
                                                    : std::max(0.f, s.mode_blend - mode_fade_step_);
                            const float wet = lpg(s, x, control, b.param(8).value(voice, i));
                            const float vca = x * control;
                            y = vca + s.mode_blend * (wet - vca);
                            if (s.mode_blend <= 0.f) s = {}; // One reset per completed fade.
                        }
                    }
                    output[k][i] = y;
                    sum += y;
                }
                output[4][i] = std::clamp(sum, -4.f, 4.f);
            }
            for (uint32_t k = 0; k < 4; ++k) state_[k][voice] = local[k];
            primed_ |= voice_bit;
        }
    }

  private:
    static constexpr uint8_t kDynamic = 0, kVca = 1, kLpg = 2;
    struct VoiceState {
        float follower = 0, ic1 = 0, ic2 = 0, mode_blend = 0;
    };
    // The original LPG step, shared by the settled and crossfade paths.
    float lpg(VoiceState &s, float x, float control, float response_param) const {
        const float response = std::clamp(response_param, 0.f, 1.f);
        const float decay = lookup(release_alpha_, response);
        const float alpha = control > s.follower ? attack_alpha_ : decay;
        s.follower = alpha * s.follower + (1.f - alpha) * control;
        const float g = lookup(cutoff_g_, s.follower);
        constexpr float k = 1.f / .6f;
        const float a1 = 1.f / (1.f + g * (g + k));
        const float v1 = (s.ic1 + g * (x - s.ic2)) * a1;
        const float v2 = s.ic2 + g * v1;
        s.ic1 = 2.f * v1 - s.ic1;
        s.ic2 = 2.f * v2 - s.ic2;
        return v2 * s.follower;
    }
    static float lookup(const std::array<float, 257> &table, float x) {
        const float scaled = std::clamp(x, 0.f, 1.f) * 256.f;
        const uint32_t index = std::min<uint32_t>(uint32_t(scaled), uint32_t(255));
        return table[index] + (table[index + 1] - table[index]) * (scaled - float(index));
    }
    std::array<std::array<VoiceState, 16>, 4> state_{};
    uint32_t primed_ = 0; // One bit per voice; all four channels start together.
    std::array<float, 257> cutoff_g_{}, release_alpha_{};
    float attack_alpha_ = 0, mode_fade_step_ = 0;
};
} // namespace quad_vca
