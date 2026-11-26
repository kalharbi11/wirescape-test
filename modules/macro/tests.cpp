// SPDX-License-Identifier: GPL-3.0-only
// essentials.macro (MOD-5) tests, written before the DSP: the 8 outputs equal the effective knob values
// per sample; constant flags follow the parameter signals; the labels slot packs and round-trips with
// the lab's data_pack.h; zero allocations; 1 and 16 host channels (mono outputs); finite extremes.
#include "dsp.h"
#include "data_pack.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "macro test line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

namespace {
struct Pool { alignas(64) unsigned char bytes[16384]; size_t used = 0; };
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
    const ws2_descriptor *d = ws::Module<wsmod::Macro>::descriptor();
    void *state = nullptr;
    ws2_config cfg{sizeof(cfg), 48000, 256, 16, 42, nullptr, 0};
    alignas(32) float out[8][16][256]{};
    float *dst[8][16]{};
    alignas(32) float knob[8][256]{};
    const float *pp[8][1]{};
    ws2_signal params[8]{};
    ws2_signal_out outs[8]{};
    alignas(16) wsmod::MacroLabels labels{};  // the host always passes the slot's view
    ws2_data view{sizeof(ws2_data), &labels, sizeof(labels), 1};
    ws2_process ctx{};
    Rig() {
        for (int k = 0; k < 8; ++k) {
            for (int c = 0; c < 16; ++c) dst[k][c] = out[k][c];
            pp[k][0] = knob[k];
            params[k] = {sizeof(ws2_signal), pp[k], 1, WS2_CONSTANT};
            outs[k] = {sizeof(ws2_signal_out), dst[k], 0, 0, 16};
        }
        state = d->create(&host);
        REQUIRE(state);
        REQUIRE(d->prepare(state, &cfg, &host) == WS2_OK);
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = 48000;
        ctx.output_count = 8;
        ctx.param_count = 8;
        ctx.params = params;
        ctx.outputs = outs;
        ctx.data_count = 1;
        ctx.data = &view;
    }
    ~Rig() { d->destroy(state); }
    uint64_t run(uint32_t frames, uint32_t channels = 1) {
        ctx.frames = frames;
        ctx.channels = channels;
        allocation_watch::Scope watch;
        REQUIRE(d->process(state, &ctx) == WS2_OK);
        return watch.allocations();
    }
};

