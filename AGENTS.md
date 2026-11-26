# AGENTS.md — wirescape-test (2026-11-26)

Rules for every agent here. They replace the provisional rules and come from lab
[`proposals/0024`](https://github.com/kalharbi11/wirescape-lab/blob/main/proposals/0024-test-repo-macros-modules.md)
§2 (Planner run 2a). **Loose by design:** any agent implements, another agent session reviews once, and
any agent merges with T0 and T1 green.

**Mandatory reading:** this file, `README.md`, your task issue, and the module's spec (0024 §4 or the
issue). Open other lab files only for a specific fact.

## Keep always
1. **Only module folders, the harness and docs live here.** Paths: `modules/**`, `tools/**`,
   `.github/workflows/**`, `LAB_PIN` and the top-level docs.
2. **Never edit the lab from here.** The only change made to a lab tree is the harness's mechanical
   registration in its scratch copy.
   - A module that needs anything else gets **one issue in `kalharbi11/wirescape-lab` labelled
     `limitation found`**. Anything else means the engine, host, SDK, Studio, a lab test or a lab tool.
   - The issue holds the module and PR, what it needed, the smallest lab change, and any workaround.
   - A workaround may only be code inside the module's own folder, listed in `DESIGN.md` "Limitations".
   - A module that needs a lab behaviour change is blocked (`blocked: lab #N` in `modules/README.md`).
3. **Realtime rules in module code:** no allocation, locks, I/O, exceptions, logging or JSON in
   process, event or reset. Use fixed arrays (at most 16 voices), no heap and no OS calls, so the module
   stays portable to Daisy-class hardware. Document any `double` state in `DESIGN.md`.
4. **Appended only.**
   - New parameters and ports go after the existing ones, with defaults that keep today's output
     bit-identical.
   - Parameter and port IDs are API: never rename or reorder them.
   - Overrides must pass `wstest identical`.
5. **Patches stay self-contained;** module IDs never go into core code.
6. **Modulatable policy** (lab 0023 R3.3):
   - continuous parameters are `MODULATABLE` unless `DESIGN.md` says why not;
   - stepped ones are `STEPPED|MODULATABLE` unless they set allocation or structure.
7. **Devices off.** No audio or MIDI devices and no LAN in tests, CI or agent runs. Renders are offline
   and their WAVs are CI artifacts, never committed.
8. **No secrets**, machine paths, user names or host names.
9. **SPDX header on every new file.** GPL-3.0-only for modules; MIT for SDK-style helpers meant for the
   lab SDK. A port of outside code needs its licence file and pinned source revision in `DESIGN.md`.
10. **No force-push and no history rewrite** on shared branches. The Pi runner (`wirescape-pi`) is not
    used from here.

## Tasks, review and merge
- **One task, one PR** against `main`; the branch name is free. Write the failing test first.
- **CI.** Push to run T0. Mark the PR ready to run T1.
- **One simple review by a different agent session.** It ends with `Reviewed <sha7>: ok`, or with what
  must change. It checks:
  - [ ] realtime rules (3) in `process`, event and reset paths;
  - [ ] appended-only parameters and ports (4); for overrides, `identical` green;
  - [ ] flags follow the modulatable policy (6) and the spec;
  - [ ] `tests.cpp` covers the spec's test rows, 1 and 16 channels where polyphonic, and state;
  - [ ] at least one demo; `README.md` (IDs, ports, parameters, ranges) and `DESIGN.md` updated;
  - [ ] SPDX and licences (9); no lab edits (2).
- **Any agent merges (squash)** when the review names the head SHA and T0 and T1 are green on that head.
  Docs-only PRs need only the review.
- **T2 is the owner's dispatch.** Ask for it in the PR when the module is done.
- **Promotion to the lab** is lab work (B4), after lab MB2, as a byte copy of a named commit with its T2
  run (0024 §2.7). After it merges, the next pin bump deletes the folder here.

## Lab pin
`LAB_PIN` is one commit on lab `main`. Bump it in its own PR, which runs T0 and T1 for every module:
- when a lab fix a module needs has merged;
- when lab MB2 merges;
- otherwise weekly while modules are in progress.

## Handoff (in the PR)
```
**Status:** done | handoff-ready | blocked   **PR:** #…   **Head:** sha7
**Done:** …
**Next:** ordered steps any agent can follow
**Tests:** command or run link → result
**Limitations:** lab issues filed, workarounds in DESIGN.md
Signed: <name>, <YYYY-MM-DD>
```
Dates are New York time.

Signed: claude-planner, 2026-11-26
