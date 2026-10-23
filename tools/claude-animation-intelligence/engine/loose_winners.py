"""Loose-file precedence for animation-relevant paths.

This reconstructs ONE of the three kinds of precedence a user confuses: which
enabled mod supplies a given loose path. It is not the OAR evaluation order and
it is not Pandora's behaviour patching.

Deliberate coverage limits, reported rather than hidden:
  * BSA/BA2 archives are never opened, so a path that exists only inside an
    archive is absent here and must never be shown as a loose winner (M10).
  * The game's own ``Data`` folder is not walked, per the mission's read limits.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from . import issues, sandbox
from .mo2_profile import Instance, Profile
from .sandbox import Budget, Clock, WalkReport, norm_key, resolve_confined, spelling_variance

ANIMATION_DIR = "/animations/"
BEHAVIOR_DIR = "/behaviors/"
OAR_DIR = "/openanimationreplacer/"
DAR_DIR = "/dynamicanimationreplacer/"
TOOL_LOG_RE = re.compile(
    r"(pandora|nemesis|fnis)[^/]*\.(log|txt)$|/(pandora|nemesis|fnis)[^/]*/.*\.(log|txt)$",
    re.IGNORECASE,
)

KIND_ANIMATION = "animation_hkx"
KIND_BEHAVIOR = "behavior_hkx"
KIND_HKX_OTHER = "hkx_other"
KIND_OAR_CONFIG = "oar_config"
KIND_DAR_CONDITIONS = "dar_conditions"
KIND_ARCHIVE = "archive"
KIND_TOOL_LOG = "tool_log"

# Top-level mod folders the engine never loads animations, behaviours or OAR/DAR
# rules from: animations come from meshes/ only. Measured on the real 621-mod
# profile: these held 47% of all directories and zero relevant files. Skipping
# them is a policy choice, counted in WalkReport.pruned_dirs and reversible with
# --full-walk.
PRUNED_TOP_DIRS = frozenset({"textures", "sound", "music", "calientetools"})


def _prune_top_level(relative: str) -> bool:
    return "/" not in relative and relative.lower() in PRUNED_TOP_DIRS


HASHABLE_KINDS = (
    KIND_ANIMATION,
    KIND_BEHAVIOR,
    KIND_HKX_OTHER,
    KIND_OAR_CONFIG,
    KIND_DAR_CONDITIONS,
)


def classify(relative: str) -> str | None:
    """Return the relevance kind of a Data-relative path, or ``None`` to ignore it.

    Classification is by path structure, never by a cosmetic mod name.
    """
    low = "/" + relative.replace("\\", "/").lower()
    base = low.rsplit("/", 1)[-1]

    if base.endswith(".hkx"):
        if BEHAVIOR_DIR in low:
            return KIND_BEHAVIOR
        if ANIMATION_DIR in low:
            return KIND_ANIMATION
        return KIND_HKX_OTHER
    if base in ("config.json", "user.json"):
        return KIND_OAR_CONFIG if OAR_DIR in low or DAR_DIR in low else None
    if base == "_conditions.txt":
        return KIND_DAR_CONDITIONS
    if base.endswith((".bsa", ".ba2")):
        return KIND_ARCHIVE
    if TOOL_LOG_RE.search(low):
        return KIND_TOOL_LOG
    return None


@dataclass
class Entry:
    """One provider's copy of a path."""

    mod: str
    priority: int
    absolute: Path
    raw_relative: str
    size: int
    sha256: str | None = None


@dataclass
class PathRecord:
    """Every enabled provider that supplies one normalised path."""

    key: str
    display: str
    kind: str
    entries: list[Entry] = field(default_factory=list)

    @property
    def is_conflict(self) -> bool:
        """A real file conflict: two or more enabled mods at the SAME path."""
        return len({e.mod for e in self.entries}) > 1

    @property
    def winner(self) -> Entry:
        return max(self.entries, key=lambda e: e.priority)

    @property
    def losers(self) -> list[Entry]:
        top = self.winner
        return sorted(
            (e for e in self.entries if e is not top), key=lambda e: e.priority, reverse=True
        )

    @property
    def tied(self) -> bool:
        """Two entries at the same MO2 priority: impossible for distinct mods."""
        top = self.winner.priority
        return sum(1 for e in self.entries if e.priority == top) > 1


