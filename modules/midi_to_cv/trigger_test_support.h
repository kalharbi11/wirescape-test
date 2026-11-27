// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../../host/midi/module_test_support.h"
#include "dsp.h"

// Scope-local 10-output test specialization; do not change the shared MIDI fixture during S6.
template<> struct MidiRig<wirescape::midi::MidiToCv> {
    MidiArena arena; ws2_host host{sizeof(host), &arena, MidiArena::alloc};
    const ws2_descriptor* d = ws::Module<wirescape::midi::MidiToCv>::descriptor(); void* instance = nullptr;
    struct Buffers {
        float output[10][16][256]{};
    };
    std::unique_ptr<Buffers> buffer = std::make_unique<Buffers>();
    float values[32]{}; const float* parameter_rows[32]{}; ws2_signal params[32]{};
    ws2_signal input{sizeof(ws2_signal), nullptr, 0, 0};
    float *output_rows[10][16]{};
    ws2_signal_out outputs[10]{};
    ws2_event_out event_outputs[10]{};
    ws2_event_list input_events{}; ws2_process ctx{};
    std::vector<ws2_event> emitted; size_t event_limit = 20000;
    static int WS2_CALL push(void* context, uint32_t port, const ws2_event* event) {
        auto& r = *static_cast<MidiRig*>(context); if (r.emitted.size() >= r.event_limit) return WS2_CAPACITY;
        auto copy = *event; copy.port = port; r.emitted.push_back(copy); return WS2_OK;
    }
    explicit MidiRig(const uint8_t* config = nullptr, uint32_t bytes = 0, double rate = 48000) {
        emitted.reserve(20000); instance = d->create(&host); REQUIRE(instance);
        ws2_config cfg{sizeof(cfg), rate, 256, 16, 42, config, bytes}; REQUIRE(d->prepare(instance, &cfg, &host) == WS2_OK);
        for (uint32_t i = 0; i < d->param_count; ++i) { values[i] = d->params[i].initial; parameter_rows[i] = &values[i]; params[i] = {sizeof(ws2_signal), &parameter_rows[i], 1, WS2_CONSTANT}; }
        for (uint32_t p = 0; p < d->output_count; ++p) { for (uint32_t c = 0; c < 16; ++c) output_rows[p][c] = buffer->output[p][c]; outputs[p] = {sizeof(ws2_signal_out), output_rows[p], 1, 0, 16}; event_outputs[p] = {sizeof(ws2_event_out), this, p, push}; }
        ctx.struct_size = sizeof(ctx); ctx.sample_rate = rate; ctx.channels = 1; ctx.input_count = d->input_count; ctx.output_count = d->output_count; ctx.param_count = d->param_count;
        ctx.inputs = &input; ctx.params = params; ctx.outputs = outputs; ctx.event_outputs = event_outputs; ctx.event_inputs = &input_events;
    }
    ~MidiRig() { d->destroy(instance); }
    void run(std::vector<ws2_event> events = {}, uint32_t frames = 32) {
        std::stable_sort(events.begin(), events.end(), [](const ws2_event& a, const ws2_event& b) { return a.offset < b.offset; });
        input_events = {sizeof(ws2_event_list), uint32_t(events.size()), events.data()}; ctx.events = input_events; ctx.frames = frames;
        int result; { allocation_watch::Scope scope; result = d->process(instance, &ctx); REQUIRE(scope.allocations() == 0); }
        input_events = {}; ctx.events = {};
        REQUIRE(result == WS2_OK); ctx.frame_position += frames;
        for (uint32_t p = 0; p < d->output_count; ++p) if (d->outputs[p].kind == WS2_SIGNAL) for (uint32_t c = 0; c < outputs[p].channels; ++c) for (uint32_t i = 0; i < frames; ++i) REQUIRE(std::isfinite(buffer->output[p][c][i]));
    }
    float sample(uint32_t p, uint32_t channel = 0, uint32_t frame = 31) const { return buffer->output[p][channel][frame]; }
};
