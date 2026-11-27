// SPDX-License-Identifier: GPL-3.0-only
// GUTTR tests, written before the DSP: descriptor; silence; one note (finite, stereo, <= 0.9); release
// to exact silence; choke (matching and all); 16 notes accepted and a 17th steals; host blocks 1, 17,
// 128 and 512 bit-identical; 1 and 16 channels with per-voice parameters read on the note's channel;
// zero allocations in process; latency at 44.1/48/96 kHz; memory report; determinism; extremes;
// reset. Ends with a diagnostic CPU bench (no threshold: timing is reported, never asserted).
#include "dsp.h"
#include "../../tests/native/abi2/allocation_watch.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <vector>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "guttr test line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

namespace {
using wsmod::Guttr;
namespace g = wsmod::guttr;
constexpr uint32_t kParams = g::kParamCount;
constexpr uint32_t kMaxBlock = 512;
static_assert(std::is_standard_layout_v<Guttr>, "tests read the module through the instance pointer");

struct Scheduled { uint64_t frame; ws2_event e; };

ws2_event note(uint32_t type, int32_t key, int32_t channel = 0, int32_t note_id = -1, uint32_t source = 1,
               float velocity = 1.f) {
    ws2_event e{};
    e.struct_size = sizeof(e);
    e.type = type;
    e.key = key;
    e.channel = channel;
    e.note_id = note_id < 0 ? key : note_id;
    e.source_id = source;
    e.value = velocity;
    e.port = 0;
    return e;
}
ws2_event anonymous(uint32_t type, int32_t key, int32_t channel = 0) {
    auto e = note(type, key, channel);
    e.note_id = -1;
    return e;
}
ws2_event choke(uint32_t source, int32_t note_id = -1, int32_t channel = -1) {
    ws2_event e{};
    e.struct_size = sizeof(e);
    e.type = WS2_NOTE_CHOKE;
    e.source_id = source;
    e.note_id = note_id;
    e.channel = channel;
    return e;
}

struct Rig {
    std::vector<unsigned char> pool;
    size_t used = 0, large = 0, fast = 0;
    uint32_t large_calls = 0;
    static void *WS2_CALL allocate(void *ctx, size_t bytes, size_t alignment, uint32_t hint) {
        auto &r = *static_cast<Rig *>(ctx);
        const uintptr_t base = reinterpret_cast<uintptr_t>(r.pool.data());
        const uintptr_t next = (base + r.used + alignment - 1) & ~(uintptr_t(alignment) - 1);
        if (next - base + bytes > r.pool.size()) return nullptr;
        r.used = next - base + bytes;
        (hint == WS2_LARGE ? r.large : r.fast) += bytes;
        if (hint == WS2_LARGE) ++r.large_calls;
        return r.pool.data() + (next - base);
    }
    ws2_host host{sizeof(ws2_host), this, allocate};
    const ws2_descriptor *d = ws::Module<Guttr>::descriptor();
    void *state = nullptr;
    ws2_config cfg{};
    ws2_memory memory{sizeof(ws2_memory), 0, 0, 0};
    std::vector<float> out_store = std::vector<float>(2 * 16 * kMaxBlock);
    float *dst[2][16]{};
    ws2_signal_out outs[2]{};
    // Parameter signals: per parameter a channel count and a value per (channel, absolute frame).
    std::array<uint32_t, kParams> channels{};
    std::array<std::array<float, 16>, kParams> value{};
    std::function<float(uint32_t, uint32_t, uint64_t)> varying;  // (param, channel, frame) when set
    std::vector<float> pstore = std::vector<float>(size_t(kParams) * 16 * kMaxBlock);
    std::array<std::array<const float *, 16>, kParams> prow{};
    std::array<ws2_signal, kParams> params{};
    std::vector<Scheduled> schedule;
    std::vector<ws2_event> events;
    ws2_signal notes_view{sizeof(ws2_signal), nullptr, 0, 0};  // the engine passes a 0-channel view
    ws2_process ctx{};
    uint64_t position = 0;
    uint64_t allocations = 0;
    std::vector<float> L, R;

