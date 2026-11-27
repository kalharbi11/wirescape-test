// SPDX-License-Identifier: GPL-3.0-only
// GUTTR room (owner's GUTTUR package, src/room.cpp): six early reflections and an eight-line
// Hadamard FDN with three-band damping and slow line modulation. The delay lines live in the module's
// prepared memory and are sized from the sample rate (the package's fixed 8192-sample lines clamped
// the longest lines above ~70 kHz; DESIGN.md "Deviations").
#pragma once
#include "common.h"

namespace wsmod::guttr {

constexpr double kRoomBases[8] = {1153, 1327, 1559, 1801, 2063, 2297, 2539, 2861};
constexpr double kRoomSizeMax = .55 + 1.05;  // size at room = 1

inline uint32_t pow2_at_least(double n) noexcept {
    uint32_t p = 1;
    while (double(p) < n && p < (1u << 30)) p <<= 1;
    return p;
}

struct Room {
    double sr = 48000, size = 1.075, rt = 1.8, cLo = 0, cHi = 0;
    float *lines = nullptr;  // 8 lines of `len` floats
    float *pre = nullptr;    // pre-delay and early-reflection taps
    uint32_t len = 0, mask = 0, pre_len = 0, pre_mask = 0;
    std::array<double, 8> base{}, d{}, gL{}, gM{}, gH{}, lo{}, hi{}, mph{}, dStart{}, dInc{}, v{};
    std::array<int, 6> tapsD{};
    std::array<double, 6> tapsL{}, tapsR{};
    Biquad hpIn{}, hpIn2{};
    uint32_t w = 0, pw = 0, control_rem = 0;
    int preD = 0;
    double peak = 0;  // largest pre-wet output since the caller last cleared it (bus idle detection)

    // Longest line: 2861 samples at 48 kHz times the largest size, plus modulation (5) and interpolation.
    static uint32_t line_length(double rate) noexcept { return pow2_at_least(2861 * rate / 48000. * kRoomSizeMax + 16); }
    // Longest early reflection: 52 ms times the largest size.
    static uint32_t pre_length(double rate) noexcept { return pow2_at_least(.052 * rate * kRoomSizeMax + 4); }

