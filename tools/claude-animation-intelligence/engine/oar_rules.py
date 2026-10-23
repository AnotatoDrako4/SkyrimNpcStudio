"""Conservative static analysis of OAR rules.

What this module will say:
  * two active submods replace the same animation path (STATIC_OVERLAP),
  * their priorities differ, tie, or one is disabled by config,
  * a condition name is not built into OAR, so an extension supplies it,
  * a referenced plugin is not in the profile's plugin list.

What it refuses to say:
  * which submod wins at runtime. Conditions depend on race, equipment, weather,
    combat state, random rolls and the in-game OAR menu, none of which can be
    observed by reading files. There is no OAR_WINNER_CONFIRMED verdict here.
"""

from __future__ import annotations

from . import issues
from .mo2_profile import BASE_MASTERS, Profile
from .oar_discovery import (
    ALWAYS_STATIC_IF_NO_CONDITIONS,
    CUSTOM_CONDITION_REQUIRES_PLUGIN,
    DISABLED_BY_CONFIG,
    INVALID_CONFIG,
    OarCatalog,
    SubMod,
)
from .sandbox import Budget, norm_key

SOURCE_PARSING = (
    "ersh1/OpenAnimationReplacer@main src/Parsing.cpp (DeserializeSubMod, ParseModSubdirectory)"
)
SOURCE_CONDITIONS = "ersh1/OpenAnimationReplacer@main src/Conditions.h (built-in condition names)"
SOURCE_CONDITION_API = (
    "https://github.com/ersh1/OpenAnimationReplacer/blob/main/src/API/"
    "OpenAnimationReplacerAPI-Conditions.h"
)

RUNTIME_CAVEAT = (
    "OAR picks a replacement at runtime by evaluating each candidate's conditions in "
    "priority order, and the in-game OAR menu can disable a submod, so the winner "
    "cannot be determined by reading files."
)


def _targets(submod: SubMod) -> list[str]:
    """The original animation paths a submod replaces (variants collapsed, disabled
    animations removed). replacementAnimDatas are settings, never targets: using them
    as targets once hid overlaps between submods that did and did not carry them."""
    return [norm_key(t) for t in submod.targets]


def _submod_label(submod: SubMod) -> str:
    group = submod.group_name or submod.group_path.rsplit("/", 1)[-1]
    leaf = submod.name or submod.data_path.rsplit("/", 1)[-1]
    return f"{group} / {leaf}"


def _provider_ref(submod: SubMod, role: str = "provider") -> issues.ProviderRef:
    return issues.ProviderRef(
        mod=submod.provider_mod,
        priority=submod.priority if submod.priority is not None else -1,
        path=submod.data_path,
        role=role,
    )


