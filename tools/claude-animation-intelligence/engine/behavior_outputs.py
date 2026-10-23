"""Pandora / Nemesis / FNIS output observation. Nothing is ever executed.

The patchers are never run, no DLL or executable is loaded, and no file is
injected. We look at generated behaviour files and at logs that already exist.

Tool attribution requires evidence: a filename pattern the tool itself produces,
a directory the tool owns, or a line in a log. A mod merely being named after a
patcher is not evidence.

PANDORA_OUTPUT_PRESENT is not PANDORA_PATCH_SUCCESS, and a behaviour file
conflict is a FILE conflict, never an OAR rule conflict.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from . import hkx_probe, issues, sandbox
from .loose_winners import KIND_BEHAVIOR, KIND_TOOL_LOG, LooseIndex
from .sandbox import Budget

# Filename and directory patterns each tool actually produces.
FNIS_FILE_RE = re.compile(r"(^|/)FNIS_.*_Behavior\.hkx$", re.IGNORECASE)
FNIS_DIR_RE = re.compile(r"/tools/generatefnisfor|/fnis(behavior|_output)", re.IGNORECASE)
NEMESIS_DIR_RE = re.compile(r"/nemesis_engine/|/nemesis(_| )?output/", re.IGNORECASE)
PANDORA_DIR_RE = re.compile(r"/pandora[^/]*/|/pandora(_| )?output/", re.IGNORECASE)
MASTER_BEHAVIOR_RE = re.compile(r"(^|/)(0_master|defaultmale|defaultfemale)\.hkx$", re.IGNORECASE)

# Markers that may appear inside a log the tool wrote.
LOG_MARKERS = {
    "Pandora": re.compile(r"pandora\s*behaviour\s*engine|pandora engine", re.IGNORECASE),
    "Nemesis": re.compile(r"nemesis\s*(unlimited\s*)?behavior\s*engine", re.IGNORECASE),
    "FNIS": re.compile(r"fores new idles|\bFNIS\b", re.IGNORECASE),
}

SOURCE_PANDORA = "https://github.com/Monitor221hz/Pandora-Behaviour-Engine-Plus"


@dataclass
class ToolEvidence:
    """Why we believe a given patcher touched this installation."""

    tool: str
    kind: str  # "filename" | "directory" | "log-line"
    path: str
    mod: str
    excerpt: str = ""

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {
            "tool": self.tool,
            "evidenceKind": self.kind,
            "path": self.path,
            "mod": self.mod,
        }
        if self.excerpt:
            out["excerpt"] = self.excerpt
        return out


@dataclass
class BehaviorReport:
    behavior_files: int = 0
    by_mod: dict[str, int] = field(default_factory=dict)
    conflicts: int = 0
    master_files: list[str] = field(default_factory=list)
    evidence: list[ToolEvidence] = field(default_factory=list)
    logs_seen: int = 0
    probes: dict[str, dict[str, object]] = field(default_factory=dict)

    @property
    def tools_with_evidence(self) -> list[str]:
        return sorted({e.tool for e in self.evidence})

    def to_json(self) -> dict[str, object]:
        return {
            "behaviorFiles": self.behavior_files,
            "behaviorFileConflicts": self.conflicts,
            "byMod": dict(sorted(self.by_mod.items())),
            "masterBehaviorFiles": self.master_files,
            "logsSeen": self.logs_seen,
            "toolsWithEvidence": self.tools_with_evidence,
            "evidence": [e.to_json() for e in self.evidence],
            "headerProbes": self.probes,
            "note": (
                "Generated behaviour files were observed, not executed. Their presence does "
                "not prove the patcher ran successfully or that the behaviour graph is valid."
            ),
        }


def _attribute(relative: str) -> str | None:
    """Name the tool a path belongs to, or ``None`` when there is no evidence."""
    path = "/" + relative.replace("\\", "/")
    if FNIS_FILE_RE.search(path) or FNIS_DIR_RE.search(path):
        return "FNIS"
    if NEMESIS_DIR_RE.search(path):
        return "Nemesis"
    if PANDORA_DIR_RE.search(path):
        return "Pandora"
    return None


def detect(index: LooseIndex, budget: Budget, *, probe_limit: int = 40) -> BehaviorReport:
    """Observe generated behaviour files and existing logs, without running anything."""
    report = BehaviorReport()

    for record in sorted(index.records.values(), key=lambda r: r.key):
        if record.kind != KIND_BEHAVIOR:
            continue
        report.behavior_files += 1
        if record.is_conflict:
            report.conflicts += 1
        for entry in record.entries:
            report.by_mod[entry.mod] = report.by_mod.get(entry.mod, 0) + 1
        if MASTER_BEHAVIOR_RE.search("/" + record.display):
            report.master_files.append(record.display)

        tool = _attribute(record.display)
        if tool:
            report.evidence.append(
                ToolEvidence(
                    tool=tool,
                    kind="filename" if FNIS_FILE_RE.search("/" + record.display) else "directory",
                    path=record.display,
                    mod=record.winner.mod,
                )
            )
        if len(report.probes) < probe_limit:
            report.probes[record.display] = hkx_probe.probe(record.winner.absolute).to_json()

    for record in sorted(index.records.values(), key=lambda r: r.key):
        if record.kind != KIND_TOOL_LOG:
            continue
        report.logs_seen += 1
        try:
            text = sandbox.read_text_confined(record.winner.absolute, budget, max_bytes=1024 * 1024)
        except sandbox.SandboxError:
            continue
        head = "\n".join(text.splitlines()[:40])
        for tool, pattern in LOG_MARKERS.items():
            match = pattern.search(head)
            if not match:
                continue
            line = next(
                (ln.strip() for ln in head.splitlines() if pattern.search(ln)), match.group(0)
            )
            report.evidence.append(
                ToolEvidence(
                    tool=tool,
                    kind="log-line",
                    path=record.display,
                    mod=record.winner.mod,
                    excerpt=line[:200],
                )
            )

    return report


def behavior_issues(report: BehaviorReport, index: LooseIndex) -> list[issues.Issue]:
    """State presence or absence, and keep behaviour conflicts separate from OAR."""
    out: list[issues.Issue] = []

    if report.behavior_files == 0 and report.logs_seen == 0:
        out.append(
            issues.Issue(
                category=issues.OUTPUT_PRESENT_UNVERIFIED,
                subject="no behaviour-engine output found",
                what_was_observed=(
                    "No loose behaviour .hkx files and no Pandora/Nemesis/FNIS logs were found "
                    "in the enabled mods of this profile."
                ),
                why_it_matters=(
                    "This is an ABSENCE state, not an error. The output may live inside a BSA, "
                    "in the Overwrite folder outside this scan, or the profile may simply not "
                    "need a behaviour patcher."
                ),
                action="No action. Absence here is not a fault.",
                confidence=issues.PROVEN,
                runtime_verification=issues.STATIC_FILE_FACT,
                source_reference=SOURCE_PANDORA,
                detail={"state": "ABSENT"},
            )
        )
        return out

    out.append(
        issues.Issue(
            category=issues.OUTPUT_PRESENT_UNVERIFIED,
            subject="behaviour-engine output is present",
            what_was_observed=(
                f"{report.behavior_files} loose behaviour .hkx file(s) across "
                f"{len(report.by_mod)} enabled mod(s); {report.logs_seen} patcher log(s). "
                + (
                    "Tools with evidence: " + ", ".join(report.tools_with_evidence)
                    if report.tools_with_evidence
                    else "No tool could be attributed from filenames, directories or logs."
                )
            ),
            why_it_matters=(
                "The files exist, nothing more. Whether the patch run succeeded, whether the "
                "behaviour graph is consistent, and whether animations registered correctly "
                "all require running the patcher and the game. Pandora patches behaviour "
                "graphs; it does not evaluate OAR rules, and its priorities are unrelated to "
                "MO2 mod order or OAR submod priority."
            ),
            action=(
                "If animations are missing in game, re-run your behaviour patcher yourself. "
                "This tool never runs it."
            ),
            confidence=issues.PROVEN,
            runtime_verification=issues.NOT_VERIFIED_IN_GAME,
            source_reference=SOURCE_PANDORA,
            detail=report.to_json(),
        )
    )

    for record in sorted(index.records.values(), key=lambda r: r.key):
        if record.kind != KIND_BEHAVIOR or not record.is_conflict:
            continue
        winner = record.winner
        out.append(
            issues.Issue(
                category=issues.LOOSE_FILE_OVERRIDE,
                subject=record.display,
                what_was_observed=(
                    f"{len({e.mod for e in record.entries})} enabled mods supply this behaviour "
                    f"file. MO2 file priority gives it to {winner.mod} (priority "
                    f"{winner.priority})."
                ),
                why_it_matters=(
                    "This is a FILE conflict between generated behaviour graphs, not an OAR "
                    "rule conflict. Two patcher outputs overwriting each other usually means "
                    "one run is stale and should be regenerated, which only you can do."
                ),
                action=(
                    "Re-run your behaviour patcher so a single, current output wins, then "
                    "re-scan. Nothing is regenerated by this tool."
                ),
                confidence=issues.PROVEN,
                runtime_verification=issues.NOT_VERIFIED_IN_GAME,
                providers=[
                    issues.ProviderRef(
                        mod=e.mod,
                        priority=e.priority,
                        path=e.raw_relative,
                        bytes=e.size,
                        sha256=e.sha256,
                        role="winner" if e is winner else "overridden",
                    )
                    for e in [winner, *record.losers]
                ],
                source_reference=SOURCE_PANDORA,
                detail={"kind": "behavior_hkx", "notAnOarRuleConflict": True},
            )
        )

    return out
