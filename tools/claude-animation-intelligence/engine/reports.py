"""Versioned JSON and a single standalone offline HTML report.

The HTML has no remote dependency: the CSS and JS from ``web/`` are inlined at
generation time, so ``report.html`` opens from disk with no server and no
network. Scan data is embedded as JSON inside a ``type="application/json"``
block and rendered by ``report.js`` using ``textContent`` only.
"""

from __future__ import annotations

import html
import json
from dataclasses import dataclass, field
from pathlib import Path

from . import dar_discovery, loose_winners, oar_rules
from . import issues as issue_mod
from .behavior_outputs import BehaviorReport
from .bsa_index import ArchiveCatalog
from .dar_discovery import DarCatalog
from .fingerprint import Fingerprint, utc_now
from .loose_winners import LooseIndex, PathRecord
from .mo2_profile import Instance, Profile
from .oar_discovery import OarCatalog

SCHEMA_VERSION = "1.0.0"
MISSION = "CLAUDE-B-ANIM-01"
WEB_DIR = Path(__file__).resolve().parent.parent / "web"

CLAIM_LIMITS = [
    "SCANNED_STATICALLY is not SUPPORTED_IN_GAME.",
    "HKX_FILE_PRESENT is not PLAYABLE_ANIMATION: no .hkx was deserialised, only its "
    "size and first 16 bytes were read.",
    "CONDITIONS_PARSED is not OAR_RUNTIME_WINNER: condition trees were recorded, never "
    "evaluated.",
    "PANDORA_OUTPUT_PRESENT is not PANDORA_PATCH_SUCCESS: no patcher was executed.",
    "MO2_LOOSE_FILE_WINNER is not PROVEN_GAME_RESULT: archive contents were never extracted, "
    "archive-vs-archive winners follow plugin load order and are not resolved, and the game "
    "Data folder was not walked.",
    "TESTS_PASS is not UI_INTEGRATED: this is a standalone tool, not a Skyrim NPC Studio tab.",
]


@dataclass
class ScanResult:
    """Everything one profile scan produced, ready to serialise."""

    instance: Instance
    profile: Profile
    fingerprint: Fingerprint
    index: LooseIndex
    oar: OarCatalog
    dar: DarCatalog
    behavior: BehaviorReport
    issues: list[issue_mod.Issue] = field(default_factory=list)
    witness: dict[str, object] = field(default_factory=dict)
    duration_seconds: float = 0.0
    archives: ArchiveCatalog | None = None

    @property
    def unknown_count(self) -> int:
        unknown_categories = {
            issue_mod.UNKNOWN_EXTENSION,
            issue_mod.UNSUPPORTED_FIELD,
            issue_mod.UNVERIFIED_FORMAT,
            issue_mod.TRUNCATED,
            issue_mod.ACCESS_ERROR,
        }
        return sum(1 for i in self.issues if i.category in unknown_categories)


def _conflict_rows(index: LooseIndex) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for record in sorted(index.conflicts(), key=lambda r: r.key):
        winner = record.winner
        identical = bool(winner.sha256) and all(
            e.sha256 and e.sha256 == winner.sha256 for e in record.losers
        )
        rows.append(
            {
                "path": record.display,
                "kind": record.kind,
                "winner": {
                    "mod": winner.mod,
                    "priority": winner.priority,
                    "bytes": winner.size,
                    "sha256": winner.sha256,
                },
                "overridden": [
                    {"mod": e.mod, "priority": e.priority, "bytes": e.size, "sha256": e.sha256}
                    for e in record.losers
                ],
                "identicalContent": identical,
            }
        )
    return rows


def _overlap_rows(result: ScanResult) -> list[dict[str, object]]:
    rows = [
        {
            "target": issue.subject,
            "candidates": issue.detail.get("candidates", []),
            "crossMod": bool(issue.detail.get("crossMod")),
            "providerMods": issue.detail.get("providerMods", []),
            "runtimeWinner": "UNKNOWN",
            "issueId": issue.id,
        }
        for issue in result.issues
        if issue.category in (issue_mod.STATIC_OVERLAP, issue_mod.INTRA_MOD_OVERLAP)
    ]
    # Cross-mod competition first: that is where a surprising animation usually comes from.
    return sorted(rows, key=lambda r: (not r["crossMod"], r["target"]))


