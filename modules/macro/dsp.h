// SPDX-License-Identifier: GPL-3.0-only
// essentials.macro, "Macros" (wirescape-test MOD-5, lab proposals/0024 §3.1): eight knobs whose
// effective values (base + modulation, after the engine's D7 smoothing) appear on eight outputs.
// No inputs, no state, no heap, no OS calls: portable tier.
#pragma once
#include "wirescape_sdk.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace wsmod {

// Data slot `labels` (§3.1): page and 8 knob names, each at most 32 bytes of text. Empty means
// "Macros n" and "Knob k". The DSP never reads it; names are edited live with setData and live in the
// patch, so every client can show them. Follows OSC In's record pattern.
#define WS_MACRO_ROW_FIELDS(X, S) X(S, name, char[33], WS2_F_TEXT, "Name", ws::DataFieldRules{})
WS_DEFINE_DATA_STRUCT(MacroLabelRow, WS_MACRO_ROW_FIELDS)
#undef WS_MACRO_ROW_FIELDS
#define WS_MACRO_LABEL_FIELDS(X, S) \
    X(S, page, char[33], WS2_F_TEXT, "Page", ws::DataFieldRules{}) \
    X(S, rows, MacroLabelRow[8], WS2_F_RECORD, "Knobs", ws::data_record(MacroLabelRow_fields))
WS_DEFINE_DATA_STRUCT(MacroLabels, WS_MACRO_LABEL_FIELDS)
#undef WS_MACRO_LABEL_FIELDS

struct Macro {
    static constexpr ws::Info info{"essentials.macro", "Macros", 1};
    static constexpr std::array<ws2_port, 0> inputs{};
    static constexpr std::array outputs{
        ws::port("out1", "Out 1", WS2_CV_UNI), ws::port("out2", "Out 2", WS2_CV_UNI),
        ws::port("out3", "Out 3", WS2_CV_UNI), ws::port("out4", "Out 4", WS2_CV_UNI),
        ws::port("out5", "Out 5", WS2_CV_UNI), ws::port("out6", "Out 6", WS2_CV_UNI),
        ws::port("out7", "Out 7", WS2_CV_UNI), ws::port("out8", "Out 8", WS2_CV_UNI)};
    // Mono (no PER_VOICE): a knob is one value for the whole patch.
    static constexpr uint32_t knob_flags = WS2_MODULATABLE | WS2_AUTOMATABLE | WS2_AUDIO_RATE_OK;
    static constexpr std::array params{
        ws::param("knob1", "Knob 1", 0, 1, 0, "", knob_flags, WS2_LINEAR, 0),
        ws::param("knob2", "Knob 2", 0, 1, 0, "", knob_flags, WS2_LINEAR, 1),
        ws::param("knob3", "Knob 3", 0, 1, 0, "", knob_flags, WS2_LINEAR, 2),
        ws::param("knob4", "Knob 4", 0, 1, 0, "", knob_flags, WS2_LINEAR, 3),
        ws::param("knob5", "Knob 5", 0, 1, 0, "", knob_flags, WS2_LINEAR, 4),
        ws::param("knob6", "Knob 6", 0, 1, 0, "", knob_flags, WS2_LINEAR, 5),
        ws::param("knob7", "Knob 7", 0, 1, 0, "", knob_flags, WS2_LINEAR, 6),
        ws::param("knob8", "Knob 8", 0, 1, 0, "", knob_flags, WS2_LINEAR, 7)};
    static constexpr std::array data_slots{ws::data_slot<MacroLabels>("labels", "Labels", MacroLabels_fields)};

    void prepare(const ws::Setup &) {}
    void reset() {}
    void process(ws::Block &b) {
        for (uint32_t k = 0; k < 8; ++k) {
            auto out = b.out(k);
            out.set_channels(1);
            float *o = out.ch(0);
            const auto knob = b.param(k).ch(0);
            // Constant knob signal: one value, marked constant, so downstream can skip work.
            const bool constant = b.param(k).constant();
            out.mark_constant(constant);
            const uint32_t n = constant ? 1u : b.frames();
            for (uint32_t i = 0; i < n; ++i) o[i] = std::isfinite(knob[i]) ? knob[i] : 0.f;
        }
    }
};

}  // namespace wsmod