    void prepare(double rate, float *line_memory, uint32_t line_len, float *pre_memory, uint32_t pre_size) noexcept {
        sr = rate;
        lines = line_memory; len = line_len; mask = line_len - 1;
        pre = pre_memory; pre_len = pre_size; pre_mask = pre_size - 1;
        for (int i = 0; i < 8; i++) { base[i] = kRoomBases[i] * rate / 48000.; mph[i] = i * .77; }
        set(.5, 1.8);
        reset();
    }
    void set(double room, double decay) noexcept {
        size = .55 + 1.05 * room;
        rt = decay;
        const double longest = double(mask) - 8;  // never reached: the lines are sized for room = 1
        for (int i = 0; i < 8; i++) {
            const double D = d[i] = std::min(longest, base[i] * size);
            gL[i] = std::pow(10., -3 * D / (sr * rt * .45));
            gM[i] = std::pow(10., -3 * D / (sr * rt));
            gH[i] = std::pow(10., -3 * D / (sr * rt * .32));
        }
        cLo = 1 - std::exp(-TAU * 320 / sr);
        cHi = 1 - std::exp(-TAU * 3800 / sr);
        hpIn.set(Biquad::HighPass, 240, .7, 0, sr);
        hpIn2.set(Biquad::HighPass, 240, .7, 0, sr);
        constexpr double ts[6][3] = {{.011, .5, -.2}, {.019, -.25, .45}, {.027, .32, .22}, {.034, -.18, -.3}, {.043, .2, -.16}, {.052, -.14, .18}};
        for (int i = 0; i < 6; i++) {
            tapsD[i] = int(std::floor(ts[i][0] * sr * size + .5));
            tapsL[i] = ts[i][1];
            tapsR[i] = ts[i][2];
        }
        preD = int(std::floor(.012 * sr + .5));
    }
    // Zeroes the signal memory only (lines, filters); modulation phases and positions continue.
    void clear() noexcept {
        std::fill_n(lines, size_t(len) * 8, 0.f);
        std::fill_n(pre, pre_len, 0.f);
        lo.fill(0);
        hi.fill(0);
        hpIn.reset();
        hpIn2.reset();
        peak = 0;
    }
    void reset() noexcept {
        clear();
        for (int i = 0; i < 8; i++) mph[i] = i * .77;
        w = pw = 0;
        control_rem = 0;
    }
    void control(uint32_t n) noexcept {
        for (int k = 0; k < 8; k++) {
            const double a = d[k] + 5 * std::sin(mph[k]);
            mph[k] += (.11 + .05 * k) * TAU * n / sr;
            const double b = d[k] + 5 * std::sin(mph[k]);
            dStart[k] = a;
            dInc[k] = (b - a) / n;
        }
        control_rem = 0;
    }
    void sample(double input, double &outL, double &outR, double wet) noexcept {
        const double x = hpIn2.tick(hpIn.tick(input));
        pre[pw] = float(x);
        double eL = 0, eR = 0;
        for (int j = 0; j < 6; j++) {
            const float tap = pre[(pw - uint32_t(tapsD[j])) & pre_mask];
            eL += tap * tapsL[j];
            eR += tap * tapsR[j];
        }
        const double xin = pre[(pw - uint32_t(preD)) & pre_mask];
        pw = (pw + 1) & pre_mask;
        for (int k = 0; k < 8; k++) {
            const float *line = lines + size_t(k) * len;
            const double rp = w - (dStart[k] + dInc[k] * control_rem), ri = std::floor(rp), fr = rp - ri;
            const uint32_t index = uint32_t(int(ri)) & mask;
            const double a = line[index], y = a + (line[(index + 1) & mask] - a) * fr;
            const double l = lo[k] + (y - lo[k]) * cLo;
            lo[k] = l;
            const double rest = y - l, h = hi[k] + (rest - hi[k]) * cHi;
            hi[k] = h;
            v[k] = l * gL[k] + h * gM[k] + (rest - h) * gH[k];
        }
        const double a0 = v[0] + v[1], a1 = v[0] - v[1], a2 = v[2] + v[3], a3 = v[2] - v[3], a4 = v[4] + v[5], a5 = v[4] - v[5], a6 = v[6] + v[7], a7 = v[6] - v[7];
        const double b0 = a0 + a2, b2 = a0 - a2, b1 = a1 + a3, b3 = a1 - a3, b4 = a4 + a6, b6 = a4 - a6, b5 = a5 + a7, b7 = a5 - a7;
        constexpr double c = .35355339059327373;
        const double y0 = (b0 + b4) * c, y4 = (b0 - b4) * c, y1 = (b1 + b5) * c, y5 = (b1 - b5) * c, y2 = (b2 + b6) * c, y6 = (b2 - b6) * c, y3 = (b3 + b7) * c, y7 = (b3 - b7) * c;
        const float xi = float(xin * .35);
        float *L = lines;
        L[w] = float(y0 + xi); L[len + w] = float(y1 + xi); L[2 * len + w] = float(y2 + xi); L[3 * len + w] = float(y3 + xi);
        L[4 * len + w] = float(y4 + xi); L[5 * len + w] = float(y5 + xi); L[6 * len + w] = float(y6 + xi); L[7 * len + w] = float(y7 + xi);
        w = (w + 1) & mask;
        control_rem++;
        const double rl = (y0 + y2 + y4 + y6) * .5 + eL * .6, rr = (y1 + y3 + y5 + y7) * .5 + eR * .6;
        peak = std::max(peak, std::max(std::abs(rl), std::abs(rr)));
        outL += rl * wet;
        outR += rr * wet;
    }
};

}  // namespace wsmod::guttr
