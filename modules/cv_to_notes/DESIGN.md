<!-- SPDX-License-Identifier: GPL-3.0-only -->
# CV to Notes design / provenance

Original GPL-3.0-only ABI2 C++ module developed for Wirescape Lab issue #327,
following `proposals/0020-note-tools-and-gate-delay.md` §2 (2026-11-22).
No third-party DSP, protocol revision, SDK ABI change or external dependency.
The descriptor, registration and event emission follow the existing
`notes_to_cv` and `chord` module conventions. Fixed 16-lane state keeps all
audio-thread processing allocation-free. The module is experimental.

## MOD-7 override

Pinned lab `c083b26722b5b4d09e3285ab43f93e93be192f4f`. The pinned folder was copied before adding the descriptor and effective-stream regression (test-first commit `4cb705e`). The sole DSP change is adding `WS2_MODULATABLE` to the existing stepped, automatable legato descriptor. The per-sample channel-zero decision cadence (`b.param(0).value(0, i) >= .5f`), note identity, state and allocation remain unchanged. No double-valued DSP state. Default-legato demo is a keyboard CV → note events → CV → synth round trip; it exercises the override without modulation for byte identity.

The regression first failed on the missing flag in hosted T0 at `4cb705e` (tests.cpp:319), after successful registration, checks and build. It exercises held notes with effective nonconstant modulation, nonzero sample offsets, release-before-attack ordering, and 1/16 lanes. Existing reset/choke, source ownership, state and allocation tests are retained. No lab changes or known limitations; see PR #55 for exact-head CI.
