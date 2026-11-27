<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design

Original Wirescape code, GPL-3.0-only, no upstream source. The shared note adapter
`native/voice/note_to_cv.h` is MIT and reuses the fixed-capacity voice allocator.
Anonymous repeated-key releases use FIFO. Per-source/channel internal identities
isolate sustain; outbound events retain the original source and assigned voice.
Stealing emits CHOKE before replacement ON at the same sample offset.

All storage is fixed. No allocation, locks, logging or I/O in process/reset.
Gate has a one-sample low retrigger when reusing an assigned voice, matching the
existing allocator/reference adapter; mono legato is available to shared-core
clients but the keyboard's four modes retain allocator defaults.

## MOD-7 batch A (#30)

Base: `c083b26722b5b4d09e3285ab43f93e93be192f4f` (LAB_PIN); the override starts from the pinned
module files, unchanged. Lab 0024 §4.6 requires octave to be stepped and
modulatable, retaining its existing AUTOMATABLE bit. Voice count and mode remain
STEPPED | AUTOMATABLE only: `configure_at` reads both and can call
`change_configuration` to resize or reconfigure the allocator, emitting releases.

The existing pitch CV expression reads `b.param(2).value(0, i)` per sample;
no read-site or DSP change is necessary. Effective octave affects pitch CV for
all assigned voices, not the emitted note-event identity. No parameter/port ID,
range, order, default, state wire format or read cadence changes. The pre-existing
`double state_rate` records the exact sample rate for state restore validation;
it is not sample-loop DSP state.

The new engine-graph test asserts exact descriptor flags, rejection of allocator
modulation, and sample-by-sample pitch, gate and velocity for 1 and 16 held voices
with a bipolar modulation source. Existing state and allocation tests remain.
The demo deliberately omits modulation so wstest identical can compare against
the unmodified pin. No lab limitation or workaround identified by inspection;
execution evidence is still required.
