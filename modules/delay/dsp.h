// SPDX-License-Identifier: GPL-3.0-only
// fx.delay: stereo delay. Original code; see DESIGN.md.
#pragma once
#include "wirescape_sdk.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

// A72: keep the settled and transition loops as separate functions so the
// compiler cannot merge them; their FMA contraction then matches the original.
#if defined(_MSC_VER) && !defined(__clang__)
#define WS_DELAY_NOINLINE __declspec(noinline)
#else
#define WS_DELAY_NOINLINE __attribute__((noinline))
#endif
namespace fxdelay {
struct Delay {
    static constexpr ws::Info info{"fx.delay", "Delay", 1};
    static constexpr std::array inputs{ws::port("left", "In L", WS2_AUDIO, WS2_SUM, 0, "stereo"),
                                       ws::port("right", "In R", WS2_AUDIO, WS2_SUM, 0, "stereo"),
                                       ws::port("clock", "Clock", WS2_TRIGGER, WS2_FIRST)};
    static constexpr std::array outputs{ws::port("left", "Out L", WS2_AUDIO, WS2_POLY, 0, "stereo"),
                                        ws::port("right", "Out R", WS2_AUDIO, WS2_POLY, 0, "stereo")};
    static constexpr uint32_t stepped = WS2_STEPPED | WS2_AUTOMATABLE | WS2_MODULATABLE;
    static constexpr std::array params{
        ws::param("time", "Time", 0.001f, 2.f, 0.375f, "s", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LOG, 0),
        ws::param("division", "Division", 0, 7, 5, "", stepped, WS2_LINEAR, 1, 4, 1),
        ws::param("feedback", "Feedback", 0, 1, 0.4f, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 2),
        ws::param("tone", "Tone", -1, 1, 0, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 3),
        ws::param("mode", "Mode", 0, 1, 0, "", stepped, WS2_LINEAR, 4, 4, 1),
        ws::param("mix", "Mix", 0, 1, 0.35f, "", WS2_MODULATABLE | WS2_AUTOMATABLE, WS2_LINEAR, 5)};
    enum Slot : uint8_t { kTime, kDivision, kFeedback, kTone, kMode, kMix };

    static constexpr double kMax = 2.0, kMin = 0.001;   // seconds
    static constexpr double kGlide = 0.05, kSlew = 0.5; // glide time constant; max samples moved per sample
    static constexpr float kKnee = 1.f, kCeiling = 1.5f, kWetMax = 1.6f;
    static constexpr double kDivisions[8]{0.25, 1.0 / 3, 0.5, 2.0 / 3, 0.75, 1.0, 1.5, 2.0};

    float *line[2]{};
    uint32_t size = 0, w = 0;
    double rate = 48000, cur = 0, alpha = 0;
    float dc_r = 0.999f, lp[2]{}, dc_x[2]{}, dc_y[2]{};
    bool primed = false;
    float mode_blend = 0.f, mode_step = 0.f;
    uint32_t fade_frames = 1;
    long division_seen = -1;
    uint32_t division_left = 0;
    double division_to = 0;
    // tone cache (a pure function of the tone parameter)
    float tone_seen = 9.f, tone_a = 0, tone_k = 0;
    int tone_kind = 0;
    float tone_current_a = 0, tone_low = 0, tone_high = 0;
    uint32_t tone_left = 0;
    // clock follower
    ws::Schmitt schmitt;
    bool high = false, seen = false;
    uint64_t since = 0;
    double applied = 0, pending = 0;