def overlap_issues(catalog: OarCatalog, budget: Budget) -> list[issues.Issue]:
    """One issue per animation path claimed by two or more active submods."""
    by_target: dict[str, list[SubMod]] = {}
    for submod in catalog.active:
        for target in _targets(submod):
            by_target.setdefault(target, []).append(submod)

    out: list[issues.Issue] = []
    for target, claimants in sorted(by_target.items()):
        if len(claimants) < 2:
            continue
        if len(out) >= budget.max_html_rows:
            break
        ordered = sorted(claimants, key=lambda s: (-(s.priority or 0), s.data_path))
        top_priority = ordered[0].priority
        tied = [s for s in ordered if s.priority == top_priority]
        unconditional = [s for s in ordered if s.classification == ALWAYS_STATIC_IF_NO_CONDITIONS]

        if len(tied) > 1:
            out.append(
                issues.Issue(
                    category=issues.PRIORITY_TIE,
                    subject=target,
                    what_was_observed=(
                        f"{len(tied)} active submods claim this animation path at the SAME "
                        f"priority {top_priority}: "
                        + "; ".join(_submod_label(s) for s in tied)
                    ),
                    why_it_matters=(
                        "With equal priority there is no documented tie-break we can read, "
                        "so no evaluation order can be stated. " + RUNTIME_CAVEAT
                    ),
                    action=(
                        "Give one of these submods a different priority in its config.json if "
                        "you want a deterministic order. This tool changes nothing."
                    ),
                    confidence=issues.UNVERIFIABLE,
                    runtime_verification=issues.RUNTIME_CONDITION_DEPENDENT,
                    providers=[_provider_ref(s) for s in tied],
                    source_reference=SOURCE_PARSING,
                    detail={"submods": [s.data_path for s in tied], "priority": top_priority},
                )
            )
            continue

        provider_mods = sorted({s.provider_mod for s in ordered})
        cross_mod = len(provider_mods) > 1
        confidence = issues.LIKELY if len(unconditional) >= 2 else issues.UNVERIFIABLE
        observed = (
            f"{len(claimants)} active submods replace this animation path. Evaluation order "
            "by OAR priority: "
            + " > ".join(f"{_submod_label(s)} (priority {s.priority})" for s in ordered)
        )
        if len(unconditional) >= 2:
            observed += (
                f" {len(unconditional)} of them declare no conditions at all, so the "
                "highest-priority unconditional submod would normally apply."
            )
        if cross_mod:
            why = (
                f"{len(provider_mods)} different mods compete for this clip, so which one "
                "you see depends on how their conditions and priorities interleave. This "
                "row lets a surprising animation be traced to its pack. " + RUNTIME_CAVEAT
            )
        else:
            why = (
                f"Every candidate comes from the same mod ({provider_mods[0]}), which almost "
                "always means the author's own randomised or conditional variants. Listed "
                "for completeness; not a cross-mod concern. " + RUNTIME_CAVEAT
            )
        out.append(
            issues.Issue(
                category=issues.STATIC_OVERLAP if cross_mod else issues.INTRA_MOD_OVERLAP,
                subject=target,
                what_was_observed=observed,
                why_it_matters=why,
                action=(
                    "If the wrong animation plays, adjust the priority of the submod you want "
                    "in its own config.json, or disable one submod from the in-game OAR menu. "
                    "Both are reversible; this tool applies neither."
                ),
                confidence=confidence,
                runtime_verification=issues.RUNTIME_CONDITION_DEPENDENT,
                providers=[
                    _provider_ref(s, "candidate-highest" if s is ordered[0] else "candidate")
                    for s in ordered
                ],
                source_reference=f"{SOURCE_PARSING}; {SOURCE_CONDITIONS}",
                detail={
                    "candidates": [
                        {
                            "submod": s.data_path,
                            "name": s.name,
                            "priority": s.priority,
                            "classification": s.classification,
                            "conditionCount": len(s.conditions),
                            "providerMod": s.provider_mod,
                        }
                        for s in ordered
                    ],
                    "unconditionalCount": len(unconditional),
                    "crossMod": cross_mod,
                    "providerMods": provider_mods,
                },
            )
        )
    return out