    explicit Rig(double rate = 48000, uint64_t seed = 42) {
        cfg = {sizeof(cfg), rate, kMaxBlock, 16, seed, nullptr, 0};
        REQUIRE(d->memory_requirements(&cfg, &memory) == WS2_OK);
        pool.resize(memory.fast_bytes + memory.large_bytes + 4096);
        for (int o = 0; o < 2; ++o) {
            for (int c = 0; c < 16; ++c) dst[o][c] = out_store.data() + (size_t(o) * 16 + c) * kMaxBlock;
            outs[o] = {sizeof(ws2_signal_out), dst[o], 0, 0, 16};
        }
        for (uint32_t k = 0; k < kParams; ++k) {
            channels[k] = 1;
            value[k].fill(g::kParams[k].initial);
            for (uint32_t c = 0; c < 16; ++c) prow[k][c] = pstore.data() + (size_t(k) * 16 + c) * kMaxBlock;
            params[k] = {sizeof(ws2_signal), prow[k].data(), 1, WS2_CONSTANT};
        }
        state = d->create(&host);
        REQUIRE(state);
        REQUIRE(d->prepare(state, &cfg, &host) == WS2_OK);
        ctx.struct_size = sizeof(ctx);
        ctx.sample_rate = rate;
        ctx.input_count = 1;
        ctx.inputs = &notes_view;
        ctx.output_count = 2;
        ctx.param_count = kParams;
        ctx.outputs = outs;
        ctx.params = params.data();
    }
    ~Rig() { d->destroy(state); }
    Rig(const Rig &) = delete;
    Rig &operator=(const Rig &) = delete;
    Guttr &dsp() { return *reinterpret_cast<Guttr *>(state); }
    void set(uint32_t k, float v) { value[k].fill(v); }
    void set_channel(uint32_t k, uint32_t c, float v) { channels[k] = 16; value[k][c] = v; }
    void at(uint64_t frame, const ws2_event &e) { schedule.push_back({frame, e}); }
    void at_seconds(double seconds, const ws2_event &e) { at(uint64_t(seconds * cfg.sample_rate), e); }
    // Renders `frames` more frames in host blocks of `block`, recording the outputs.
    void run(uint64_t frames, uint32_t block = 128, uint32_t host_channels = 1) {
        std::stable_sort(schedule.begin(), schedule.end(),
                         [](const Scheduled &a, const Scheduled &b) { return a.frame < b.frame; });
        const uint64_t end = position + frames;
        while (position < end) {
            const uint32_t n = uint32_t(std::min<uint64_t>(block, end - position));
            events.clear();
            for (const auto &s : schedule)
                if (s.frame >= position && s.frame < position + n) {
                    events.push_back(s.e);
                    events.back().offset = uint32_t(s.frame - position);
                }
            for (uint32_t k = 0; k < kParams; ++k) {
                params[k].channels = channels[k];
                params[k].flags = varying ? 0u : uint32_t(WS2_CONSTANT);
                for (uint32_t c = 0; c < channels[k]; ++c) {
                    float *row = const_cast<float *>(prow[k][c]);
                    if (varying)
                        for (uint32_t i = 0; i < n; ++i) row[i] = varying(k, c, position + i);
                    else row[0] = value[k][c];
                }
            }
            ctx.frames = n;
            ctx.channels = host_channels;
            ctx.frame_position = position;
            ctx.events = {sizeof(ws2_event_list), uint32_t(events.size()), events.empty() ? nullptr : events.data()};
            {
                allocation_watch::Scope watch;
                REQUIRE(d->process(state, &ctx) == WS2_OK);
                allocations += watch.allocations();
            }
            for (int o = 0; o < 2; ++o) {
                REQUIRE(outs[o].channels == 1);  // one channel per stereo side, whatever the host
                const bool constant = outs[o].flags & WS2_CONSTANT;
                auto &rec = o ? R : L;
                for (uint32_t i = 0; i < n; ++i) rec.push_back(dst[o][0][constant ? 0 : i]);
            }
            position += n;
        }
        REQUIRE(allocations == 0);
    }
    void reset_checked() {
        allocation_watch::Scope watch;
        d->reset(state);
        REQUIRE(watch.allocations() == 0);
    }
    void run_seconds(double seconds, uint32_t block = 128, uint32_t host_channels = 1) {
        run(uint64_t(seconds * cfg.sample_rate), block, host_channels);
    }
};

float peak(const std::vector<float> &x, size_t from = 0, size_t to = SIZE_MAX) {
    float p = 0;
    for (size_t i = from; i < std::min(to, x.size()); ++i) p = std::max(p, std::abs(x[i]));
    return p;
}
double rms(const std::vector<float> &x, size_t from, size_t to) {
    double s = 0;
    to = std::min(to, x.size());
    for (size_t i = from; i < to; ++i) s += double(x[i]) * x[i];
    return to > from ? std::sqrt(s / double(to - from)) : 0;
}
bool finite(const std::vector<float> &x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

void descriptor() {
    const auto *d = ws::Module<Guttr>::descriptor();
    REQUIRE(!std::strcmp(d->id, "guttr") && !std::strcmp(d->name, "GUTTR") && d->version == 1);
    REQUIRE(d->input_count == 1 && d->output_count == 2 && d->param_count == 37 && d->param_count <= WS2_MAX_PARAMS);
    const auto &in = d->inputs[0];
    REQUIRE(!std::strcmp(in.id, "notes") && in.kind == WS2_EVENT && in.hint == WS2_EV_NOTES && in.poly == WS2_POLY);
    for (uint32_t o = 0; o < 2; ++o) {
        const auto &p = d->outputs[o];
        REQUIRE(!std::strcmp(p.id, o ? "right" : "left") && p.kind == WS2_SIGNAL && p.hint == WS2_AUDIO);
        REQUIRE(p.stereo_pair && !std::strcmp(p.stereo_pair, "stereo"));
    }
    // The old package's table: same order, ranges and defaults (include/guttur/parameters.h).
    const char *ids[37] = {"pressure", "breath", "whisper", "jitter", "shimmer", "tremor", "wander", "vibrato",
                           "growl", "creak", "tail_fry", "flutter", "rasp", "breaks", "size", "pharynx", "mouth",
                           "ring", "vowel", "style", "attack", "release", "glide", "throats", "looseness",
                           "spread", "size_spread", "distance", "azimuth", "room", "decay", "wet", "drive", "body",
                           "air", "level", "grit_after"};
    const float ranges[37][3] = {{0, 1, .5f}, {0, 1, .12f}, {0, 1, 0}, {0, 5, .3f}, {0, 20, 2.5f}, {0, 60, 6},
        {0, 40, 4}, {0, 80, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
        {13, 20.5f, 17.5f}, {0, 1, 0}, {0, 1, .5f}, {0, 1, .3f}, {0, 1, .5f}, {0, 1, .45f}, {3, 1200, 60},
        {20, 2500, 220}, {0, 600, 60}, {1, 8, 3}, {0, 1, .35f}, {0, 1, .5f}, {0, 1, .4f}, {0, 1, .15f},
        {-180, 180, 0}, {0, 1, .5f}, {.3f, 7, 1.8f}, {0, 1, .28f}, {0, 1, 0}, {-12, 6, 0}, {-6, 9, 0},
        {-24, 6, 0}, {0, 1, 0}};
    const std::set<std::string> global = {"attack", "release", "glide", "throats", "distance", "azimuth", "room",
                                          "decay", "wet", "drive", "body", "air", "level", "grit_after"};
    std::set<std::string> unique;
    for (uint32_t k = 0; k < 37; ++k) {
        const auto &p = d->params[k];
        REQUIRE(!std::strcmp(p.id, ids[k]) && unique.insert(p.id).second);
        REQUIRE(p.minimum == ranges[k][0] && p.maximum == ranges[k][1] && p.initial == ranges[k][2]);
        REQUIRE(p.legacy_index == k && p.taper == WS2_LINEAR && p.name && *p.name && p.unit);
        const std::string id = p.id;
        if (id == "throats") {
            // Sets the number of throats a note allocates: stepped, never modulatable.
            REQUIRE(p.flags == (WS2_STEPPED | WS2_AUTOMATABLE) && p.step == 1);
        } else if (id == "grit_after") {
            REQUIRE(p.flags == (WS2_STEPPED | WS2_MODULATABLE | WS2_AUTOMATABLE) && p.step == 1);
        } else if (global.count(id)) {
            REQUIRE(p.flags == (WS2_MODULATABLE | WS2_AUTOMATABLE) && p.step == 0);
        } else {
            REQUIRE(p.flags == (WS2_MODULATABLE | WS2_AUTOMATABLE | WS2_PER_VOICE) && p.step == 0);
        }
    }
    REQUIRE(unique.size() == 37);
    REQUIRE(!std::strcmp(d->params[20].unit, "ms") && !std::strcmp(d->params[35].unit, "dB") &&
            !std::strcmp(d->params[14].unit, "cm") && !std::strcmp(d->params[28].unit, "°"));
    REQUIRE(d->save_state && d->load_state);  // the SDK always fills these; GUTTR answers UNSUPPORTED
    Rig rig;
    unsigned char byte = 0;
    ws2_writer w{sizeof(ws2_writer), &byte, [](void *, const void *, uint32_t) { return int(WS2_OK); }};
    REQUIRE(d->save_state(rig.state, &w) == WS2_UNSUPPORTED);  // parameters live in the patch
}

// Memory: one WS2_LARGE allocation in prepare, exactly what memory_requirements reports; room lines long
// enough at 96 kHz; latency = round(2.5 ms).
void memory_and_latency() {
    for (double rate : {44100., 48000., 96000.}) {
        Rig rig(rate);
        REQUIRE(rig.large_calls == 1 && rig.large == rig.memory.large_bytes);
        REQUIRE(rig.memory.fast_bytes >= sizeof(Guttr) && rig.memory.fast_bytes < 4096);
        REQUIRE(rig.d->latency_frames(rig.state) == uint32_t(std::lround(.0025 * rate)));
        const auto &room = rig.dsp().core->room;
        for (int i = 0; i < 8; ++i) REQUIRE(room.base[i] * 1.6 + 8 < double(room.mask));  // room = 1 never clamps
        REQUIRE(.0832 * rate + 2 < double(room.pre_mask));
    }
    REQUIRE(ws::Module<Guttr>::descriptor()->latency_frames(nullptr) == 0);
}

void silence_without_notes() {
    Rig rig;
    rig.run_seconds(1.0);
    REQUIRE(peak(rig.L) == 0 && peak(rig.R) == 0);
    REQUIRE(rig.outs[0].flags & WS2_CONSTANT);  // idle bus: constant zero
}

void one_note() {
    Rig rig;
    rig.at(1000, note(WS2_NOTE_ON, 48));
    rig.at_seconds(1.2, note(WS2_NOTE_OFF, 48));
    rig.run_seconds(2.0);
    REQUIRE(finite(rig.L) && finite(rig.R));
    REQUIRE(peak(rig.L) <= .9f && peak(rig.R) <= .9f);
    const size_t a = size_t(.3 * 48000), b = size_t(1.1 * 48000);
    REQUIRE(rms(rig.L, a, b) > .01 && rms(rig.R, a, b) > .01);
    bool differs = false;  // a stereo pair: the throats sit at different azimuths
    for (size_t i = a; i < b; ++i) differs |= rig.L[i] != rig.R[i];
    REQUIRE(differs);
    REQUIRE(peak(rig.L, 0, 1000) == 0);  // nothing before the note
}

// After note-off the throats release and the room tail decays; the bus then goes idle and the output is
// exactly zero. Bound: 10 s after note-off at the default decay (1.8 s).
void release_to_silence() {
    Rig rig;
    rig.at(0, note(WS2_NOTE_ON, 50));
    rig.at_seconds(.5, note(WS2_NOTE_OFF, 50));
    rig.run_seconds(11.0);
    size_t last = 0;
    for (size_t i = 0; i < rig.L.size(); ++i)
        if (rig.L[i] != 0 || rig.R[i] != 0) last = i;
    std::printf("release: last non-zero sample %.3f s after note-off\n", (double(last) - 24000) / 48000);
    REQUIRE(last > 24000 && last < size_t(10.5 * 48000));
    REQUIRE(rig.dsp().active_throats() == 0 && rig.dsp().live_notes() == 0);
    // Anonymous notes (note_id < 0) release by (source, channel, key), newest first.
    Rig anon;
    anon.at(0, anonymous(WS2_NOTE_ON, 60, 2));
    anon.at(256, anonymous(WS2_NOTE_ON, 60, 2));
    anon.run(1024);
    REQUIRE(anon.dsp().live_notes() == 2);
    anon.at(1024, anonymous(WS2_NOTE_OFF, 60, 5));  // other channel: no match
    anon.run(512);
    REQUIRE(anon.dsp().held_notes() == 2);
    anon.at(1536, anonymous(WS2_NOTE_OFF, 60, 2));
    anon.run(512);
    REQUIRE(anon.dsp().held_notes() == 1);
}

void choke_notes() {
    Rig rig;
    rig.at(0, note(WS2_NOTE_ON, 48, 0, 1, 1));
    rig.at(0, note(WS2_NOTE_ON, 55, 1, 2, 1));
    rig.at(0, note(WS2_NOTE_ON, 60, 2, 7, 2));
    rig.set(g::throats, 2);
    rig.run(4800);
    REQUIRE(rig.dsp().live_notes() == 3 && rig.dsp().active_throats() == 6);
    // A matching choke (source 1, note 2) kills that note only, within two control ticks.
    rig.at(4900, choke(1, 2, 1));
    rig.run(4900 + 256 - 4800 + 128);
    REQUIRE(rig.dsp().live_notes() == 2 && rig.dsp().active_throats() == 4);
    REQUIRE(rig.dsp().has_note(1, 1) && !rig.dsp().has_note(1, 2) && rig.dsp().has_note(2, 7));
    // A source choke (UINT32_MAX = every source) kills the rest.
    rig.at(rig.position + 10, choke(UINT32_MAX));
    rig.run(400);
    REQUIRE(rig.dsp().live_notes() == 0 && rig.dsp().active_throats() == 0);
    REQUIRE(finite(rig.L) && peak(rig.L) <= .9f);
    // The room tail still decays to silence.
    rig.run_seconds(8.0);
    REQUIRE(peak(rig.L, rig.L.size() - 48000) == 0);
}

// 16 notes fill the table (one throat each); the 17th takes the oldest note's record and its throat.
void seventeen_notes() {
    Rig rig;
    rig.set(g::throats, 1);
    for (int n = 0; n < 16; ++n) rig.at(uint64_t(n) * 256, note(WS2_NOTE_ON, 40 + n, n, 100 + n));
    rig.run(16 * 256 + 1024);
    REQUIRE(rig.dsp().live_notes() == 16 && rig.dsp().active_throats() == 16);
    rig.at(rig.position + 5, note(WS2_NOTE_ON, 70, 0, 500));
    rig.run(1024);
    REQUIRE(rig.dsp().live_notes() == 16 && rig.dsp().active_throats() == 16);
    REQUIRE(!rig.dsp().has_note(1, 100) && rig.dsp().has_note(1, 101) && rig.dsp().has_note(1, 500));
    REQUIRE(rig.dsp().throats_of(1, 500) == 1 && rig.dsp().throats_of(1, 101) == 1);
    // With three throats per note the 16-throat budget steals the oldest notes' throats.
    Rig three;
    for (int n = 0; n < 8; ++n) three.at(uint64_t(n) * 256, note(WS2_NOTE_ON, 40 + n, n, 200 + n));
    three.run(8 * 256 + 2048);
    REQUIRE(three.dsp().active_throats() == 16 && three.dsp().throats_of(1, 207) == 3);
    REQUIRE(finite(three.L) && peak(three.L) <= .9f);
    // A note-off for a stolen note is ignored; one for a live note releases it.
    rig.at(rig.position, note(WS2_NOTE_OFF, 40, 0, 100));
    rig.at(rig.position, note(WS2_NOTE_OFF, 41, 1, 101));
    rig.run(256);
    REQUIRE(rig.dsp().held_notes() == 15);
}

// Same timeline (notes between control ticks, a per-voice ramp on two channels and a global ramp) is
// bit-identical for host blocks of 1, 17, 128 and 512 frames.
void block_sizes() {
    std::vector<float> reference_l, reference_r;
    for (uint32_t block : {512u, 128u, 17u, 1u}) {
        Rig rig;
        rig.channels[g::vowel] = 16;
        rig.channels[g::pressure] = 16;
        rig.varying = [](uint32_t k, uint32_t c, uint64_t f) {
            const float t = float(f) / 48000.f;
            if (k == g::vowel) return c == 3 ? .5f + .4f * std::sin(6.f * t) : .2f + .1f * t;
            if (k == g::pressure) return c == 0 ? .7f : .5f;
            if (k == g::level) return -3.f + 2.f * std::sin(3.f * t);
            if (k == g::wet) return .2f + .1f * t;
            return g::kParams[k].initial;
        };
        rig.at(37, note(WS2_NOTE_ON, 45, 0, 1));
        rig.at(5000, note(WS2_NOTE_ON, 57, 3, 2));
        rig.at(5000, note(WS2_NOTE_ON, 64, 3, 3));
        rig.at(30001, note(WS2_NOTE_OFF, 45, 0, 1));
        rig.at(40000, choke(1, 3, 3));
        rig.at(41111, note(WS2_NOTE_ON, 52, 5, 4));
        rig.at(60000, note(WS2_NOTE_OFF, 57, 3, 2));
        rig.run(72000, block);
        REQUIRE(finite(rig.L) && rms(rig.L, 6000, 30000) > .01);
        if (reference_l.empty()) {
            reference_l = rig.L;
            reference_r = rig.R;
        } else {
            REQUIRE(rig.L.size() == reference_l.size());
            REQUIRE(std::memcmp(rig.L.data(), reference_l.data(), rig.L.size() * sizeof(float)) == 0);
            REQUIRE(std::memcmp(rig.R.data(), reference_r.data(), rig.R.size() * sizeof(float)) == 0);
        }
    }
}

std::vector<float> render_note(uint32_t channel, uint32_t host_channels, const std::function<void(Rig &)> &setup) {
    Rig rig;
    setup(rig);
    rig.at(0, note(WS2_NOTE_ON, 52, int32_t(channel)));
    rig.at(30000, note(WS2_NOTE_OFF, 52, int32_t(channel)));
    rig.run(36000, 128, host_channels);
    std::vector<float> both = rig.L;
    both.insert(both.end(), rig.R.begin(), rig.R.end());
    return both;
}
bool same(const std::vector<float> &a, const std::vector<float> &b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// Per-voice parameters are read on the note's channel; global ones on channel 0. 1 and 16 host channels.
void channels() {
    auto plain = [](Rig &) {};
    auto ch3_vowel = [](Rig &r) { r.set_channel(g::vowel, 3, 1.f); };
    auto ch3_level = [](Rig &r) { r.set_channel(g::level, 3, -24.f); };
    auto ch0_level = [](Rig &r) { r.set_channel(g::level, 0, -12.f); };
    const auto on0 = render_note(0, 16, plain), on3 = render_note(3, 16, plain);
    REQUIRE(same(on0, on3));  // the voice channel alone does not change the sound
    REQUIRE(same(render_note(0, 16, ch3_vowel), on0));   // channel-3 modulation leaves the note on 0 alone
    REQUIRE(!same(render_note(3, 16, ch3_vowel), on3));  // and changes the note on channel 3
    REQUIRE(same(render_note(3, 16, ch3_level), on3));   // global: channel 3 is never read
    REQUIRE(!same(render_note(3, 16, ch0_level), on3));  // global: channel 0 applies to every note
    REQUIRE(same(render_note(3, 1, plain), on3));        // 1 host channel = 16 host channels
    // A one-channel signal applies to every voice channel (Signal::ch clamps to channel 0).
    auto all_vowel = [](Rig &r) { r.set(g::vowel, 1.f); };
    auto ch3_only = [](Rig &r) { r.set_channel(g::vowel, 3, 1.f); };
    REQUIRE(same(render_note(3, 1, all_vowel), render_note(3, 16, ch3_only)));
    // Voice channels at or above the signal's channel count read channel 0.
    auto four = [](Rig &r) { r.channels[g::vowel] = 4; r.value[g::vowel].fill(.5f); r.value[g::vowel][0] = 1.f; };
    REQUIRE(same(render_note(9, 16, four), render_note(3, 1, all_vowel)));
    // Two notes on two channels: modulation on channel 3 changes the mix.
    Rig a, b;
    b.set_channel(g::vowel, 3, 1.f);
    for (Rig *r : {&a, &b}) {
        r->at(0, note(WS2_NOTE_ON, 48, 0));
        r->at(0, note(WS2_NOTE_ON, 55, 3));
        r->run(24000, 128, 16);
    }
    REQUIRE(a.L != b.L);
}

// A polyphonic texture cable must not drive a different channel's post-grit.
// Use the same topology (16 channels) in both the baseline and modulated renders.
void post_grit_channels() {
    auto settings = [](Rig &r) {
        r.set(g::grit_after, 1.f);
        r.set_channel(g::growl, 0, 0.f);
        r.set(g::wet, .35f);
    };
    auto ch0 = [&](Rig &r) { settings(r); r.set_channel(g::growl, 0, 1.f); };
    auto ch3 = [&](Rig &r) { settings(r); r.set_channel(g::growl, 3, 1.f); };
    const auto on0 = render_note(0, 16, settings), on3 = render_note(3, 16, settings);
    REQUIRE(same(on0, on3));
    REQUIRE(same(render_note(3, 16, ch0), on3));
    REQUIRE(!same(render_note(3, 16, ch3), on3));
    REQUIRE(same(render_note(0, 16, ch3), on0));
    REQUIRE(!same(render_note(0, 16, ch0), on0));

    // Both channels actually sing; changing either channel changes the mix.
    Rig a, b, c;
    settings(a); ch0(b); ch3(c);
    for (Rig *r : {&a, &b, &c}) {
        r->at(0, note(WS2_NOTE_ON, 48, 0, 1));
        r->at(37, note(WS2_NOTE_ON, 55, 3, 2));
        r->run(24000, 17, 16);
        REQUIRE(r->dsp().held_notes() == 2 && finite(r->L) && finite(r->R));
    }
    REQUIRE(!same(a.L, b.L) && !same(a.L, c.L));
    // Mono texture signals retain the shared-bus mode, regardless of note channel.
    auto mono = [](Rig &r) { r.set(g::grit_after, 1.f); r.set(g::growl, .8f); };
    REQUIRE(same(render_note(0, 1, mono), render_note(3, 16, mono)));
}

// The idle bus must forget low-mid guard history, and a restart must not
// depend on how a host partitions the silent interval.
void idle_restart() {
    {
        Rig r;
        auto &x = *r.dsp().core;
        x.lmCut = 5.; x.lmC = .4; x.lmCStep = .001; x.lmRatio = 22.;
        x.clear_bus();
        REQUIRE(x.lmCut == 0 && x.lmC == 0 && x.lmCStep == 0 && x.lmRatio == 0);
    }
    std::vector<float> reference_l, reference_r;
    for (uint32_t block : {512u, 128u, 17u, 1u}) {
        Rig r;
        r.set(g::throats, 1.f);
        r.set(g::release, 20.f);
        r.set(g::decay, .3f);
        r.set(g::wet, .2f);
        r.at(37, note(WS2_NOTE_ON, 45));
        r.run(24000, block);
        REQUIRE(r.dsp().active_throats() == 1 && peak(r.L) > .01f);
        // Stress a nonzero guard state at the same absolute frame in each run.
        r.dsp().core->lmCut = 5.;
        r.dsp().core->lmC = .4;
        r.at(r.position, note(WS2_NOTE_OFF, 45));
        r.run(192000, block);
        REQUIRE(r.dsp().core->busIdle && r.dsp().live_notes() == 0);
        REQUIRE(peak(r.L, r.L.size() - 48000) == 0);
        r.at(r.position + 37, note(WS2_NOTE_ON, 52));
        r.run(12000, block);
        REQUIRE(peak(r.L, r.L.size() - 12000) > .01f && finite(r.L) && finite(r.R));
        if (reference_l.empty()) { reference_l = r.L; reference_r = r.R; }
        else { REQUIRE(same(reference_l, r.L)); REQUIRE(same(reference_r, r.R)); }
    }
}

void note_identity_edges() {
    Rig r;
    r.set(g::throats, 1.f);
    r.set(g::release, 20.f);
    r.set(g::decay, .3f);
    r.at(0, note(WS2_NOTE_ON, 48, 1, 42, 1));
    r.at(0, note(WS2_NOTE_ON, 55, 2, 42, 2));
    r.run(512);
    r.at(r.position, note(WS2_NOTE_OFF, 70, 9, 42, 1)); // ID wins over key/channel.
    r.run(128);
    REQUIRE(r.dsp().find(1, 42)->released && !r.dsp().find(2, 42)->released);
    auto older = anonymous(WS2_NOTE_ON, 60, 3);
    auto newer = older;
    r.at(r.position, older);
    r.at(r.position + 128, newer);
    r.run(512);
    auto wrong = anonymous(WS2_NOTE_OFF, 60, 3); wrong.source_id = 99;
    r.at(r.position, wrong);
    r.at(r.position, anonymous(WS2_NOTE_OFF, 61, 3));
    r.run(128);
    REQUIRE(r.dsp().held_notes() == 3);
    r.at(r.position, anonymous(WS2_NOTE_OFF, 60, 3));
    r.run(128);
    const auto &notes = r.dsp().core->notes;
    uint64_t held = 0, released = 0;
    for (const auto &n : notes) if (n.id && n.note_id < 0 && n.key == 60) {
        if (n.released) released = n.seq; else held = n.seq;
    }
    REQUIRE(held && released > held);
    auto key_choke = choke(1, -1, 3); key_choke.key = 60;
    r.at(r.position, key_choke);
    r.run(384);
    REQUIRE(r.dsp().held_notes() == 1 && r.dsp().has_note(2, 42));
    r.at(r.position, choke(1)); // source-only, source 2 survives.
    r.run(384);
    REQUIRE(r.dsp().held_notes() == 1 && r.dsp().has_note(2, 42));
    r.at(r.position, choke(2));
    r.run_seconds(4.);
    REQUIRE(r.dsp().live_notes() == 0 && r.dsp().active_throats() == 0 && r.dsp().core->busIdle);

    for (bool kill : {false, true}) {
        Rig s;
        s.set(g::throats, 1.f);
        s.set(g::release, 20.f);
        s.set(g::decay, .3f);
        for (int i = 0; i < 16; ++i) s.at(0, note(WS2_NOTE_ON, 40+i, i, 100+i, 1));
        s.run(1024);
        s.at(1029, note(WS2_NOTE_ON, 70, 3, 500, 2));
        s.at(1030, kill ? choke(2, 500) : note(WS2_NOTE_OFF, 70, 3, 500, 2));
        s.at(1031, choke(1));
        s.run_seconds(4.);
        REQUIRE(s.dsp().live_notes() == 0 && s.dsp().active_throats() == 0 && s.dsp().core->busIdle);
        REQUIRE(peak(s.L, s.L.size()-48000) == 0 && peak(s.R, s.R.size()-48000) == 0);
    }
}

void determinism() {
    auto render = [](uint64_t seed, bool reset_first) {
        Rig rig(48000, seed);
        if (reset_first) {
            rig.at(0, note(WS2_NOTE_ON, 47));
            rig.set(g::growl, .6f);
            rig.run(20000);
            rig.reset_checked();
            rig.schedule.clear();
            rig.set(g::growl, 0.f);
            rig.L.clear();
            rig.R.clear();
        }
        rig.at(rig.position + 10, note(WS2_NOTE_ON, 50));
        rig.at(rig.position + 100, note(WS2_NOTE_ON, 57));
        rig.at(rig.position + 25000, note(WS2_NOTE_OFF, 50));
        rig.set(g::jitter, 2.f);
        rig.set(g::creak, .4f);
        rig.run(36000);
        std::vector<float> both = rig.L;
        both.insert(both.end(), rig.R.begin(), rig.R.end());
        return both;
    };
    const auto a = render(7, false), b = render(7, false), c = render(8, false), r = render(7, true);
    REQUIRE(same(a, b));
    REQUIRE(!same(a, c));  // the seed places the followers
    REQUIRE(same(a, r));   // reset returns to the prepared state
}

void extremes() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (int mode = 0; mode < 7; ++mode) {
        Rig rig(mode == 6 ? 96000 : 48000);
        for (uint32_t k = 0; k < kParams; ++k) {
            const auto &p = g::kParams[k];
            const float v = mode == 0 ? p.minimum : mode == 1 ? p.maximum : mode == 2 ? nan : mode == 3 ? inf
                          : mode == 4 ? -1e30f : p.maximum;
            rig.set(k, v);
        }
        if (mode == 5)  // every tick flips between minimum and maximum, per channel
            rig.varying = [](uint32_t k, uint32_t c, uint64_t f) {
                const auto &p = g::kParams[k];
                return ((f / 128 + c + k) % 2) ? p.maximum : p.minimum;
            };
        if (mode == 5)
            for (uint32_t k = 0; k < kParams; ++k) rig.channels[k] = 16;
        for (int n = 0; n < 6; ++n) rig.at(uint64_t(n) * 333, note(WS2_NOTE_ON, n == 0 ? 0 : n == 1 ? 127 : 30 + 9 * n, n));
        rig.at(3000, note(WS2_NOTE_ON, 200, 40));    // key and channel out of range: clamped
        rig.at(20000, note(WS2_NOTE_OFF, 127, 1));
        rig.run(40000, 128, 16);
        REQUIRE(finite(rig.L) && finite(rig.R));
        REQUIRE(peak(rig.L) <= .9f && peak(rig.R) <= .9f);
        if (mode != 0 && mode != 2 && mode != 3) REQUIRE(peak(rig.L) > 0);
    }
}

void reset_silences() {
    Rig rig;
    rig.at(0, note(WS2_NOTE_ON, 45));
    rig.run(24000);
    REQUIRE(peak(rig.L, 12000) > .01f);
    rig.reset_checked();
    rig.L.clear();
    rig.R.clear();
    rig.schedule.clear();
    rig.run(4800);
    REQUIRE(peak(rig.L) == 0 && peak(rig.R) == 0);
    REQUIRE(rig.dsp().active_throats() == 0 && rig.dsp().live_notes() == 0);
}

// Diagnostic only: microseconds per 128-frame block at 48 kHz with every throat busy.
void bench() {
    struct Case { const char *name; int notes; float throats; bool rough; };
    for (const Case &c : {Case{"8 notes x 3 throats (16-throat budget)", 8, 3, false},
                          Case{"16 notes x 1 throat", 16, 1, false},
                          Case{"2 notes x 8 throats, rough texture", 2, 8, true}}) {
        Rig rig;
        rig.set(g::throats, c.throats);
        if (c.rough) {
            rig.set(g::growl, .6f); rig.set(g::rasp, .4f); rig.set(g::drive, .5f); rig.set(g::creak, .3f);
            rig.set(g::flutter, .4f); rig.set(g::vibrato, 30.f);
        }
        for (int n = 0; n < c.notes; ++n) rig.at(uint64_t(n) * 64, note(WS2_NOTE_ON, 40 + 3 * n, n));
        rig.run(48000);  // warm up: every throat voicing
        REQUIRE(rig.dsp().active_throats() == 16);
        std::vector<double> us;
        for (int k = 0; k < 750; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            rig.run(128);
            us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(us.begin(), us.end());
        const double period = 128e6 / 48000;
        std::printf("bench %s: median %.1f us (%.1f%%), p99 %.1f us, max %.1f us of %.0f us (diagnostic)\n",
                    c.name, us[us.size() / 2], 100 * us[us.size() / 2] / period, us[us.size() * 99 / 100],
                    us.back(), period);
    }
}
}  // namespace

int main() {
    descriptor();
    memory_and_latency();
    silence_without_notes();
    one_note();
    release_to_silence();
    choke_notes();
    seventeen_notes();
    block_sizes();
    channels();
    post_grit_channels();
    idle_restart();
    note_identity_edges();
    determinism();
    extremes();
    reset_silences();
    bench();
    std::puts("PASS guttr: descriptor, memory, latency, notes, choke, steal, blocks, channels, determinism, "
              "extremes, reset, no allocations");
    return 0;
}
