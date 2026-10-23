# Integration handoff

Mission `CLAUDE-B-ANIM-01`. This is a **specification and a stable contract**, not an
integration. No QML, viewmodel, CMake, engine, worker or shared script was touched, and
none should be touched on the strength of this document alone. Integration needs an agreed
transfer of ownership.

## What exists today

A standalone Python CLI under `tools/claude-animation-intelligence/` that emits a
versioned JSON document. Any consumer, including a future **Animaciones** panel in
Character Forge, can read that document without linking against this tool.

There is no Animations tab in Skyrim NPC Studio. `TESTS_PASS` is not `UI_INTEGRATED`.

## The contract

`result.json`, `schemaVersion` `1.0.0`. Breaking changes bump the major; additive fields
bump the minor. A consumer must ignore unknown fields and must not assume field order.

```jsonc
{
  "schemaVersion": "1.0.0",
  "mission": "CLAUDE-B-ANIM-01",
  "generatedAt": "2026-10-22T23:35:15Z",     // UTC ISO-8601
  "tool": "claude-animation-intelligence",
  "instance": { "ini": "...", "gameName": "...", "modsDir": "...",
                "profilesDir": "...", "readOnly": true },
  "profile": {
    "name": "BodyChangeNG-Test",
    "readOnly": true,
    "fingerprint": "sha256:<hex>",            // content-based, see fingerprintDetail
    "enabledMods": 621,
    "disabledMods": 79,
    "separators": 0,
    "countedLooseAnimations": 2429,
    "countedBehaviorFiles": 32,
    "activePlugins": 416,
    "fingerprintDetail": { "covers": [], "doesNotCover": [] }
  },
  "summary": {
    "loosePathConflicts": 6, "oarCandidates": 212, "oarPotentialOverlaps": 71,
    "darRuleSets": 17, "behaviorOutputs": 32, "unknowns": 0,
    "filesWalked": 3087, "directoriesWalked": 5952, "prunedDirectories": 431,
    "oarCrossModOverlaps": 13, "truncated": false,
    "durationSeconds": 5.0
  },
  "oar": { "groups": 0, "submods": 0, "activeCandidates": 0,
           "byClassification": { "CONDITIONAL_RUNTIME_UNKNOWN": 209 },
           "classificationMeaning": {} },
  "dar": { "ruleSets": 17, "byVariant": { "_CustomConditions": 17 },
           "withSyntaxErrors": 0 },
  "behavior": { "behaviorFiles": 32, "toolsWithEvidence": ["FNIS"],
                "evidence": [ { "tool": "FNIS", "evidenceKind": "filename",
                                "path": "...", "mod": "..." } ] },
  "issues": [ {
    "id": "loose_file_override-1a2b3c4d5e6f",   // stable: sha256(category|subject|mods)[:12]
    "category": "LOOSE_FILE_OVERRIDE",
    "severity": "high",                          // info | low | medium | high
    "confidence": "proven",                      // proven | likely | unverifiable
    "subject": "meshes/actors/character/animations/female/mt_idle.hkx",
    "whatWasObserved": "...",
    "providers": [ { "mod": "...", "priority": 620, "path": "...",
                     "role": "winner", "bytes": 53264, "sha256": "<hex>" } ],
    "whyItMatters": "...",
    "action": "...",                             // manual and reversible
    "runtimeVerification": "STATIC_FILE_FACT",   // or RUNTIME_CONDITION_DEPENDENT /
                                                 //    NOT_VERIFIED_IN_GAME
    "sourceReference": "...",
    "detail": {}                                 // category-specific, may grow
  } ],
  "fileConflicts": [ { "path": "...", "kind": "animation_hkx",
                       "winner": { "mod": "...", "priority": 0 },
                       "overridden": [], "identicalContent": false } ],
  "oarOverlaps": [ { "target": "female/mt_walk.hkx", "runtimeWinner": "UNKNOWN",
                     "candidates": [ { "submod": "...", "name": "...", "priority": 0,
                                       "classification": "...", "conditionCount": 0,
                                       "providerMod": "..." } ] } ],
  "recommendations": [ { "category": "...", "action": "...", "because": "...",
                         "issueIds": [], "count": 0 } ],
  "evidence":    [ { "claim": "...", "source": "..." } ],
  "limitations": [ "SCANNED_STATICALLY is not SUPPORTED_IN_GAME.", "..." ],
  "readOnlyWitness": { "capturedAt": "...", "files": { "<path>": "<sha256>" } },
  "comparison": null,                            // populated by `compare`
  "runtimeVerification": "NOT_VERIFIED_IN_GAME"   // never anything else
}
```

