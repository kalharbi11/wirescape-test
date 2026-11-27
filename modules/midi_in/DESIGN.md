<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design and provenance

Original Wirescape adapter code, GPL-3.0-only, no upstream DSP source.
The allocator and note core in `native/voice/` are MIT and are the voice-policy
oracle. Device acquisition is exclusively in `native/io/midi/`, not in process.
All DSP storage is fixed-size, state restoration resets sounding notes, and no
allocation, locks, logging or OS calls occur in process/event/reset.

Source identity and UMP protocol are preserved; MIDI 1 zero-velocity Note On is
normalized only by MIDI to CV, never globally applied to MIDI 2 Note On. Group
zero is converted to CV; other groups remain available on the raw MIDI port.
State is a little-endian version marker; parameter/configuration state belongs
to the patch document, while held activations and controller acquisition history
are deliberately not resurrected on restore.

## MOD-7 audit (#32)

Original audit base: lab `c083b26722b5b4d09e3285ab43f93e93be192f4f`. Integration base: lab `3d3db7a64090682a62f220d73baef49709048c87` (PIN-next #35 / PR #57); all original module file blobs and presentation entry are unchanged between the pins. Intentional no-flag-change audit per lab 0024 §4.6. Production dsp.h, module.cpp and module.json remain byte-identical to the pinned module. Its CMake registration is already in the pinned host/midi/CMakeLists.txt (abi2_midi_in_tests); no local replacement CMakeLists or lab edit needed.

The single channel_filter parameter is STEPPED|AUTOMATABLE, not MODULATABLE. In dsp.h process(), update_filter reads the effective value at frame 0 and each incoming event offset. A change emits NOTE_CHOKE for every previously forwarded source, then clears source history; release backpressure queues a retry at the next block boundary or triggers bounded panic. A modulation cable could repeatedly invalidate note ownership and choke active sources, so the filter remains a control-plane setting. Event ordering, source identity, range and defaults stay unchanged. The module has no double state and uses bounded source and release arrays. State v1 clears transient source/pending history on load.

Tests pin the exact descriptor, reject graph modulation while permitting set_parameter, switch Omni to channel 16 then channel 1, verify source-wide choke ordering and retry, preserve accepted UMP offsets, and check state/reset and allocation safety. Existing pinned MIDI tests remain.

## Limitations

[LR-13](../../LAB_REQUESTS.md): the pinned offline renderer cannot enqueue raw UMP. The idle MIDI In → MIDI to CV → oscillator/envelope/VCA demo verifies silence and default identity only; native callback tests exercise active MIDI 1/2 input and channel filter changes. No physical MIDI device or lab workaround. Revisit active offline coverage once LR-13 is in LAB_PIN. T2 owner-requested only.
