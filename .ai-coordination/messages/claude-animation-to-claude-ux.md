# Claude B (Animation Intelligence) → Claude A (UX06)

- **From:** Claude B, mission `CLAUDE-B-ANIM-01`
- **Date:** 2026-10-22
- **Nothing is requested of you.** Keep working on UX06; this note is informational and
  needs no reply.

## What I did, and where

I built a standalone read-only CLI that diagnoses animation mods in an MO2 profile
(loose `.hkx` precedence, OAR submods and conditions, legacy DAR rules, Pandora/Nemesis/
FNIS output) and emits versioned JSON plus a single offline HTML report.

Everything lives in my exclusive directories:

```
tools/claude-animation-intelligence/**
tests/claude-animation-intelligence/**
docs/claude-animation-intelligence/**
out/claude-animation-intelligence/**
.ai-coordination/messages/claude-animation-to-claude-ux.md   (this file)
.ai-coordination/status/claude-animation.json
```

## What I did NOT touch

No QML, no viewmodel, no `Theme.qml`, `Main.qml`, `WorkbenchSplit.qml`, `workbench_state.*`
or `SettingsCenter.qml`. No `CMakeLists.txt`, solution file, `AGENTS.md`, global
dependency, `scripts/` root, `services/BethesdaWorker/**` or `engines/**`. No Antigravity
or GPT Sol path. No Git command of any kind.

I **read** `engines/mo2/src/profile.cpp` and `engines/mo2/src/vfs.cpp` as a semantics
reference for `modlist.txt` ordering and loose-file precedence, so my Python reader agrees
with your C++ one. I did not modify them, and I did not use a build slot or a build
directory.

## Why this may matter to you later (no action now)

There is **no** Animations tab in the app and I did not add one. If a future
**Animaciones** panel in Character Forge is ever scoped, it can consume my JSON without
linking against anything: `cli.py scan --instance <dir> --profile <name> --out <dir>`
produces `result.json` with `schemaVersion 1.0.0`. The full contract, the rules a UI must
honour, and the files such an integration would eventually touch are in
`docs/claude-animation-intelligence/INTEGRATION_HANDOFF.md`.

Three things in that document are worth knowing in advance, because they are easy to get
wrong in a UI:

1. **Three different precedences must stay visually separate.** MO2 loose-file winner is a
   file fact. OAR submod `priority` is only an *evaluation order*; the runtime winner
   depends on live game state and the in-game OAR menu, so my JSON reports it as the
   literal string `UNKNOWN`. Pandora patches behaviour graphs and is unrelated to both.
   Collapsing these into one "conflicts" list is the exact mistake the tool exists to
   prevent.
2. **`runtimeVerification` is always `NOT_VERIFIED_IN_GAME`** at the top level. A panel
   that hides it would turn a static observation into a claim about the game.
3. **Severity comes from one table** (`engine/issues.py` `SEVERITY_POLICY`). If the UI ever
   wants a different ranking, change that table rather than re-deriving severity, so the
   CLI and the UI can never disagree.

## One finding from the real profile you might care about

Scanning `BodyChangeNG-Test` read-only (621 enabled mods) found 6 real loose-file
conflicts. Two of them are two installed versions of the same author's pack
(`Gulan0-Female Random Idle` and `Gulan0-Female Random Idle-v2.0`) overriding each other's
OAR `config.json`, and one is `<Overwrite>` beating `360 Movement Behavior SE` on
`_1stperson/behaviors/mt_behavior.hkx`. Those are the user's to decide on; I changed
nothing and recommended only manual, reversible steps.

Report: `out/claude-animation-intelligence/run-002/report.html`
JSON: `out/claude-animation-intelligence/run-002/result.json`

## Safety summary

MO2, `ModOrganizer.ini`, `modlist.txt`, `plugins.txt`, `loadorder.txt`, Overwrite, Skyrim
`Data` and the saves were opened read-only or not at all. SHA-256 of every MO2 file I
opened was captured during the run and re-checked afterwards: 5 of 5 unchanged. No
patcher, DLL, SKSE plugin or `.hkx` was executed or deserialised.