GROUP_ACTIONS = {
    issue_mod.LOOSE_FILE_OVERRIDE: (
        "For each listed path, open the finding to see which mod wins it. Where that is "
        "not the mod you want, move the mod you want above the current winner in Mod "
        "Organizer. Reversible: restore the previous order. Byte-identical copies need no "
        "action."
    ),
    issue_mod.STATIC_OVERLAP: (
        "If an animation looks wrong in game, find its clip here and adjust the priority "
        "of the submod you want in its own config.json, or disable a competing submod from "
        "the in-game OAR menu. Both are reversible; this tool applies neither."
    ),
    issue_mod.MISSING_REFERENCE: (
        "Install or enable the plugins named in each finding if you want those submods to "
        "be able to match; otherwise ignore the rows for optional packs."
    ),
    issue_mod.INVALID_CONFIG: (
        "Reinstall the affected mods or report the broken config to their authors. Do not "
        "hand-edit a config without a backup."
    ),
    issue_mod.CONFIG_ERROR: (
        "Report each malformed line to the mod author; the finding gives the exact line. "
        "This tool does not rewrite condition files."
    ),
}


def _recommendations(result: ScanResult) -> list[dict[str, object]]:
    """Group actionable findings into a short, manual, reversible to-do list."""
    buckets: dict[str, list[issue_mod.Issue]] = {}
    for issue in result.issues:
        if issue.severity not in ("high", "medium"):
            continue
        if issue.category in (issue_mod.SIMILAR_FILENAME, issue_mod.BSA_COVERAGE_LIMIT):
            continue
        buckets.setdefault(issue.category, []).append(issue)

    order = [
        issue_mod.INVALID_CONFIG,
        issue_mod.CONFIG_ERROR,
        issue_mod.LOOSE_FILE_OVERRIDE,
        issue_mod.USER_JSON_OVERRIDE,
        issue_mod.MISSING_REFERENCE,
        issue_mod.STATIC_OVERLAP,
        issue_mod.PRIORITY_TIE,
        issue_mod.TRUNCATED,
    ]

    def entry(category: str, group: list[issue_mod.Issue]) -> dict[str, object]:
        sample = group[0]
        # A group's action must hold for every member, so a single finding's own
        # wording (which names that finding's winner) is used only for a group of one.
        action = sample.action if len(group) == 1 else GROUP_ACTIONS.get(category, sample.action)
        return {
            "category": category,
            "action": action,
            "because": f"{len(group)} finding(s): {sample.why_it_matters}",
            "issueIds": [i.id for i in group[:12]],
            "count": len(group),
            "runtimeVerification": sample.runtime_verification,
        }

    out = [entry(c, buckets[c]) for c in order if buckets.get(c)]
    out.extend(entry(c, g) for c, g in sorted(buckets.items()) if c not in order)
    return out


def _archive_section(result: ScanResult) -> dict[str, object] | None:
    if result.archives is None:
        return None
    return {
        **result.archives.summary(),
        "items": [
            {
                "mod": a.mod,
                "path": a.path,
                "bytes": a.bytes,
                "version": a.index.version,
                "pluginLoaded": a.plugin_loaded,
                "animationPaths": len(a.relevant),
                "error": a.index.error,
            }
            for a in sorted(result.archives.archives, key=lambda x: (-len(x.relevant), x.path))
        ][:200],
        "note": (
            "Directory only: names were read, file data was never read or decompressed. "
            "Archive-vs-archive precedence follows plugin load order and is not resolved."
        ),
    }


