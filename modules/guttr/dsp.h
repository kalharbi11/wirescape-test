// SPDX-License-Identifier: GPL-3.0-only
// guttr, "GUTTR": the owner's vocal-tract throat synthesizer (lab docs/context/future_modules/guttur),
// restructured for the Wirescape SDK. Notes in; one internally mixed stereo pair out (one channel per
// side). Each note sings with 1-8 throats from a 16-throat budget. Per-voice parameters are read on the
// note's voice channel, global ones on channel 0, at a fixed 128-frame control tick.
// All large data sits in one WS2_LARGE allocation made in prepare (DESIGN.md "Memory").
#pragma once
#include "wirescape_sdk.h"
#include "guttr/engine.h"
#include <new>
#include <utility>

namespace wsmod {

namespace guttr {
constexpr ws2_param descriptor_param(uint32_t k) {
    const ParamDef &d = kParams[k];
    // throats sets how many throats a note allocates, so it is not modulatable; grit_after is a
    // two-state switch (stepped, modulatable by policy).
    const uint32_t flags = k == throats ? uint32_t(WS2_STEPPED | WS2_AUTOMATABLE)
                         : k == grit_after ? uint32_t(WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE)
                         : d.per_voice ? uint32_t(WS2_MODULATABLE | WS2_AUTOMATABLE | WS2_PER_VOICE)
                                       : uint32_t(WS2_MODULATABLE | WS2_AUTOMATABLE);
    ws2_param p = ws::param(d.id, d.name, d.minimum, d.maximum, d.initial, d.unit, flags, WS2_LINEAR, k, 4,
                            d.stepped ? 1.f : 0.f);
    p.decimals = d.decimals;
    return p;
}
template <size_t... I> constexpr std::array<ws2_param, sizeof...(I)> descriptor_params(std::index_sequence<I...>) {
    return {{descriptor_param(uint32_t(I))...}};
}
}  // namespace guttr

struct Guttr {
    static constexpr ws::Info info{"guttr", "GUTTR", 1};
    static constexpr std::array inputs{ws::port("notes", "Notes", WS2_EV_NOTES, WS2_POLY, 0, nullptr, WS2_EVENT)};
    static constexpr std::array outputs{ws::port("left", "Left", WS2_AUDIO, WS2_POLY, 0, "stereo"),
                                        ws::port("right", "Right", WS2_AUDIO, WS2_POLY, 0, "stereo")};
    static constexpr std::array params = guttr::descriptor_params(std::make_index_sequence<guttr::kParamCount>{});
    static_assert(params.size() == 37 && params.size() <= WS2_MAX_PARAMS);

    guttr::Core *core = nullptr;  // inside `memory`
    unsigned char *memory = nullptr;
    size_t capacity = 0;

    static ws2_memory memory_requirements(const ws2_config &c) noexcept {
        return {sizeof(ws2_memory), 0, guttr::Layout::of(c.sample_rate).total, guttr::Layout::align};
    }
    // Control thread: one allocation (reused by a second prepare when it fits), tables built here.
    bool prepare(const ws::Setup &s) noexcept {
        const auto layout = guttr::Layout::of(s.sample_rate());
        if (!memory || capacity < layout.total) {
            memory = static_cast<unsigned char *>(s.alloc(layout.total, guttr::Layout::align, WS2_LARGE));
            capacity = memory ? layout.total : 0;
        }
        if (!memory) { core = nullptr; return false; }
        core = new (memory) guttr::Core{};
        core->prepare(s.sample_rate(), s.config.seed, memory, layout);
        return true;
    }
    void reset() noexcept { if (core) core->reset(); }
    uint32_t latency_frames() const noexcept { return core ? core->limLook : 0; }

    // A parameter on voice channel c at frame i: channels the signal lacks read channel 0; non-finite
    // values fall back to the default and the rest are clamped to the range.
    static double read(const ws::Block &b, uint32_t k, uint32_t c, uint32_t i) noexcept {
        const auto &d = guttr::kParams[k];
        const ws::Signal s = b.param(k);
        const uint32_t n = s.channels();
        if (!n) return d.initial;
        if (c >= n) c = 0;
        const float v = s.value(c, i);
        if (!std::isfinite(v)) return d.initial;
        return std::clamp(double(v), double(d.minimum), double(d.maximum));
    }
    static uint8_t voice_channel(int32_t channel) noexcept {
        return uint8_t(std::clamp<int32_t>(channel, 0, guttr::kChannels - 1));
    }
    static int note_key(int32_t key) noexcept { return std::clamp<int32_t>(key, 0, 127); }

