// SPDX-License-Identifier: GPL-3.0-only
// wstest negative fixture: the gain fixture allocating in process(). T0 must fail it at "cpu_probe".
// It breaks the realtime rules on purpose; never copy it.
#pragma once
#include "wirescape_sdk.h"
#include <array>
#include <cstdint>

namespace wstest_fixture {

struct AllocGain {
    static constexpr ws::Info info{"fixture.alloc_gain", "Fixture Allocating Gain", 1};
    static constexpr std::array inputs{ws::port("audio", "Audio", WS2_AUDIO)};
    static constexpr std::array outputs{ws::port("audio", "Audio", WS2_AUDIO)};
    static constexpr std::array params{
        ws::param("gain", "Gain", 0, 2, 1, "", WS2_AUTOMATABLE | WS2_MODULATABLE)};
    float *scratch = nullptr;
    float *heap = nullptr;
    AllocGain() = default;
    AllocGain(const AllocGain &) = delete;
    AllocGain &operator=(const AllocGain &) = delete;
    ~AllocGain() { delete[] heap; }
    uint32_t stride = 0;
    static ws2_memory memory_requirements(const ws2_config &c) {
        return {sizeof(ws2_memory), 2 * sizeof(float) * c.max_block_frames, 0, 32};
    }
    bool prepare(const ws::Setup &s) {
        stride = s.max_block();
        scratch = static_cast<float *>(s.alloc(2 * sizeof(float) * stride));
        return scratch != nullptr;
    }
    void reset() {}
    void process(ws::Block &b) {
        delete[] heap; // deliberate realtime violation: a heap round trip every block
        heap = new float[b.frames() + 1];
        b.out(0).set_channels(b.channels());
        const bool constant = b.in(0).constant() && b.param(0).constant();
        b.out(0).mark_constant(constant);
        const uint32_t count = constant ? 1 : b.frames();
        for (uint32_t c = 0; c < b.channels(); ++c) {
            const float *audio = b.in(0).row(c, scratch, count);
            const float *gain = b.param(0).row(c, scratch + stride, count);
            for (uint32_t i = 0; i < count; ++i) b.out(0).ch(c)[i] = audio[i] * gain[i];
        }
    }
};

} // namespace wstest_fixture
