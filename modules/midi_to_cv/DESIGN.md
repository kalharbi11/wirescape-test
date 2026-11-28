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

## MOD-7 batch A (#31)

Original base: `c083b26722b5b4d09e3285ab43f93e93be192f4f` (old LAB_PIN). Started from a byte copy of the seven
pinned module files. Integration base: `3d3db7a64090682a62f220d73baef49709048c87` (PIN-next #57). Only descriptor flags change; existing STEPPED and
AUTOMATABLE bits remain. No IDs, legacy indices, defaults, state format or
DSP processing changes. Both process paths sample sustain at frame zero;
changing from hold to ignore clears pedal-held notes before the event split.
Both paths read apply-bend and bend range per sample in the pitch expression;
raw bend is independent. Allocation and filter settings (voice_count,
allocation_mode, mono_priority, legato, channel_filter) can release notes,
resize or rebuild the allocator and remain setParam-only. steal_policy has
range 0..0 and no read site; it also remains setParam-only. No new double state.

The new device-free engine graph test connects CV to all three performance
controls and checks exact flags, rejection of the six excluded cables, 1 and
16 voices, event and idle paths, per-sample pitch, raw bend, and block-boundary
pedal release. Existing state/allocator tests remain unchanged.

## LR-13 active offline regression (#76)

Lab PR [#736](https://github.com/kalharbi11/wirescape-lab/pull/736) added raw UMP
renderer ingress; workshop #75 pinned `74f4a3a3260e19d5377a1aa906cc55de400edb99`.
The original idle case is retained byte-for-byte. The new active case reuses the
same VCO/ADSR/VCA/output chain and default MIDI-to-CV parameters: MIDI 1 C4,
maximum positive bend (+2 semitones), center reset, note off, MIDI 2 A4 and note off.
Four active frequency windows and three silent windows witness audible output,
bend/reset and release; both cases must remain byte-identical to the pristine pin.
The evidence-first commit `f94d022` passed Linux x64 native tests, both demos,
zero allocation/fault CPU probe and two-case identity ([T0 job](https://github.com/kalharbi11/wirescape-test/actions/runs/46114629833/job/140885602323)).
The follow-up extends the window checks to directly witness the center reset;
final-head T0/T1 and independent review are required. No DSP, flags, API,
state or tooling changes; no artificial red-stage claim.

## Limitations

[LR-13](../../LAB_REQUESTS.md), [lab #722](https://github.com/kalharbi11/wirescape-lab/issues/722):
raw UMP was unavailable at the former pin; the renderer fix is now pinned.
The active demo checks default audible behavior, not every MIDI protocol message,
modulation or all polyphonic paths. Device-free C++ graph tests cover synthetic
UMP through MIDI In and MIDI to CV and performance modulation. No devices or lab edits.

[LR-12](../../LAB_REQUESTS.md): historical C4 failure on the old pin: on the untouched descriptor, test-first T0 at decafcf
failed before compilation: `param[2] range/initial invalid: steal_policy`.
[Job log](https://github.com/kalharbi11/wirescape-test/actions/runs/45970820334/job/140410596032).
The old pinned wsmod validator rejected min == max; the mandated inert parameter
has min=max=default=0. This is tracked in lab #719. No module-local workaround
without altering the parameter contract or bypassing C4; neither is allowed.
The new regression was not executed on the old pin. Lab PR [#724](https://github.com/kalharbi11/wirescape-lab/pull/724)
merged the C4 singleton-range fix at `3d3db7a64090682a62f220d73baef49709048c87`.
The original DSP, module.cpp, module.json, tests and trigger support are unchanged
between the old pin and this candidate. PIN-next #57 merged at workshop `ed24cc8`; the singleton fix is now pinned.
On new-pin pretest head `75c0485`, all three release platforms passed C4, abi2_midi_to_cv_tests, CPU probe (zero allocations and faults), and byte-identical idle render; ASan/UBSan passed tests and demo. Final integration head `3dbbd25` passed the same gates and #47 merged at `0bbabb6`. LR-13 was a separate renderer limitation, now addressed by the pinned renderer and the active-demo follow-up above.
