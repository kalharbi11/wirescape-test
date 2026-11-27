// SPDX-License-Identifier: GPL-3.0-only
// GUTTR parameter table: the 37 controls of the owner's GUTTUR package (include/guttur/parameters.h),
// same order, ranges and defaults, with snake_case ids. `per_voice` marks the controls read per note
// from the note's voice channel; the rest are read once at channel 0 (DESIGN.md "Per-voice").
#pragma once
#include <array>
#include <cstdint>

namespace wsmod::guttr {

enum Param : uint32_t {
    pressure, breath, whisper, jitter, shimmer, tremor, wander, vibrato, growl, creak, tail_fry, flutter,
    rasp, breaks, size, pharynx, mouth, ring, vowel, style, attack, release, glide, throats, looseness,
    spread, size_spread, distance, azimuth, room, decay, wet, drive, body, air, level, grit_after,
    kParamCount
};

struct ParamDef {
    const char *id, *name, *unit;
    float minimum, maximum, initial;
    bool per_voice, stepped;
    uint32_t decimals;
};

inline constexpr std::array<ParamDef, kParamCount> kParams{{
    {"pressure", "Pressure", "", 0.f, 1.f, .5f, true, false, 2},
    {"breath", "Breath", "", 0.f, 1.f, .12f, true, false, 2},
    {"whisper", "Whisper", "", 0.f, 1.f, 0.f, true, false, 2},
    {"jitter", "Jitter", "%", 0.f, 5.f, .3f, true, false, 2},
    {"shimmer", "Shimmer", "%", 0.f, 20.f, 2.5f, true, false, 1},
    {"tremor", "Tremor", "cent", 0.f, 60.f, 6.f, true, false, 1},
    {"wander", "Wander", "cent", 0.f, 40.f, 4.f, true, false, 1},
    {"vibrato", "Vibrato", "cent", 0.f, 80.f, 0.f, true, false, 1},
    {"growl", "Growl", "", 0.f, 1.f, 0.f, true, false, 2},
    {"creak", "Creak", "", 0.f, 1.f, 0.f, true, false, 2},
    {"tail_fry", "Tail fry", "", 0.f, 1.f, 0.f, true, false, 2},
    {"flutter", "Flutter", "", 0.f, 1.f, 0.f, true, false, 2},
    {"rasp", "Rasp", "", 0.f, 1.f, 0.f, true, false, 2},
    {"breaks", "Breaks", "", 0.f, 1.f, 0.f, true, false, 2},
    {"size", "Size", "cm", 13.f, 20.5f, 17.5f, true, false, 1},
    {"pharynx", "Pharynx", "", 0.f, 1.f, 0.f, true, false, 2},
    {"mouth", "Mouth", "", 0.f, 1.f, .5f, true, false, 2},
    {"ring", "Ring", "", 0.f, 1.f, .3f, true, false, 2},
    {"vowel", "Vowel", "", 0.f, 1.f, .5f, true, false, 2},
    {"style", "Style", "", 0.f, 1.f, .45f, true, false, 2},
    {"attack", "Attack", "ms", 3.f, 1200.f, 60.f, false, false, 0},
    {"release", "Release", "ms", 20.f, 2500.f, 220.f, false, false, 0},
    {"glide", "Glide", "ms", 0.f, 600.f, 60.f, false, false, 0},
    {"throats", "Throats", "", 1.f, 8.f, 3.f, false, true, 0},
    {"looseness", "Looseness", "", 0.f, 1.f, .35f, true, false, 2},
    {"spread", "Spread", "", 0.f, 1.f, .5f, true, false, 2},
    {"size_spread", "Size spread", "", 0.f, 1.f, .4f, true, false, 2},
    {"distance", "Distance", "", 0.f, 1.f, .15f, false, false, 2},
    {"azimuth", "Azimuth", "°", -180.f, 180.f, 0.f, false, false, 0},
    {"room", "Room", "", 0.f, 1.f, .5f, false, false, 2},
    {"decay", "Decay", "s", .3f, 7.f, 1.8f, false, false, 2},
    {"wet", "Wet", "", 0.f, 1.f, .28f, false, false, 2},
    {"drive", "Drive", "", 0.f, 1.f, 0.f, false, false, 2},
    {"body", "Body", "dB", -12.f, 6.f, 0.f, false, false, 1},
    {"air", "Air", "dB", -6.f, 9.f, 0.f, false, false, 1},
    {"level", "Level", "dB", -24.f, 6.f, 0.f, false, false, 1},
    {"grit_after", "Grit after", "", 0.f, 1.f, 0.f, false, true, 0},
}};

}  // namespace wsmod::guttr
