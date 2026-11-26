<!-- SPDX-License-Identifier: GPL-3.0-only -->
# wirescape-test

A fast, loose workspace for developing Wirescape modules **before** they go to
[`kalharbi11/wirescape-lab`](https://github.com/kalharbi11/wirescape-lab) for full testing and review.
It builds against a pinned lab commit (`LAB_PIN`). Every module either proves the SDK and engine
already support it, or names a limitation the lab must fix.

**Status (2026-11-26): skeleton.** The layout, build tooling, test tiers and rules are decided by the
Planner run 2 (`claude-planner`) (brief: lab `docs/context/planner/`). The intended flow:

| Tier | Where | What |
|---|---|---|
| **T0** | locally or Linux CI, minutes | build the module, its own tests (reference math, reset, no allocation, NaN), offline render of a demo patch to WAV for listening, CPU per block |
| **T1** | Linux + Windows CI | T0 on both, plus sanitizers |
| **T2** | owner-triggered | the lab's full module matrix (rates, blocks, channels, state), arm64, callgrind |
| **Promote** | lab PR | the module folder only, plus the T2 result; the lab runs full CI and the Architect reviews |

A module folder here has exactly the lab's `modules/<name>/` layout, so promotion is a copy.

**Known limitation (lab `11a68c2`):** a new module needs hand edits in three core places in the lab
(`engine/control/module_catalog.h`, `engine/control/module_catalog.inc`, `engine/CMakeLists.txt`). Until lab
CAT-1 #358 / MB1 (generated registry) lands, this repo registers modules in a local build only and never
commits those edits upstream.

See `modules/README.md` for the first candidates and [AGENTS.md](AGENTS.md) for rules.
