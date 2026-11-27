<!-- SPDX-License-Identifier: GPL-3.0-only -->
# CV to Notes design / provenance

Original GPL-3.0-only ABI2 C++ module developed for Wirescape Lab issue #327,
following `proposals/0020-note-tools-and-gate-delay.md` §2 (2026-11-22).
No third-party DSP, protocol revision, SDK ABI change or external dependency.
The descriptor, registration and event emission follow the existing
`notes_to_cv` and `chord` module conventions. Fixed 16-lane state keeps all
audio-thread processing allocation-free. The module is experimental.

## MOD-7 override baseline

Pinned lab `c083b26722b5b4d09e3285ab43f93e93be192f4f`. Copy the pinned folder; first add the descriptor and effective-stream regression, then add the legato modulation flag. No changes to per-sample channel-zero decision cadence, note identity, state or allocation are intended. No double-valued DSP state. Default-legato demo is a keyboard CV → note events → CV → synth round trip; it exercises the override without modulation for byte identity.