    void event(const ws::Block &b, const ws2_event &e) noexcept {
        auto &notes = core->notes;
        const uint8_t ch = voice_channel(e.channel);
        const int key = note_key(e.key);
        if (e.type == WS2_NOTE_ON) {
            const uint32_t at = std::min(e.offset, b.frames() - 1);
            guttr::Params desired{};
            for (uint32_t k = 0; k < guttr::kParamCount; ++k) desired[k] = read(b, k, guttr::kParams[k].per_voice ? ch : 0, at);
            if (e.note_id >= 0)  // the same identity again: release the old note first
                for (auto &n : notes)
                    if (n.id && !n.released && n.source == e.source_id && n.note_id == e.note_id) core->release_note(n.id);
            core->add_note(e.source_id, e.note_id, ch, key, std::clamp(double(e.value), 0., 1.), desired);
        } else if (e.type == WS2_NOTE_OFF) {
            // note_id >= 0: (source, note_id); otherwise the newest anonymous (source, channel, key).
            guttr::Core::Note *best = nullptr;
            for (auto &n : notes)
                if (n.id && !n.released && n.source == e.source_id &&
                    (e.note_id >= 0 ? n.note_id == e.note_id : n.note_id < 0 && n.channel == ch && n.key == key) &&
                    (!best || n.seq > best->seq))
                    best = &n;
            if (best) core->release_note(best->id);
        } else if (e.type == WS2_NOTE_CHOKE) {
            // Source UINT32_MAX means every source. A note_id chokes that note; channel >= 0 with no
            // note_id chokes the anonymous (channel, key); neither chokes the whole source.
            for (auto &n : notes)
                if (n.id && (e.source_id == UINT32_MAX || n.source == e.source_id) &&
                    (e.note_id >= 0 ? n.note_id == e.note_id
                                    : e.channel < 0 || (n.note_id < 0 && n.channel == ch && n.key == key)))
                    core->choke_note(n.id);
        }
    }
    void process(ws::Block &b) noexcept {
        auto left = b.out(0), right = b.out(1);
        left.set_channels(1);
        right.set_channels(1);
        float *l = left.ch(0), *r = right.ch(0);
        if (!core || !l || !r) {
            if (l) l[0] = 0;
            if (r) r[0] = 0;
            left.mark_constant(true);
            right.mark_constant(true);
            return;
        }
        auto &x = *core;
        bool silent = true;
        b.split(
            [&](uint32_t first, uint32_t end) {
                while (first < end) {
                    if (x.ctlLeft == 0) {  // a control tick: sample the parameters at this frame
                        // The pinned engine supplies one channel for unmodulated/mono
                        // parameters and multiple channels for poly texture modulation.
                        // Select topology on the fixed clock, not on host block boundaries.
                        x.polyGrit = b.param(guttr::growl).channels() > 1 ||
                                     b.param(guttr::rasp).channels() > 1 ||
                                     b.param(guttr::shimmer).channels() > 1 ||
                                     b.param(guttr::jitter).channels() > 1 ||
                                     b.param(guttr::creak).channels() > 1;
                        x.sample_parameters([&](uint32_t k, uint32_t c) { return read(b, k, c, first); });
                        x.control();
                        x.ctlLeft = guttr::kTick;
                    }
                    const uint32_t n = std::min(end - first, x.ctlLeft);
                    silent &= x.render(l + first, r + first, n);
                    first += n;
                    x.frame += n;
                    x.ctlLeft -= n;
                    if (x.ctlLeft == 0) x.finish_tick();
                }
            },
            [&](const ws2_event &e) { event(b, e); });
        left.mark_constant(silent);
        right.mark_constant(silent);
    }

    // Inspection for tests and tools (control thread; not part of the ABI).
    uint32_t active_throats() const noexcept {
        uint32_t n = 0;
        if (core)
            for (const auto &s : core->singers) n += s.active ? 1u : 0u;
        return n;
    }
    uint32_t live_notes() const noexcept {
        uint32_t n = 0;
        if (core)
            for (const auto &x : core->notes) n += x.id ? 1u : 0u;
        return n;
    }
    uint32_t held_notes() const noexcept {
        uint32_t n = 0;
        if (core)
            for (const auto &x : core->notes) n += x.id && !x.released ? 1u : 0u;
        return n;
    }
    const guttr::Core::Note *find(uint32_t source, int32_t note_id) const noexcept {
        if (core)
            for (const auto &x : core->notes)
                if (x.id && x.source == source && x.note_id == note_id) return &x;
        return nullptr;
    }
    bool has_note(uint32_t source, int32_t note_id) const noexcept { return find(source, note_id) != nullptr; }
    uint32_t throats_of(uint32_t source, int32_t note_id) const noexcept {
        const auto *n = find(source, note_id);
        uint32_t count = 0;
        if (n)
            for (const auto &s : core->singers) count += s.ownerId == n->id ? 1u : 0u;
        return count;
    }
};

}  // namespace wsmod
