// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../../engine/voice/note_to_cv.h"
#include "wirescape_sdk.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace notes_to_cv {

// A Notes-event counterpart to midi.to_cv. Input channel is a note's MIDI
// channel, not its allocated poly output lane. Voice ownership belongs to the
// existing NoteToCv/Allocator code (including anonymous IDs and steals).
struct NotesToCv {
    static constexpr ws::Info info{"essentials.notes_to_cv", "Notes to CV", 1};
    static constexpr std::array inputs{ws::port("notes", "Notes", WS2_EV_NOTES, WS2_POLY, 0, nullptr, WS2_EVENT)};
    static constexpr std::array outputs{ws::port("pitch", "Pitch", WS2_PITCH), ws::port("gate", "Gate", WS2_GATE),
                                        ws::port("velocity", "Velocity", WS2_CV_UNI),
                                        ws::port("trig", "Trigger", WS2_GATE)};
    static constexpr std::array params{
        ws::param("voices", "Voices", 1, 16, 4, "", WS2_STEPPED | WS2_AUTOMATABLE, WS2_LINEAR, 0, 4, 1)};

    wirescape::voice::NoteToCv core;
    uint32_t max_channels = 16;
    int configured_voices = 0;
    std::array<uint32_t, wirescape::voice::kMaxVoices> trig_left{};
    uint32_t trig_active_mask = 0; // avoid scanning all voices in idle blocks