@dataclass
class LooseIndex:
    """The animation-relevant slice of the reconstructed virtual Data folder."""

    records: dict[str, PathRecord] = field(default_factory=dict)
    per_mod: dict[str, dict[str, int]] = field(default_factory=dict)
    archives: list[dict[str, object]] = field(default_factory=list)
    walk: WalkReport = field(default_factory=WalkReport)
    missing_mod_dirs: list[str] = field(default_factory=list)
    hashed: int = 0
    hash_truncated: bool = False

    def conflicts(self) -> list[PathRecord]:
        return [r for r in self.records.values() if r.is_conflict]

    def of_kind(self, kind: str) -> list[PathRecord]:
        return [r for r in self.records.values() if r.kind == kind]

    def count_kind(self, kind: str) -> int:
        return sum(1 for r in self.records.values() if r.kind == kind)


def build_index(
    instance: Instance,
    profile: Profile,
    budget: Budget,
    clock: Clock | None = None,
    *,
    include_overwrite: bool = True,
    hash_limit: int = 600,
    full_walk: bool = False,
    progress: "Callable[[dict[str, object]], None] | None" = None,
) -> LooseIndex:
    """Walk every enabled provider in ascending priority and index relevant paths."""
    clock = clock or Clock(budget)
    index = LooseIndex()

    providers: list[tuple[str, int, Path]] = []
    for mod in profile.providers:
        try:
            root = resolve_confined(instance.mods_dir, mod.name)
        except sandbox.ConfinementError as exc:
            index.walk.note_skip(mod.name, f"mod folder refused: {exc}")
            continue
        if not root.is_dir():
            index.missing_mod_dirs.append(mod.name)
            continue
        providers.append((mod.name, mod.priority, root))

    if include_overwrite and instance.overwrite_dir.is_dir():
        top = (providers[-1][1] + 1) if providers else 0
        providers.append(("<Overwrite>", top, instance.overwrite_dir))

    for done, (mod_name, priority, root) in enumerate(providers):
        if progress is not None:
            progress({"event": "progress", "phase": "walk", "done": done,
                      "total": len(providers), "mod": mod_name})
        if clock.expired:
            index.walk.note_truncation("time budget exhausted before all mods were walked")
            break
        counts = index.per_mod.setdefault(mod_name, {})
        for relative, absolute in sandbox.walk_files(
            root,
            budget,
            index.walk,
            clock,
            want=lambda rel: classify(rel) is not None,
            prune_dir=None if full_walk else _prune_top_level,
        ):
            kind = classify(relative)
            if kind is None:
                continue
            try:
                size = absolute.stat().st_size
            except OSError as exc:
                index.walk.note_skip(relative, f"size unreadable: {exc.strerror or exc}")
                continue
            counts[kind] = counts.get(kind, 0) + 1

            if kind == KIND_ARCHIVE:
                index.archives.append(
                    {
                        "mod": mod_name,
                        "priority": priority,
                        "path": relative,
                        "bytes": size,
                        # The real location: "<Overwrite>" is not under the mods folder.
                        "absolute": str(absolute),
                        "indexed": False,
                    }
                )
                continue

            key = norm_key(relative)
            record = index.records.get(key)
            if record is None:
                record = PathRecord(key=key, display=relative, kind=kind)
                index.records[key] = record
            record.entries.append(
                Entry(
                    mod=mod_name,
                    priority=priority,
                    absolute=absolute,
                    raw_relative=relative,
                    size=size,
                )
            )

    _hash_conflict_participants(index, budget, hash_limit)
    return index


def _hash_conflict_participants(index: LooseIndex, budget: Budget, hash_limit: int) -> None:
    """Hash only the files involved in a conflict; hashing everything is wasteful."""
    for record in sorted(index.conflicts(), key=lambda r: r.key):
        if record.kind not in HASHABLE_KINDS:
            continue
        for entry in record.entries:
            if index.hashed >= hash_limit:
                index.hash_truncated = True
                return
            try:
                entry.sha256, complete = sandbox.sha256_file(entry.absolute, budget)
                if not complete:
                    entry.sha256 = f"{entry.sha256}~partial"
            except OSError as exc:
                index.walk.note_skip(entry.raw_relative, f"hash failed: {exc.strerror or exc}")
                continue
            index.hashed += 1


# --- Issue construction ----------------------------------------------------------

_SOURCE_MODLIST = "engines/mo2/src/profile.cpp::parse_modlist (verified local reader)"
_SOURCE_MO2_WIKI = "https://github.com/ModOrganizer2/modorganizer/wiki/Mod-information-window"