def _limitations(result: ScanResult) -> list[str]:
    limits = list(CLAIM_LIMITS)
    if result.archives is not None:
        summary = result.archives.summary()
        limits.append(
            f"Archive names were read for {summary['indexed']} of {summary['archives']} "
            "BSA archive(s) in enabled mods (directory only; file data never read). "
            f"{summary['failed']} could not be read with the verified layout and are not "
            "covered. Whether an archive loads depends on its plugin; that link is a "
            "best-effort name match."
        )
    else:
        limits.append(
            f"{len(result.index.archives)} BSA/BA2 archive(s) in enabled mods were listed but "
            "never opened; animations that exist only inside them are absent from this report."
        )
    limits.append(
        "The game's Data folder was not walked, so a vanilla or DLC loose file is not "
        "counted as a provider."
    )
    if result.index.walk.pruned_dirs:
        limits.append(
            f"{result.index.walk.pruned_dirs} top-level textures/, sound/, music/ or "
            "calientetools/ folder(s) were not walked by policy: the engine never loads "
            "animations from them. Re-run with --full-walk to include them."
        )
    if result.index.walk.is_truncated:
        limits.append("TRUNCATED: " + "; ".join(result.index.walk.truncated))
    if result.index.hash_truncated:
        limits.append("SHA-256 hashing stopped at its per-run limit; some rows have no digest.")
    if result.oar.truncated or result.dar.truncated:
        limits.append("TRUNCATED: the OAR/DAR submod budget was reached.")
    skipped = [s for s in result.index.walk.skipped if "reparse" in s["reason"]]
    if skipped:
        limits.append(
            f"{len(skipped)} junction/symlink path(s) were skipped rather than followed, so "
            "their contents are unscanned."
        )
    not_covered = result.fingerprint.to_json().get("doesNotCover", [])
    if isinstance(not_covered, list):
        limits.extend(
            f"Fingerprint does not cover: {item}" for item in not_covered
        )
    return limits


def _evidence() -> list[dict[str, str]]:
    return [
        {
            "claim": "modlist.txt order: first line is the highest priority",
            "source": "engines/mo2/src/profile.cpp::parse_modlist (verified local C++ reader)",
        },
        {
            "claim": "OAR submod fields and the required integer priority",
            "source": "ersh1/OpenAnimationReplacer@main src/Parsing.cpp::DeserializeSubMod",
        },
        {
            "claim": "user.json replaces the whole data section; config.json keeps only "
            "name/description",
            "source": "ersh1/OpenAnimationReplacer@main src/Parsing.cpp::ParseModSubdirectory "
            "lines 1025-1066",
        },
        {
            "claim": "built-in OAR condition names",
            "source": "ersh1/OpenAnimationReplacer@main src/Conditions.h",
        },
        {
            "claim": "legacy DAR _conditions.txt grammar and IsEquippedShout alias",
            "source": "ersh1/OpenAnimationReplacer@main src/Conditions.cpp::"
            "CreateConditionFromString, CorrectLegacyConditionName",
        },
        {
            "claim": "Havok packfile header magic 57E0E05710C0C010",
            "source": "empirical: first 16 bytes of installed .hkx animation and behaviour "
            "files, read 2026-10-22",
        },
        {
            "claim": "MO2 mod priority decides loose-file providers",
            "source": "https://github.com/ModOrganizer2/modorganizer/wiki/Mod-information-window",
        },
        {
            "claim": "Pandora patches behaviour graphs, not OAR rules",
            "source": "https://github.com/Monitor221hz/Pandora-Behaviour-Engine-Plus",
        },
    ]