    void prepare(const ws::Setup &s) { max_channels = s.max_channels(); }
    void reset() {
        core.reset();
        configured_voices = 0;
        trig_left = {};
        trig_active_mask = 0;
    }
    void process_no_trigger(ws::Block &b) {
        const int desired =
            std::clamp(static_cast<int>(std::lround(b.param(0).value(0, 0))), 1, static_cast<int>(max_channels));
        if (desired != configured_voices) {
            wirescape::voice::Config cfg{};
            cfg.max_voices = static_cast<uint8_t>(desired);
            cfg.mode = wirescape::voice::Mode::Rotate;
            if (configured_voices) {
                ws2_event cause{};
                cause.struct_size = sizeof(cause);
                core.resize(desired, cause, [](const ws2_event &) {});
                for (uint32_t c = uint32_t(core.allocator().voice_count()); c < wirescape::voice::kMaxVoices; ++c) {
                    trig_left[c] = 0;
                    trig_active_mask &= ~(uint32_t{1} << c);
                }
            } else {
                core.configure(cfg);
                trig_left = {};
                trig_active_mask = 0;
            }
            configured_voices = desired;
        }
        const uint32_t n = static_cast<uint32_t>(core.allocator().voice_count());
        for (uint32_t p = 0; p < 3; ++p) {
            b.out(p).set_channels(n);
            b.out(p).mark_constant(false);
        }
        // Cache stable signal output rows once per block, not per sample.
        std::array<std::array<float*, wirescape::voice::kMaxVoices>, 3> output_rows{};
        for (uint32_t p = 0; p < 3; ++p)
            for (uint32_t c = 0; c < n; ++c)
                output_rows[p][c] = b.out(p).ch(c);
        b.split(
            [&](uint32_t first, uint32_t end) {
                for (uint32_t i = first; i < end; ++i) {
                    for (uint32_t c = 0; c < n; ++c) {
                        const auto &v = core.allocator().voice(static_cast<int>(c));
                        output_rows[0][c][i] = v.pitch;
                        // A stolen/retriggered voice drops gate for one sample,
                        // allowing the downstream envelope to retrigger.
                        output_rows[1][c][i] = v.gate && !(v.trigger && v.retrigger) ? 1.f : 0.f;
                        output_rows[2][c][i] = v.velocity;
                    }
                    core.allocator().clear_triggers();
                }
            },
            [&](const ws2_event &e) {
                if (e.type == WS2_NOTE_ON || e.type == WS2_NOTE_OFF || e.type == WS2_NOTE_CHOKE ||
                    e.type == WS2_NOTE_EXPRESSION) {
                    // NotesToCv has signal outputs only. The allocator handles
                    // releases and expressions internally; it does not emit here.
                    core.event(e, [](const ws2_event &) {});
                }
            });
    }
    void process(ws::Block &b) {
        // The original hot loop is unchanged on idle, pulse-free blocks.
        if (b.events().size() == 0 && trig_active_mask == 0) {
            process_no_trigger(b);
            const uint32_t n = b.out(0).channels();
            b.out(3).set_channels(n);
            b.out(3).mark_constant(true);
            for (uint32_t c = 0; c < n; ++c) b.out(3).ch(c)[0] = 0.f;
            return;
        }
        const int desired =
            std::clamp(static_cast<int>(std::lround(b.param(0).value(0, 0))), 1, static_cast<int>(max_channels));
        if (desired != configured_voices) {
            wirescape::voice::Config cfg{};
            cfg.max_voices = static_cast<uint8_t>(desired);
            cfg.mode = wirescape::voice::Mode::Rotate;
            if (configured_voices) {
                ws2_event cause{};
                cause.struct_size = sizeof(cause);
                core.resize(desired, cause, [](const ws2_event &) {});
                for (uint32_t c = uint32_t(core.allocator().voice_count()); c < wirescape::voice::kMaxVoices; ++c) {
                    trig_left[c] = 0;
                    trig_active_mask &= ~(uint32_t{1} << c);
                }
            } else {
                core.configure(cfg);
                trig_left = {};
                trig_active_mask = 0;
            }
            configured_voices = desired;
        }
        const uint32_t n = static_cast<uint32_t>(core.allocator().voice_count());
        const auto trig_frames = std::max<uint32_t>(1u, uint32_t(std::lround(b.sample_rate() / 1000.0)));
        // The common no-event path has no live trigger: represent it as a
        // constant zero without a per-sample pulse/decrement branch.
        const bool silent_trigger = b.events().size() == 0 && trig_active_mask == 0;
        for (uint32_t p = 0; p < 4; ++p) {
            b.out(p).set_channels(n);
            b.out(p).mark_constant(false);
        }
        b.out(3).mark_constant(silent_trigger);
        if (silent_trigger)
            for (uint32_t c = 0; c < n; ++c) b.out(3).ch(c)[0] = 0.f;
        // Cache stable signal output rows once per block, not per sample.
        std::array<std::array<float*, wirescape::voice::kMaxVoices>, 3> output_rows{};
        for (uint32_t p = 0; p < 3; ++p)
            for (uint32_t c = 0; c < n; ++c)
                output_rows[p][c] = b.out(p).ch(c);
        b.split(
            [&](uint32_t first, uint32_t end) {
                for (uint32_t i = first; i < end; ++i) {
                    for (uint32_t c = 0; c < n; ++c) {
                        const auto &v = core.allocator().voice(static_cast<int>(c));
                        output_rows[0][c][i] = v.pitch;
                        // A stolen/retriggered voice drops gate for one sample,
                        // allowing the downstream envelope to retrigger.
                        output_rows[1][c][i] = v.gate && !(v.trigger && v.retrigger) ? 1.f : 0.f;
                        output_rows[2][c][i] = v.velocity;
                    }
                    core.allocator().clear_triggers();
                }
                // Event-bearing blocks and pulses crossing a block boundary
                // retain sample-accurate 1 ms output on every allocated lane.
                if (!silent_trigger) {
                    for (uint32_t c = 0; c < n; ++c) {
                        auto* trig = b.out(3).ch(c);
                        for (uint32_t i = first; i < end; ++i) {
                            trig[i] = trig_left[c] ? 1.f : 0.f;
                            if (trig_left[c] && --trig_left[c] == 0)
                                trig_active_mask &= ~(uint32_t{1} << c);
                        }
                    }
                }
            },
            [&](const ws2_event &e) {
                if (e.type == WS2_NOTE_ON || e.type == WS2_NOTE_OFF || e.type == WS2_NOTE_CHOKE ||
                    e.type == WS2_NOTE_EXPRESSION) {
                    // NotesToCv has signal outputs only. The allocator handles
                    // releases and expressions internally; it does not emit here.
                    core.event(e, [&](const ws2_event &out) {
                        // Only an accepted Note On assigned to this poly voice retriggers.
                        if (e.type == WS2_NOTE_ON && out.type == WS2_NOTE_ON &&
                            out.channel >= 0 && uint32_t(out.channel) < n) {
                            trig_left[uint32_t(out.channel)] = trig_frames;
                            trig_active_mask |= uint32_t{1} << uint32_t(out.channel);
                        }
                    });
                }
            });
    }
    int save_state(ws::Writer w) { return w.u32(1) ? WS2_OK : WS2_CAPACITY; }
    int load_state(ws::Reader r) {
        uint32_t version = 0;
        if (!r.u32(version)) return WS2_INVALID;
        if (version != 1) return WS2_UNSUPPORTED;
        reset(); // Active notes never persist into a restored patch.
        return WS2_OK;
    }
};

} // namespace notes_to_cv
