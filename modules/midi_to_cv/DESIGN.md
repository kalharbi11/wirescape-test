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

Base: `c083b26722b5b4d09e3285ab43f93e93be192f4f` (LAB_PIN). Started from a byte copy of the seven
pinned module files. Only descriptor flags change; existing STEPPED and
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

## Limitations

[LR-13](../../LAB_REQUESTS.md), [lab #722](https://github.com/kalharbi11/wirescape-lab/issues/722): the pinned
renderer script cannot inject raw UMP (only noteOn, noteOff, gate, setParam).
The idle demo verifies silence and unmodulated identity only; it is not evidence
of an audible MIDI note or active behavior. Device-free C++ tests exercise
synthetic UMP through MIDI In and MIDI to CV, without a shared tool change.
No lab edits; active offline render evidence remains unavailable until lab
support is approved. No gate is weakened.

[LR-12](../../LAB_REQUESTS.md): blocking C4 failure on the current pin: on the untouched descriptor, test-first T0 at decafcf
failed before compilation: `param[2] range/initial invalid: steal_policy`.
[Job log](https://github.com/kalharbi11/wirescape-test/actions/runs/45970820334/job/140410596032).
The pinned wsmod validator rejects min == max; the mandated inert parameter
has min=max=default=0. This is tracked in lab #719. No module-local workaround
without altering the parameter contract or bypassing C4; neither is allowed.
The new regression was not executed on the old pin. Lab PR [#724](https://github.com/kalharbi11/wirescape-lab/pull/724)
merged the C4 singleton-range fix at `3d3db7a64090682a62f220d73baef49709048c87`.
The original DSP, module.cpp, module.json, tests and trigger support are unchanged
between the old pin and this candidate. PIN-next #57 is still under validation;
#31 remains blocked until the pin merges and the regression and identical gates
actually execute. LR-13 remains open independently; the idle demo is intentional.