`conflict-index.json` is a separate, smaller document for a path-to-provider table:
`paths[] = { path, normalisedKey, kind, providers[] }`, plus `providerOrder[]` giving the
whole profile's ascending priority list.

## Rules a consumer must honour

1. **Show `runtimeVerification`.** A UI that hides it turns a static observation into a
   claim about the game. The top-level value is always `NOT_VERIFIED_IN_GAME`.
2. **Never present an OAR overlap as a resolved winner.** `oarOverlaps[].runtimeWinner`
   is the literal string `UNKNOWN` by design. Render the candidate order as *evaluation
   order*, not as an outcome.
3. **Keep the three precedences visually distinct** — MO2 loose-file winner, OAR candidate
   priority, Pandora behaviour patching. Collapsing them into one "conflict" list is the
   mistake this tool exists to prevent.
4. **Render `limitations` somewhere reachable.** Especially the BSA and `Data` coverage
   limits: absence of a finding is not absence of a problem.
5. **Treat `detail` as opaque and additive.** Read the keys you know; ignore the rest.
6. **Sort by `severity` then `id`**, as the tool already does, so the order is stable.
7. **Do not re-derive severity.** It comes from one table (`engine/issues.py`
   `SEVERITY_POLICY`). If product wants a different ranking, change that table so the CLI
   and the UI never disagree.

## How a future Animaciones panel could consume it

Least-coupled option, and the one recommended first:

- the panel shells out to `cli.py scan --instance <ini dir> --profile <name> --out <temp>`
  and reads `result.json`. No linkage, no Python embedding, cancellable, and the user can
  reproduce the exact same command by hand.

Contracts that would have to be agreed before anything deeper:

| need | why | owner |
|------|-----|-------|
| a JSON-RPC or stdout-JSON streaming mode with progress events | 621 enabled mods took about 10 s; a UI needs progress and cancellation rather than a blocking call | this tool, additively |
| a C++ DTO mirroring the schema above | so the viewmodel is typed instead of walking a `QJsonObject` | whoever owns `engines/` |
| a decision on where the tool ships | the app would need Python present, or the engine reimplemented in C++ against the same contract and the same test fixtures | project owner |
| reuse of the existing `engines/mo2` VFS instead of this reconstruction | the C++ reader already resolves the full virtual Data folder; re-using it would remove this tool's loose-file-only limitation | whoever owns `engines/mo2` |

If the engine is reimplemented in C++, the fixtures in
`tests/claude-animation-intelligence/` are the acceptance suite: a C++ port should produce
the same winners, the same classifications and the same `UNKNOWN` verdicts.

## Files another agent would touch — and must not touch yet

Only with an explicit ownership transfer:

- `apps/desktop/qml/**` — a new Animations panel (owned by Claude A / UX06 today)
- `apps/desktop/viewmodels/**` — a viewmodel to hold the parsed document
- `engines/**`, `services/BethesdaWorker/**`, `CMakeLists.txt`, `scripts/**` — only if the
  engine is ported to C++ or the tool is packaged

This mission changed **none** of them. Everything it created lives under
`tools/claude-animation-intelligence/`, `tests/claude-animation-intelligence/`,
`docs/claude-animation-intelligence/`, `out/claude-animation-intelligence/` and its own two
`.ai-coordination` files.

## Next increments, in priority order

1. ~~Read BSA headers (names only, no decompression)~~ — DONE in revision 4
   (`engine/bsa_index.py`); next would be resolving archive-vs-archive precedence from
   plugin load order.
2. Include the game `Data` folder as the lowest-priority provider behind an explicit flag,
   so vanilla loose files are counted.
3. Streaming progress plus cancellation, as the prerequisite for any UI.
4. Parse `conditionPresets` from the group `config.json` and resolve `PRESET` references in
   submod conditions.
5. Resolve `pluginName`/`formID` operands against the real plugin headers (the repository
   already has `engines/mo2/plugin_header`) to turn `MISSING_REFERENCE` from `likely` into
   `proven`.
6. Optional cross-check of `replacementAnimDatas[].projectName` against behaviour project
   names found on disk, to tighten overlap targeting.
