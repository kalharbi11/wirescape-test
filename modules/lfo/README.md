<!-- SPDX-License-Identifier: GPL-3.0-only -->
# LFO — MOD-8 override

Module `essentials.lfo`, version 2. Existing outputs Control (bipolar), Phase (unipolar) remain polyphonic, same indices. Appended mono-first Clock (gate) and Reset (trigger) inputs. Clock takes priority in tempo sync; reset restarts phase on a rising edge.

| Index | ID | Range | Default | Modulation |
|---|---|---|---|---|
| 0 | rate | .01–20 Hz | 1 | per voice, audio rate |
| 1 | depth | 0–1 | 1 | per voice, audio rate |
| 2 | offset | -1–1 | 0 | per voice, audio rate |
| 3 | sync | 0–1 | 0 | stepped, setParam |
| 4 | beats | .125–16 | 1 | continuous |
| 5 | shape | 0–6 | 0 | stepped, modulatable |
| 6 | width | 0–1 | .5 | same flags as rate |
| 7 | division | 0–18 | 0 | stepped, modulatable |
| 8 | clock_ppq | 1–24 | 1 | stepped, setParam |
| 9 | seed | 0–65535 | 0 | stepped, setParam |

Shapes: sine, triangle, rising ramp, falling ramp, square (width = high duty fraction), random stepped, random smooth. Division 0 uses Beats; 1–18 are the C3 table, 4 bars through 1/32 triplet. Seed 0 uses the instance seed. See DESIGN.md for timing and defaults. Demo is deliberately compatible with the pristine LFO to witness identity; new features are covered by tests.cpp.
