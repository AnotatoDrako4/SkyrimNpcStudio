# Status — Animation Intelligence Lab

- **Mission:** `CLAUDE-B-ANIM-01`
- **Agent:** Claude B (animation and conflict diagnostics). Independent of Claude A / UX06.
- **Working directory:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- **Last updated:** 2026-10-22
- **Stack:** Python 3.11.15, standard library only (no .NET SDK is installed on this machine)
- **Git:** not used, per the owner's policy. Evidence is preserved as SHA-256 manifests.

## State

| area | state (AGENTS.md vocabulary) |
|------|------------------------------|
| CLI (`scan`, `compare`, `inspect-oar`, `benchmark`, `verify-manifest`, `selftest`) | `TESTS_UNITARIOS_OK` |
| MO2 profile and loose-file precedence | `INTEGRACION_WINDOWS_OK` (run against the real instance, read-only) |
| OAR discovery and static rule analysis | `INTEGRACION_WINDOWS_OK` |
| Legacy DAR lexer and migration detection | `INTEGRACION_WINDOWS_OK` |
| Pandora / Nemesis / FNIS observation | `INTEGRACION_WINDOWS_OK` |
| Offline HTML explorer | `INTEGRACION_WINDOWS_OK` — rendered and exercised in Chrome (revision 2): search, filters, row expansion, Reset, OAR panel, comparison, light/dark, 390 px |
| Anything about in-game behaviour | `PENDIENTE_SKYRIM` — `NOT_VERIFIED_IN_GAME` on every report |
| Integration into Skyrim NPC Studio | `NO_INICIADO` by design. A contract and handoff exist; no core file was touched. |

## Scope and method of validation

**Scope.** Read-only diagnosis of animation mods in an MO2 profile: which enabled mod wins
a loose `.hkx`, which OAR submods exist with what priorities and conditions, which legacy
DAR rules exist and whether their lines parse, and which behaviour-engine output is
present. Plus a profile comparison, a versioned JSON contract and a standalone HTML report.

**Method.** Three independent layers of evidence:

1. *Format evidence* — the OAR and DAR semantics were read out of
   `ersh1/OpenAnimationReplacer@main` source (`Parsing.cpp`, `Conditions.cpp`,
   `OpenAnimationReplacer.cpp`) and the MO2 `modlist.txt` order out of this repository's
   own verified C++ reader. Anything not found there is marked `UNVERIFIED_FORMAT` or
   `UNSUPPORTED_FIELD`, never inferred. See `SOURCES_VERIFIED.md`.
2. *Synthetic fixtures* — 91 executable tests over temporary MO2 instances covering all
   36 mandatory cases, including hostile input, budget exhaustion and exit codes. See
   `ACCEPTANCE_MATRIX.md`.
3. *Real data* — read-only runs against the user's own instance (`D:\Apps`, base
   `F:\Mods Skyrim`), profiles `Default` and `BodyChangeNG-Test`, with before/after
   SHA-256 of every MO2 file opened to prove nothing was written.

## Phases completed

- **Phase 0** — environment checked (`python` 3.11.15 present, `dotnet --version` reports
  no SDK), coordination contract read, exclusive directories created empty, first MO2
  collision test written and made to pass.
- **Phase 1** — read-only MO2 reader, loose-file winner/loser index, `scan` and `compare`,
  cases M01–M10 and S01–S03.
- **Phase 2** — OAR and DAR parsers against verified sources, cases O01–O10 and DP01–DP03.
- **Phase 3** — behaviour-output observation, the offline HTML explorer, cases DP04–DP06
  and S04–S10.
- **Phase 4** — real-profile runs, benchmark, manifests, `test-results.txt`,
  `FINAL_REPORT.md`.
- **Phase 5** — `INTEGRATION_HANDOFF.md` with the versioned contract. No core file touched.

## One defect found and fixed during real-data validation

The first real scan reported **10 `CONFIG_ERROR` findings at `high` severity** against
legitimate DAR rule sets, because `IsEquippedRightType` and `IsEquippedLeftType` were not
in the condition set extracted from `Conditions.h`. Reading
`OpenAnimationReplacer.cpp` showed OAR keeps 23 further *hidden* factories
(`_hiddenConditionFactories`) specifically so legacy DAR files keep working. Those names
are now recognised and flagged as `legacyOnly`, and the false positives are gone. Tests
`O08b` and `DP02` pin the fix. This is the clearest argument for the mission's own rule:
fixtures alone would never have caught it.

## Revision 2 — three defects found by opening the report in a real browser

Once chrome-devtools was reachable the report was exercised in Chrome. None of these was
caught by the structural check of revision 1, which is why that check was never presented
as visual validation.

