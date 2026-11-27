<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Chord override — MOD-7-chord (#26)

Base: wirescape-lab commit `c083b26722b5b4d09e3285ab43f93e93be192f4f`, modules/chord.
Spec: proposal 0024 §4.6. The eight existing stepped, automatable parameters
(enable1–4 and shift1–4) gain WS2_MODULATABLE; IDs, legacy indices,
ranges, defaults, port order, state version and all processing stay unchanged.

## Read cadence and realtime
note_on reads the effective channel-zero enable and shift signals at the
incoming event's offset. Releases and expressions refer to the previously
stored pitches and identities, not the current modulation. No allocation,
locks or I/O; the original fixed activation and pending-release storage is
unchanged. No new double state.

## Tests
The tests-first commit adds exact descriptor flags, effective signal reads
at offsets 0 and 17 for all eight parameters, retained release identities,
1 and 16 host channels, allocation checks and engine modulation connection
acceptance. Original pinned identity, capacity, reset and graph tests remain.
The offline demo exercises default chord expansion into VCO/ADSR/VCA;
wstest identical must compare the unmodulated output with the pristine pin.

## Limitations
No new lab changes or workarounds. Original instance-tag alias and activation
capacity limits remain as documented in README. Local build and test execution
were unavailable in the authoring environment; require T0/T1 on the PR head.
