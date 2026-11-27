// SPDX-License-Identifier: GPL-3.0-only
// GUTTR tables built in prepare (owner's GUTTUR package: src/tables.cpp, src/tube.cpp,
// src/reference_constants.h): LF glottal pulse mipmaps with flow and harmonic tables (radix-2 FFT),
// and the 34-section tube resonance grid with its bandwidth model.
#pragma once
#include "common.h"

namespace wsmod::guttr {

namespace detail {
inline void fft(double *re, double *im, int n, bool inverse) noexcept {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double a = (inverse ? TAU : -TAU) / len, wr = std::cos(a), wi = std::sin(a);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                const int p = i + k, q = p + len / 2;
                const double tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr; im[q] = im[p] - ti; re[p] += tr; im[p] += ti;
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
    if (inverse)
        for (int i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}
struct LF { double Tp, Te, Ta, eps, shift, Delta, omega, alpha, E0; };
inline LF lf_params(double Rd) noexcept {
    const double Ra = -.01 + .048 * Rd, Rk = .224 + .118 * Rd;
    const double Rg = (Rk / 4) * (.5 + 1.2 * Rk) / (.11 * Rd - Ra * (.5 + 1.2 * Rk));
    const double Ta = Ra, Tp = 1 / (2 * Rg), Te = Tp + Tp * Rk, eps = 1 / Ta, shift = std::exp(-eps * (1 - Te)), Delta = 1 - shift;
    double rhs = (1 / eps) * (shift - 1) + (1 - Te) * shift;
    rhs /= Delta;
    const double lower = -(Te - Tp) / 2 + rhs, upper = -lower, omega = PI / Tp, si = std::sin(omega * Te);
    const double y = -PI * si * upper / (Tp * 2), z = std::log(y), alpha = z / (Tp / 2 - Te), E0 = -1 / (si * std::exp(alpha * Te));
    return {Tp, Te, Ta, eps, shift, Delta, omega, alpha, E0};
}
inline double lf_sample(const LF &p, double t) noexcept {
    if (t > p.Te) return (-std::exp(-p.eps * (t - p.Te)) + p.shift) / p.Delta;
    return p.E0 * std::exp(p.alpha * t) * std::sin(p.omega * t);
}
// Vowel area anchors (5 profiles).
struct Anchor { double x, area; };
inline constexpr int kAnchorsCount[5] = {12, 11, 10, 10, 11};
inline constexpr Anchor kAnchors[5][12] = {
    {{0, .6}, {.06, 1.2}, {.12, 3}, {.32, 5.5}, {.45, 3}, {.53, .6}, {.58, .45}, {.68, 3}, {.82, 6.5}, {.9, 2}, {.93, .25}, {1, .2}},
    {{0, .6}, {.06, 1.2}, {.12, 1.8}, {.3, 1.5}, {.42, 1.2}, {.52, 1.6}, {.66, 4.5}, {.84, 6}, {.9, 2}, {.93, .6}, {1, .5}, {1, .5}},
    {{0, .6}, {.06, 1.2}, {.12, .9}, {.25, .55}, {.42, .7}, {.55, 2.2}, {.68, 4.6}, {.82, 6}, {.92, 5.2}, {1, 3.8}, {1, 3.8}, {1, 3.8}},
    {{0, .6}, {.06, 1.2}, {.12, 2.6}, {.3, 4.5}, {.5, 3.2}, {.65, 1.6}, {.78, 1.1}, {.88, 1.6}, {.95, 2.4}, {1, 2.6}, {1, 2.6}, {1, 2.6}},
    {{0, .6}, {.06, 1.2}, {.12, 3.2}, {.3, 7}, {.48, 6}, {.62, 2.2}, {.72, .5}, {.82, .32}, {.9, .7}, {.96, 1.6}, {1, 2}, {1, 2}},
};
inline double profile_at(int v, double x) noexcept {
    const auto &cp = kAnchors[v];
    for (int i = 1; i < kAnchorsCount[v]; i++)
        if (x <= cp[i].x) {
            const double t = (x - cp[i - 1].x) / (cp[i].x - cp[i - 1].x);
            const double d = (1 - t) * std::sqrt(cp[i - 1].area) + t * std::sqrt(cp[i].area);
            return d * d;
        }
    return cp[kAnchorsCount[v] - 1].area;
}
inline void area_function(double vowel, double pharynx, double mouth, double ring, double *out) noexcept {
    const double pos = clamp(vowel, 0, 1) * 4;
    const int i0 = std::min(3, int(std::floor(pos)));
    const double t = pos - i0;
    const double mk = std::pow(2, (clamp(mouth, 0, 1) - .5) * 4.4);
    for (int i = 0; i < NS; i++) {
        const double x = (i + .5) / NS, d = (1 - t) * std::sqrt(profile_at(i0, x)) + t * std::sqrt(profile_at(i0 + 1, x));
        double A = d * d;
        const double wp = std::exp(-std::pow((x - .27) / .09, 2)) * clamp(pharynx, 0, 1);
        A = std::pow(A, 1 - wp) * std::pow(.3, wp);
        const double wr = (1 - smooth(.08, .16, x)) * clamp(ring, 0, 1) * .85;
        A = std::pow(A, 1 - wr) * std::pow(.22, wr);
        A *= std::pow(mk, smooth(.76, .92, x));
        out[i] = std::max(.05, A);
    }
}
inline double tube_m22(const double *A, double L, double f) noexcept {
    const double l = L / NS, k = TAU * f * l / SOUND, cs = std::cos(k), sn = std::sin(k);
    double a = 1, b = 0, c = 0, d = 1;
    for (int i = 0; i < NS; i++) {
        const double Z = 1 / A[i], b2 = Z * sn, c2 = sn / Z;
        const double na = a * cs - b * c2, nb = a * b2 + b * cs, nc = c * cs + d * c2, nd = -c * b2 + d * cs;
        a = na; b = nb; c = nc; d = nd;
    }
    const double r = std::sqrt(A[NS - 1] / PI), ke = TAU * f * .8 * r / SOUND, ce = std::cos(ke), se = std::sin(ke);
    const double Z = 1 / A[NS - 1];
    return -c * Z * se + d * ce;
}
inline void tube_resonances(const double *A, double L, double *out, double *amp) noexcept {
    int found = 0;
    const double df = 25, f0 = 40;
    double v0 = tube_m22(A, L, f0);
    for (double f = f0 + df; f < 16000 && found < NRES; f += df) {
        const double v = tube_m22(A, L, f);
        if (v0 * v < 0) {
            double lo = f - df, hi = f, vl = v0;
            for (int it = 0; it < 22; it++) {
                const double m = .5 * (lo + hi), vm = tube_m22(A, L, m);
                if (vl * vm <= 0) hi = m;
                else { lo = m; vl = vm; }
            }
            const double fk = .5 * (lo + hi), dl = .25, dd = (tube_m22(A, L, fk + dl) - tube_m22(A, L, fk - dl)) / (2 * dl);
            amp[found] = -2 / (fk * dd);
            out[found++] = fk;
        }
        v0 = v;
    }
    for (; found < NRES; found++) { out[found] = found ? out[found - 1] * 1.15 : 500; amp[found] = 0; }
}
}  // namespace detail

struct Pulse {
    std::array<float, TABLE_TOTAL> data{};
    std::array<float, NR * (FLOW + 1)> flow{};
    std::array<float, NR * NH> harm{};
    std::array<float, NR> te{};
    std::array<int, NLEV> sizes{}, hmax{}, base{};
    // re/im: M doubles each; xr/xi: MAX_LEVEL doubles each (prepare-time scratch).
    void build(double *re, double *im, double *xr, double *xi) noexcept {
        int total = 0;
        for (int l = 0; l < NLEV; ++l) {
            hmax[l] = std::max(2, 512 >> l);
            sizes[l] = l == 0 ? MAX_LEVEL : std::min(MAX_LEVEL, 64 * hmax[l]);
            base[l] = total;
            total += (sizes[l] + 1) * NR;
        }
        for (int r = 0; r < NR; ++r) {
            const auto p = detail::lf_params(RD_MIN + r * RD_STEP);
            te[r] = float(p.Te);
            double mean = 0;
            for (int i = 0; i < M; i++) { re[i] = detail::lf_sample(p, double(i) / M); im[i] = 0; mean += re[i]; }
            mean /= M;
            double acc = 0, fmin = std::numeric_limits<double>::infinity(), fmax = 1e-9;
            for (int i = 0; i < M; i++) { acc += re[i] - mean; im[i] = acc; fmin = std::min(fmin, acc); }  // flow
            for (int i = 0; i < M; i++) fmax = std::max(fmax, im[i] - fmin);
            for (int k = 0; k <= FLOW; k++) {
                const int idx = std::min(M - 1, int(std::floor(double(k) * (M - 1) / FLOW + .5)));
                flow[r * (FLOW + 1) + k] = float((im[idx] - fmin) / fmax);
            }
            std::fill_n(im, M, 0.);
            detail::fft(re, im, M, false);
            for (int h = 1; h <= NH; h++) harm[r * NH + h - 1] = float(std::hypot(re[h], im[h]) / M * 2);
            for (int l = 0; l < NLEV; l++) {
                const int S = sizes[l], H = hmax[l];
                std::fill_n(xr, S, 0.);
                std::fill_n(xi, S, 0.);
                for (int h = 1; h <= H && h < S / 2; h++) {
                    const double x = double(h) / H, tap = x < .7 ? 1. : .5 * (1 + std::cos(PI * (x - .7) / .3)), g = tap * S / double(M);
                    xr[h] = re[h] * g; xi[h] = im[h] * g; xr[S - h] = re[h] * g; xi[S - h] = -im[h] * g;
                }
                detail::fft(xr, xi, S, true);
                const int o = base[l] + r * (S + 1);
                for (int i = 0; i < S; i++) data[o + i] = float(xr[i]);
                data[o + S] = float(xr[0]);
            }
        }
    }
};

struct Tube {
    std::array<float, GRID * NRES> F{}, Am{};
    std::array<float, GRID> minA{};
    void build() noexcept {
        double A[NS]{}, R[NRES]{}, RA[NRES]{};
        int idx = 0;
        for (int iv = 0; iv < 9; iv++)
            for (int ip = 0; ip < 5; ip++)
                for (int im = 0; im < 5; im++)
                    for (int ir = 0; ir < 4; ir++) {
                        detail::area_function(double(iv) / 8, double(ip) / 4, double(im) / 4, double(ir) / 3, A);
                        detail::tube_resonances(A, L_REF, R, RA);
                        double mn = 99;
                        for (double a : A) mn = std::min(mn, a);
                        for (int k = 0; k < NRES; k++) { F[idx * NRES + k] = float(R[k]); Am[idx * NRES + k] = float(RA[k]); }
                        minA[idx++] = float(mn);
                    }
    }
    double lookup(double vowel, double pharynx, double mouth, double ring, double *outF, double *outA) const noexcept {
        const double fv = clamp(vowel, 0, 1) * 8, fp = clamp(pharynx, 0, 1) * 4, fm = clamp(mouth, 0, 1) * 4, fr = clamp(ring, 0, 1) * 3;
        const int iv = std::min(7, int(fv)), ip = std::min(3, int(fp)), im = std::min(3, int(fm)), ir = std::min(2, int(fr));
        const double tv = fv - iv, tp = fp - ip, tm = fm - im, tr = fr - ir;
        std::fill_n(outF, NRES, 0.);
        if (outA) std::fill_n(outA, NRES, 0.);
        double mA = 0;
        for (int c = 0; c < 16; c++) {
            const int dv = c & 1, dp = (c >> 1) & 1, dm = (c >> 2) & 1, dr = (c >> 3) & 1;
            const double w = (dv ? tv : 1 - tv) * (dp ? tp : 1 - tp) * (dm ? tm : 1 - tm) * (dr ? tr : 1 - tr);
            if (w == 0) continue;
            const int idx = (((iv + dv) * 5 + (ip + dp)) * 5 + (im + dm)) * 4 + (ir + dr), o = idx * NRES;
            for (int k = 0; k < NRES; k++) { outF[k] += w * F[o + k]; if (outA) outA[k] += w * Am[o + k]; }
            mA += w * minA[idx];
        }
        return mA;
    }
};

inline double bandwidth(double F, int n, double rd, double minA) noexcept {
    double B;
    if (F < 500) B = 165.327516 + F * (-.673636734 + F * (1.80874446e-3 + F * (-4.52201682e-6 + F * (7.495140e-9 - F * 4.70219241e-12))));
    else if (F < 5000) B = 15.8146139 + F * (8.10159009e-2 + F * (-9.79728215e-5 + F * (5.28725064e-8 + F * (-1.07099364e-11 + F * 7.91528509e-16))));
    else B = 360 * std::pow(F / 5000, 1.6);
    B = std::max(B, 40.);
    if (n == 0) B += 28 * rd;
    else if (n == 1) B += 14 * rd;
    else if (n == 2) B += 6 * rd;
    const double narrow = clamp((.9 - minA) / .75, 0, 1);
    if (n < 3) B *= 1 + narrow * (n == 0 ? .9 : n == 1 ? .6 : .3);
    if (F > 3000) B = std::max(B, .075 * F);
    return B;
}

}  // namespace wsmod::guttr
