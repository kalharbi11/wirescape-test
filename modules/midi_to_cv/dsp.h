// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wirescape_sdk.h"
#include "../../engine/voice/note_to_cv.h"
namespace wirescape::midi {
struct MidiToCv {
    static constexpr ws::Info info{"midi.to_cv", "MIDI to CV", 1};
    static constexpr std::array inputs{ws::port("midi", "MIDI", WS2_EV_MIDI, WS2_POLY, 0, nullptr, WS2_EVENT)};
    static constexpr std::array outputs{ws::port("pitch", "Pitch", WS2_PITCH),
                                        ws::port("gate", "Gate", WS2_GATE),
                                        ws::port("velocity", "Velocity", WS2_CV_UNI),
                                        ws::port("aftertouch", "Aftertouch", WS2_CV_UNI),
                                        ws::port("pressure", "Pressure", WS2_CV_UNI),
                                        ws::port("bend", "Bend", WS2_CV_BI),
                                        ws::port("mod", "Mod", WS2_CV_UNI),
                                        ws::port("notes", "Notes", WS2_EV_NOTES, WS2_POLY, 0, nullptr, WS2_EVENT),
                                        ws::port("last_note", "Last note", WS2_PITCH),
                                        ws::port("trig", "Trigger", WS2_GATE)};
    static constexpr uint32_t stepped = WS2_STEPPED | WS2_AUTOMATABLE;
    static constexpr std::array params{
        ws::param("voice_count", "Voices", 1, 16, 4, "", stepped, WS2_LINEAR, 0, 4, 1),
        ws::param("allocation_mode", "Mode (rotate/reuse/reset/mono)", 0, 3, 0, "", stepped, WS2_LINEAR, 1, 4, 1),
        ws::param("steal_policy", "Steal (oldest)", 0, 0, 0, "", stepped, WS2_LINEAR, 2, 4, 1),
        ws::param("mono_priority", "Priority (last/low/high)", 0, 2, 0, "", stepped, WS2_LINEAR, 3, 4, 1),
        ws::param("legato", "Legato", 0, 1, 0, "", stepped, WS2_LINEAR, 4, 4, 1),
        ws::param("sustain", "Sustain (ignore/hold)", 0, 1, 1, "", stepped | WS2_MODULATABLE, WS2_LINEAR, 6, 4, 1),
        ws::param("pitch_bend_range_semitones", "Bend range", 0, 48, 2, "st", stepped | WS2_MODULATABLE, WS2_LINEAR, 7, 4, 1),
        ws::param("apply_bend", "Apply bend to pitch", 0, 1, 1, "", stepped | WS2_MODULATABLE, WS2_LINEAR, 8, 4, 1),
        ws::param("channel_filter", "Channel (0 = Omni)", 0, 16, 0, "", stepped, WS2_LINEAR, 9, 4, 1)};
    voice::NoteToCv core;
    struct Expression { bool used = false; uint32_t source = 0, channel = 0; float bend = 0, aftertouch = 0, mod = 0; bool pedal = false; };
    // Service supports 16 endpoints x 16 channels; retain controller state until source choke.
    std::array<Expression, size_t{16} * 16> expressions{};
    std::array<ws2_event, 256> pending{}; uint32_t pending_count = 0; bool panic_pending = false;
    uint32_t max_channels = 16; voice::Config configured{}; uint32_t filter = 0; bool valid_config = false, sustain_hold = true;
    uint64_t dropped = 0, ignored = 0;
    float last_note = 0; // mono raw NoteOn pitch, intentionally independent of bend/gate
    std::array<uint32_t, voice::kMaxVoices> trig_left{};
    uint32_t trig_active_mask = 0; // avoid scanning all voices in idle blocks // samples remaining per allocated voice
    void prepare(const ws::Setup& s) { max_channels = s.max_channels(); }
    void reset() {
        core.reset();
        expressions = {};
        pending_count = 0;
        panic_pending = false;
        valid_config = false;
        sustain_hold = true;
        dropped = ignored = 0;
        last_note = 0;
        trig_left = {};
        trig_active_mask = 0;
    }
    Expression* expression(uint32_t source, uint32_t channel, bool create) {
        Expression* free = nullptr;
        for (auto& e : expressions) { if (e.used && e.source == source && e.channel == channel) return &e; if (!e.used && !free) free = &e; }
        if (!free || !create) return nullptr;
        *free = {}; free->used = true; free->source = source; free->channel = channel; return free;
    }
    void process_no_trigger(ws::Block &b) {
        auto emit = [&](const ws2_event& event) {
            if (b.emit(7, event)) return;
            ++dropped;
            if (event.type == WS2_NOTE_OFF || event.type == WS2_NOTE_CHOKE) { if (pending_count < pending.size()) pending[pending_count++] = event; else panic_pending = true; }
        };
        b.out(7).set_channels(0);
        b.out(8).set_channels(1);
        b.out(8).mark_constant(false);
        uint32_t retained = 0;
        for (uint32_t i = 0; i < pending_count; ++i) { auto e = pending[i]; e.offset = 0; if (!b.emit(7, e)) pending[retained++] = e; }
        pending_count = retained;
        ws2_event cause{}; cause.struct_size = sizeof(cause); cause.type = WS2_NOTE_CHOKE; cause.source_id = UINT32_MAX; cause.channel = -1; cause.note_id = -1;
        if (panic_pending) { core.release_all(UINT32_MAX, cause, emit); if (b.emit(7, cause)) panic_pending = false; }
        voice::Config wanted;
        wanted.max_voices = uint8_t(std::clamp(std::lround(b.param(0).value(0, 0)), 1l, long(max_channels)));
        wanted.mode = static_cast<voice::Mode>(std::clamp(std::lround(b.param(1).value(0, 0)), 0l, 3l));
        wanted.mono_priority = static_cast<voice::Priority>(std::clamp(std::lround(b.param(3).value(0, 0)), 0l, 2l)); wanted.legato = b.param(4).value(0, 0) > .5f;
        const uint32_t requested_filter = uint32_t(std::clamp(std::lround(b.param(8).value(0, 0)), 0l, 16l));
        if (!valid_config || wanted.max_voices != configured.max_voices || wanted.mode != configured.mode || wanted.mono_priority != configured.mono_priority || wanted.legato != configured.legato || filter != requested_filter) {
            if (valid_config && wanted.mode == configured.mode &&
                wanted.mono_priority == configured.mono_priority && wanted.legato == configured.legato &&
                filter == requested_filter) {
                core.resize(wanted.max_voices, cause, emit);
                for (uint32_t c = uint32_t(core.allocator().voice_count()); c < voice::kMaxVoices; ++c) {
                    trig_left[c] = 0;
                    trig_active_mask &= ~(uint32_t{1} << c);
                }
            } else {
                core.release_all(UINT32_MAX, cause, emit);
                core.configure(wanted);
                trig_left = {};
                trig_active_mask = 0;
            }
            configured = wanted; filter = requested_filter; valid_config = true;
        }
        const bool hold = b.param(5).value(0, 0) > .5f;
        if (sustain_hold && !hold) for (auto& e : expressions) if (e.used && e.pedal) { core.sustain(e.source, int(e.channel), false, cause, emit); e.pedal = false; }
        sustain_hold = hold;
        const uint32_t n = uint32_t(core.allocator().voice_count());
        for (uint32_t p = 0; p < 7; ++p) { b.out(p).set_channels(n); b.out(p).mark_constant(false); }
        // Signal buffers are stable across event-split spans: resolve them once.
        std::array<std::array<float*, voice::kMaxVoices>, 7> output_rows{};
        for (uint32_t p = 0; p < 7; ++p)
            for (uint32_t c = 0; c < n; ++c)
                output_rows[p][c] = b.out(p).ch(c);
        float* last_note_row = b.out(8).ch(0);
        b.split(
            [&](uint32_t first, uint32_t end) {
                for (uint32_t i = first; i < end; ++i) last_note_row[i] = last_note;
                for (uint32_t c = 0; c < n; ++c) {
                    const auto &v = core.allocator().voice(int(c));
                    const auto *ex = v.assigned
                                         ? expression(core.original_source(v.source_id), uint32_t(v.channel), false)
                                         : nullptr;
                    const float bend = ex ? ex->bend : 0;
                    for (uint32_t i = first; i < end; ++i) {
                        output_rows[0][c][i] =
                            v.pitch + (b.param(7).value(0, i) > .5f ? bend * b.param(6).value(0, i) / 12.f : 0);
                        output_rows[1][c][i] = v.gate ? 1.f : 0.f;
                        output_rows[2][c][i] = v.velocity;
                        output_rows[3][c][i] = ex ? ex->aftertouch : 0;
                        output_rows[4][c][i] = v.pressure;
                        output_rows[5][c][i] = bend;
                        output_rows[6][c][i] = ex ? ex->mod : 0;
                    }
                }
                core.allocator().clear_triggers();
            },
            [&](const ws2_event &event) {
                if (event.type == WS2_NOTE_CHOKE) {
                    core.release_all(event.source_id, event, emit);
                    for (auto &ex : expressions)
                        if (event.source_id == UINT32_MAX || ex.source == event.source_id) ex = {};
                    return;
                }
                if (event.type != WS2_MIDI || !event.ump_words) return;
                const uint32_t word = event.ump[0], type = word >> 28, group = (word >> 24) & 15,
                               status = (word >> 20) & 15, channel = (word >> 16) & 15, key = (word >> 8) & 127;
                if (group || (type != 2 && type != 4) || (type == 4 && event.ump_words != 2) ||
                    (filter && channel != filter - 1)) {
                    ++ignored;
                    return;
                }
                auto *ex = expression(event.source_id, channel, true);
                if (!ex) {
                    ++dropped;
                    return;
                }
                const uint32_t data = type == 2 ? word & 127 : event.ump[1];
                auto note = event;
                note.ump_words = 0;
                note.bytes = 0;
                note.data = nullptr;
                note.note_id = -1;
                note.channel = int(channel);
                note.key = int(key);
                if (status == 8 || status == 9) {
                    note.type = status == 8 || (type == 2 && data == 0) ? WS2_NOTE_OFF : WS2_NOTE_ON;
                    note.value = type == 2 ? float(data) / 127.f : float(data >> 16) / 65535.f;
                    const int held_before = note.type == WS2_NOTE_ON ? core.allocator().held_count() : 0;
                    bool attack = false;
                    core.event(note, [&](const ws2_event &out) {
                        // Observe allocation before output-queue delivery can fail.
                        attack = attack || out.type == WS2_NOTE_ON;
                        emit(out);
                    });
                    // Mono priority may accept a held note without selecting it to sound.
                    if (note.type == WS2_NOTE_ON && (attack || core.allocator().held_count() > held_before))
                        last_note = float(int(key) - 60) / 12.f;
                } else if (status == 11) {
                    if (key == 64) {
                        ex->pedal = sustain_hold && (type == 2 ? data >= 64 : data >= 0x80000000u);
                        core.sustain(event.source_id, int(channel), ex->pedal, note, emit);
                    } else if (key == 120 || key == 123) {
                        core.release_channel(event.source_id, int(channel), note, emit);
                        ex->pedal = false;
                    } else if (key == 1) ex->mod = type == 2 ? float(data) / 127.f : float(double(data) / 4294967295.);
                } else if (status == 14) {
                    if (type == 2) {
                        const int signed_value = int(key | data << 7) - 8192;
                        ex->bend = float(signed_value) / float(signed_value < 0 ? 8192 : 8191);
                    } else {
                        const int64_t signed_value = int64_t(data) - 2147483648LL;
                        ex->bend = float(double(signed_value) / (signed_value < 0 ? 2147483648. : 2147483647.));
                    }
                } else if (status == 13)
                    ex->aftertouch = type == 2 ? float(key) / 127.f : float(double(data) / 4294967295.);
                else if (status == 10) {
                    for (uint32_t c = 0; c < n; ++c) {
                        const auto &v = core.allocator().voice(int(c));
                        if (v.gate && core.original_source(v.source_id) == event.source_id &&
                            v.channel == int(channel) && v.key == int(key)) {
                            auto pressure = note;
                            pressure.type = WS2_NOTE_EXPRESSION;
                            pressure.expression = WS2_EX_PRESSURE;
                            pressure.note_id = v.note_id;
                            pressure.value = type == 2 ? float(data) / 127.f : float(double(data) / 4294967295.);
                            core.event(pressure, emit);
                        }
                    }
                } else ++ignored;
            });
    }
    void process(ws::Block& b) {
        // The original hot loop is unchanged on idle, pulse-free blocks.
        if (b.events().size() == 0 && trig_active_mask == 0) {
            process_no_trigger(b);
            const uint32_t n = b.out(0).channels();
            b.out(9).set_channels(n);
            b.out(9).mark_constant(true);
            for (uint32_t c = 0; c < n; ++c) b.out(9).ch(c)[0] = 0.f;
            return;
        }
        auto emit = [&](const ws2_event& event) {
            if (b.emit(7, event)) return;
            ++dropped;
            if (event.type == WS2_NOTE_OFF || event.type == WS2_NOTE_CHOKE) { if (pending_count < pending.size()) pending[pending_count++] = event; else panic_pending = true; }
        };
        b.out(7).set_channels(0);
        b.out(8).set_channels(1);
        b.out(8).mark_constant(false);
        const auto trig_frames = std::max<uint32_t>(1u, uint32_t(std::lround(b.sample_rate() / 1000.0)));
        uint32_t retained = 0;
        for (uint32_t i = 0; i < pending_count; ++i) { auto e = pending[i]; e.offset = 0; if (!b.emit(7, e)) pending[retained++] = e; }
        pending_count = retained;
        ws2_event cause{}; cause.struct_size = sizeof(cause); cause.type = WS2_NOTE_CHOKE; cause.source_id = UINT32_MAX; cause.channel = -1; cause.note_id = -1;
        if (panic_pending) { core.release_all(UINT32_MAX, cause, emit); if (b.emit(7, cause)) panic_pending = false; }
        voice::Config wanted;
        wanted.max_voices = uint8_t(std::clamp(std::lround(b.param(0).value(0, 0)), 1l, long(max_channels)));
        wanted.mode = static_cast<voice::Mode>(std::clamp(std::lround(b.param(1).value(0, 0)), 0l, 3l));
        wanted.mono_priority = static_cast<voice::Priority>(std::clamp(std::lround(b.param(3).value(0, 0)), 0l, 2l)); wanted.legato = b.param(4).value(0, 0) > .5f;
        const uint32_t requested_filter = uint32_t(std::clamp(std::lround(b.param(8).value(0, 0)), 0l, 16l));
        if (!valid_config || wanted.max_voices != configured.max_voices || wanted.mode != configured.mode || wanted.mono_priority != configured.mono_priority || wanted.legato != configured.legato || filter != requested_filter) {
            if (valid_config && wanted.mode == configured.mode &&
                wanted.mono_priority == configured.mono_priority && wanted.legato == configured.legato &&
                filter == requested_filter) {
                core.resize(wanted.max_voices, cause, emit);
                for (uint32_t c = uint32_t(core.allocator().voice_count()); c < voice::kMaxVoices; ++c) {
                    trig_left[c] = 0;
                    trig_active_mask &= ~(uint32_t{1} << c);
                }
            } else {
                core.release_all(UINT32_MAX, cause, emit);
                core.configure(wanted);
                trig_left = {};
                trig_active_mask = 0;
            }
            configured = wanted; filter = requested_filter; valid_config = true;
        }
        const bool hold = b.param(5).value(0, 0) > .5f;
        if (sustain_hold && !hold) for (auto& e : expressions) if (e.used && e.pedal) { core.sustain(e.source, int(e.channel), false, cause, emit); e.pedal = false; }
        sustain_hold = hold;
        const uint32_t n = uint32_t(core.allocator().voice_count());
        for (uint32_t p = 0; p < 7; ++p) { b.out(p).set_channels(n); b.out(p).mark_constant(false); }
        b.out(9).set_channels(n);
        // Constant zero avoids a per-sample trigger tax for idle MIDI blocks.
        const bool silent_trigger = b.events().size() == 0 && trig_active_mask == 0;
        b.out(9).mark_constant(silent_trigger);
        if (silent_trigger)
            for (uint32_t c = 0; c < n; ++c) b.out(9).ch(c)[0] = 0.f;
        // Signal buffers are stable across event-split spans: resolve them once.
        std::array<std::array<float*, voice::kMaxVoices>, 7> output_rows{};
        for (uint32_t p = 0; p < 7; ++p)
            for (uint32_t c = 0; c < n; ++c)
                output_rows[p][c] = b.out(p).ch(c);
        float* last_note_row = b.out(8).ch(0);
        b.split(
            [&](uint32_t first, uint32_t end) {
                for (uint32_t i = first; i < end; ++i) last_note_row[i] = last_note;
                for (uint32_t c = 0; c < n; ++c) {
                    const auto &v = core.allocator().voice(int(c));
                    const auto *ex = v.assigned
                                         ? expression(core.original_source(v.source_id), uint32_t(v.channel), false)
                                         : nullptr;
                    const float bend = ex ? ex->bend : 0;
                    for (uint32_t i = first; i < end; ++i) {
                        output_rows[0][c][i] =
                            v.pitch + (b.param(7).value(0, i) > .5f ? bend * b.param(6).value(0, i) / 12.f : 0);
                        output_rows[1][c][i] = v.gate ? 1.f : 0.f;
                        output_rows[2][c][i] = v.velocity;
                        output_rows[3][c][i] = ex ? ex->aftertouch : 0;
                        output_rows[4][c][i] = v.pressure;
                        output_rows[5][c][i] = bend;
                        output_rows[6][c][i] = ex ? ex->mod : 0;
                    }
                    // Pulse work is off the unchanged pitch/gate hot path.
                    if (!silent_trigger) {
                        auto* trig = b.out(9).ch(c);
                        for (uint32_t i = first; i < end; ++i) {
                            trig[i] = trig_left[c] ? 1.f : 0.f;
                            if (trig_left[c] && --trig_left[c] == 0)
                                trig_active_mask &= ~(uint32_t{1} << c);
                        }
                    }
                }
                core.allocator().clear_triggers();
            },
            [&](const ws2_event &event) {
                if (event.type == WS2_NOTE_CHOKE) {
                    core.release_all(event.source_id, event, emit);
                    for (auto &ex : expressions)
                        if (event.source_id == UINT32_MAX || ex.source == event.source_id) ex = {};
                    return;
                }
                if (event.type != WS2_MIDI || !event.ump_words) return;
                const uint32_t word = event.ump[0], type = word >> 28, group = (word >> 24) & 15,
                               status = (word >> 20) & 15, channel = (word >> 16) & 15, key = (word >> 8) & 127;
                if (group || (type != 2 && type != 4) || (type == 4 && event.ump_words != 2) ||
                    (filter && channel != filter - 1)) {
                    ++ignored;
                    return;
                }
                auto *ex = expression(event.source_id, channel, true);
                if (!ex) {
                    ++dropped;
                    return;
                }
                const uint32_t data = type == 2 ? word & 127 : event.ump[1];
                auto note = event;
                note.ump_words = 0;
                note.bytes = 0;
                note.data = nullptr;
                note.note_id = -1;
                note.channel = int(channel);
                note.key = int(key);
                if (status == 8 || status == 9) {
                    note.type = status == 8 || (type == 2 && data == 0) ? WS2_NOTE_OFF : WS2_NOTE_ON;
                    note.value = type == 2 ? float(data) / 127.f : float(data >> 16) / 65535.f;
                    const int held_before = note.type == WS2_NOTE_ON ? core.allocator().held_count() : 0;
                    bool attack = false;
                    core.event(note, [&](const ws2_event &out) {
                        // Observe allocation before output-queue delivery can fail.
                        attack = attack || out.type == WS2_NOTE_ON;
                        // One accepted attack starts a 1 ms pulse on its allocated poly lane,
                        // even when mono legato keeps the gate continuously high.
                        if (note.type == WS2_NOTE_ON && out.type == WS2_NOTE_ON &&
                            out.channel >= 0 && uint32_t(out.channel) < n) {
                            trig_left[uint32_t(out.channel)] = trig_frames;
                            trig_active_mask |= uint32_t{1} << uint32_t(out.channel);
                        }
                        emit(out);
                    });
                    // Mono priority may accept a held note without selecting it to sound.
                    if (note.type == WS2_NOTE_ON && (attack || core.allocator().held_count() > held_before))
                        last_note = float(int(key) - 60) / 12.f;
                } else if (status == 11) {
                    if (key == 64) {
                        ex->pedal = sustain_hold && (type == 2 ? data >= 64 : data >= 0x80000000u);
                        core.sustain(event.source_id, int(channel), ex->pedal, note, emit);
                    } else if (key == 120 || key == 123) {
                        core.release_channel(event.source_id, int(channel), note, emit);
                        ex->pedal = false;
                    } else if (key == 1) ex->mod = type == 2 ? float(data) / 127.f : float(double(data) / 4294967295.);
                } else if (status == 14) {
                    if (type == 2) {
                        const int signed_value = int(key | data << 7) - 8192;
                        ex->bend = float(signed_value) / float(signed_value < 0 ? 8192 : 8191);
                    } else {
                        const int64_t signed_value = int64_t(data) - 2147483648LL;
                        ex->bend = float(double(signed_value) / (signed_value < 0 ? 2147483648. : 2147483647.));
                    }
                } else if (status == 13)
                    ex->aftertouch = type == 2 ? float(key) / 127.f : float(double(data) / 4294967295.);
                else if (status == 10) {
                    for (uint32_t c = 0; c < n; ++c) {
                        const auto &v = core.allocator().voice(int(c));
                        if (v.gate && core.original_source(v.source_id) == event.source_id &&
                            v.channel == int(channel) && v.key == int(key)) {
                            auto pressure = note;
                            pressure.type = WS2_NOTE_EXPRESSION;
                            pressure.expression = WS2_EX_PRESSURE;
                            pressure.note_id = v.note_id;
                            pressure.value = type == 2 ? float(data) / 127.f : float(double(data) / 4294967295.);
                            core.event(pressure, emit);
                        }
                    }
                } else ++ignored;
            });
    }
    int save_state(ws::Writer w) { return w.u32(1) ? WS2_OK : WS2_CAPACITY; }
    int load_state(ws::Reader r) { uint32_t v; if (!r.u32(v)) return WS2_INVALID; if (v != 1) return WS2_UNSUPPORTED; reset(); return WS2_OK; }
};
}
