// SPDX-License-Identifier: GPL-3.0-only
// GUTTR shared constants and helpers, from the owner's GUTTUR package (src/internal.h, src/room.cpp).
#pragma once
#include "params.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wsmod::guttr {

constexpr double PI = 3.141592653589793238462643383279502884, TAU = 2 * PI, LN2 = .6931471805599453094;
// 17 Rd tables x 8 mip levels, 160 harmonics, 10 resonators, 34 tube sections, 32768-point LF cycle,
// 256-point flow table, a 9x5x5x4 tract grid.
constexpr int NR = 17, NLEV = 8, NH = 160, NRES = 10, NS = 34, M = 32768, FLOW = 256, GRID = 9 * 5 * 5 * 4;
constexpr int TABLE_TOTAL = 831368, MAX_LEVEL = 16384;
constexpr double RD_MIN = .3, RD_MAX = 2.7, RD_STEP = (RD_MAX - RD_MIN) / 16., L_REF = 17.5, SOUND = 35000.;
constexpr int kMaxNotes = 16, kMaxThroats = 16, kMaxThroatsPerNote = 8, kChannels = 16;
constexpr uint32_t kTick = 128;  // the fixed internal control clock, in frames

using Params = std::array<double, kParamCount>;
inline double get(const Params &p, Param x) noexcept { return p[x]; }

inline double clamp(double x, double a, double b) noexcept { return x < a ? a : x > b ? b : x; }
inline double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }
inline double smooth(double a, double b, double x) noexcept {
    const double t = clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
inline double db(double x) noexcept { return std::pow(10., x / 20.); }
inline double hz(int midi) noexcept { return 440. * std::pow(2., (midi - 69.) / 12.); }
inline uint32_t hash(int32_t a, int32_t b, int32_t c, int32_t d) noexcept {
    uint32_t h = 2166136261u ^ uint32_t(a);
    h = (h ^ uint32_t(b)) * 16777619u;
    h = (h ^ uint32_t(c)) * 2246822519u;
    h = (h ^ uint32_t(d)) * 3266489917u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return h;
}
inline double h01(int32_t a, int32_t b, int32_t c, int32_t d) noexcept { return hash(a, b, c, d) / 4294967296.; }
inline double h11(int32_t a, int32_t b, int32_t c, int32_t d) noexcept { return h01(a, b, c, d) * 2 - 1; }
inline uint32_t xorshift(uint32_t &s) noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline double rnd(uint32_t &s) noexcept { return xorshift(s) / 4294967296.; }
inline double gauss(uint32_t &s) noexcept { return (rnd(s) + rnd(s) + rnd(s) + rnd(s) - 2) * 1.7320508; }
// Antiderivative of tanh, for the anti-aliased drive.
inline double antiderivative(double x) noexcept {
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2 * a)) - LN2;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LowShelf, HighShelf, Peak, BandPass, HighPass, LowPass };
    void set(Type type, double f, double q, double gain, double sr) noexcept {
        const double A = std::pow(10., gain / 40), w = TAU * f / sr, cs = std::cos(w), sn = std::sin(w), al = sn / (2 * q);
        double bb0 = 0, bb1 = 0, bb2 = 0, aa0 = 1, aa1 = 0, aa2 = 0;
        if (type == LowShelf || type == HighShelf) {
            const double s = 2 * std::sqrt(A) * al, sg = type == LowShelf ? 1 : -1;
            bb0 = A * ((A + 1) - sg * (A - 1) * cs + s);
            bb1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * cs);
            bb2 = A * ((A + 1) - sg * (A - 1) * cs - s);
            aa0 = (A + 1) + sg * (A - 1) * cs + s;
            aa1 = -sg * 2 * ((A - 1) + sg * (A + 1) * cs);
            aa2 = (A + 1) + sg * (A - 1) * cs - s;
        } else if (type == Peak) { bb0 = 1 + al * A; bb1 = -2 * cs; bb2 = 1 - al * A; aa0 = 1 + al / A; aa1 = -2 * cs; aa2 = 1 - al / A; }
        else if (type == BandPass) { bb0 = al; bb1 = 0; bb2 = -al; aa0 = 1 + al; aa1 = -2 * cs; aa2 = 1 - al; }
        else if (type == HighPass) { bb0 = (1 + cs) / 2; bb1 = -(1 + cs); bb2 = (1 + cs) / 2; aa0 = 1 + al; aa1 = -2 * cs; aa2 = 1 - al; }
        else { bb0 = (1 - cs) / 2; bb1 = 1 - cs; bb2 = (1 - cs) / 2; aa0 = 1 + al; aa1 = -2 * cs; aa2 = 1 - al; }
        b0 = bb0 / aa0; b1 = bb1 / aa0; b2 = bb2 / aa0; a1 = aa1 / aa0; a2 = aa2 / aa0;
    }
    double tick(double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() noexcept { z1 = z2 = 0; }
};

struct Calibration {
    double jitter = .87, shimmer = .95, voice = .1743, lowMidTarget = 14, afterTrim = -6.5, preDrive = .25;
};

}  // namespace wsmod::guttr