    static uint32_t length(double r) { return static_cast<uint32_t>(std::ceil(kMax * r)) + 8; }
    static ws2_memory memory_requirements(const ws2_config &c) {
        ws2_memory m{};
        m.struct_size = sizeof(m);
        m.alignment = 32;
        m.large_bytes = 2 * (size_t(length(c.sample_rate)) * sizeof(float) + 64);
        return m;
    }
    bool prepare(const ws::Setup &s) {
        rate = s.sample_rate();
        size = length(rate);
        for (auto &l : line) {
            l = static_cast<float *>(s.alloc(size * sizeof(float), 32, WS2_LARGE));
            if (!l) return false;
        }
        alpha = 1.0 - std::exp(-1.0 / (kGlide * rate));
        mode_step = static_cast<float>(1.0 / (0.010 * rate));
        fade_frames = std::max<uint32_t>(1u, static_cast<uint32_t>(std::ceil(.010 * rate)));
        dc_r = static_cast<float>(1.0 - 6.283185307179586 * 10.0 / rate);
        reset();
        return true;
    }
    void reset() {
        for (auto *l : line)
            if (l) std::fill_n(l, size, 0.f);
        w = 0;
        cur = 0;
        primed = false;
        mode_blend = 0.f;
        division_seen = -1;
        division_left = tone_left = 0;
        division_to = 0;
        tone_seen = 9.f;
        tone_current_a = tone_low = tone_high = 0.f;
        for (int i = 0; i < 2; ++i) lp[i] = dc_x[i] = dc_y[i] = 0.f;
        clock_reset();
    }
    void clock_reset() {
        schmitt.reset();
        high = seen = false;
        since = 0;
        applied = pending = 0;
    }
    // Version 2 is an explicit little-endian, same-rate runtime snapshot.
    static constexpr uint64_t state_cap = 1024u * 1024u;
    static bool put_u(ws::Writer o,uint32_t v){return o.u32(v);}
    static bool get_u(ws::Reader i,uint32_t&v){return i.u32(v);}
    static bool put_q(ws::Writer o,uint64_t v){return o.u32(uint32_t(v))&&o.u32(uint32_t(v>>32));}
    static bool get_q(ws::Reader i,uint64_t&v){uint32_t a,b;if(!i.u32(a)||!i.u32(b))return false;v=uint64_t(a)|(uint64_t(b)<<32);return true;}
    static bool put_d(ws::Writer o,double v){uint64_t b;std::memcpy(&b,&v,8);return put_q(o,b);}
    static bool get_d(ws::Reader i,double&v){uint64_t b;if(!get_q(i,b))return false;std::memcpy(&v,&b,8);return std::isfinite(v);}
    static bool put_f(ws::Writer o,float v){return o.f32(v);}
    static bool get_f(ws::Reader i,float&v){return i.f32(v)&&std::isfinite(v);}
    static bool put_b(ws::Writer o,bool v){return o.u32(v?1u:0u);}
    static bool get_b(ws::Reader i,bool&v){uint32_t b;if(!i.u32(b)||b>1)return false;v=b!=0;return true;}
    static bool put_l(ws::Writer o,long v){return o.u32(v<0?UINT32_MAX:uint32_t(v));}
    static bool get_l(ws::Reader i,long&v){uint32_t b;if(!i.u32(b)||(b!=UINT32_MAX&&b>7))return false;v=b==UINT32_MAX?-1:long(b);return true;}
    static bool put_i(ws::Writer o,int v){return o.u32(uint32_t(v+1));}
    static bool get_i(ws::Reader i,int&v){uint32_t b;if(!i.u32(b)||b>2)return false;v=int(b)-1;return true;}
    int save_state(ws::Writer out) const {
        if(uint64_t(144)+uint64_t(size)*8>state_cap)return WS2_CAPACITY;
        auto edge=schmitt;
        if(!out.u32(2) || !put_u(out, size) ||
            !put_d(out, rate) ||
            !put_u(out, w) ||
            !put_d(out, cur) ||
            !put_f(out, lp[0]) ||
            !put_f(out, lp[1]) ||
            !put_f(out, dc_x[0]) ||
            !put_f(out, dc_x[1]) ||
            !put_f(out, dc_y[0]) ||
            !put_f(out, dc_y[1]) ||
            !put_b(out, primed) ||
            !put_f(out, mode_blend) ||
            !put_l(out, division_seen) ||
            !put_u(out, division_left) ||
            !put_d(out, division_to) ||
            !put_f(out, tone_seen) ||
            !put_f(out, tone_a) ||
            !put_f(out, tone_k) ||
            !put_i(out, tone_kind) ||
            !put_f(out, tone_current_a) ||
            !put_f(out, tone_low) ||
            !put_f(out, tone_high) ||
            !put_u(out, tone_left) ||
            !put_b(out, high) ||
            !put_b(out, seen) ||
            !put_q(out, since) ||
            !put_d(out, applied) ||
            !put_d(out, pending) || !put_b(out,edge.process(.375f)))return WS2_CAPACITY;
        for(auto*l:line)for(uint32_t j=0;j<size;++j)if(!out.f32(l[j]))return WS2_CAPACITY;
        return WS2_OK;
    }
    int load_state(ws::Reader in) {
        uint32_t version;
        if(!in.u32(version))return WS2_INVALID;
        if(version==1){reset();return WS2_OK;}
        if(version!=2)return WS2_UNSUPPORTED;
        uint32_t payload_bytes=0;
        if(!in.size(payload_bytes) || uint64_t(144)+uint64_t(size)*8>state_cap ||
           uint64_t(payload_bytes)!=uint64_t(144)+uint64_t(size)*8)return WS2_INVALID;
        Delay staged=*this;bool latch=false;
        if(!get_u(in, staged.size) ||
            !get_d(in, staged.rate) ||
            !get_u(in, staged.w) ||
            !get_d(in, staged.cur) ||
            !get_f(in, staged.lp[0]) ||
            !get_f(in, staged.lp[1]) ||
            !get_f(in, staged.dc_x[0]) ||
            !get_f(in, staged.dc_x[1]) ||
            !get_f(in, staged.dc_y[0]) ||
            !get_f(in, staged.dc_y[1]) ||
            !get_b(in, staged.primed) ||
            !get_f(in, staged.mode_blend) ||
            !get_l(in, staged.division_seen) ||
            !get_u(in, staged.division_left) ||
            !get_d(in, staged.division_to) ||
            !get_f(in, staged.tone_seen) ||
            !get_f(in, staged.tone_a) ||
            !get_f(in, staged.tone_k) ||
            !get_i(in, staged.tone_kind) ||
            !get_f(in, staged.tone_current_a) ||
            !get_f(in, staged.tone_low) ||
            !get_f(in, staged.tone_high) ||
            !get_u(in, staged.tone_left) ||
            !get_b(in, staged.high) ||
            !get_b(in, staged.seen) ||
            !get_q(in, staged.since) ||
            !get_d(in, staged.applied) ||
            !get_d(in, staged.pending) || !get_b(in,latch))return WS2_INVALID;
        if(staged.size!=size || staged.rate!=rate || uint64_t(144)+uint64_t(size)*8>state_cap ||
           staged.w>=size || staged.cur<0 || staged.cur>kMax*rate ||
           staged.mode_blend<0 || staged.mode_blend>1 || staged.division_left>fade_frames ||
           staged.tone_left>fade_frames || staged.division_to<0 || staged.division_to>kMax*rate ||
           staged.applied<0 || staged.pending<0)return WS2_INVALID;
        // load_state is non-RT; retain ownership until the complete payload validates.
        std::unique_ptr<float[]> history(new (std::nothrow) float[size_t(size)*2]);
        if(!history)return WS2_CAPACITY;
        for(size_t j=0;j<size_t(size)*2;++j)if(!get_f(in,history[j]))return WS2_INVALID;
        staged.schmitt.reset();if(latch)staged.schmitt.process(1.f);
        std::copy_n(history.get(),size,line[0]);
        std::copy_n(history.get()+size,size,line[1]);
        *this=staged;
        return WS2_OK;
    }

