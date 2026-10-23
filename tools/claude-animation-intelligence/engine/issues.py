"""Issue taxonomy and severity policy.

Every finding this lab emits carries an explicit confidence and an explicit
runtime-verification state. The taxonomy deliberately separates what we PROVED
by reading files from what merely MIGHT matter once Skyrim runs.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field

# --- Runtime verification states -------------------------------------------------
# Nothing in this tool can observe the running game, so NOT_VERIFIED_IN_GAME is the
# ceiling for any claim about behaviour. The others narrow *why* it stays unknown.
NOT_VERIFIED_IN_GAME = "NOT_VERIFIED_IN_GAME"
RUNTIME_CONDITION_DEPENDENT = "RUNTIME_CONDITION_DEPENDENT"
STATIC_FILE_FACT = "STATIC_FILE_FACT"

# --- Categories ------------------------------------------------------------------
LOOSE_FILE_OVERRIDE = "LOOSE_FILE_OVERRIDE"
SIMILAR_FILENAME = "SIMILAR_FILENAME"
SPELLING_VARIANCE = "SPELLING_VARIANCE"
STATIC_OVERLAP = "STATIC_OVERLAP"
INTRA_MOD_OVERLAP = "INTRA_MOD_OVERLAP"
PRIORITY_TIE = "PRIORITY_TIE"
CONFIG_ERROR = "CONFIG_ERROR"
INVALID_CONFIG = "INVALID_CONFIG"
UNKNOWN_EXTENSION = "UNKNOWN_EXTENSION"
UNSUPPORTED_FIELD = "UNSUPPORTED_FIELD"
UNVERIFIED_FORMAT = "UNVERIFIED_FORMAT"
MISSING_REFERENCE = "MISSING_REFERENCE"
OUTPUT_PRESENT_UNVERIFIED = "OUTPUT_PRESENT_UNVERIFIED"
MIGRATION_CANDIDATE = "MIGRATION_CANDIDATE"
DISABLED_BY_CONFIG = "DISABLED_BY_CONFIG"
USER_JSON_OVERRIDE = "USER_JSON_OVERRIDE"
BSA_COVERAGE_LIMIT = "BSA_COVERAGE_LIMIT"
ARCHIVE_ONLY_ANIMATION = "ARCHIVE_ONLY_ANIMATION"
LOOSE_OVERRIDES_ARCHIVE = "LOOSE_OVERRIDES_ARCHIVE"
ARCHIVE_PATH_OVERLAP = "ARCHIVE_PATH_OVERLAP"
ACCESS_ERROR = "ACCESS_ERROR"
TRUNCATED = "TRUNCATED"

# --- Confidence ------------------------------------------------------------------
# PROVEN      : re-readable from the files on disk; a second run must agree.
# LIKELY      : the files say so, but a mechanism we cannot read (BSA, runtime
#               condition, SKSE extension) could change the outcome.
# UNVERIFIABLE: we recorded the data but cannot judge it with what we can read.
PROVEN = "proven"
LIKELY = "likely"
UNVERIFIABLE = "unverifiable"

SEVERITIES = ("info", "low", "medium", "high")

# ---------------------------------------------------------------------------------
# SEVERITY POLICY -- the one deliberately tunable table in this tool.
#
# Rationale for the defaults: a finding is ranked "high" only when a user acting on
# it cannot be misled, i.e. the evidence is PROVEN *and* the consequence is a file
# the game will never see. Anything whose outcome depends on OAR conditions, BSA
# contents or a Pandora patch is capped at "medium", because telling a user to
# reorder mods over a guess is worse than saying nothing. Change a row here and
# every report follows; no severity is hardcoded anywhere else.
# ---------------------------------------------------------------------------------
SEVERITY_POLICY: dict[str, dict[str, str]] = {
    LOOSE_FILE_OVERRIDE: {PROVEN: "high", LIKELY: "medium", UNVERIFIABLE: "low"},
    STATIC_OVERLAP: {PROVEN: "medium", LIKELY: "medium", UNVERIFIABLE: "low"},
    # Submods of ONE mod competing for a clip are almost always the author's own
    # randomised variants, so they are listed but never ranked as a concern.
    INTRA_MOD_OVERLAP: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    PRIORITY_TIE: {PROVEN: "medium", LIKELY: "low", UNVERIFIABLE: "low"},
    INVALID_CONFIG: {PROVEN: "high", LIKELY: "medium", UNVERIFIABLE: "low"},
    CONFIG_ERROR: {PROVEN: "high", LIKELY: "medium", UNVERIFIABLE: "low"},
    MISSING_REFERENCE: {PROVEN: "medium", LIKELY: "low", UNVERIFIABLE: "low"},
    UNKNOWN_EXTENSION: {PROVEN: "low", LIKELY: "low", UNVERIFIABLE: "low"},
    UNSUPPORTED_FIELD: {PROVEN: "low", LIKELY: "low", UNVERIFIABLE: "info"},
    UNVERIFIED_FORMAT: {PROVEN: "low", LIKELY: "low", UNVERIFIABLE: "info"},
    MIGRATION_CANDIDATE: {PROVEN: "low", LIKELY: "low", UNVERIFIABLE: "info"},
    USER_JSON_OVERRIDE: {PROVEN: "medium", LIKELY: "low", UNVERIFIABLE: "info"},
    DISABLED_BY_CONFIG: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    OUTPUT_PRESENT_UNVERIFIED: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    SIMILAR_FILENAME: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    SPELLING_VARIANCE: {PROVEN: "low", LIKELY: "info", UNVERIFIABLE: "info"},
    BSA_COVERAGE_LIMIT: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    ARCHIVE_ONLY_ANIMATION: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    LOOSE_OVERRIDES_ARCHIVE: {PROVEN: "info", LIKELY: "info", UNVERIFIABLE: "info"},
    # Between archives the winner follows plugin load order, which this tool does not
    # resolve; worth a look, never a verdict.
    ARCHIVE_PATH_OVERLAP: {PROVEN: "low", LIKELY: "low", UNVERIFIABLE: "low"},
    ACCESS_ERROR: {PROVEN: "low", LIKELY: "low", UNVERIFIABLE: "low"},
    TRUNCATED: {PROVEN: "medium", LIKELY: "medium", UNVERIFIABLE: "medium"},
}


def severity_for(category: str, confidence: str) -> str:
    """Look up a severity. An unknown category is deliberately ranked ``low``."""
    return SEVERITY_POLICY.get(category, {}).get(confidence, "low")


@dataclass(frozen=True)
class ProviderRef:
    """One mod that supplies a path, with the MO2 priority that decided its rank."""

    mod: str
    priority: int
    path: str
    bytes: int | None = None
    sha256: str | None = None
    role: str = "provider"  # "winner" | "overridden" | "provider"

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {
            "mod": self.mod,
            "priority": self.priority,
            "path": self.path,
            "role": self.role,
        }
        if self.bytes is not None:
            out["bytes"] = self.bytes
        if self.sha256 is not None:
            out["sha256"] = self.sha256
        return out


@dataclass
class Issue:
    """A single finding. ``id`` is stable across runs for the same evidence."""

    category: str
    subject: str
    what_was_observed: str
    why_it_matters: str
    action: str
    confidence: str = LIKELY
    runtime_verification: str = NOT_VERIFIED_IN_GAME
    providers: list[ProviderRef] = field(default_factory=list)
    source_reference: str = ""
    detail: dict[str, object] = field(default_factory=dict)

    @property
    def severity(self) -> str:
        return severity_for(self.category, self.confidence)

    @property
    def id(self) -> str:
        seed = "|".join([self.category, self.subject, *(p.mod for p in self.providers)])
        return f"{self.category.lower()}-{hashlib.sha256(seed.encode('utf-8')).hexdigest()[:12]}"

    def to_json(self) -> dict[str, object]:
        return {
            "id": self.id,
            "category": self.category,
            "severity": self.severity,
            "confidence": self.confidence,
            "subject": self.subject,
            "whatWasObserved": self.what_was_observed,
            "providers": [p.to_json() for p in self.providers],
            "whyItMatters": self.why_it_matters,
            "action": self.action,
            "runtimeVerification": self.runtime_verification,
            "sourceReference": self.source_reference,
            "detail": self.detail,
        }


def sort_issues(issues: list[Issue]) -> list[Issue]:
    """Most severe first, then deterministic by id so two runs agree byte for byte."""
    rank = {name: index for index, name in enumerate(reversed(SEVERITIES))}
    return sorted(issues, key=lambda i: (rank.get(i.severity, 99), i.category, i.id))
