<!-- SPDX-License-Identifier: GPL-3.0-only -->
# tools: the wstest harness

`wstest.py` builds and tests module folders against the one lab commit in `LAB_PIN` (lab
[`proposals/0024`](https://github.com/kalharbi11/wirescape-lab/blob/main/proposals/0024-test-repo-macros-modules.md)
§2.2–§2.4). Python 3.10+, standard library only. Needs git, CMake, Ninja (or MSVC) and a C++17 compiler.
Devices stay off: nothing opens audio, MIDI or the LAN.

```sh
python tools/wstest.py fetch                  # the lab at LAB_PIN into .lab/<sha>/
python tools/wstest.py t0 <name>...           # T0 for modules/<name> (or a folder path)
python tools/wstest.py render <name>          # fast offline WAV preview, not T0
python tools/wstest.py t0 --sanitize <name>   # T1's ASan+UBSan run: register, check, build, tests, demos
python tools/wstest.py build <name>...        # register and build only
python tools/wstest.py identical <name>...    # overrides: renders byte-identical to the pin
python tools/wstest.py selftest               # T0 on tools/fixtures; outcomes in fixtures/expected.json
python tools/wstest.py t2 <name>              # T2 (normally the owner's dispatch of t2.yml)
python tools/wstest.py app <name>...          # local only: engine + Studio + desktop build in build/app-<sha7>/; --launch starts it (owner only: may open audio)
python tools/wstest.py ui <name>...           # local only: Studio dev server (fake engine); edit panel.json / presentation.json, reload
python tools/wstest.py shot <name> [--dark]   # local only: render the module's card in the app tree's Studio to a PNG
python tools/wstest.py studio-diff            # regenerate tools/studio/lab.patch (workshop Studio overlay, app tree only)
python -m unittest discover -s tools/tests    # fast harness unit tests (fake lab, no build)
```
If a proxy blocks `codeload.github.com`, add `--git-deps`: it runs the pinned lab's own
`tools/cloud/git_deps.py` once and reuses its cache.

## What T0 does, per module
| Step | Fails when |
|---|---|
| `register` | the folder has zero or two `WS_REGISTER_MODULE` lines; `presentation.json`'s `id` does not match `dsp.h`, or the pin's generator refuses it; or the scratch tree differs from the pin in any other way |
| `check` | lab `wsmod check`: any of C1–C4 is not `pass`, or C5 is not `pass` (`skip` only without `module.json`). C6 is advisory |
| `build` | `wirescape_render`, `abi2_module_cpu_probe` or the module's test target do not build |
| `tests` | CTest `abi2_<name>_tests` (or the lab's `<name>_*_tests` for an override) is missing or fails |
| `demos` | no `demos/*.json`, or the lab's `tools/render/evidence.py` rejects a render: non-finite, over `peak_limit`, a window wrong, renderer faults or late events |
| `cpu_probe` | the lab's CPU probe (2 instances, 48 kHz, 128 frames) counts any allocation or fault. Timing is reported only |
| `identical` | overrides: a demo or a lab render case using the module renders differently from a pristine build of the pin, with new parameters at their defaults |

T0 stops a module at its first failing step and names it. Results go to `build/wstest-out/<variant>/`
(`t0.json`, `check.json`, `cpu_probe.json`, the demo WAVs); CI uploads that folder as an artifact.

## MB1 compatibility (lab A106)

Pins containing the MB1 descriptor-equality witness prepare a scratch tree before the normal build:
configure a fresh out-of-tree build, build only its `wire-describe` target, and run the pin's
`tools/gen_ui_catalogue.py --describe <scratch build>/wire-describe`. Missing targets or generated
known-ID drift refuse registration. Only two more scratch paths are permitted:
`tools/foundation/fixtures/known_module_ids.txt` (checked against the live descriptor's sorted IDs)
and `bench/module_sets.json` (only staged new IDs appended to `experimental`, all existing bytes retained;
overrides add nothing). Older pins continue to use their original generator. The UI, S0 and
`abi2_wire_describe_equality` gates are unchanged. The descriptor build is isolated from the full build,
and no binary from another scratch or the host is used.

## Fast offline listening preview

```sh
python tools/wstest.py render macro
python tools/wstest.py render macro --rate 44100 --block 48
```

`render <name> [--demo <file>] [--rate R] [--block B]` registers one module in the usual
strictly verified scratch tree, builds only `wirescape_render` (plus the MB1 descriptor prerequisite
when required), and renders every demo or the named JSON file from its `demos/` folder.
The rate/block default to each demo's settings. Each WAV is at
`build/wstest-out/render/<name>/<demo-stem>/audio.wav`; the command prints peak (linear FS)
and whether every sample is finite. A failed renderer or non-finite audio returns nonzero.
This is not a test tier: no wsmod check, module CTest, CPU probe, identical or demo evidence
window/peak-limit assertions. Run T0 for those gates. Devices remain off; WAVs are ignored and
the Linux x64 harness-wide CI job smoke-runs Macro.

## T2 verdict
T2 fails on a full-build failure, any failing lab CTest, a demo-matrix row that is not finite, over its
peak or not byte-identical across two runs, or an engine-limit probe that cannot render 48 kHz/128.
Callgrind and Cortex-M7 are advisory. **Lab #652 final ruling (0024 §9.1):**
- **Scratch tree.** A module's `presentation.json` is inserted into the scratch
  `catalogue-presentation.json`, and the pin's `tools/gen_ui_catalogue.py` runs there. So
  `ui_catalogue_sync` and `ui_patch_conformance` gate.
- **The S0 snapshot.** `abi2_sidecar_snapshot_tests` is recorded under
  `lab_ctest.deferred_to_promotion` for a catalogue-changing module. The gating `s0_catalogue_others`
  check compares the scratch `wirescape-engine --null-audio` hello catalogue with the parsed pinned
  fixture.
- **Blocks** are 16, 17, 48, 128 and 512 (§9.2). A row skips only when the demo itself refuses as the
  lab's `adsr-notes` case does at that rate and block (lab #663).

## Linux panel smoke (T0)
The separate Linux T0 app-smoke job consumes the existing changed-module scope (all modules for tools,
workflow or pin changes). For each scoped module with `panel.json`, it runs `ui --no-serve`,
`app` with `ELECTRON_SKIP_BINARY_DOWNLOAD=1` (never `--launch`), then `shot` using the Studio's
locked Playwright Chromium. PNGs in `build/shots/` are uploaded as `panel-smoke-*` artifacts.
Non-panel modules are explicitly skipped; command failures fail the required `t0` gate. This checks
buildability and the fake-engine card, not Electron runtime, audio, or MIDI. Devices stay off.

## `app`, `ui` and `panel.json` (local conveniences, not test tiers)
`modules/<name>/panel.json` is optional: one Studio `PanelOverride` object plus the SPDX key (keys in
[PROTOTYPING.md](../PROTOTYPING.md); unknown keys and an `id` that does not match `dsp.h` are refused).
Only `app` and `ui` use it: they prepare `build/app-<sha7>/` like T0's tree plus
`apps/studio/public/panels/<id>.json` and the id in `panels/index.json`. T0, T1 and T2 never copy the file
(not even into the module folder), so their scratch tree is unchanged.
- `app`: builds `wirescape-engine` (own build dir `build/wstest-<sha7>/app/`), runs `npm ci` in `apps/studio`
  and `apps/desktop` only when `node_modules` is missing or `package-lock.json` changed, runs
  `npm run build` in `apps/desktop` (it builds Studio too) and prints the Electron command. Only `--launch`
  starts the app; it may open audio devices, so agents and CI never pass it.
- `ui`: no engine build (MB1 pins require the descriptor-only build above); runs `npm run dev -- --port N --strictPort` in the app tree and polls `panel.json` and
  `presentation.json` every 0.5 s, re-applying them (and re-running the pin's `gen_ui_catalogue.py`).
  `--no-serve` prepares the tree and exits.
`node_modules`, `dist` and `build` inside the app tree survive a refresh.

## Where things live (all git-ignored)
- `.lab/<sha>/`: the pinned lab, a shallow git checkout. Refused if it is at another commit or edited.
- `build/scratch-<sha7>/`: recreated on every run from the pin, plus the module folders and, before lab
  MB2, the three registration edits (`engine/CMakeLists.txt`, `module_catalog.h`, `module_catalog.inc`).
  It is verified file by file after preparing and again at the end of the run.
- `build/wstest-<sha7>/<variant>/`: the CMake build. Changed files get a fresh mtime only when their content
  changed, so repeated runs are incremental.
- `build/app-<sha7>/`: the `app`/`ui` copy (see above); `build/wstest-<sha7>/app/` its engine build.
- `build/pristine-<sha7>/`: the untouched pin, built once for `identical`.

Signed: claude-builder1, 2026-11-26