void descriptor() {
    const auto *d = ws::Module<wsmod::Macro>::descriptor();
    REQUIRE(!std::strcmp(d->id, "essentials.macro") && !std::strcmp(d->name, "Macros"));
    REQUIRE(d->input_count == 0 && d->output_count == 8 && d->param_count == 8);
    for (uint32_t k = 0; k < 8; ++k) {
        const std::string knob = "knob" + std::to_string(k + 1), out = "out" + std::to_string(k + 1);
        const auto &p = d->params[k];
        REQUIRE(knob == p.id && p.legacy_index == k && p.minimum == 0 && p.maximum == 1 && p.initial == 0);
        REQUIRE(p.taper == WS2_LINEAR);
        REQUIRE(p.flags == (WS2_MODULATABLE | WS2_AUTOMATABLE | WS2_AUDIO_RATE_OK));  // mono: no PER_VOICE
        REQUIRE(out == d->outputs[k].id && d->outputs[k].hint == WS2_CV_UNI);
    }
}
// Each output is its knob's effective value per sample; constant when the parameter signal is constant.
void outputs_follow_knobs() {
    for (uint32_t channels : {1u, 16u}) {
        Rig rig;
        for (int k = 0; k < 8; ++k) {
            if (k % 2) {  // modulated knobs vary per sample
                rig.params[k].flags = 0;
                for (int i = 0; i < 256; ++i) rig.knob[k][i] = float(i) / 255.f * (k + 1) / 8.f;
            } else {
                rig.knob[k][0] = .125f * k;
            }
        }
        REQUIRE(rig.run(200, channels) == 0);
        for (int k = 0; k < 8; ++k) {
            REQUIRE(rig.outs[k].channels == 1);  // mono outputs whatever the host's channel count
            if (k % 2) {
                REQUIRE(!(rig.outs[k].flags & WS2_CONSTANT));
                for (int i = 0; i < 200; ++i) REQUIRE(rig.out[k][0][i] == rig.knob[k][i]);
            } else {
                REQUIRE(rig.outs[k].flags & WS2_CONSTANT);
                REQUIRE(rig.out[k][0][0] == .125f * k);
            }
        }
        // A constant knob that starts moving clears the flag in that block.
        rig.params[0].flags = 0;
        for (int i = 0; i < 256; ++i) rig.knob[0][i] = .5f;
        rig.run(64, channels);
        REQUIRE(!(rig.outs[0].flags & WS2_CONSTANT));
        for (int i = 0; i < 64; ++i) REQUIRE(rig.out[0][0][i] == .5f);
        // Reset changes nothing (no state).
        rig.d->reset(rig.state);
        rig.run(64, channels);
        for (int i = 0; i < 64; ++i) REQUIRE(rig.out[0][0][i] == .5f);
    }
}
void extremes() {
    Rig rig;
    const float values[] = {0.f, 1.f, -1.f, 2.f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), 1e30f};
    for (float v : values) {
        for (int k = 0; k < 8; ++k) { rig.params[k].flags = k % 2 ? 0 : WS2_CONSTANT; std::fill_n(rig.knob[k], 256, v); }
        rig.run(256);
        for (int k = 0; k < 8; ++k) for (int i = 0; i < (k % 2 ? 256 : 1); ++i) REQUIRE(std::isfinite(rig.out[k][0][i]));
    }
}
// The labels slot: layout, pack from JSON, round trip, and limits (32 bytes of text).
void labels_slot() {
    const auto *d = ws::Module<wsmod::Macro>::descriptor();
    REQUIRE(d->data_slot_count == 1);
    const ws2_data_slot &slot = d->data_slots[0];
    REQUIRE(!std::strcmp(slot.id, "labels") && slot.bytes == sizeof(wsmod::MacroLabels) && slot.field_count == 2);
    REQUIRE(slot.bytes == 33 + 8 * 33 && slot.bytes <= WS2_MAX_DATA_BYTES);
    const ws2_field &page = slot.fields[0], &rows = slot.fields[1];
    REQUIRE(!std::strcmp(page.id, "page") && page.type == WS2_F_TEXT && page.count == 33 && page.offset == 0);
    REQUIRE(!std::strcmp(rows.id, "rows") && rows.type == WS2_F_RECORD && rows.count == 8 && rows.stride == 33);
    REQUIRE(rows.field_count == 1 && !std::strcmp(rows.fields[0].id, "name") && rows.fields[0].count == 33);

    using nlohmann::json;
    using namespace ws::data_layout;
    std::vector<uint8_t> bytes, again;
    std::string error;
    // Empty: every name empty ("Macros n" and "Knob k" are the clients' defaults).
    REQUIRE(pack_data_slot(slot, json::object(), DataLoad::Edit, bytes, error));
    REQUIRE(bytes.size() == slot.bytes && std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0; }));
    const std::string longest(32, 'x');
    json labels = {{"page", "Drums"}, {"rows", json::array()}};
    for (int k = 0; k < 8; ++k) labels["rows"].push_back({{"name", k == 7 ? longest : "Knob name " + std::to_string(k)}});
    REQUIRE(pack_data_slot(slot, labels, DataLoad::Edit, bytes, error));
    wsmod::MacroLabels unpacked{};
    std::memcpy(&unpacked, bytes.data(), sizeof(unpacked));
    REQUIRE(!std::strcmp(unpacked.page, "Drums"));
    REQUIRE(!std::strcmp(unpacked.rows[0].name, "Knob name 0") && longest == unpacked.rows[7].name);
    // Round trip: the unpacked record, written back as JSON, packs to the same bytes.
    json back = {{"page", std::string(unpacked.page)}, {"rows", json::array()}};
    for (const auto &row : unpacked.rows) back["rows"].push_back({{"name", std::string(row.name)}});
    REQUIRE(pack_data_slot(slot, back, DataLoad::Edit, again, error) && again == bytes);
    // 33 bytes is refused by an edit.
    json too_long = {{"page", std::string(33, 'y')}};
    REQUIRE(!pack_data_slot(slot, too_long, DataLoad::Edit, again, error));
}
void data_is_never_read() {
    // The DSP output does not depend on the labels' content.
    Rig a, b;
    std::strcpy(b.labels.page, "Live");
    std::strcpy(b.labels.rows[3].name, "Cutoff");
    b.view.serial = 2;
    for (Rig *r : {&a, &b}) for (int k = 0; k < 8; ++k) r->knob[k][0] = .1f * k;
    a.run(32);
    b.run(32);
    for (int k = 0; k < 8; ++k) REQUIRE(a.out[k][0][0] == b.out[k][0][0]);
}
}  // namespace

int main() {
    descriptor();
    outputs_follow_knobs();
    extremes();
    labels_slot();
    data_is_never_read();
    std::puts("PASS essentials.macro (MOD-5): outputs, constant flags, labels slot, extremes, no allocations");
    return 0;
}
