// SPDX-License-Identifier: GPL-3.0-only
// wstest fixture tests: reference maths, 1 and 16 channels, reset, zero allocations in process.
#include "dsp.h"
// The fixtures share this file; each one sets what its module is expected to satisfy.
#define FIXTURE_CLASS AllocGain
#define FIXTURE_EXACT 1
#define FIXTURE_ZERO_ALLOCATIONS 0
#include "../../tests/native/abi2/allocation_watch.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "fixture check line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

namespace {
using Module = wstest_fixture::FIXTURE_CLASS;
struct Pool {
    alignas(64) unsigned char bytes[16384];
    size_t used = 0;
};
void *WS2_CALL allocate(void *ctx, size_t bytes, size_t alignment, uint32_t) {
    auto &p = *static_cast<Pool *>(ctx);
    p.used = (p.used + alignment - 1) & ~(alignment - 1);
    if (p.used + bytes > sizeof(p.bytes)) return nullptr;
    void *r = p.bytes + p.used;
    p.used += bytes;
    return r;
}

struct Rig {
    Pool pool;
    ws2_host host{sizeof(host), &pool, allocate};
    const ws2_descriptor *d = ws::Module<Module>::descriptor();
    void *state = nullptr;
    ws2_config cfg{sizeof(cfg), 48000, 256, 16, 42, nullptr, 0};
    alignas(32) float out[16][256]{};
    float *dst[16]{};
    alignas(32) float input[256]{};
    alignas(32) float gain[256]{};
    const float *ip[1] = {input};
    const float *pp[1] = {gain};
    ws2_signal in{sizeof(in), ip, 1, WS2_CONNECTED};
    ws2_signal param{sizeof(param), pp, 1, 0};
    ws2_signal_out sig{sizeof(sig), dst, 1, 0, 16};
    ws2_process ctx{};
    Rig() {
        for (int c = 0; c < 16; ++c) dst[c] = out[c];
        state = d->create(&host);
        REQUIRE(state);
        REQUIRE(d->prepare(state, &cfg, &host) == WS2_OK);
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = 48000;
        ctx.input_count = 1;
        ctx.output_count = 1;
        ctx.param_count = 1;
        ctx.inputs = &in;
        ctx.params = &param;
        ctx.outputs = &sig;
    }
    ~Rig() { d->destroy(state); }
    uint64_t run(uint32_t frames, uint32_t channels) {
        ctx.frames = frames;
        ctx.channels = channels;
        allocation_watch::Scope watch;
        const int status = d->process(state, &ctx);
#if FIXTURE_EXACT
        REQUIRE(status == WS2_OK);
#else
        (void)status; // the SDK reports the NaN output as a fault; T0's demos step must catch it
#endif
        return watch.allocations();
    }
};
} // namespace

int main() {
    for (uint32_t channels : {1u, 16u}) {
        Rig rig;
        for (int i = 0; i < 256; ++i) {
            rig.input[i] = std::sin(0.05f * float(i));
            rig.gain[i] = 0.5f + float(i) / 512.f;
        }
        const uint64_t allocations = rig.run(200, channels);
        REQUIRE(rig.sig.channels == channels);
#if FIXTURE_EXACT
        for (uint32_t c = 0; c < channels; ++c)
            for (int i = 0; i < 200; ++i) REQUIRE(rig.out[c][i] == rig.input[i] * rig.gain[i]);
        float first[200];
        for (int i = 0; i < 200; ++i) first[i] = rig.out[0][i];
        rig.d->reset(rig.state);
        rig.run(200, channels);
        for (int i = 0; i < 200; ++i) REQUIRE(rig.out[0][i] == first[i]);
#endif
#if FIXTURE_ZERO_ALLOCATIONS
        REQUIRE(allocations == 0);
#else
        (void)allocations; // deliberately unchecked: T0's CPU probe step must catch it
#endif
    }
    std::puts("fixture tests passed");
    return 0;
}
