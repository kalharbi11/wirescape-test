<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Notes→CV design and provenance

Original GPL-3.0-only module wrapper around the repository's existing MIT `native/voice/NoteToCv` and `Allocator`. No third-party DSP, new dependency, SDK/ABI header or engine event-routing change. Approved surface and semantics: #421 / Coda's 2026-11-10 ruling, `docs/tasks/done/NOTES_TO_CV_MODULE_421.md`.

An input NOTE_ON enters the shared allocator with its original (source_id, channel, note_id/key) identity. The allocator returns independently assigned poly lanes, so several notes from one channel sound simultaneously. The signal generator uses a bounded 16-voice snapshot per sample with pitch from the exact allocator MIDI-note-to-V/oct convention, gate and velocity. A stolen voice emits a one-sample gate dip before the replacement to retrigger an ADSR; this is the established host.keyboard gate convention. No note-event forwarding output exists. Count-only changes resize the fixed allocator in place (A55 / #439): retained lanes keep gate, pitch, velocity and note identity; removed lanes release, and newly available lanes are idle. The count is still a live stepped parameter; reset/load never resurrects notes.

## MOD-7 audit (#33)

Original audit base: lab `c083b26722b5b4d09e3285ab43f93e93be192f4f`. Integration base: lab `3d3db7a64090682a62f220d73baef49709048c87` (PIN-next #35 / PR #57); original module files and presentation entry unchanged between pins. Intentional no-flag-change audit per lab 0024 §4.6. Production dsp.h, module.cpp, module.json and CMakeLists.txt remain byte-identical to the pin. The single voices parameter is STEPPED|AUTOMATABLE, not MODULATABLE. Both process_no_trigger and the event-bearing process path read it at channel 0/frame 0, clamp to the host limit, and configure or resize the fixed shared allocator. Changing it changes all four poly output channel counts, preserves retained lanes and their identities, and removes unavailable lanes and their trigger timers. This is a structural voice-allocation setting, not a CV performance control; no heap allocation is implied by the term allocator. No new state or double state is introduced.

Regression tests pin descriptor IDs/ranges/defaults/flags, reject a graph modulation cable while allowing set_parameter, and exercise sixteen simultaneous Notes events, 16→1 shrink with late retired-note release, 1→16 expand on the idle path, channel counts, gate/pitch/velocity ownership, trigger clearing, and state-v1 load/reset under allocation watch. A direct callback fixture also uses a synthetic nonconstant host parameter buffer to prove the original frame-zero cadence on both processing paths (not an accepted modulation cable), including a one-channel host cap. Existing allocator, trigger, bounds, rate, and native graph tests remain. The default Keyboard → Notes to CV → oscillator/envelope/VCA demo is the unmodulated byte-identity witness.

## Limitations

No new lab-facing gap identified. Device-free native Notes events and offline keyboard events only; no hardware MIDI or audio. T2 owner-requested only.