def build_payload(
    result: ScanResult, *, comparison: dict[str, object] | None = None
) -> dict[str, object]:
    """Assemble the versioned result document. Every count comes from real data."""
    sorted_issues = issue_mod.sort_issues(result.issues)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "mission": MISSION,
        "generatedAt": utc_now(),
        "tool": "claude-animation-intelligence",
        "instance": {
            "ini": str(result.instance.ini_path),
            "gameName": result.instance.game_name,
            "modsDir": str(result.instance.mods_dir),
            "profilesDir": str(result.instance.profiles_dir),
            "readOnly": True,
        },
        "profile": {
            "name": result.profile.name,
            "readOnly": True,
            "fingerprint": result.fingerprint.digest,
            "enabledMods": result.profile.enabled_count,
            "disabledMods": len(result.profile.disabled_names),
            "separators": sum(1 for m in result.profile.mods if m.kind == "separator"),
            "countedLooseAnimations": result.index.count_kind(loose_winners.KIND_ANIMATION),
            "countedBehaviorFiles": result.index.count_kind(loose_winners.KIND_BEHAVIOR),
            "activePlugins": sum(1 for _, active in result.profile.plugins if active),
            "fingerprintDetail": result.fingerprint.to_json(),
        },
        "summary": {
            "loosePathConflicts": len(result.index.conflicts()),
            "oarCandidates": len(result.oar.active),
            "oarPotentialOverlaps": sum(
                1
                for i in result.issues
                if i.category in (issue_mod.STATIC_OVERLAP, issue_mod.INTRA_MOD_OVERLAP)
            ),
            "oarCrossModOverlaps": sum(
                1 for i in result.issues if i.category == issue_mod.STATIC_OVERLAP
            ),
            "darRuleSets": len(result.dar.rule_sets),
            "behaviorOutputs": result.behavior.behavior_files,
            "unknowns": result.unknown_count,
            "filesWalked": result.index.walk.files_seen,
            "directoriesWalked": result.index.walk.dirs_seen,
            "prunedDirectories": result.index.walk.pruned_dirs,
            "archivesIndexed": len(result.archives.indexed) if result.archives else 0,
            "archiveAnimationPaths": (
                len(result.archives.relevant_paths()) if result.archives else 0
            ),
            "archiveOnlyAnimationPaths": (
                sum(1 for k in result.archives.relevant_paths() if k not in result.index.records)
                if result.archives
                else 0
            ),
            "truncated": result.index.walk.is_truncated,
            "durationSeconds": round(result.duration_seconds, 3),
        },
        "oar": oar_rules.summarise(result.oar),
        "dar": dar_discovery.summarise(result.dar),
        "behavior": result.behavior.to_json(),
        "archives": _archive_section(result),
        "issues": [i.to_json() for i in sorted_issues],
        "fileConflicts": _conflict_rows(result.index),
        "oarOverlaps": _overlap_rows(result),
        "recommendations": _recommendations(result),
        "evidence": _evidence(),
        "limitations": _limitations(result),
        "readOnlyWitness": result.witness,
        "comparison": comparison,
        "runtimeVerification": issue_mod.NOT_VERIFIED_IN_GAME,
    }


def index_records(index: LooseIndex) -> list[PathRecord]:
    """Conflicting paths only; a single-provider path carries no precedence story."""
    return index.conflicts()


def conflict_index(result: ScanResult) -> dict[str, object]:
    """A standalone path-to-provider index, verifiable row by row."""
    return {
        "schemaVersion": SCHEMA_VERSION,
        "mission": MISSION,
        "generatedAt": utc_now(),
        "profile": result.profile.name,
        "fingerprint": result.fingerprint.digest,
        "providerOrder": [
            {"priority": m.priority, "mod": m.name} for m in result.profile.providers
        ],
        "paths": [
            {
                "path": record.display,
                "normalisedKey": record.key,
                "kind": record.kind,
                "providers": [
                    {
                        "mod": e.mod,
                        "priority": e.priority,
                        "bytes": e.size,
                        "sha256": e.sha256,
                        "role": "winner" if e is record.winner else "overridden",
                    }
                    for e in sorted(record.entries, key=lambda x: -x.priority)
                ],
            }
            for record in sorted(index_records(result.index), key=lambda r: r.key)
        ],
        "note": (
            "Loose files only. BSA/BA2 contents and the game Data folder are not indexed, "
            "so this is a reconstruction of MO2 loose-file precedence, not the full VFS."
        ),
    }


def write_json(directory: Path, name: str, payload: dict[str, object]) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / name
    target.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    return target


def _embed_json(payload: dict[str, object]) -> str:
    """Serialise for a ``<script type="application/json">`` block.

    ``<``, ``>`` and ``&`` are escaped as JSON unicode escapes so no scan value
    can terminate the block early. ``JSON.parse`` reverses them as plain string
    content, never as markup.
    """
    text = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    return text.replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")