1. **Most severe findings rendered last.** `report.js` re-sorted the issues with a
   falsy-zero fallback; `high` has rank `0`, so all 7 high rows landed at positions
   267–273 of 274. Fixed by removing the client re-sort: order has one source of truth,
   `engine/issues.py::sort_issues`. Pinned by `test_S07d`.
2. **A behaviour conflict was reported twice.** The real profile showed 6 file conflicts
   but 7 `LOOSE_FILE_OVERRIDE` findings, and the recommendation list carried a duplicate
   id: `_1stperson/behaviors/mt_behavior.hkx` was emitted by both `loose_winners` and
   `behavior_outputs`. `loose_winners` now leaves behaviour records to `behavior_outputs`,
   whose wording keeps a behaviour-graph file conflict apart from an OAR rule conflict.
   Pinned by `test_S07e` (ids unique, overrides == file conflicts).
3. **Sideways page scroll on a phone.** At 390 px the fingerprint and the five-column
   tables overflowed. Tables now scroll inside themselves and long code wraps.

Plus one signal-quality improvement found the same way: **58 of the 71 real OAR overlaps
are submods of a single mod** (an author's own random variants), drowning the 13 that are
genuinely cross-mod — e.g. 7 packs competing for `female/mt_idle.hkx`. Single-mod overlaps
are now `INTRA_MOD_OVERLAP` at `info`; cross-mod ones stay `STATIC_OVERLAP` and are listed
first. Grouped recommendations no longer quote one finding's winner. Pinned by `test_O06`,
`test_O06b` and `test_recommendation_wording_is_not_one_findings_winner`.

Real profile after revision 2: 273 findings, all ids unique, 6 `high` (the 6 genuine file
conflicts), 13 cross-mod overlaps, 58 within-mod. Tests: 91 total, 90 PASS, 1 justified SKIP.

## Revision 3 — systematic debugging of two unexplained symptoms

Method: observe, hypothesise, experiment against the real profile, and only then change
code; every hypothesis below was tested, and one "confirmed" hypothesis turned out wrong.

**Symptom A: `activePlugins: 0`.** Observation: the real payload says 416, matching the
416 starred lines in `plugins.txt`; the 0 was only in a hand-written example in
`INTEGRATION_HANDOFF.md` (now corrected). Hypothesis disproven — but it exposed a real
flaw. `MISSING_REFERENCE` treated every name in `loadorder.txt` as loaded, and
`loadorder.txt` lists inactive plugins too. The data seemed to confirm a false negative:
`ccQDRSSE001-SurvivalMode.esl` is unstarred yet referenced by two EVG submods. Further
evidence reversed that: MO2's `GameSkyrimSE::primaryPlugins()`
(`modorganizer-game_skyrimse/src/gameskyrimse.cpp`) is the five base masters plus every
line of `<gameDir>/Skyrim.ccc`, and that plugin is on line 17 of `Skyrim.ccc`, so it IS
loaded. A naive "starred only" fix would have *introduced* a false positive — and would
also have flagged `Skyrim.esm`, which MO2 never writes to `plugins.txt`.
Fix: loaded = starred in `plugins.txt` ∪ primary plugins (base masters ∪ `Skyrim.ccc`).
A non-primary plugin that is installed but disabled is now reported as such; without a
readable `Skyrim.ccc`, references to `cc*` plugins drop to `unverifiable`. On the real
profile the output is unchanged (22 findings, as predicted before running), because it
had been right by accident. Pinned by 6 new tests.

**Symptom B: scan times of 25.7 / 10.3 / 26.5 / 8.7 / 9.3 s for one profile.** Phase
timing showed the directory walk is ~95% of the run (11 131 directories for 3 087
relevant files); hashing and parsing are negligible, so the variance is the filesystem
metadata cache — the two slow runs were each the first after a long pause. A per-folder
census then showed top-level `textures/` (32%), `calientetools/` (11%) and `sound/` (4%)
held 47% of all directories and **zero** relevant files; the engine only loads
animations from `meshes/`. Those folders (plus `music/`) are now pruned at a mod's top
level, counted in `summary.prunedDirectories`, stated in the limitations, and reversible
with `--full-walk`. Equivalence was proven on the real profile before keeping it: pruned
and full walks produce the **identical** 273 finding ids and 2 429 `.hkx` paths, while
directories walked fall from 11 131 to 5 952 and a warm scan from ~8.5 s to ~5.0 s.

**Symptom C (2026-10-23 08:49): the read-only witness reported 4 changed MO2 files.**
Investigated before anything else, since it touches the mission's core guarantee. The
witness was captured at 22:14 (-03:00) on 10-22; the files' modification times were
00:36–00:45 on 10-23, over two hours later, while this session was idle;
`ModOrganizer.exe` was running; and the profile itself had changed (621 → 625 enabled
mods, 416 → 414 active plugins). The user edited the profile in MO2 overnight. An audit
of every write call in the tool found writes only under `--out` — plus one latent risk:
`benchmark --fixture <dir>` would generate a synthetic `ModOrganizer.ini`/`modlist.txt`
into any directory lacking a `ModOrganizer.ini`, and would scan a real instance if pointed
at one. Benchmark now only generates into a new or empty directory, marks it with
`.anim-lab-synthetic-fixture`, and refuses (exit 2, nothing written) any other directory.
Pinned by `test_S10h`/`test_S10i`. A bracketed re-check — hash and mtime of 7 MO2 files
immediately before and after a fresh `scan` + `compare` — showed **0 changes**. Lesson:
the witness must be re-checked right after the run; a later mismatch only means someone
else changed the profile, which is exactly what it detected.

Current real profile (re-scanned 10-23): 625 enabled mods, 414 active plugins, 2 429
loose `.hkx`, 6 file conflicts, 212 OAR candidates, 71 overlaps (13 cross-mod), 17 DAR
rule sets, 32 behaviour files, 273 findings (6 high), 434 folders pruned. Compare:
19 mods only in `BodyChangeNG-Test`, 606 shared, 0 winners changed.

Tests after revision 3: **101 total, 100 PASS, 0 FAIL, 1 justified SKIP; 36/36 mandatory.**

## Revision 4 — BSA archive names are now read

The largest coverage gap is closed: `engine/bsa_index.py` reads each archive's
directory (header, folder records, file-record blocks, name table) and never its file
data. The format was verified empirically before any code was kept: on all 110 archives
of the enabled mods plus the official `Skyrim - Animations.bsa` (8 979 entries), the
position reached after parsing equals the offset implied by the header counts exactly.
All are version 105. Any archive that breaks that identity, points past its end, lacks
embedded names, exceeds the read budget, or is not a Skyrim BSA (e.g. a Fallout 4 BA2)
is refused and reported as `UNVERIFIED_FORMAT`, never half-read.

What the report now says, and how strongly:
- `ARCHIVE_ONLY_ANIMATION` (info, proven): animation or behaviour paths supplied only by
  archives — previously invisible. Real profile: **13**, all from the USSEP archive.
- `LOOSE_OVERRIDES_ARCHIVE` (info, likely): a loose copy shadows an archived one; loose
  files win in MO2. Real profile: **3**.
- `ARCHIVE_PATH_OVERLAP` (low, unverifiable): two archives share a path; the winner
  follows plugin load order and is deliberately NOT resolved. Real profile: 0.
- Each archive carries `pluginLoaded` (`yes`/`no`/`unknown`), a best-effort match of
  `<plugin>.bsa` / `<plugin> - <suffix>.bsa`. Real profile: 105 yes, 4 no, 1 unknown
  (an orphan archive in Overwrite).

Two defects caught while validating on real data, both fixed and pinned by tests:
a probe harness rebuilt archive paths as `mods/<mod>/...`, which is wrong for
`<Overwrite>` — archive records now carry their absolute path; and plugin association
split only at the first " - ", missing 4 of 110 archives whose plugin name itself
contains " - " (`Anchor SE - Johnskyrim.esp`). One apparent slowdown (11.8 s / 29.6 s
scans) was measured, not assumed: indexing all 110 archives costs ~0.1 s and the warm
walk is unchanged at ~5.2 s; the slow runs coincided with a dozen unrelated
`python.exe` processes and a cold cache.

For validation only, the directory of one file in the game's `Data` folder
(`Skyrim - Animations.bsa`) was read once, read-only. The tool itself still never walks
`Data`.

Tests after revision 4: **116 total, 115 PASS, 0 FAIL, 1 justified SKIP; 36/36 mandatory.**

## Known limits, stated rather than hidden

- BSA archive names are read, but file data is never extracted, archive-vs-archive
  precedence (plugin load order) is not resolved, and BA2 / non-Skyrim archives are refused.
- The game `Data` folder is not walked, so vanilla and DLC loose files are not providers.
- No `.hkx` is deserialised; a recognised header proves only that the first bytes look
  like a Havok container.
- OAR condition trees are recorded, never evaluated. No runtime winner is ever asserted.

## Paths this mission owns and used

```
tools/claude-animation-intelligence/**
tests/claude-animation-intelligence/**
docs/claude-animation-intelligence/**
out/claude-animation-intelligence/**
.ai-coordination/messages/claude-animation-to-claude-ux.md
.ai-coordination/status/claude-animation.json
```

Nothing else was created or modified. `engines/mo2/**` and the
`ersh1/OpenAnimationReplacer` sources were **read only**, as semantics references.
