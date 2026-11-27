// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#ifdef emit
#pragma push_macro("emit")
#undef emit
#define CV_TO_NOTES_RESTORE_EMIT
#endif
#include "wirescape_sdk.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cv_to_notes {

struct CvToNotes {
    static constexpr ws::Info info{"essentials.cv_to_notes", "CV to Notes", 1};
    static constexpr std::array inputs{
        ws::port("pitch", "Pitch", WS2_PITCH),
        ws::port("gate", "Gate", WS2_GATE),
        ws::port("velocity", "Velocity", WS2_CV_UNI, WS2_POLY, .8f)};
    static constexpr std::array outputs{
        ws::port("notes", "Notes", WS2_EV_NOTES, WS2_POLY, 0, nullptr, WS2_EVENT)};
    static constexpr std::array params{
        ws::param("legato", "Legato", 0, 1, 0, "", WS2_STEPPED | WS2_AUTOMATABLE, WS2_LINEAR, 0, 4, 1)};

    struct Lane {
        bool active = false;
        bool release_pending = false;
        bool inhibit_until_low = false;
        int key = 60;
        float velocity = .8f;
    };
    std::array<Lane, 16> lanes{};
    uint32_t capacity = 16, source = 0;
    bool processed = false;

    void prepare(const ws::Setup &s) {
        capacity = std::min<uint32_t>(s.max_channels(), 16u);
        // Engine setup seeds each graph node independently (seed + node ID).
        // In combination with channel and note_id this is the note owner.
        source = static_cast<uint32_t>(s.config.seed);
        processed = false;
    }
    void reset() {
        // reset() has no event sink. Preserve owed releases for the next block;
        // do not re-trigger a high gate until it first falls after a panic.
        for (auto &v : lanes) {
            if (v.active) v.release_pending = true;
            if (processed) v.inhibit_until_low = true;
        }
    }
    ws2_event note(uint32_t type, uint32_t channel, int key, float value, uint32_t offset) const {
        ws2_event e{};
        e.struct_size = sizeof(e);
        e.type = type;
        e.port = 0;
        e.offset = offset;
        e.source_id = source;
        e.channel = static_cast<int32_t>(channel);
        e.note_id = static_cast<int32_t>(channel);
        e.key = key;
        e.value = value;
        return e;
    }
    bool release(const ws::Block &b, uint32_t channel, uint32_t at) {
        auto &v = lanes[channel];
        if (!v.active) {
            v.release_pending = false;
            return true;
        }
        if (!b.emit(0, note(WS2_NOTE_OFF, channel, v.key, 0, at))) {
            v.release_pending = true; // Never strand a previously accepted attack.
            return false;
        }
        v.active = false;
        v.release_pending = false;
        return true;
    }
    void choke(const ws::Block &b, uint32_t at) {
        for (uint32_t c = 0; c < capacity; ++c) {
            release(b, c, at);
            lanes[c].inhibit_until_low = true;
        }
    }
    static int key_for(float pitch) {
        if (!std::isfinite(pitch)) pitch = 0;
        // Bound before rounding; arbitrarily large CV must not overflow lround.
        pitch = std::clamp(pitch, -5.f, 6.f);
        return std::clamp(static_cast<int>(std::lround(60.f + 12.f * pitch)), 0, 127);
    }
    static float velocity_for(float velocity) {
        return std::isfinite(velocity) ? std::clamp(velocity, 0.f, 1.f) : .8f;
    }
    void process(ws::Block &b) {
        b.out(0).set_channels(0); // Event port, not a signal output.
        const auto pitch = b.in(0), gate = b.in(1), velocity = b.in(2);
        const uint32_t voices =
            std::min(capacity, std::max<uint32_t>({pitch.channels(), gate.channels(), velocity.channels(), 1u}));
        b.split(
            [&](uint32_t first, uint32_t end) {
                for (uint32_t i = first; i < end; ++i) {
                    const bool legato = b.param(0).value(0, i) >= .5f;
                    for (uint32_t c = 0; c < capacity; ++c) {
                        auto &v = lanes[c];
                        const bool high = c < voices && gate.value(c, i) >= .5f;
                        if (!high) v.inhibit_until_low = false;
                        // Retries always precede new attacks and use the latched key.
                        if (v.release_pending && !release(b, c, i)) continue;
                        if (v.inhibit_until_low) continue;
                        const int key = key_for(pitch.value(c, i));
                        if (v.active && (!high || (legato && key != v.key)))
                            if (!release(b, c, i)) continue;
                        if (!v.active && high) {
                            const float vel = velocity.connected() ? velocity_for(velocity.value(c, i)) : .8f;
                            if (b.emit(0, note(WS2_NOTE_ON, c, key, vel, i))) {
                                v.active = true;
                                v.key = key;
                                v.velocity = vel;
                            }
                        }
                    }
                }
            },
            [&](const ws2_event &e) {
                if (e.type == WS2_NOTE_CHOKE) choke(b, e.offset);
            });
        processed = true;
    }
    int save_state(ws::Writer w) { return w.u32(1) ? WS2_OK : WS2_CAPACITY; }
    int load_state(ws::Reader r) {
        uint32_t version = 0;
        if (!r.u32(version)) return WS2_INVALID;
        if (version != 1) return WS2_UNSUPPORTED;
        reset(); // Held notes are not persisted. Pending off remains owed.
        return WS2_OK;
    }
};
} // namespace cv_to_notes
#ifdef CV_TO_NOTES_RESTORE_EMIT
#pragma pop_macro("emit")
#undef CV_TO_NOTES_RESTORE_EMIT
#endif
