<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Task pool (any agent)

Short lettered bundles (owner, 2026-11-27).
- **No agent is assigned a bundle until a session claims it.** Each bundle is one session, at most three
  tasks and **one open PR at a time**.
- Only the bundles below are open. The owner can stop after any bundle and re-plan.
- Issues hold the acceptance criteria and live status.
- One task issue, one branch and one PR. A bundle is a sequence of tasks, never a combined PR.

## Rules

1. **Read first:** AGENTS.md, README.md, tools/README.md, PROTOTYPING.md, this file and the task issue.
   Check open PRs and the issue's last comments.
2. **Review first when practical.** Review another session's eligible PR before you start (AGENTS.md
   checklist, exact head, `Reviewed <sha7>: ok`). Squash-merge needs that review and T0 and T1 green on
   the head; docs-only PRs need only the review.
3. **Claim.** Comment `Claimed: <session>; <date>; branch: <branch>` on the bundle's next open task issue.
   - Before taking over an old claim, check its branch and PR and record the handoff. Never overwrite
     active work.
   - No permanent assignees or agent names in repo docs.
4. **Work** one task at a time, tests first. Put the task ID in the PR title and `Closes #N` in the body,
   and add the AGENTS.md handoff with the tested head and evidence. The next task starts from current main.
5. **Serial paths:** `tools/**`, `.github/workflows/**` and `LAB_PIN` take one PR at a time across all
   bundles. Module bundles edit only their own folders and their own `modules/README.md` rows.
6. **Lab gaps:** a lab gap gets a `LAB_REQUESTS.md` row (reuse one if it exists). Nothing is filed in the
   lab unless the owner asks. Never weaken a gate.
7. **Stopping:** comment `handoff-ready` or `blocked: <dependency>` on the issue and remove your claim.
   Close the issue after merge.

## Bundles

| Bundle | Tasks in order | Scope | Start condition |
|---|---|---|---|
| **G: pin integration** | PIN-next #35 → finish chord #26 (PR #40) → finish midi_to_cv #31 (PR #47) | `LAB_PIN` and pin evidence; then the two existing module PRs | **Lab PR [#724](https://github.com/kalharbi11/wirescape-lab/pull/724) merged** (fixes LR-11 and LR-12). Bump to a lab main commit that contains it. |
| **H: batch A finish** (done) | #27, #32, #33 | those three folders and their rows | Merged; no new claim. |
| **A: workshop CI** | CI-app-smoke #23 (PR #61) → CI-windows-app #24 | `tools/**`, workflows, tools docs | #61 active; preserve its claim. G's pin PR #57 merged. One shared-tool writer. |
| **I: batch B effects** | MOD-9-delay [#62](https://github.com/kalharbi11/wirescape-test/issues/62) → MOD-9-reverb [#63](https://github.com/kalharbi11/wirescape-test/issues/63) | `modules/delay/**`, then `modules/reverb/**`, own index rows | Unassigned. After MOD-7 completes (#40 and #47 merge). |
| **J: batch B dynamics and sound** | MOD-9-quad_vca [#64](https://github.com/kalharbi11/wirescape-test/issues/64) → MOD-9-chaos_lite [#65](https://github.com/kalharbi11/wirescape-test/issues/65) → MOD-9-spectral [#66](https://github.com/kalharbi11/wirescape-test/issues/66) | Those three module folders and own index rows | Unassigned. Same MOD-7 gate; may run alongside I. |
| **K: MB2 pin and stereo evidence** | [#78](https://github.com/kalharbi11/wirescape-test/issues/78) | `LAB_PIN` and pin integration evidence | Unassigned. Lab MB2 #725 and V2 #758 merged. Check the serial tooling queue before claiming. |

**2026-11-28 release:** Chaos Lite [#73](https://github.com/kalharbi11/wirescape-test/pull/73) merged as `3cbf701`. Spectral [#66](https://github.com/kalharbi11/wirescape-test/issues/66) is the next task in J. Bundle K is a separate pin task. The active MIDI-to-CV render evidence remains in [#77](https://github.com/kalharbi11/wirescape-test/pull/77).

**G details.**
- The bump picks up lab MB1 (first MB1 pin, with the #18 compatibility), the #683 fixes for LR-9 and
  LR-10, and the #724 fixes.
- Run T0 and T1 on every module, including GUTTR.
- Check that the five Studio overlay BASE files still match at the new pin. If one changed, rebase the
  overlay in A's serial queue; never refresh hashes blindly.
- Then rebase #40 and #47 on the new main and re-run their exact-head T0/T1. If their original sessions
  are idle, record the handoff on the PR.
- #47's active MIDI render coverage waits for lab #722 (LR-13). The idle demo is acceptable until then.
  Keep this listed in its DESIGN.md "Limitations".

Overrides stay bit-identical without modulation (`wstest identical`). Each module PR includes
presentation.json, a demo, tests, README and DESIGN. `panel.json` is optional.
`midi_in/channel_filter` and `notes_to_cv/voices` stay setParam-only (lab 0024 §4.6). Their audits
document and verify why.

**Next batch, now filed:** MOD-9 is split into exactly two unassigned bundles, I (two tasks) and J (three tasks), at the owner's request. Claim the first task only after MOD-7's remaining PRs #40 and #47 merge; no draft-ahead. Within a bundle, each predecessor merges before the next starts from main. The two bundles share only their index rows; preserve both on conflict. Read-site audits may retain structural parameters as setParam-only with reasons. Default renders stay bit-identical. No tooling/pin or lab edits. Promotions still wait for lab MB2 and owner-requested T2.

## Status snapshot (2026-11-27)

| Task | Issue | PR | State |
|---|---|---|---|
| GUTTR | — | #38 | Merged `3cc1b4b`; owner listening is next |
| MOD-T2c, TOOL-render, TOOL-shot | #18, #21, #22 | #41, #52, overlay #54 | Merged |
| MOD-6, MOD-7 groove / cv_to_notes / host_keyboard, MOD-8 | #25, #28, #29, #30, #34 | #42, #43, #55, #44, #50 | Merged |
| MOD-7-chord | #26 | #40 | New pin integrated; final-head CI and independent review (bundle G). LR-11 fix pinned. |
| MOD-7-midi_to_cv | #31 | #47 | Pretesting passed; final integration follows #40, then exact-head T0/T1 and review. LR-13 active-render gap retained. |
| PIN-next | #35 | #57 | Merged ed24cc8; lab #724 pinned at 3d3db7a. |
| MOD-7-quantizer, midi_in, notes_to_cv | #27, #32, #33 | #58, #59, #60 | Merged (bundle H complete). |
| CI-app-smoke, CI-windows-app | #23, #24 | #61, then new PR | #61 active; #24 follows its merge. |
| MOD-9 batch B | #62–#66 | — | Filed in I/J; unassigned, gated on MOD-7 completion. |

Signed: claude-architect, 2026-11-27

Signed: codex-lead, 2026-11-27, 13:56 EST
