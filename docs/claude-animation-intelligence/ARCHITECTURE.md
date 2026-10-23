# Architecture

Mission `CLAUDE-B-ANIM-01`. One language by choice: **Python 3.11 with the standard
library only**.

## Why Python and not .NET

`python --version` reported 3.11.15 and `py -3 --version` 3.12.10 on this machine, while
`dotnet --version` reported *"No .NET SDKs were found"* (only an 8.0 runtime is present).
Choosing .NET would have meant installing an SDK, which the mission forbids. Python also
keeps the tool independent of the C++/Qt app and the .NET worker, so it can be run,
tested and reviewed without building anything.

## Module map

```
tools/claude-animation-intelligence/
  cli.py                  commands, exit codes, budgets, benchmark harness
  engine/
    sandbox.py            path confinement, budgets, walking, UTF-8 reads, SHA-256
    mo2_profile.py        ModOrganizer.ini, modlist.txt, plugins.txt, loadorder.txt
    loose_winners.py      animation-relevant loose path index; winner and losers
    oar_discovery.py      OAR groups and submods; config.json / user.json precedence
    oar_rules.py          static classification, overlaps, per-submod findings
    dar_discovery.py      legacy DAR layouts and a conservative _conditions.txt lexer
    behavior_outputs.py   Pandora / Nemesis / FNIS observation, never execution
    hkx_probe.py          size and first 16 bytes only
    fingerprint.py        profile fingerprint, read-only witness, SHA-256 manifest
    issues.py             taxonomy, confidence, the single severity policy table
    reports.py            versioned JSON and the standalone HTML page
  web/
    report.css            sober application styling, light and dark
    report.js             DOM rendering through textContent only
```

## Data flow

```
ModOrganizer.ini ──► mo2_profile.load_instance
                         │
profiles/<name>/* ──► mo2_profile.load_profile ──► Profile (ascending priority)
                         │
                         ▼
          loose_winners.build_index   walks each enabled mod, lowest priority first,
                         │            keeping only animation-relevant paths
                         ├──► oar_discovery.discover ──► oar_rules.{config,overlap}_issues
                         ├──► dar_discovery.discover ──► dar_discovery.dar_issues
                         ├──► behavior_outputs.detect ─► behavior_outputs.behavior_issues
                         └──► loose_winners.conflict_issues
                                        │
                         fingerprint ───┤
                                        ▼
                              reports.build_payload
                                        │
                      ┌─────────────────┼─────────────────┐
                      ▼                 ▼                 ▼
                 result.json    conflict-index.json   report.html
                                        │
                              fingerprint.write_manifest
```

Every analyser consumes the **same** index, so a path is walked once. Only the MO2
*winner* of a config file is parsed: a lower-priority copy is not what the game reads, so
parsing it would invent a rule that does not exist.

## The three precedences, kept apart on purpose

| precedence | decided by | what this tool claims |
|---|---|---|
| MO2 loose-file winner | highest enabled mod priority for that path | `STATIC_FILE_FACT`, confidence `proven` |
| OAR candidate priority | submod `priority`, evaluated against live state | `RUNTIME_CONDITION_DEPENDENT`, winner `UNKNOWN` |
| Pandora behaviour patching | the patcher's own run | `OUTPUT_PRESENT_UNVERIFIED` |

`issues.py` is the only place a severity is decided. `SEVERITY_POLICY` maps
`(category, confidence)` to `info|low|medium|high`; a finding reaches `high` only when the
evidence is `proven` *and* the consequence is a file the game will never see. Anything
gated by OAR conditions, BSA contents or a patcher run is capped at `medium`, because
telling a user to reorder mods over a guess is worse than saying nothing. Change a row
there and every report follows; no severity is hardcoded anywhere else.

## Confinement and budgets

`sandbox.resolve_confined(root, relative)` is the only way a path enters the tool. It
rejects `..`, absolute and UNC paths, and any real path that resolves outside `root`.
`sandbox.walk_files` refuses to descend into a reparse point (junction or symlink) and
records the skip instead, because on Windows we cannot cheaply prove where the link lands.

`sandbox.Budget` carries `max_depth`, `max_files`, `max_json_bytes`, `max_submods`,
`max_html_rows`, `max_seconds` and `max_hash_bytes`. A `Clock` shares one deadline across
the whole scan. Hitting any limit produces a `TRUNCATED` finding at `medium` severity and
exit code `3`; partial counts are never presented as complete.

Text is read as UTF-8 with an optional BOM. Invalid bytes raise `DecodeError` rather than
being silently replaced, so a corrupt condition file is reported, not guessed at.

## Report security model

`report.html` is one self-contained file: `web/report.css` and `web/report.js` are inlined
at generation time. Scan data lives in a `<script type="application/json">` island where
`<`, `>` and `&` are written as JSON unicode escapes, so no mod name can terminate the
block. `report.js` builds every node with `createElement` and `textContent`; there is no
`innerHTML`, no `insertAdjacentHTML`, no `document.write`, no `eval`, and no remote
resource. The skeleton's own interpolations go through `html.escape`. Tests S05-S05d
assert all of this on a report generated from hostile input.

## Determinism

Records are keyed by a casefolded, NFC-normalised path, iterated in sorted order, and
issue ids are `sha256(category|subject|mods)[:12]`. Two runs over the same fixture produce
byte-identical JSON once the timestamp, duration and absolute paths are removed, which
test `S07` asserts.

## Extension points

- a new finding type: add a category plus a `SEVERITY_POLICY` row in `issues.py`;
- a new relevant file kind: add a branch to `loose_winners.classify`;
- a new analyser: consume `LooseIndex` and return `list[Issue]`, then wire it into
  `cli.scan_profile` and `reports.build_payload`;
- a new output: add a writer in `reports.py`; the manifest picks it up automatically.
