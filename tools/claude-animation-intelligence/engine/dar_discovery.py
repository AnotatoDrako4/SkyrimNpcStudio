"""Legacy Dynamic Animation Replacer discovery and a conservative lexer.

Grammar evidence, from the OAR source that still parses DAR packs:

* ``Conditions.cpp::CreateConditionFromString`` -- a line starting with ``;`` is a
  comment; a leading ``NOT`` negates; the function name runs up to the first
  space or ``(``; the argument is the text between ``(`` and the first ``)``; a
  line with no space or ``(`` is treated as a bare condition name with no
  argument; an unrecognised name produces an ``InvalidCondition``.
* ``Parsing.cpp::ConditionsTxtFile::GetConditions`` -- a line ending in ``OR``
  opens an OR block, and lines are otherwise AND-joined.
* ``Conditions.cpp::CorrectLegacyConditionName`` -- ``IsEquippedShout`` is an
  alias of ``IsEquippedPower``.
* ``Parsing.cpp`` lines 895 / 1874 -- ``_CustomConditions`` is the reserved
  directory name; the priority is the numeric subdirectory name.

The lexer reports syntax it cannot accept. It never rewrites a rule, and it
never writes a converted config into the installation.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from . import issues, sandbox
from .loose_winners import KIND_DAR_CONDITIONS, LooseIndex
from .oar_discovery import DAR_MARKER, KNOWN_CONDITIONS, LEGACY_ONLY_CONDITIONS, OarCatalog
from .sandbox import Budget

CUSTOM_CONDITIONS_DIR = "_customconditions"
LEGACY_ALIASES = {
    "IsEquippedShout": "IsEquippedPower",
    "CurrentPackageProcedureType": "CurrentPackageType",
}
FORMID_RE = re.compile(r"^(0x)?[0-9a-fA-F]{1,8}$")

SOURCE_LEXER = (
    "ersh1/OpenAnimationReplacer@main src/Conditions.cpp::CreateConditionFromString; "
    "src/Parsing.cpp::ConditionsTxtFile::GetConditions"
)


@dataclass
class ConditionLine:
    """One lexed line of a ``_conditions.txt`` file."""

    number: int
    raw: str
    kind: str  # "comment" | "blank" | "condition" | "error"
    name: str = ""
    argument: str = ""
    negated: bool = False
    or_continuation: bool = False
    recognised: bool = True
    legacy_only: bool = False
    error: str = ""

    def to_json(self) -> dict[str, object]:
        out: dict[str, object] = {"line": self.number, "kind": self.kind}
        if self.kind == "condition":
            out.update(
                {
                    "name": self.name,
                    "argument": self.argument,
                    "negated": self.negated,
                    "orContinuation": self.or_continuation,
                    "recognised": self.recognised,
                    "legacyOnly": self.legacy_only,
                }
            )
        if self.error:
            out["error"] = self.error
            out["raw"] = self.raw
        return out


def lex_conditions(text: str) -> list[ConditionLine]:
    """Lex a ``_conditions.txt`` body. Unsupported syntax is flagged, never fixed."""
    out: list[ConditionLine] = []
    for number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            out.append(ConditionLine(number, raw_line, "blank"))
            continue
        if line.startswith(";"):
            out.append(ConditionLine(number, raw_line, "comment"))
            continue

        or_continuation = line.endswith("OR")
        body = line[:-2].strip() if or_continuation else line

        negated = body.startswith("NOT")
        if negated:
            body = body[3:].strip()

        if not body:
            out.append(
                ConditionLine(
                    number,
                    raw_line,
                    "error",
                    negated=negated,
                    or_continuation=or_continuation,
                    recognised=False,
                    error="line has a NOT or OR keyword but no condition function",
                )
            )
            continue

        open_paren = body.find("(")
        close_paren = body.find(")")
        if open_paren != -1 and close_paren == -1:
            out.append(
                ConditionLine(
                    number,
                    raw_line,
                    "error",
                    name=body[:open_paren].strip(),
                    negated=negated,
                    or_continuation=or_continuation,
                    recognised=False,
                    error="unbalanced parenthesis: no closing ')' on this line",
                )
            )
            continue
        if close_paren != -1 and open_paren != -1 and close_paren < open_paren:
            out.append(
                ConditionLine(
                    number,
                    raw_line,
                    "error",
                    negated=negated,
                    or_continuation=or_continuation,
                    recognised=False,
                    error="closing parenthesis appears before the opening one",
                )
            )
            continue

        if open_paren == -1 and " " not in body:
            name, argument = body, ""
        else:
            split_at = min(p for p in (open_paren, body.find(" ")) if p != -1)
            name = body[:split_at].strip()
            argument = body[open_paren + 1 : close_paren].strip() if open_paren != -1 else ""

        canonical = LEGACY_ALIASES.get(name, name)
        recognised = canonical in KNOWN_CONDITIONS
        out.append(
            ConditionLine(
                number,
                raw_line,
                "condition",
                name=name,
                argument=argument,
                negated=negated,
                or_continuation=or_continuation,
                recognised=recognised,
                legacy_only=canonical in LEGACY_ONLY_CONDITIONS,
                error=(
                    ""
                    if recognised
                    else f"'{name}' is not a condition OAR registers; it would become an "
                    "InvalidCondition"
                ),
            )
        )
    return out


@dataclass
class DarRuleSet:
    """One legacy DAR rule directory."""

    data_path: str
    provider_mod: str
    variant: str  # "_CustomConditions" | "actor-base" | "unknown"
    priority: int | None = None
    plugin_name: str = ""
    form_id: str = ""
    lines: list[ConditionLine] = field(default_factory=list)
    animation_files: list[str] = field(default_factory=list)
    conditions_sha256: str = ""
    errors: list[str] = field(default_factory=list)

    @property
    def syntax_errors(self) -> list[ConditionLine]:
        return [line for line in self.lines if line.kind == "error" or not line.recognised]

    @property
    def condition_count(self) -> int:
        return sum(1 for line in self.lines if line.kind == "condition")

    def to_json(self) -> dict[str, object]:
        return {
            "dataPath": self.data_path,
            "providerMod": self.provider_mod,
            "variant": self.variant,
            "priority": self.priority,
            "pluginName": self.plugin_name,
            "formId": self.form_id,
            "conditionCount": self.condition_count,
            "lines": [line.to_json() for line in self.lines if line.kind != "blank"],
            "syntaxErrorCount": len(self.syntax_errors),
            "animationFiles": self.animation_files,
            "conditionsSha256": self.conditions_sha256,
            "errors": self.errors,
        }


@dataclass
class DarCatalog:
    rule_sets: list[DarRuleSet] = field(default_factory=list)
    truncated: bool = False

    @property
    def present(self) -> bool:
        return bool(self.rule_sets)


def _describe_variant(data_path: str) -> tuple[str, int | None, str, str]:
    """Work out which DAR layout a rule directory uses."""
    parts = data_path.replace("\\", "/").split("/")
    lowered = [p.lower() for p in parts]
    if DAR_MARKER not in lowered:
        return "unknown", None, "", ""
    marker = lowered.index(DAR_MARKER)
    rest = parts[marker + 1 :]
    if not rest:
        return "unknown", None, "", ""
    if rest[0].lower() == CUSTOM_CONDITIONS_DIR:
        priority = None
        if len(rest) > 1 and rest[1].lstrip("-").isdigit():
            priority = int(rest[1])
        return "_CustomConditions", priority, "", ""
    if len(rest) >= 2 and FORMID_RE.match(rest[1]):
        return "actor-base", None, rest[0], rest[1]
    return "unknown", None, rest[0], ""


def discover(index: LooseIndex, budget: Budget) -> DarCatalog:
    """Find legacy DAR rule directories among the winning ``_conditions.txt`` files."""
    catalog = DarCatalog()
    records = [
        r for r in index.records.values() if r.kind == KIND_DAR_CONDITIONS and DAR_MARKER in r.key
    ]

    for record in sorted(records, key=lambda r: r.key):
        if len(catalog.rule_sets) >= budget.max_submods:
            catalog.truncated = True
            break
        directory = record.display.rsplit("/", 1)[0]
        variant, priority, plugin_name, form_id = _describe_variant(directory)
        rule_set = DarRuleSet(
            data_path=directory,
            provider_mod=record.winner.mod,
            variant=variant,
            priority=priority,
            plugin_name=plugin_name,
            form_id=form_id,
        )
        try:
            text = sandbox.read_text_confined(record.winner.absolute, budget)
        except sandbox.SandboxError as exc:
            rule_set.errors.append(str(exc))
            catalog.rule_sets.append(rule_set)
            continue
        rule_set.conditions_sha256 = sandbox.sha256_text(text)
        rule_set.lines = lex_conditions(text)

        prefix = sandbox.norm_key(directory) + "/"
        rule_set.animation_files = sorted(
            other.key[len(prefix) :]
            for other in index.records.values()
            if other.key.startswith(prefix) and other.key.endswith(".hkx")
        )
        catalog.rule_sets.append(rule_set)

    return catalog


def _provider_ref(rule_set: DarRuleSet) -> issues.ProviderRef:
    return issues.ProviderRef(
        mod=rule_set.provider_mod,
        priority=rule_set.priority if rule_set.priority is not None else -1,
        path=rule_set.data_path,
        role="dar-rule",
    )


def dar_issues(catalog: DarCatalog, oar: OarCatalog, budget: Budget) -> list[issues.Issue]:
    """Report DAR presence, per-line syntax faults and DAR-to-OAR migration candidates."""
    out: list[issues.Issue] = []
    if not catalog.present:
        return out

    out.append(
        issues.Issue(
            category=issues.MIGRATION_CANDIDATE,
            subject="legacy Dynamic Animation Replacer rules are present",
            what_was_observed=(
                f"{len(catalog.rule_sets)} legacy DAR rule directory/directories were found "
                f"across {len({r.provider_mod for r in catalog.rule_sets})} enabled mod(s)."
            ),
            why_it_matters=(
                "DAR is superseded by Open Animation Replacer, which still reads DAR folders. "
                "Their presence is not a fault and needs no action by itself."
            ),
            action="Informational. No conversion is performed or suggested automatically.",
            confidence=issues.PROVEN,
            runtime_verification=issues.STATIC_FILE_FACT,
            source_reference=SOURCE_LEXER,
            detail={
                "variants": sorted({r.variant for r in catalog.rule_sets}),
                "mods": sorted({r.provider_mod for r in catalog.rule_sets})[:50],
            },
        )
    )

    for rule_set in catalog.rule_sets:
        if len(out) >= budget.max_html_rows:
            break
        faults = rule_set.syntax_errors
        if faults:
            detail_lines = [
                f"line {line.number}: {line.error or 'unrecognised condition'}"
                for line in faults[:20]
            ]
            out.append(
                issues.Issue(
                    category=issues.CONFIG_ERROR,
                    subject=f"{rule_set.data_path}/_conditions.txt",
                    what_was_observed="; ".join(detail_lines),
                    why_it_matters=(
                        "A line OAR cannot parse becomes an InvalidCondition, so this rule "
                        "set may never match. The exact line number is given so it can be "
                        "checked by hand."
                    ),
                    action=(
                        "Report the malformed line to the mod author. This tool does not "
                        "rewrite condition files."
                    ),
                    confidence=issues.PROVEN,
                    runtime_verification=issues.STATIC_FILE_FACT,
                    providers=[_provider_ref(rule_set)],
                    source_reference=SOURCE_LEXER,
                    detail={
                        "faults": [line.to_json() for line in faults[:20]],
                        "faultCount": len(faults),
                    },
                )
            )
        if rule_set.errors:
            out.append(
                issues.Issue(
                    category=issues.ACCESS_ERROR,
                    subject=f"{rule_set.data_path}/_conditions.txt",
                    what_was_observed="; ".join(rule_set.errors),
                    why_it_matters="The rule could not be read, so it was not analysed at all.",
                    action="Check file permissions or encoding; nothing was modified.",
                    confidence=issues.PROVEN,
                    providers=[_provider_ref(rule_set)],
                    source_reference=SOURCE_LEXER,
                )
            )

    out.extend(_migration_issues(catalog, oar, budget))
    return out


def _migration_issues(catalog: DarCatalog, oar: OarCatalog, budget: Budget) -> list[issues.Issue]:
    """Flag a DAR rule whose animation files are also shipped by an OAR submod.

    Labelled MIGRATION_CANDIDATE on purpose: the same filenames are strong
    evidence that the author migrated the pack, but proving the OAR submod
    actually supersedes the DAR rule needs the running game.
    """
    oar_by_file: dict[str, list[str]] = {}
    for submod in oar.active:
        for animation in submod.animation_files:
            oar_by_file.setdefault(animation.rsplit("/", 1)[-1], []).append(submod.data_path)

    out: list[issues.Issue] = []
    for rule_set in catalog.rule_sets:
        if len(out) >= max(1, budget.max_html_rows // 4):
            break
        basenames = {a.rsplit("/", 1)[-1] for a in rule_set.animation_files}
        overlapping = sorted(b for b in basenames if b in oar_by_file)
        if not overlapping:
            continue
        submods = sorted({s for b in overlapping for s in oar_by_file[b]})
        out.append(
            issues.Issue(
                category=issues.MIGRATION_CANDIDATE,
                subject=rule_set.data_path,
                what_was_observed=(
                    f"{len(overlapping)} animation file name(s) in this DAR rule also ship in "
                    f"{len(submods)} OAR submod(s), e.g. " + ", ".join(overlapping[:5])
                ),
                why_it_matters=(
                    "This looks like a pack that was migrated from DAR to OAR, leaving both "
                    "copies installed. It is NOT proof that the rules are both active or "
                    "that they duplicate each other: matching filenames are not matching "
                    "replacement targets."
                ),
                action=(
                    "Check the mod page for a DAR-to-OAR migration note before removing "
                    "anything. No file is changed by this tool; any migration template it "
                    "writes is marked NOT_APPLIED."
                ),
                confidence=issues.UNVERIFIABLE,
                runtime_verification=issues.NOT_VERIFIED_IN_GAME,
                providers=[_provider_ref(rule_set)],
                source_reference=SOURCE_LEXER,
                detail={"sharedFilenames": overlapping[:20], "oarSubmods": submods[:20]},
            )
        )
    return out


def summarise(catalog: DarCatalog) -> dict[str, object]:
    return {
        "ruleSets": len(catalog.rule_sets),
        "present": catalog.present,
        "byVariant": {
            variant: sum(1 for r in catalog.rule_sets if r.variant == variant)
            for variant in sorted({r.variant for r in catalog.rule_sets})
        },
        "withSyntaxErrors": sum(1 for r in catalog.rule_sets if r.syntax_errors),
        "truncated": catalog.truncated,
    }