def _read_asset(name: str) -> str:
    path = WEB_DIR / name
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def render_html(payload: dict[str, object], title: str) -> str:
    """Build the whole standalone page. Skeleton text is escaped at build time."""
    profile = payload.get("profile", {})
    instance = payload.get("instance", {})
    safe_title = html.escape(title)
    safe_profile = html.escape(str(profile.get("name", "")))
    safe_fingerprint = html.escape(str(profile.get("fingerprint", "")))
    safe_generated = html.escape(str(payload.get("generatedAt", "")))
    safe_instance = html.escape(str(instance.get("ini", "")))
    comparison_hidden = "" if payload.get("comparison") else " hidden"

    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{safe_title}</title>
<style>
{_read_asset("report.css")}
</style>
</head>
<body>
<header class="app">
  <h1>Animation Intelligence Lab</h1>
  <div class="sub">
    Profile <strong>{safe_profile}</strong> &middot; read-only scan &middot;
    generated {safe_generated}<br>
    Instance: <code>{safe_instance}</code><br>
    Profile fingerprint: <code>{safe_fingerprint}</code>
  </div>
</header>

<div class="banner">
  <strong>runtimeVerification = NOT_VERIFIED_IN_GAME.</strong>
  Everything here was derived by reading files. No animation was played, no behaviour
  patcher was run, and no .hkx was deserialised. A potential conflict does not mean a
  mod is broken.
</div>

<main>
  <section>
    <h2>Summary</h2>
    <div id="summary"></div>
  </section>

  <section>
    <h2>Findings</h2>
    <div class="controls">
      <label>Search
        <input type="search" id="f-search" placeholder="path, mod, category, text...">
      </label>
      <label>Severity
        <select id="f-severity">
          <option value="">any</option>
          <option value="high">high</option>
          <option value="medium">medium</option>
          <option value="low">low</option>
          <option value="info">info</option>
        </select>
      </label>
      <label>Category
        <select id="f-category"><option value="">any</option></select>
      </label>
      <label>Confidence
        <select id="f-confidence">
          <option value="">any</option>
          <option value="proven">proven</option>
          <option value="likely">likely</option>
          <option value="unverifiable">unverifiable</option>
        </select>
      </label>
      <label>Provider mod
        <select id="f-mod"><option value="">any</option></select>
      </label>
      <button type="button" id="f-reset">Reset</button>
      <button type="button" id="export-json">Export JSON</button>
      <span class="count" id="issue-count"></span>
    </div>
    <table>
      <thead>
        <tr>
          <th>severity</th><th>confidence</th><th>category</th>
          <th>subject</th><th>providers</th>
        </tr>
      </thead>
      <tbody id="issue-body"></tbody>
    </table>
    <p class="hint">Click any row to expand its evidence, providers and the safe action.</p>
  </section>

  <section>
    <h2>Loose file conflicts (MO2 file precedence)</h2>
    <div id="conflicts"></div>
  </section>

  <section>
    <h2>OAR overlaps (runtime winner unknown)</h2>
    <div id="overlaps"></div>
  </section>

  <section id="comparison-section"{comparison_hidden}>
    <h2>Profile comparison</h2>
    <div id="comparison"></div>
  </section>

  <section>
    <h2>Recommended manual steps</h2>
    <div id="recommendations"></div>
  </section>

  <section>
    <h2>Verified sources</h2>
    <div id="evidence"></div>
  </section>

  <section>
    <h2>Limits of this report</h2>
    <div id="limitations"></div>
  </section>
</main>

<footer class="app">
  Mission {html.escape(MISSION)} &middot; schema {html.escape(SCHEMA_VERSION)} &middot;
  offline report, no network access, no third-party script.
</footer>

<script id="report-data" type="application/json">{_embed_json(payload)}</script>
<script>
{_read_asset("report.js")}
</script>
</body>
</html>
"""


def write_html(directory: Path, payload: dict[str, object], title: str) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / "report.html"
    target.write_text(render_html(payload, title), encoding="utf-8")
    return target
