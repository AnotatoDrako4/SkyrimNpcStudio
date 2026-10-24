# Animation Intelligence Lab

Read-only diagnostics for Skyrim SE/AE animation mods in a Mod Organizer 2 profile.
Mission `CLAUDE-B-ANIM-01`. Standalone: it does not need Skyrim NPC Studio or MO2 running.

It answers, with evidence you can re-check:

- which enabled mods supply a given loose `.hkx`, and which one wins the **file**;
- which Open Animation Replacer submods exist, with their priorities and conditions;
- which legacy DAR rules exist and which of their lines are syntactically sound;
- which Pandora / Nemesis / FNIS output is present, attributed only where there is evidence;
- what changes between two profiles.

## Requirements

Python 3.11+ and the standard library. Nothing to install. No network access at runtime.

## Commands

```powershell
# help and version
python tools/claude-animation-intelligence/cli.py --help
python tools/claude-animation-intelligence/cli.py version

# analyse one profile
python tools/claude-animation-intelligence/cli.py scan `
  --instance "D:\Apps" --profile "BodyChangeNG-Test" `
  --out "out/claude-animation-intelligence/run-001"

# compare two profiles, each in its own snapshot
python tools/claude-animation-intelligence/cli.py compare `
  --instance "D:\Apps" --profile-a "Default" --profile-b "BodyChangeNG-Test" `
  --out "out/claude-animation-intelligence/compare-001"

# validate an exported OAR pack in an isolated folder
python tools/claude-animation-intelligence/cli.py inspect-oar `
  --root "tests/claude-animation-intelligence/fixtures/oar-pack" `
  --out "out/claude-animation-intelligence/oar-001"

# synthetic benchmark (fixtures only, never a real profile)
python tools/claude-animation-intelligence/cli.py benchmark `
  --fixture "tests/claude-animation-intelligence/fixtures/perf" `
  --out "out/claude-animation-intelligence/bench-001" --repetitions 6

# re-verify the SHA-256 manifest of a finished run
python tools/claude-animation-intelligence/cli.py verify-manifest `
  --run "out/claude-animation-intelligence/run-001"

# run the test suite
python tools/claude-animation-intelligence/cli.py selftest --verbose
```

`--instance` is the folder that holds `ModOrganizer.ini`. The profile is resolved by
reading that file; no path is assumed and no disk is scanned looking for MO2.

### Budget flags

`--max-depth --max-files --max-json-bytes --max-submods --max-rows --max-seconds`.

`--progress` prints one JSON event per line on stderr (`phase`, `progress`, `done`,
`cancelled`) so a UI can show progress and cancel by interrupting the process (exit 3).

`--full-walk` also walks a mod's top-level `textures/`, `sound/`, `music/` and
`calientetools/`. They are skipped by default because the engine never loads animations
from them; on a real 621-mod profile skipping them gave identical findings in ~40% less
time. The number of pruned folders is reported in `summary.prunedDirectories`.
These are guardrails, not an excuse to report a partial scan as complete: when one is
reached the run emits a `TRUNCATED` finding and exits `3`.

### Exit codes

| code | meaning |
|------|---------|
| 0 | PASS: the scan completed within its budgets |
| 2 | input error: bad arguments, missing instance or profile |
| 3 | partial: truncated, or some paths were unreadable |
| 4 | internal error: an unexpected failure, traceback printed |

## Output

Written **only** under the `--out` directory:

| file | contents |
|------|----------|
| `result.json` | versioned findings, summary, evidence, limitations |
| `conflict-index.json` | path to provider index, winner and overridden per row |
| `report.html` | single standalone offline explorer, no server, no CDN |
| `comparison.json` | profile delta (`compare` only) |
| `benchmark.json` | timings, cold/warm, working set (`benchmark` only) |
| `manifest.sha256.json` | SHA-256 of every other file in the run |

## What it never does

- Write, move or delete anything outside `--out`. MO2, `ModOrganizer.ini`, `modlist.txt`,
  `plugins.txt`, `loadorder.txt`, Overwrite, Skyrim `Data` and saves are opened read-only
  or not at all.
- Run Skyrim, MO2, Pandora, Nemesis or FNIS; load a DLL, SKSE plugin or `.pex`.
- Deserialise a Havok `.hkx`. Only the size and first 16 bytes are read.
- Follow a junction or symlink out of the instance: those paths are skipped and reported.
- Use `eval`, a remote script, or any third-party library.

## What it cannot tell you

`SCANNED_STATICALLY` is not `SUPPORTED_IN_GAME`. `HKX_FILE_PRESENT` is not
`PLAYABLE_ANIMATION`. `CONDITIONS_PARSED` is not `OAR_RUNTIME_WINNER`.
`PANDORA_OUTPUT_PRESENT` is not `PANDORA_PATCH_SUCCESS`. `MO2_LOOSE_FILE_WINNER` is not
`PROVEN_GAME_RESULT`. `TESTS_PASS` is not `UI_INTEGRATED`.

Every report carries `runtimeVerification: NOT_VERIFIED_IN_GAME`. BSA archive names are
read (directory only, file data never touched), so animations that exist only inside an
archive are reported; which of two archives wins a path follows plugin load order and is
not resolved. BA2 and other formats are refused, not guessed at.

## The three precedences it keeps apart

1. **MO2 loose-file winner** — highest mod priority supplies the file. A file fact.
2. **OAR candidate priority** — the order OAR *evaluates* submods in. The winner depends
   on live game state and the in-game OAR menu, so it is reported as `UNKNOWN`.
3. **Pandora behaviour patching** — generated behaviour graphs. Unrelated to the other two.

See `docs/claude-animation-intelligence/` for architecture, verified sources, the
acceptance matrix and the integration handoff.
