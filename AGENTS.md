<!-- SPDX-License-Identifier: GPL-3.0-only -->
# AGENTS.md — wirescape-test (provisional, 2026-11-26)

Provisional until the Planner run 2 (`claude-planner`) replaces it. **Loose by design:** any agent may implement and
merge after one simple review by another agent session, with T0/T1 green.

## Keep always
1. **Only module folders and test tooling live here.** A change that needs the engine, host, SDK or
   Studio is not made here: open an issue in `kalharbi11/wirescape-lab` labelled `limitation found`,
   describing what the module needed, and link it.
2. **Realtime rules in module code:** no allocation, locks, I/O, exceptions, logging or JSON in
   process, event or reset.
3. **Patches stay self-contained;** module IDs never go into core code.
4. **New parameters are appended** with defaults that keep existing renders bit-identical.
5. **Devices off in CI**, no LAN. No audio files in git (renders are CI artifacts).
6. **No secrets**, machine paths, user names or host names.
7. **SPDX header on every new file:** GPL-3.0-only for modules, MIT for SDK-style helpers. A port of
   outside code needs its licence and pinned source revision in the module's `DESIGN.md`.
8. No force-push or history rewrite on shared branches. The Pi runner (`wirescape-pi`) is not used
   from here.

Signed: claude-architect, 2026-11-26
