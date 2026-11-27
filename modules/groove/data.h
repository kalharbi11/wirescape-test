// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "config.h"
#include "wirescape_sdk.h"
#include <cstddef>
#include <cstring>

namespace groove {
// Flat records, one variable-length step array per track. Construction-time
// configuration still owns the stable seed, armed/mute defaults and divisors.
// Empty authored data defaults to a silent 32-step pattern; direct engine
// callers with legacy config must install migrated data before prepare.
struct LivePattern {
    uint32_t length_1, length_2, count_1, count_2, tempo_source;
    Step steps_1[64], steps_2[64];
};
inline constexpr const char *step_kinds[]{"rest", "note", "tie"};
inline constexpr const char *tempo_sources[]{"internal", "host"};
inline constexpr std::array live_step_fields{
    ws::data_field<WS2_F_ENUM, uint32_t>("kind", "Kind", offsetof(Step, kind), ws::data_options(step_kinds, 0)),
    ws::data_field<WS2_F_I32, int32_t>("key", "Key", offsetof(Step, key), ws::data_range(0, 127, 60)),
    ws::data_field<WS2_F_F32, float>("velocity", "Velocity", offsetof(Step, velocity), ws::data_range(0, 1, 0)),
    ws::data_field<WS2_F_F32, float>("mod1", "Mod 1", offsetof(Step, mod1), ws::data_range(0, 1, 0)),
    ws::data_field<WS2_F_F32, float>("mod2", "Mod 2", offsetof(Step, mod2), ws::data_range(0, 1, 0)),
    ws::data_field<WS2_F_U8, uint8_t>("gate_q", "Gate quarters", offsetof(Step, gate_q), ws::data_range(1, 4, 4)),
    ws::data_field<WS2_F_U8, uint8_t>("cond_type", "Condition", offsetof(Step, cond_type), ws::data_range(0, 4, 0)),
    ws::data_field<WS2_F_U8, uint8_t>("cond_a", "Condition A", offsetof(Step, cond_a), ws::data_range(0, 99, 0)),
    ws::data_field<WS2_F_U8, uint8_t>("cond_b", "Condition B", offsetof(Step, cond_b), ws::data_range(0, 8, 0)),
    ws::data_field<WS2_F_U8, uint8_t>("key_count", "Key count", offsetof(Step, key_count), ws::data_range(1, 5, 1)),
    // std::array's contiguous int32_t storage is the existing fixed POD layout.
    ws::data_field<WS2_F_I32, int32_t[5]>("keys", "Chord keys", offsetof(Step, keys), ws::data_range(0, 127, 60))
};
static_assert(sizeof(Step::keys) == sizeof(int32_t[5]));
inline constexpr std::array live_pattern_fields{
    ws::data_field<WS2_F_U32, uint32_t>("length_1", "Track 1 length", offsetof(LivePattern, length_1), ws::data_range(0, 64, 32)),
    ws::data_field<WS2_F_U32, uint32_t>("length_2", "Track 2 length", offsetof(LivePattern, length_2), ws::data_range(0, 64, 32)),
    ws::data_field<WS2_F_U32, uint32_t>("count_1", "Track 1 row count", offsetof(LivePattern, count_1), ws::data_range(0, 64, 0)),
    ws::data_field<WS2_F_U32, uint32_t>("count_2", "Track 2 row count", offsetof(LivePattern, count_2), ws::data_range(0, 64, 0)),
    ws::data_field<WS2_F_ENUM, uint32_t>("tempo_source", "Tempo source", offsetof(LivePattern, tempo_source), ws::data_options(tempo_sources, 0)),
    ws::data_field<WS2_F_RECORD, Step[64]>("steps_1", "Track 1 steps", offsetof(LivePattern, steps_1), ws::data_record(live_step_fields, 2)),
    ws::data_field<WS2_F_RECORD, Step[64]>("steps_2", "Track 2 steps", offsetof(LivePattern, steps_2), ws::data_record(live_step_fields, 3))
};
inline bool valid_live_length(uint32_t length) noexcept {
    return length == 8 || length == 16 || length == 32 || length == 64;
}
// Control-side one-way adapter for an already decoded legacy config.
// Preserve hidden rows beyond the visible track length as well.
inline LivePattern materialize_legacy_pattern(const Config &legacy) {
    LivePattern data{};
    data.length_1 = legacy.tracks[0].length;
    data.length_2 = legacy.tracks[1].length;
    data.count_1 = data.count_2 = 64;
    data.tempo_source = legacy.tempo_source;
    for (uint32_t i = 0; i < 64; ++i) {
        data.steps_1[i] = legacy.tracks[0].steps[i];
        data.steps_2[i] = legacy.tracks[1].steps[i];
    }
    return data;
}
// Constant-time view guard for direct legacy callers and defensive bounds.
// Cross-field row validation belongs to the off-render host callback.
inline bool live_pattern_bounds(const LivePattern &data) noexcept {
    return valid_live_length(data.length_1) && valid_live_length(data.length_2) &&
        data.count_1 <= 64 && data.count_2 <= 64 && data.tempo_source <= TempoHost;
}
// POD error location; the host adapter formats the diagnostic off render.
struct PatternValidation {
    const char *field = nullptr;
    int row = -1;
    explicit operator bool() const noexcept { return field == nullptr; }
};
inline PatternValidation validate_pattern_slot(const ws2_data_slot &slot,
                                                const void *bytes, size_t size) noexcept {
    if (std::strcmp(slot.id, "pattern") != 0 || !bytes || size != sizeof(LivePattern))
        return {"pattern"};
    LivePattern data{};
    std::memcpy(&data, bytes, sizeof(data));
    if (!valid_live_length(data.length_1)) return {"pattern.length_1"};
    if (!valid_live_length(data.length_2)) return {"pattern.length_2"};
    if (data.count_1 > 64) return {"pattern.count_1"};
    if (data.count_2 > 64) return {"pattern.count_2"};
    if (data.tempo_source > TempoHost) return {"pattern.tempo_source"};
    for (uint32_t t = 0; t < 2; ++t) {
        const auto *rows = t == 0 ? data.steps_1 : data.steps_2;
        const auto count = t == 0 ? data.count_1 : data.count_2;
        for (uint32_t i = 0; i < count; ++i)
            if (!valid_step(rows[i]))
                return {t == 0 ? "pattern.steps_1" : "pattern.steps_2", static_cast<int>(i)};
    }
    return {};
}
} // namespace groove