def _provider_refs(record: PathRecord) -> list[issues.ProviderRef]:
    winner = record.winner
    refs = [
        issues.ProviderRef(
            mod=winner.mod,
            priority=winner.priority,
            path=winner.raw_relative,
            bytes=winner.size,
            sha256=winner.sha256,
            role="winner",
        )
    ]
    refs.extend(
        issues.ProviderRef(
            mod=e.mod,
            priority=e.priority,
            path=e.raw_relative,
            bytes=e.size,
            sha256=e.sha256,
            role="overridden",
        )
        for e in record.losers
    )
    return refs


def conflict_issues(index: LooseIndex, budget: Budget) -> list[issues.Issue]:
    """Build one issue per real same-path conflict, plus informational findings."""
    out: list[issues.Issue] = []

    for record in sorted(index.conflicts(), key=lambda r: r.key):
        if record.kind == KIND_BEHAVIOR:
            # behavior_outputs.behavior_issues owns these, with wording that keeps a
            # behaviour-graph file conflict apart from an OAR rule conflict.
            continue
        winner = record.winner
        losers = record.losers
        identical = bool(winner.sha256) and all(
            e.sha256 and e.sha256 == winner.sha256 for e in losers
        )
        observed = (
            f"{len({e.mod for e in record.entries})} enabled mods supply the same loose path. "
            f"Highest MO2 priority wins the file: {winner.mod} (priority {winner.priority})."
        )
        if identical:
            observed += " All copies share the same SHA-256, so the override changes nothing."
        out.append(
            issues.Issue(
                category=issues.LOOSE_FILE_OVERRIDE,
                subject=record.display,
                what_was_observed=observed,
                why_it_matters=(
                    "The copies are byte-identical, so this override is harmless noise."
                    if identical
                    else "Only the winning copy is deployed to the virtual Data folder. The "
                    "other copies are inert, so a tweak made in a lower-priority mod has "
                    "no effect."
                ),
                action=(
                    "If you expected a different mod to supply this animation, raise that mod "
                    f"above {winner.mod} in Mod Organizer. Reversible: change the order back."
                ),
                confidence=issues.PROVEN,
                runtime_verification=issues.STATIC_FILE_FACT,
                providers=_provider_refs(record),
                source_reference=f"{_SOURCE_MODLIST}; {_SOURCE_MO2_WIKI}",
                detail={
                    "kind": record.kind,
                    "identicalContent": identical,
                    "providerCount": len(record.entries),
                },
            )
        )
        if record.tied:
            out.append(
                issues.Issue(
                    category=issues.PRIORITY_TIE,
                    subject=record.display,
                    what_was_observed="Two providers report the same MO2 priority for this path.",
                    why_it_matters=(
                        "Distinct enabled mods cannot share a priority in a well-formed "
                        "modlist.txt, so the reconstruction is ambiguous here."
                    ),
                    action="Re-check the profile in Mod Organizer; do not act on this row alone.",
                    confidence=issues.UNVERIFIABLE,
                    providers=_provider_refs(record),
                    source_reference=_SOURCE_MODLIST,
                )
            )

    out.extend(_spelling_issues(index))
    out.extend(_similar_filename_issues(index, budget))
    out.extend(_coverage_issues(index))
    return out


def _spelling_issues(index: LooseIndex) -> list[issues.Issue]:
    out: list[issues.Issue] = []
    for record in sorted(index.records.values(), key=lambda r: r.key):
        variance = spelling_variance([e.raw_relative for e in record.entries])
        if variance in ("none", "mixed"):
            continue
        out.append(
            issues.Issue(
                category=issues.SPELLING_VARIANCE,
                subject=record.display,
                what_was_observed=(
                    f"Providers spell this path differently ({variance} difference): "
                    + ", ".join(sorted({e.raw_relative for e in record.entries}))
                ),
                why_it_matters=(
                    "Windows normally treats these as one file, so they collide, but whether "
                    "they do depends on the filesystem and cannot be proven by reading."
                ),
                action="Informational. No change needed unless the override surprises you.",
                confidence=issues.UNVERIFIABLE,
                providers=_provider_refs(record),
                source_reference="Windows path case-insensitivity; NFC normalisation",
            )
        )
    return out