    static float sum(const ws::Signal &s, uint32_t i) {
        float t = 0;
        for (uint32_t c = 0; c < s.channels(); ++c) t += s.ch(c)[i];
        return t;
    }
    // Soft limiter: identity up to the knee, then tanh-compressed towards the ceiling (1.5).
    static float limit(float x) {
        const float a = std::fabs(x);
        if (a <= kKnee) return x;
        return std::copysign(kKnee + (kCeiling - kKnee) * std::tanh((a - kKnee) / (kCeiling - kKnee)), x);
    }
    static float flush(float x) { return std::fabs(x) < 1e-18f ? 0.f : x; }
    // 4-point Hermite (Catmull-Rom) read `d` samples behind the write position (2 <= d <= 2 s).
    float read(const float *buf, double d) const {
        const int di = static_cast<int>(d);
        const float fr = static_cast<float>(d - di);
        auto at = [&](int age) {
            int k = static_cast<int>(w) - age;
            if (k < 0) k += static_cast<int>(size);
            return buf[k];
        };
        const float y1 = at(di);
        if (fr == 0.f) return y1;
        const float ym1 = at(di + 2), y0 = at(di + 1), y2 = at(di - 1), mu = 1.f - fr;
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * mu + c2) * mu + c1) * mu + y0;
    }
    void update_tone(float tone) {
        if (tone == tone_seen) return;
        const int old_kind = tone_kind;
        const bool initial = tone_seen == 9.f;
        tone_seen = tone;
        const double t = std::clamp(static_cast<double>(tone), -1.0, 1.0);
        double fc = 0;
        if (std::fabs(t) < 1e-6) {
            tone_kind = 0;
        } else if (t < 0) {
            tone_kind = -1;
            fc = 800.0 * std::pow(25.0, 1.0 + t); // low-pass 20 kHz .. 800 Hz
        } else {
            tone_kind = 1;
            fc = 20.0 * std::pow(75.0, t); // high-pass 20 Hz .. 1.5 kHz
        }
        fc = std::min(fc, 0.45 * rate);
        tone_a = static_cast<float>(1.0 - std::exp(-6.283185307179586 * fc / rate));
        tone_k = static_cast<float>(std::min(1.0, 4.0 * std::fabs(t))); // fades in, so tone 0 is exactly flat
        if (!initial && (old_kind != tone_kind || tone_left)) {
            // Preserve the live filter state and ramp the low/high-pass mix
            // across bypass. Retarget from the current coefficients.
            if (tone_low == 0.f && tone_high == 0.f) tone_current_a = tone_a;
            // Same-kind host ramp samples retarget the current fade without
            // restarting its duration; only a real filter-kind change rearms it.
            if (old_kind != tone_kind) tone_left = fade_frames;
        } else {
            tone_current_a = tone_a;
            tone_low = tone_kind < 0 ? tone_k : 0.f;
            tone_high = tone_kind > 0 ? tone_k : 0.f;
        }
    }
    void advance_tone() {
        if (!tone_left) return;
        const float fraction = 1.f / static_cast<float>(tone_left);
        // Keep the old cutoff while fading out to a flat bypass.
        if (tone_kind) tone_current_a += fraction * (tone_a - tone_current_a);
        tone_low += fraction * ((tone_kind < 0 ? tone_k : 0.f) - tone_low);
        tone_high += fraction * ((tone_kind > 0 ? tone_k : 0.f) - tone_high);
        --tone_left;
    }
    void update_tone_settled(float tone) { // verbatim pre-S7.6 (A72)
        if (tone == tone_seen) return;
        tone_seen = tone;
        const double t = std::clamp(static_cast<double>(tone), -1.0, 1.0);
        double fc = 0;
        if (std::fabs(t) < 1e-6) {
            tone_kind = 0;
        } else if (t < 0) {
            tone_kind = -1;
            fc = 800.0 * std::pow(25.0, 1.0 + t); // low-pass 20 kHz .. 800 Hz
        } else {
            tone_kind = 1;
            fc = 20.0 * std::pow(75.0, t); // high-pass 20 Hz .. 1.5 kHz
        }
        fc = std::min(fc, 0.45 * rate);
        tone_a = static_cast<float>(1.0 - std::exp(-6.283185307179586 * fc / rate));
        tone_k = static_cast<float>(std::min(1.0, 4.0 * std::fabs(t))); // fades in, so tone 0 is exactly flat
    }
    // Feedback path of one line: tone filter, then DC blocker.
    float loop(int ch, float x) {
        float y = x;
        if (tone_left) {
            lp[ch] = flush(lp[ch] + tone_current_a * (x - lp[ch]));
            y = x + tone_low * (lp[ch] - x) + tone_high * ((x - lp[ch]) - x);
        } else if (tone_kind) {
            lp[ch] = flush(lp[ch] + tone_a * (x - lp[ch]));
            const float f = tone_kind < 0 ? lp[ch] : x - lp[ch];
            y = x + tone_k * (f - x);
        }
        const float out = flush(y - dc_x[ch] + dc_r * dc_y[ch]);
        dc_x[ch] = y;
        dc_y[ch] = out;
        return out;
    }
    // Settled feedback path: verbatim pre-S7.6 loop(), kept separate so the
    // compiler sees the original expression shape (FMA contraction, A72).
    float loop_settled(int ch, float x) {
        float y = x;
        if (tone_kind) {
            lp[ch] = flush(lp[ch] + tone_a * (x - lp[ch]));
            const float f = tone_kind < 0 ? lp[ch] : x - lp[ch];
            y = x + tone_k * (f - x);
        }
        const float out = flush(y - dc_x[ch] + dc_r * dc_y[ch]);
        dc_x[ch] = y;
        dc_y[ch] = out;
        return out;
    }
    // Measures the clock period from rising edges. The first interval applies at the second edge; a later change
    // needs two consistent intervals. No edge for 4 s: keep the last period and restart measuring.
    void clock_tick(float v) {
        const bool now = schmitt.process(v);
        const bool edge = now && !high;
        high = now;
        ++since;
        if (edge) {
            if (seen) {
                const double iv = static_cast<double>(since);
                auto same = [](double a, double b) { return std::fabs(a - b) <= 0.02 * std::max(a, b); };
                if (applied == 0 || same(iv, applied) || (pending > 0 && same(iv, pending))) {
                    applied = iv;
                    pending = 0;
                } else {
                    pending = iv;
                }
            }
            seen = true;
            since = 0;
        } else if (seen && static_cast<double>(since) > 4.0 * rate) {
            seen = false;
            pending = 0;
        }
    }

    // A settled block cannot start or continue a switch transition: mode,
    // tone and clocked division rows are constant at their settled targets.
    // Such blocks run the verbatim pre-S7.6 loop; every other block runs the
    // transition loop. Both read and write the same state.
    WS_DELAY_NOINLINE bool settled_block(ws::Block &b) {
        if (!b.frames() || division_left || tone_left) return false;
        const auto p_tone = b.param(kTone), p_mode = b.param(kMode), p_div = b.param(kDivision);
        if (!p_mode.constant() || !p_tone.constant()) return false;
        const float mode_target = std::lround(p_mode.ch(0)[0]) >= 1 ? 1.f : 0.f;
        if (primed && mode_blend != mode_target) return false;
        update_tone(p_tone.ch(0)[0]);
        if (tone_left) return false;
        if (b.in(2).connected()) {
            if (!p_div.constant()) return false;
            const long d = std::clamp(std::lround(p_div.ch(0)[0]), 0l, 7l);
            if (division_seen >= 0 && d != division_seen) return false;
        }
        mode_blend = mode_target;
        return true;
    }
    WS_DELAY_NOINLINE void settled_after(ws::Block &b) {
        if (!b.in(2).connected()) division_seen = -1;
        else if (applied > 0) division_seen = std::clamp(std::lround(b.param(kDivision).ch(0)[0]), 0l, 7l);
        else division_seen = -1;
    }
    // Frames [begin, frames) of a block, with the ws::Block accessors the
    // settled loop uses. Built out of line; constant rows are not offset.
    struct Tail {
        static constexpr uint32_t kChannels = 16;
        uint32_t n = 0;
        ws2_signal sig[9]{};
        const float *rows[9][kChannels]{};
        ws2_signal_out outs[2]{};
        float *out_rows[2][1]{};
        uint32_t frames() const { return n; }
        ws::Signal in(uint32_t i) const { return ws::Signal(&sig[i]); }
        ws::Signal param(uint32_t i) const { return ws::Signal(&sig[3 + i]); }
        ws::Output out(uint32_t i) { return ws::Output(&outs[i]); }
    };
    WS_DELAY_NOINLINE static void tail(ws::Block &b, uint32_t begin, Tail &t) {
        t.n = b.frames() - begin;
        for (uint32_t k = 0; k < 9; ++k) {
            const ws::Signal s = k < 3 ? b.in(k) : b.param(k - 3);
            const uint32_t count = std::min(s.channels(), Tail::kChannels);
            for (uint32_t c = 0; c < count; ++c) {
                const ws::Channel row = s.ch(c);
                t.rows[k][c] = row.p && !row.is_constant ? row.p + begin : row.p;
            }
            t.sig[k] = {sizeof(ws2_signal), t.rows[k], count,
                        (s.constant() ? uint32_t(WS2_CONSTANT) : 0u) | (s.connected() ? uint32_t(WS2_CONNECTED) : 0u)};
        }
        for (uint32_t o = 0; o < 2; ++o) {
            b.out(o).set_channels(1);
            b.out(o).mark_constant(false);
            t.out_rows[o][0] = b.out(o).ch(0) + begin;
            t.outs[o] = {sizeof(ws2_signal_out), t.out_rows[o], 1, 0, 1};
        }
    }
    WS_DELAY_NOINLINE void settled_tail(ws::Block &b, uint32_t begin) {
        Tail t;
        tail(b, begin, t);
        process_settled(t);
    }
    void process(ws::Block &b) {
        if (settled_block(b)) {
            process_settled(b);
        } else {
            const uint32_t begin = process_transition(b);
            if (begin == b.frames()) return;
            settled_tail(b, begin);
        }
        settled_after(b);
    }
    // Verbatim pre-S7.6 process body (A72), over a whole block or a Tail.
    template <class B> void process_settled(B &b) {
        for (uint32_t o = 0; o < 2; ++o) {
            b.out(o).set_channels(1);
            b.out(o).mark_constant(false);
        }
        float *out[2]{b.out(0).ch(0), b.out(1).ch(0)};
        const auto in_l = b.in(0), in_r = b.in(1), clock = b.in(2);
        const bool right_patched = in_r.connected(), clocked = clock.connected();
        if (!clocked) clock_reset();
        const auto p_time = b.param(kTime), p_div = b.param(kDivision), p_fb = b.param(kFeedback);
        const auto p_tone = b.param(kTone), p_mode = b.param(kMode), p_mix = b.param(kMix);
        const bool mix_constant = p_mix.constant();
        float dry = 0, wet = 0;
        const double lo = kMin * rate, hi = kMax * rate;
        for (uint32_t i = 0; i < b.frames(); ++i) {
            const float xl = sum(in_l, i), xr = right_patched ? sum(in_r, i) : xl;
            if (clocked) clock_tick(clock.ch(0)[i]);
            double target = static_cast<double>(p_time.ch(0)[i]) * rate;
            if (clocked && applied > 0) {
                const long d = std::clamp(std::lround(p_div.ch(0)[i]), 0l, 7l);
                target = applied * kDivisions[d];
            }
            target = std::clamp(target, lo, hi);
            if (!primed) {
                cur = target;
                primed = true;
            } else {
                const double diff = target - cur;
                cur += std::fabs(diff) < 1e-4 ? diff : std::clamp(diff * alpha, -kSlew, kSlew);
            }
            const float dl = std::clamp(read(line[0], cur), -kWetMax, kWetMax);
            const float dr = std::clamp(read(line[1], cur), -kWetMax, kWetMax);
            update_tone_settled(p_tone.ch(0)[i]);
            const float fb = std::clamp(p_fb.ch(0)[i], 0.f, 1.f);
            const float fl = loop_settled(0, dl), fr = loop_settled(1, dr);
            if (std::lround(p_mode.ch(0)[i]) >= 1) { // ping-pong: mono in, bounce L -> R -> L
                line[0][w] = limit(0.5f * (xl + xr) + fb * fr);
                line[1][w] = limit(fb * fl);
            } else {
                line[0][w] = limit(xl + fb * fl);
                line[1][w] = limit(xr + fb * fr);
            }
            if (++w == size) w = 0;
            if (i == 0 || !mix_constant) {
                const double m = std::clamp(static_cast<double>(p_mix.ch(0)[i]), 0.0, 1.0);
                dry = m >= 1.0 ? 0.f : static_cast<float>(std::cos(m * 1.5707963267948966));
                wet = m <= 0.0 ? 0.f : static_cast<float>(std::sin(m * 1.5707963267948966));
            }
            out[0][i] = dry * xl + wet * dl;
            out[1][i] = dry * xr + wet * dr;
        }
    }
    // Returns the first frame from which the rest of a constant-row block is
    // settled, so a transition ends at the same sample for any block size.
    WS_DELAY_NOINLINE uint32_t process_transition(ws::Block &b) {
        for (uint32_t o = 0; o < 2; ++o) {
            b.out(o).set_channels(1);
            b.out(o).mark_constant(false);
        }
        float *out[2]{b.out(0).ch(0), b.out(1).ch(0)};
        const auto in_l = b.in(0), in_r = b.in(1), clock = b.in(2);
        const bool right_patched = in_r.connected(), clocked = clock.connected();
        if (!clocked) {
            clock_reset();
            division_seen = -1;
        }
        const auto p_time = b.param(kTime), p_div = b.param(kDivision), p_fb = b.param(kFeedback);
        const auto p_tone = b.param(kTone), p_mode = b.param(kMode), p_mix = b.param(kMix);
        const bool mix_constant = p_mix.constant();
        // Stepped controls normally arrive as constant rows. Round once per
        // block in that case; retain the sample-addressed path for automation.
        const bool mode_constant = p_mode.constant(), division_constant = p_div.constant();
        const bool ping_pong_constant = mode_constant && std::lround(p_mode.ch(0)[0]) >= 1;
        const long division_value = clocked && division_constant
                                        ? std::clamp(std::lround(p_div.ch(0)[0]), 0l, 7l) : 0;
        const bool tone_constant = p_tone.constant();
        if (tone_constant && b.frames()) update_tone(p_tone.ch(0)[0]);
        const bool settle = mode_constant && tone_constant && (!clocked || division_constant);
        float dry = 0, wet = 0;
        const double lo = kMin * rate, hi = kMax * rate;
        for (uint32_t i = 0; i < b.frames(); ++i) {
            const float xl = sum(in_l, i), xr = right_patched ? sum(in_r, i) : xl;
            if (clocked) clock_tick(clock.ch(0)[i]);
            double target = static_cast<double>(p_time.ch(0)[i]) * rate;
            if (clocked && applied > 0) {
                const long d = division_constant ? division_value
                                                 : std::clamp(std::lround(p_div.ch(0)[i]), 0l, 7l);
                target = applied * kDivisions[d];
                if (!division_left && division_seen >= 0 && d != division_seen) {
                    // Only a division edit crossfades taps; clock acquisition,
                    // tempo changes and free time retain their original glide.
                    division_to = std::clamp(target, lo, hi);
                    division_left = fade_frames;
                }
                // Coalesce a retarget until the current bounded fade ends.
                if (!division_left || division_left == fade_frames) division_seen = d;
            } else if (clocked) {
                division_seen = -1;
            }
            target = std::clamp(target, lo, hi);
            const bool ping_pong = mode_constant ? ping_pong_constant : std::lround(p_mode.ch(0)[i]) >= 1;
            const float mode_target = ping_pong ? 1.f : 0.f;
            if (!primed) {
                mode_blend = mode_target; // No startup fade for a settled patch.
                cur = target;
                primed = true;
            } else if (!division_left) {
                const double diff = target - cur;
                cur += std::fabs(diff) < 1e-4 ? diff : std::clamp(diff * alpha, -kSlew, kSlew);
            }
            float dl = std::clamp(read(line[0], cur), -kWetMax, kWetMax);
            float dr = std::clamp(read(line[1], cur), -kWetMax, kWetMax);
            if (division_left) {
                const float blend = static_cast<float>(fade_frames - division_left + 1) /
                                    static_cast<float>(fade_frames);
                const float nl = std::clamp(read(line[0], division_to), -kWetMax, kWetMax);
                const float nr = std::clamp(read(line[1], division_to), -kWetMax, kWetMax);
                if (--division_left == 0) {
                    cur = division_to;
                    dl = nl;
                    dr = nr;
                } else {
                    dl += blend * (nl - dl);
                    dr += blend * (nr - dr);
                }
            }
            if (!tone_constant) update_tone(p_tone.ch(0)[i]);
            advance_tone();
            const float fb = std::clamp(p_fb.ch(0)[i], 0.f, 1.f);
            const float fl = loop(0, dl), fr = loop(1, dr);
            // Crossfade the input and feedback routing, not two complete delay
            // instances. Retarget from the current blend on a rapid reversal.
            if (mode_blend != mode_target) {
                mode_blend = ping_pong ? std::min(1.f, mode_blend + mode_step)
                                       : std::max(0.f, mode_blend - mode_step);
                const float sl = xl + fb * fl, sr = xr + fb * fr;
                const float pl = 0.5f * (xl + xr) + fb * fr, pr = fb * fl;
                line[0][w] = limit(sl + mode_blend * (pl - sl));
                line[1][w] = limit(sr + mode_blend * (pr - sr));
            } else if (ping_pong) { // Original settled routing equations and operation order.
                line[0][w] = limit(0.5f * (xl + xr) + fb * fr);
                line[1][w] = limit(fb * fl);
            } else {
                line[0][w] = limit(xl + fb * fl);
                line[1][w] = limit(xr + fb * fr);
            }
            if (++w == size) w = 0;
            if (i == 0 || !mix_constant) {
                const double m = std::clamp(static_cast<double>(p_mix.ch(0)[i]), 0.0, 1.0);
                dry = m >= 1.0 ? 0.f : static_cast<float>(std::cos(m * 1.5707963267948966));
                wet = m <= 0.0 ? 0.f : static_cast<float>(std::sin(m * 1.5707963267948966));
            }
            out[0][i] = dry * xl + wet * dl;
            out[1][i] = dry * xr + wet * dr;
            if (settle && !division_left && !tone_left && mode_blend == mode_target &&
                (!clocked || applied <= 0 || division_seen == division_value))
                return i + 1;
        }
        return b.frames();
    }
};
} // namespace fxdelay
