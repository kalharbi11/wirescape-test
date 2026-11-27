<!-- SPDX-License-Identifier: GPL-3.0-only -->
# AGENTS.md — wirescape-test (2026-11-26)

Rules for every agent here. They replace the provisional rules and come from lab
[`proposals/0024`](https://github.com/kalharbi11/wirescape-lab/blob/main/proposals/0024-test-repo-macros-modules.md)
§2 (Planner run 2a). **Loose by design:** any agent implements, another agent session reviews once, and
any agent merges with T0 and T1 green.

**Mandatory reading:** this file, `README.md`, your task issue, and the module's spec (0024 §4 or the
issue). Open other lab files only for a specific fact.


## Active owner hold and task pool (2026-11-27)

Read [TASKS.md](TASKS.md) before starting. It records the current owner hold and lettered bundles;
its temporary hold overrides the routine lab-pin schedule below. GUTTR's active build owns
`modules/guttr/**`. Other tasks must not touch it. Tools/workflows use one PR at a time after #19
(now merged); every new tools branch starts from eligible main. No pin bump before #18 merges and
the GUTTR PR is open; validate every module including GUTTR. No lab PRs; lab remains on hold
(the owner's #708 exception is outside this queue). T2 remains owner-requested only.
No permanent agent assignments in repository docs. Module bundles share only their index rows;
keep both rows on conflict. Separate sessions use separate checkouts/build trees.

## Keep always
1. **Only module folders, the harness and docs live here.** Paths: `modules/**`, `tools/**`,
   `.github/workflows/**`, `LAB_PIN` and the top-level docs.
2. **Never edit the lab from here.** The only changes made to lab trees are the harness's mechanical
   registration in its scratch copy and the Architect-approved workshop Studio overlay (`tools/studio/`,
   PR #54), when present, in the separate app tree only. The overlay never reaches T0/T1/T2 or the pinned lab checkout.
   - A module that needs anything else gets **one row in [`LAB_REQUESTS.md`](LAB_REQUESTS.md)** (the living
     ledger of lab-facing gaps). Anything else means the engine, host, SDK, Studio, a lab test or a lab tool.
   - The row (ID `LR-n`) holds the module and PR that found it, what the lab lacks, the smallest lab change,
     and any workaround here. Check the ledger first and reuse an existing row.
   - **Nothing is filed in `kalharbi11/wirescape-lab` unless the owner asks** (owner, 2026-11-27). When the
     owner asks, file the issue and set the row to `sent to lab #N`.
   - Except for the workshop-only Studio overlay described above, a workaround may only be code inside the module's own folder, listed in `DESIGN.md` "Limitations",
     which links to its `LR-n` rows.
   - A module that needs a lab behaviour change is still blocked (`blocked: LR-n` in `modules/README.md`).
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

## Build and test on every builder run

Use the connected GitHub tools to read the assigned issue and publish changes to its branch.
Use the PR's GitHub Actions jobs for routine builds and tests. A clone or DNS failure in a
chat shell does not block this route when the connected tools can read and write the repo.

- On every start and resume, check the active PR's current head, T0/T1 runs and review status
  before starting another task or handing back the claim. Use the Actions jobs and logs to
  confirm which tests ran and record links and the tested SHA in the PR Result.
- Open or update the PR to run T0. Mark it ready when eligible for T1. On a draft, the green
  T1 summary can mean all platform and sanitizer jobs were skipped. Check those jobs directly.
  For a regression or bit-identical requirement, check the log for the specific test or render.
- If CI is pending, record the run links and the next action on resume. If it fails, use the
  first failing step from the job log to make corrections on the same branch. Report whether
  an earlier test-first commit was actually run before the fix.
- For required checks outside these workflows, such as live Studio/Playwright screenshots or
  an alternate lab pin, use a Codex workspace with the appropriate checkout, tools and exact
  revisions. Record that evidence separately. Keep devices off and T2 owner-requested.
- If connected GitHub tools are missing or an action is denied, report the exact unavailable
  action or error. Use an authorized Codex checkout when one is available.

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
  - [ ] SPDX and licences (9); no lab edits (2); lab-facing gaps are `LAB_REQUESTS.md` rows, none filed in the lab
    unless the owner asked, and `DESIGN.md` "Limitations" links to them.
- **Any agent merges (squash)** when the review names the head SHA and T0 and T1 are green on that head.
  Docs-only PRs need only the review.
- **T2 is requested by the owner** (0024 §9.3). Ask for it in the PR when the module is done. An agent
  dispatches `t2.yml` only when the owner asks in the session or the PR.
- **`presentation.json`.** Every new module, and every override that changes ports or parameters,
  ships `modules/<name>/presentation.json` (0024 §9.1; README "Test tiers"). At promotion it is the one
  file not byte-copied: its object goes into the lab sidecar.
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
**Limitations:** LAB_REQUESTS.md rows, owner-requested lab issues if any, workarounds in DESIGN.md
Signed: <name>, <YYYY-MM-DD>
```
Dates are New York time.

Signed: claude-planner, 2026-11-26