def config_issues(
    catalog: OarCatalog,
    profile: Profile,
    budget: Budget,
    *,
    primary_plugins: frozenset[str] = BASE_MASTERS,
    primary_source: str = "base masters only",
) -> list[issues.Issue]:
    """Per-submod findings: broken configs, unknown conditions, missing references.

    A plugin counts as loaded when it is starred in plugins.txt OR is a primary
    plugin (base master or Skyrim.ccc entry). loadorder.txt is NOT evidence of
    loading: it lists inactive plugins too, which once hid disabled references.
    """
    out: list[issues.Issue] = []
    active = {name.lower() for name, is_active in profile.plugins if is_active}
    listed = {name.lower() for name, _ in profile.plugins}
    loaded = active | set(primary_plugins)
    have_plugin_list = bool(profile.plugins)
    ccc_known = "Skyrim.ccc" in primary_source

    for submod in catalog.submods:
        label = _submod_label(submod)

        if submod.classification == INVALID_CONFIG:
            out.append(
                issues.Issue(
                    category=issues.INVALID_CONFIG,
                    subject=submod.data_path,
                    what_was_observed=f"{label}: " + "; ".join(submod.errors),
                    why_it_matters=(
                        "OAR rejects a submod whose config cannot be deserialised, so this "
                        "pack contributes nothing even though its .hkx files are on disk."
                    ),
                    action=(
                        "Report the broken config to the mod author, or reinstall the mod. "
                        "Do not hand-edit it unless you keep a backup."
                    ),
                    confidence=issues.PROVEN,
                    runtime_verification=issues.STATIC_FILE_FACT,
                    providers=[_provider_ref(submod)],
                    source_reference=SOURCE_PARSING,
                    detail={"errors": submod.errors, "configSource": submod.config_source},
                )
            )
            continue

        if submod.disabled:
            out.append(
                issues.Issue(
                    category=issues.DISABLED_BY_CONFIG,
                    subject=submod.data_path,
                    what_was_observed=f'{label}: "disabled": true in {submod.config_source}',
                    why_it_matters=(
                        "A disabled submod is not a candidate, so it cannot be the cause of "
                        "an unexpected animation."
                    ),
                    action="Informational. Re-enable it from the in-game OAR menu if wanted.",
                    confidence=issues.PROVEN,
                    runtime_verification=issues.STATIC_FILE_FACT,
                    providers=[_provider_ref(submod)],
                    source_reference=SOURCE_PARSING,
                )
            )

        if submod.config_source == "user.json":
            out.append(
                issues.Issue(
                    category=issues.USER_JSON_OVERRIDE,
                    subject=submod.data_path,
                    what_was_observed=(
                        f"{label}: a user.json sits beside config.json. Verified OAR behaviour "
                        "is that config.json supplies ONLY name and description while "
                        "user.json supplies the entire data section (priority, disabled, "
                        "conditions). It is not a per-field merge."
                    ),
                    why_it_matters=(
                        "The author's priority and conditions in config.json are therefore "
                        "inert. Reading config.json alone would report the wrong rule."
                    ),
                    action=(
                        "If you did not create this user.json, it came from the in-game OAR "
                        "menu. Delete it only with a backup to fall back to the author's config."
                    ),
                    confidence=issues.PROVEN,
                    runtime_verification=issues.STATIC_FILE_FACT,
                    providers=[_provider_ref(submod)],
                    source_reference=f"{SOURCE_PARSING} lines 1025-1066",
                    detail={"effectivePriority": submod.priority},
                )
            )

        if submod.missing_presets:
            out.append(
                issues.Issue(
                    category=issues.CONFIG_ERROR,
                    subject=submod.data_path,
                    what_was_observed=(
                        f"{label} uses PRESET condition(s) naming preset(s) its mod group does "
                        "not define: " + ", ".join(sorted(set(submod.missing_presets)))
                    ),
                    why_it_matters=(
                        "A PRESET condition is only valid when the preset exists in the group's "
                        "conditionPresets (Conditions.h PRESETCondition::IsValid), so this "
                        "condition cannot evaluate as the author intended."
                    ),
                    action=(
                        "Reinstall the mod or report the missing preset to its author. Nothing "
                        "is changed here."
                    ),
                    confidence=issues.PROVEN,
                    runtime_verification=issues.STATIC_FILE_FACT,
                    providers=[_provider_ref(submod)],
                    source_reference=f"{SOURCE_PARSING}; src/Conditions.h PRESETCondition",
                    detail={"missingPresets": sorted(set(submod.missing_presets))},
                )
            )

        unknown_conditions = sorted({c.name for c in submod.all_conditions if not c.builtin})
        if unknown_conditions:
            out.append(
                issues.Issue(
                    category=issues.UNKNOWN_EXTENSION,
                    subject=submod.data_path,
                    what_was_observed=(
                        f"{label} uses condition name(s) not built into OAR: "
                        + ", ".join(unknown_conditions)
                    ),
                    why_it_matters=(
                        "Another SKSE plugin must register these through the OAR condition "
                        "API. Without that plugin installed the condition cannot evaluate, "
                        "but whether it is installed cannot be proven by reading mod files."
                    ),
                    action="Check the mod page for a required SKSE dependency. Nothing is changed here.",
                    confidence=issues.UNVERIFIABLE,
                    runtime_verification=issues.RUNTIME_CONDITION_DEPENDENT,
                    providers=[_provider_ref(submod)],
                    source_reference=SOURCE_CONDITION_API,
                    detail={"conditions": unknown_conditions},
                )
            )

        if submod.unknown_fields:
            fields = sorted(set(submod.unknown_fields))
            out.append(
                issues.Issue(
                    category=issues.UNSUPPORTED_FIELD,
                    subject=submod.data_path,
                    what_was_observed=(
                        f"{label} declares config field(s) this tool cannot map to the OAR "
                        "version it was validated against: " + ", ".join(fields)
                    ),
                    why_it_matters=(
                        "The values are preserved verbatim rather than guessed at. They may "
                        "belong to a newer or older OAR than the source we verified."
                    ),
                    action="Informational. No action unless the submod misbehaves.",
                    confidence=issues.UNVERIFIABLE,
                    providers=[_provider_ref(submod)],
                    source_reference=SOURCE_PARSING,
                    detail={"fields": fields},
                )
            )

        not_loaded = [p for p in submod.referenced_plugins if p.lower() not in loaded]
        absent = [p for p in not_loaded if p.lower() not in listed]
        disabled = [p for p in not_loaded if p.lower() in listed]
        if not_loaded and have_plugin_list:
            parts = []
            if absent:
                parts.append("not installed in this profile: " + ", ".join(absent))
            if disabled:
                parts.append("installed but disabled in plugins.txt: " + ", ".join(disabled))
            # Without Skyrim.ccc we cannot rule out Creation Club force-loading.
            maybe_cc = not ccc_known and any(p.lower().startswith("cc") for p in not_loaded)
            out.append(
                issues.Issue(
                    category=issues.MISSING_REFERENCE,
                    subject=submod.data_path,
                    what_was_observed=f"{label} references form(s) from plugin(s) " + "; ".join(parts),
                    why_it_matters=(
                        "A condition pointing at a plugin that is not loaded cannot match, so "
                        "the submod may silently never apply. Loaded means starred in "
                        "plugins.txt or a primary plugin (base master or Skyrim.ccc entry)."
                    ),
                    action=(
                        "Enable or install the named plugin if you want this submod active, or "
                        "ignore the row if the pack is meant to be optional."
                    ),
                    confidence=issues.UNVERIFIABLE if maybe_cc else issues.LIKELY,
                    runtime_verification=issues.RUNTIME_CONDITION_DEPENDENT,
                    providers=[_provider_ref(submod)],
                    source_reference=(
                        f"{SOURCE_PARSING}; ModOrganizer2/modorganizer-game_skyrimse "
                        "src/gameskyrimse.cpp primaryPlugins()"
                    ),
                    detail={
                        "missingPlugins": not_loaded,
                        "absentPlugins": absent,
                        "disabledPlugins": disabled,
                        "primarySource": primary_source,
                    },
                )
            )

        if len(out) >= budget.max_html_rows * 2:
            break

    if catalog.truncated:
        out.append(
            issues.Issue(
                category=issues.TRUNCATED,
                subject="OAR submod budget reached",
                what_was_observed=f"Stopped after {len(catalog.submods)} submods.",
                why_it_matters="Overlap analysis below is partial.",
                action="Raise --max-submods before acting on the OAR section.",
                confidence=issues.PROVEN,
                source_reference="sandbox.Budget.max_submods",
            )
        )
    for note in catalog.notes[:20]:
        out.append(
            issues.Issue(
                category=issues.UNVERIFIED_FORMAT,
                subject=note.split(":", 1)[0],
                what_was_observed=note,
                why_it_matters=(
                    "The layout does not match the verified Skyrim OAR shape, so no rule was "
                    "derived from it. The Fallout 4 OAR port uses a different schema and is "
                    "deliberately not assumed here."
                ),
                action="Informational. Confirm the pack is a Skyrim OAR mod.",
                confidence=issues.UNVERIFIABLE,
                source_reference=SOURCE_PARSING,
            )
        )
    return out


def summarise(catalog: OarCatalog) -> dict[str, object]:
    """Counts for the report header, by classification rather than by guess."""
    buckets: dict[str, int] = {}
    for submod in catalog.submods:
        buckets[submod.classification] = buckets.get(submod.classification, 0) + 1
    return {
        "groups": len(catalog.groups),
        "submods": len(catalog.submods),
        "activeCandidates": len(catalog.active),
        "byClassification": dict(sorted(buckets.items())),
        "legacyParsed": sum(1 for s in catalog.submods if s.legacy),
        "truncated": catalog.truncated,
        "classificationMeaning": {
            ALWAYS_STATIC_IF_NO_CONDITIONS: "no conditions declared; applies whenever evaluated",
            "CONDITIONAL_RUNTIME_UNKNOWN": "gated by conditions only the running game can test",
            DISABLED_BY_CONFIG: "disabled in config/user.json; not a candidate",
            INVALID_CONFIG: "OAR would reject this submod outright",
            CUSTOM_CONDITION_REQUIRES_PLUGIN: "needs a condition from another SKSE plugin",
        },
    }