def _similar_filename_issues(index: LooseIndex, budget: Budget) -> list[issues.Issue]:
    """Same basename at different paths is NOT an override (M05); say so explicitly."""
    by_base: dict[str, set[str]] = {}
    for record in index.records.values():
        if record.kind != KIND_ANIMATION:
            continue
        base = record.key.rsplit("/", 1)[-1]
        by_base.setdefault(base, set()).add(record.key)

    out: list[issues.Issue] = []
    for base, keys in sorted(by_base.items()):
        if len(keys) < 2:
            continue
        if len(out) >= max(1, budget.max_html_rows // 4):
            break
        out.append(
            issues.Issue(
                category=issues.SIMILAR_FILENAME,
                subject=base,
                what_was_observed=(
                    f"{len(keys)} different paths end in the same filename, e.g. "
                    + "; ".join(sorted(keys)[:3])
                ),
                why_it_matters=(
                    "Different paths are different files. This is NOT a file override and "
                    "needs no action; it is listed so a name search does not mislead you."
                ),
                action="Informational only.",
                confidence=issues.PROVEN,
                runtime_verification=issues.STATIC_FILE_FACT,
                source_reference=_SOURCE_MO2_WIKI,
                detail={"paths": sorted(keys)[:20], "pathCount": len(keys)},
            )
        )
    return out


def _coverage_issues(index: LooseIndex) -> list[issues.Issue]:
    out: list[issues.Issue] = []
    unindexed = [a for a in index.archives if not a.get("indexed")]
    if unindexed:
        mods = sorted({str(a["mod"]) for a in unindexed})
        out.append(
            issues.Issue(
                category=issues.BSA_COVERAGE_LIMIT,
                subject="BSA/BA2 archive names were not read",
                what_was_observed=(
                    f"{len(unindexed)} archive(s) in {len(mods)} enabled mod(s) may contain "
                    "animations that this scan cannot see."
                ),
                why_it_matters=(
                    "Loose files win over archive contents, so the loose winners above still "
                    "hold, but an animation present ONLY inside an archive is missing from "
                    "this report and must not be assumed absent."
                ),
                action="Treat archive-only animations as out of scope for this run.",
                confidence=issues.PROVEN,
                runtime_verification=issues.STATIC_FILE_FACT,
                source_reference="scan limit declared in loose_winners.build_index",
                detail={
                    "archives": [
                        {k: v for k, v in a.items() if k != "absolute"} for a in unindexed[:50]
                    ],
                    "mods": mods[:50],
                },
            )
        )
    if index.walk.is_truncated or index.hash_truncated:
        reasons = list(index.walk.truncated)
        if index.hash_truncated:
            reasons.append("SHA-256 hashing stopped at its per-run limit")
        out.append(
            issues.Issue(
                category=issues.TRUNCATED,
                subject="scan budget reached",
                what_was_observed="; ".join(reasons),
                why_it_matters="Counts below are partial. Absence of a finding proves nothing.",
                action="Re-run with a larger budget, or narrow the scan, before acting.",
                confidence=issues.PROVEN,
                source_reference="sandbox.Budget",
                detail={"filesSeen": index.walk.files_seen, "dirsSeen": index.walk.dirs_seen},
            )
        )
    if index.missing_mod_dirs:
        out.append(
            issues.Issue(
                category=issues.ACCESS_ERROR,
                subject="enabled mods with no folder on disk",
                what_was_observed=(
                    f"{len(index.missing_mod_dirs)} enabled mod(s) in modlist.txt have no "
                    "directory under the mods folder."
                ),
                why_it_matters="Those mods contribute no files; MO2 may be flagging them as missing.",
                action="Check the mods folder in Mod Organizer. No change is made by this tool.",
                confidence=issues.PROVEN,
                runtime_verification=issues.STATIC_FILE_FACT,
                source_reference=_SOURCE_MODLIST,
                detail={"mods": sorted(index.missing_mod_dirs)[:50]},
            )
        )
    for skip in index.walk.skipped[:20]:
        if "reparse point" not in skip["reason"]:
            continue
        out.append(
            issues.Issue(
                category=issues.ACCESS_ERROR,
                subject=skip["path"],
                what_was_observed=skip["reason"],
                why_it_matters=(
                    "A junction or symlink could point outside the instance, so the scan "
                    "refused to follow it rather than read an unconfined path."
                ),
                action="Informational. Replace the link with a real folder if you need it scanned.",
                confidence=issues.PROVEN,
                source_reference="sandbox.walk_files reparse-point guard",
            )
        )
    return out
