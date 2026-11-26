<!-- SPDX-License-Identifier: GPL-3.0-only -->
# wirescape-test

A fast, loose workshop for Wirescape modules. New modules, and changes to existing ones, are built and
tested here against one pinned lab commit, then promoted to
[`kalharbi11/wirescape-lab`](https://github.com/kalharbi11/wirescape-lab) as a folder copy for full CI and
Architect review. Every module either proves the SDK and engine are enough or names one lab limitation.

Design: lab [`proposals/0024`](https://github.com/kalharbi11/wirescape-lab/blob/main/proposals/0024-test-repo-macros-modules.md)
§2 (this repo) and §4 (module specs). Rules: [AGENTS.md](AGENTS.md). Module index:
[modules/README.md](modules/README.md).

**Status (2026-11-26):** rules and layout decided; the harness (`tools/wstest.py`, task MOD-0) is the
first task. Until it merges, only docs change here.

## Layout
```
LAB_PIN                      the one lab commit this repo builds against
modules/README.md            index: one row per folder (kind, status, links)
modules/<name>/              exactly the lab's modules/<name>/ layout, plus demos/
  README.md DESIGN.md dsp.h module.cpp module.json tests.cpp CMakeLists.txt [LICENSE] demos/*.json
tools/wstest.py              the harness (Python 3.10+, standard library only)
tools/fixtures/              harness self-tests
.github/workflows/           t0.yml, t1.yml, t2.yml
```
- **New** folder: a module the lab does not have.
- **Override:** a folder with the name of an existing lab module. It starts as a byte copy at `LAB_PIN`,
  and its `DESIGN.md` names that base commit.
- **`demos/`:** render cases in the lab's `tools/render/cases` format (patch, events, windows,
  `peak_limit`). Every module has at least one.
- **`module.json`:** present while the pinned lab still uses it (until lab MB2).

## The flow
1. **Write the failing test first** in `tests.cpp`, then the DSP in `dsp.h`, following the module's spec
   in 0024 §4 (or its issue).
2. **Run T0** locally: `python tools/wstest.py t0 <name>`. Push; T0 runs in CI on Linux.
3. **Mark the PR ready.** T1 adds linux-arm64, Windows and sanitizers.
4. **One review** by another agent session. Any agent merges when the review names the head and T0 and
   T1 are green.
5. **The owner runs T2.** After lab MB2, the lab's B4 lane promotes the folder (0024 §2.7).

## Commands (planned in MOD-0; devices stay off)
```sh
python tools/wstest.py fetch                 # the lab at LAB_PIN into .lab/<sha>/
python tools/wstest.py build <name>…         # scratch lab copy + registration + build
python tools/wstest.py t0 <name>             # check, build, tests, demo renders, CPU probe, identical
python tools/wstest.py identical <name>      # overrides: byte-identical renders at default parameters
```
Requirements: Python 3.10+, CMake, Ninja, a C++17 compiler (MSVC on Windows), and git.

## Test tiers
| Tier | Runs | What |
|---|---|---|
| **T0** | every push and PR, Linux x64 | `wsmod check` (C1–C5); scratch build with the module registered; the module's `tests.cpp` with allocation watch; every demo rendered (finite, peak, active and silent windows), WAVs as CI artifacts; CPU probe (zero allocations and faults; timing is a diagnostic); overrides: `identical` |
| **T1** | non-draft PRs and `main` | T0 on linux-arm64 (the Pi is the first target) and Windows x64, plus the build, tests and renders under ASan and UBSan |
| **T2** | owner dispatch, one module | full lab build and every lab CTest on linux-x64, linux-arm64 and windows-x64; demos at 44.1/48/96 kHz × blocks 16, 17, 128 and 512, determinism; callgrind counts and the Cortex-M7 probe (advisory) |
| **Promote** | lab PR (after lab MB2) | byte copy of a named commit, with its T2 run; the lab's full CI and Architect review |

## How a module is registered
The harness never commits to the lab. It recreates a scratch copy of the pinned lab on every build.
- **Before lab MB2:** it adds the three registration lines there (CMake `add_subdirectory`, the catalogue
  `#include`, `WS_APP_MODULE` from the folder's `WS_REGISTER_MODULE` line).
- **After MB2:** it copies the folder.
- Any other change the module needs is a lab issue labelled `limitation found` (see AGENTS.md).

## Lab pin
`LAB_PIN` names a commit on lab `main`. Bumps are their own PR and run T0 and T1 for every module. Bump
when a lab fix a module needs has merged, when MB2 merges, and otherwise weekly while modules are in
progress.

Signed: claude-planner, 2026-11-26
