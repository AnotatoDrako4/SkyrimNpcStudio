"""Open Animation Replacer discovery and conservative config parsing.

Format evidence (Skyrim OAR, NOT the Fallout 4 port) comes from
``ersh1/OpenAnimationReplacer@main``, ``src/Parsing.cpp``:

* ``DeserializeSubMod`` reads ``name`` (required) and ``description`` in
  ``kInfoOnly``/``kFull`` mode, and ``priority`` (**required int**), ``disabled``,
  ``replacementAnimDatas``, ``overrideAnimationsFolder``, ``conditions`` and the
  deprecated ``disabledAnimations``/``keepRandomResultsOnLoop``/
  ``shareRandomResults`` in ``kDataOnly``/``kFull`` mode.
* ``ParseModSubdirectory`` (Parsing.cpp:1025-1066) decides config precedence:
  with both files present, ``config.json`` supplies ONLY name/description and
  ``user.json`` supplies ALL of the data. It is a whole-section replacement, not
  a per-field merge. A legacy (DAR) submod reads ``user.json`` alone.
* A missing or non-int ``priority`` makes OAR reject the whole submod, so we
  classify that as INVALID_CONFIG rather than assuming priority 0.

Anything not covered by that source is preserved verbatim and marked UNKNOWN.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from . import sandbox
from .loose_winners import KIND_OAR_CONFIG, LooseIndex
from .sandbox import Budget

OAR_MARKER = "openanimationreplacer"
DAR_MARKER = "dynamicanimationreplacer"

# The 124 condition factories OAR registers, extracted from the authoritative
# registration table with:
#   curl -sSL .../ersh1/OpenAnimationReplacer/main/src/OpenAnimationReplacer.cpp |
#   grep -oE '_conditionFactories\.emplace\("[A-Za-z0-9]+"'
# A name absent from BUILTIN_CONDITIONS *and* LEGACY_ONLY_CONDITIONS is NOT an
# error: it is most likely a condition supplied by another SKSE plugin through
# the OAR condition API.
BUILTIN_CONDITIONS = frozenset(
    """AND AttackState CastingSpell CompareValues CrimeGold CurrentCastingType
    CurrentDeliveryType CurrentFurniture CurrentFurnitureHasKeyword CurrentGameTime
    CurrentMovementSpeed CurrentPackageType CurrentRotationSpeed CurrentTargetDistance
    CurrentTargetLineOfSight CurrentTargetRelationship CurrentTargetRelativeAngle
    CurrentWeather CurrentWeatherHasFlag EquippedObjectWeight FactionRank FallDamage
    FallDistance HasBoundWeaponEquipped HasGraphVariable HasKeyword HasMagicEffect
    HasMagicEffectWithKeyword HasPerk HasRefType HasSpell HasTarget Height IdleTime
    InventoryCount InventoryCountHasKeyword InventoryWeight IsAboveWater IsActorBase
    IsAttackTypeFlag IsAttackTypeKeyword IsAttacking IsBeingRidden IsBeingRiddenBy
    IsBlocking IsChild IsClass IsCombatSearching IsCombatState IsCombatStyle
    IsCrimeSearching IsCurrentPackage IsDoingFavor IsEquipped IsEquippedHasEnchantment
    IsEquippedHasEnchantmentWithKeyword IsEquippedHasKeyword IsEquippedPower
    IsEquippedShout IsEquippedType IsFemale IsForm IsGhost IsGreetingPlayer IsGuard
    IsInAir IsInCombat IsInFaction IsInInterior IsInLocation IsInScene
    IsInSpecifiedScene IsMenuOpen IsMovementDirection IsOnMount IsOnStairs
    IsOverEncumbered IsParentCell IsPlayerTeammate IsQuestStageDone IsRace
    IsReplacerEnabled IsRiding IsRidingHasKeyword IsRunning IsScenePlaying IsSneaking
    IsSprinting IsStaggered IsSummoned IsSwimming IsTalking IsTrespassing IsUnique
    IsVoiceType IsWeaponDrawn IsWorldSpace IsWorn IsWornHasKeyword IsWornInSlot
    IsWornInSlotHasKeyword Level LifeState LightLevel LocationCleared
    LocationHasKeyword MOUNT MagicEffectElapsedTime MovementSpeed
    MovementSurfaceAngle OR PLAYER PRESET Random Scale SitSleepState SubmergeLevel
    SurfaceMaterial TARGET Weight WindAngleDifference WindSpeed XOR""".split()
)

# OAR also registers 22 "hidden" factories that exist only so legacy DAR
# _conditions.txt files keep working. They are valid conditions, merely absent
# from the in-game OAR menu, so treating them as faults would be a false
# positive. Extracted from the same file with:
#   grep -oE '_hiddenConditionFactories\.emplace\("[A-Za-z0-9]+"'
LEGACY_ONLY_CONDITIONS = frozenset(
    """CurrentGameTimeLessThan CurrentTarget CurrentTargetFactionRank
    CurrentTargetHasKeyword IsActorValueBaseEqualTo IsActorValueBaseLessThan
    IsActorValueEqualTo IsActorValueLessThan IsActorValueMaxEqualTo
    IsActorValueMaxLessThan IsActorValuePercentageEqualTo
    IsActorValuePercentageLessThan IsEquippedLeft IsEquippedLeftHasKeyword
    IsEquippedLeftType IsEquippedRight IsEquippedRightHasKeyword
    IsEquippedRightType IsFactionRankEqualTo IsFactionRankLessThan
    IsLevelLessThan ValueEqualTo ValueLessThan""".split()
)

KNOWN_CONDITIONS = BUILTIN_CONDITIONS | LEGACY_ONLY_CONDITIONS

# Fields DeserializeSubMod actually reads. Anything else is preserved as UNKNOWN.
KNOWN_SUBMOD_FIELDS = frozenset(
    {
        "name",
        "description",
        "priority",
        "disabled",
        "disabledAnimations",
        "replacementAnimDatas",
        "overrideAnimationsFolder",
        "conditions",
        "pairedConditions",
        "conditionPresets",
        "requiredBehaviorProjectName",
        "interruptible",
        "replaceOnLoop",
        "replaceOnEcho",
        "keepRandomResultsOnLoop",
        "shareRandomResults",
        "triggersFromAnnotationsOnly",
        "ignoreNoTriggersFlag",
        "ignoreDontConvertAnnotationsToTriggersFlag",
        "hasCustomBlendTimeOnInterrupt",
        "hasCustomBlendTimeOnLoop",
        "hasCustomBlendTimeOnEcho",
        "blendTimeOnInterrupt",
        "blendTimeOnLoop",
        "blendTimeOnEcho",
        "functionsOnActivate",
        "functionsOnDeactivate",
        "functionsOnTrigger",
        "runFunctionsOnLoop",
        "runFunctionsOnEcho",
    }
)
# DeserializeMod (Parsing.cpp) reads these for a mod group.
KNOWN_GROUP_FIELDS = frozenset({"name", "author", "description", "conditionPresets"})

# Rule classifications
ALWAYS_STATIC_IF_NO_CONDITIONS = "ALWAYS_STATIC_IF_NO_CONDITIONS"
CONDITIONAL_RUNTIME_UNKNOWN = "CONDITIONAL_RUNTIME_UNKNOWN"
DISABLED_BY_CONFIG = "DISABLED_BY_CONFIG"
INVALID_CONFIG = "INVALID_CONFIG"
CUSTOM_CONDITION_REQUIRES_PLUGIN = "CUSTOM_CONDITION_REQUIRES_PLUGIN"


@dataclass
class ConditionRef:
    """One flattened condition node, with the nesting depth it was found at."""

    name: str
    depth: int
    negated: bool
    required_version: str = ""
    builtin: bool = True
    # CreateConditionFromJson reads an optional "requiredPlugin" naming the SKSE
    # plugin that registers a non-built-in condition.
    required_plugin: str = ""
    # For a PRESET condition: the preset name, read from its "Preset" component
    # (Conditions.h PRESETCondition); resolved against the group's conditionPresets.
    preset: str = ""
    plugin_refs: list[str] = field(default_factory=list)

    def to_json(self) -> dict[str, object]:
        return {
            "condition": self.name,
            "depth": self.depth,
            "negated": self.negated,
            "requiredVersion": self.required_version,
            "requiredPlugin": self.required_plugin,
            "builtin": self.builtin,
            "pluginRefs": sorted(set(self.plugin_refs)),
        }


@dataclass
class SubMod:
    """One OAR submod as read from disk, with its own evidence trail."""

    data_path: str  # Data-relative submod directory
    group_path: str  # Data-relative mod-group directory
    provider_mod: str  # MO2 mod that won this config file
    config_source: str  # "config.json" | "user.json"
    name: str = ""
    description: str = ""
    group_name: str = ""
    group_author: str = ""
    priority: int | None = None
    disabled: bool = False
    override_animations_folder: str = ""
    conditions: list[ConditionRef] = field(default_factory=list)
    replacement_targets: list[str] = field(default_factory=list)
    animation_files: list[str] = field(default_factory=list)
    unknown_fields: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    raw_config_sha256: str = ""
    legacy: bool = False
    # Conditions pulled in through PRESET references, and presets that do not exist.
    preset_conditions: list[ConditionRef] = field(default_factory=list)
    missing_presets: list[str] = field(default_factory=list)
    # Full Data keys of animations that replacementAnimDatas marks "disabled": true.
    disabled_animation_keys: set[str] = field(default_factory=set)
    # What this submod actually replaces (see _targets_for); filled by discover().
    targets: list[str] = field(default_factory=list)

    @property
    def all_conditions(self) -> list[ConditionRef]:
        return self.conditions + self.preset_conditions

    @property
    def valid(self) -> bool:
        """OAR needs a name and an int priority; without them it skips the submod."""
        return not self.errors and self.priority is not None and bool(self.name)

    @property
    def classification(self) -> str:
        if not self.valid:
            return INVALID_CONFIG
        if self.disabled:
            return DISABLED_BY_CONFIG
        if any(not c.builtin for c in self.all_conditions):
            return CUSTOM_CONDITION_REQUIRES_PLUGIN
        if not self.conditions:
            return ALWAYS_STATIC_IF_NO_CONDITIONS
        return CONDITIONAL_RUNTIME_UNKNOWN

    @property
    def referenced_plugins(self) -> list[str]:
        names: set[str] = set()
        for condition in self.all_conditions:
            names.update(condition.plugin_refs)
        return sorted(names)

    def to_json(self) -> dict[str, object]:
        return {
            "dataPath": self.data_path,
            "groupPath": self.group_path,
            "providerMod": self.provider_mod,
            "configSource": self.config_source,
            "name": self.name,
            "description": self.description,
            "groupName": self.group_name,
            "groupAuthor": self.group_author,
            "priority": self.priority,
            "disabled": self.disabled,
            "legacy": self.legacy,
            "classification": self.classification,
            "overrideAnimationsFolder": self.override_animations_folder,
            "conditions": [c.to_json() for c in self.conditions],
            "conditionCount": len(self.conditions),
            "replacementTargets": self.replacement_targets,
            "animationFiles": self.animation_files,
            "targets": self.targets,
            "disabledAnimationCount": len(self.disabled_animation_keys),
            "presetRefs": sorted({c.preset for c in self.conditions if c.preset}),
            "presetConditionCount": len(self.preset_conditions),
            "missingPresets": self.missing_presets,
            "referencedPlugins": self.referenced_plugins,
            "unknownFields": sorted(set(self.unknown_fields)),
            "errors": self.errors,
            "configSha256": self.raw_config_sha256,
        }


@dataclass
class OarCatalog:
    """Everything discovered about OAR in one profile."""

    submods: list[SubMod] = field(default_factory=list)
    groups: dict[str, dict[str, str]] = field(default_factory=dict)
    # group key -> preset name -> flattened conditions (DeserializeMod conditionPresets)
    presets: dict[str, dict[str, list[ConditionRef]]] = field(default_factory=dict)
    truncated: bool = False
    notes: list[str] = field(default_factory=list)

    @property
    def active(self) -> list[SubMod]:
        """Candidates OAR would actually consider: valid and not disabled."""
        return [s for s in self.submods if s.valid and not s.disabled]


def _split_oar_path(data_relative: str) -> tuple[str, str, str] | None:
    """Split a config path into ``(oar_root, group_dir, submod_dir)``.

    Returns ``None`` when the path is not the expected
    ``.../OpenAnimationReplacer/<group>/<submod>/config.json`` shape.
    """
    parts = data_relative.replace("\\", "/").split("/")
    lowered = [p.lower() for p in parts]
    markers = [i for i, p in enumerate(lowered) if p in (OAR_MARKER, DAR_MARKER)]
    if not markers:
        return None
    marker = markers[-1]
    root = "/".join(parts[: marker + 1])
    rest = parts[marker + 1 : -1]  # drop the json filename
    if not rest:
        return None
    if len(rest) == 1:
        return root, "/".join(parts[: marker + 2]), ""
    return root, "/".join(parts[: marker + 2]), "/".join(parts[:-1])


def _flatten_conditions(node: object, depth: int, out: list[ConditionRef]) -> None:
    """Walk the condition tree, recording names and every plugin it references.

    AND/OR/XOR semantics are NOT evaluated: we only record the shape so the
    report can say which submods are condition-gated and which are not.
    """
    if isinstance(node, list):
        for item in node:
            _flatten_conditions(item, depth, out)
        return
    if not isinstance(node, dict):
        return

    name = node.get("condition")
    if isinstance(name, str):
        ref = ConditionRef(
            name=name,
            depth=depth,
            negated=bool(node.get("negated", False)),
            required_version=str(node.get("requiredVersion", "")),
            required_plugin=str(node.get("requiredPlugin", "")),
            preset=str(node.get("Preset", "")) if name == "PRESET" else "",
            builtin=name in KNOWN_CONDITIONS,
        )
        ref.plugin_refs.extend(_collect_plugin_names(node))
        out.append(ref)

    nested = node.get("Conditions")
    if nested is not None:
        _flatten_conditions(nested, depth + 1, out)


def _collect_plugin_names(node: object) -> list[str]:
    """Pull every ``pluginName`` out of an operand tree, at any nesting."""
    found: list[str] = []
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "Conditions":
                continue
            if key == "pluginName" and isinstance(value, str) and value:
                found.append(value)
            else:
                found.extend(_collect_plugin_names(value))
    elif isinstance(node, list):
        for item in node:
            found.extend(_collect_plugin_names(item))
    return found


def _collect_replacement_targets(doc: dict[str, object]) -> list[str]:
    targets: list[str] = []
    for key in ("replacementAnimDatas", "disabledAnimations"):
        entries = doc.get(key)
        if not isinstance(entries, list):
            continue
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            project = entry.get("projectName")
            path = entry.get("path")
            if isinstance(project, str) and isinstance(path, str):
                targets.append(f"{project}:{path}".replace("\\", "/"))
    return sorted(set(targets))


def _apply_info(submod: SubMod, doc: dict[str, object]) -> None:
    name = doc.get("name")
    if isinstance(name, str) and name:
        submod.name = name
    description = doc.get("description")
    if isinstance(description, str):
        submod.description = description


def _apply_data(submod: SubMod, doc: dict[str, object]) -> None:
    priority = doc.get("priority")
    if isinstance(priority, bool) or not isinstance(priority, int):
        submod.errors.append(
            "priority is missing or not an integer; OAR rejects the whole submod "
            "(Parsing.cpp DeserializeSubMod)"
        )
    else:
        submod.priority = priority

    disabled = doc.get("disabled")
    if isinstance(disabled, bool):
        submod.disabled = disabled

    override = doc.get("overrideAnimationsFolder")
    if isinstance(override, str):
        submod.override_animations_folder = override

    _flatten_conditions(doc.get("conditions"), 0, submod.conditions)
    submod.replacement_targets = _collect_replacement_targets(doc)
    submod.unknown_fields.extend(k for k in doc if k not in KNOWN_SUBMOD_FIELDS)

    # replacementAnimDatas are per-animation SETTINGS looked up by (projectName, path)
    # for animations the submod already ships (ReplacerMods.cpp, LoadAnimData), not
    # replacement targets. "disabled": true calls SetDisabled, so that animation is
    # no longer a candidate.
    entries = doc.get("replacementAnimDatas")
    for entry in entries if isinstance(entries, list) else []:
        if isinstance(entry, dict) and entry.get("disabled") is True:
            path = entry.get("path")
            if isinstance(path, str):
                submod.disabled_animation_keys.add(_data_key(path))


def _data_key(path: str) -> str:
    """Normalise a replacementAnimDatas path ("data\\meshes\\...") to a Data-relative key."""
    key = sandbox.norm_key(path)
    return key[len("data/") :] if key.startswith("data/") else key


VARIANTS_PREFIX = "_variants_"


def _animation_root(submod: "SubMod") -> str:
    """OAR reads a sibling folder instead when overrideAnimationsFolder is set."""
    if submod.override_animations_folder:
        parent = submod.data_path.rsplit("/", 1)[0]
        return f"{parent}/{submod.override_animations_folder}"
    return submod.data_path


def _targets_for(submod: "SubMod") -> list[str]:
    """The original animation paths this submod replaces, relative to its folder.

    Verified in Parsing.cpp: a ``_variants_X`` directory is ONE replacement animation
    for ``X.hkx`` whose files are its variants (ConvertVariantsPath; OAR submods only,
    not legacy DAR). Animations disabled through replacementAnimDatas are excluded.
    """
    root_key = sandbox.norm_key(_animation_root(submod))
    out: set[str] = set()
    for rel in submod.animation_files:
        parts = rel.split("/")
        target, container = rel, None
        if not submod.legacy:
            for i, part in enumerate(parts[:-1]):
                if part.startswith(VARIANTS_PREFIX):
                    target = "/".join(parts[:i] + [part[len(VARIANTS_PREFIX):] + ".hkx"])
                    container = "/".join(parts[: i + 1])
                    break
        disabled = submod.disabled_animation_keys
        if f"{root_key}/{rel}" in disabled or (container and f"{root_key}/{container}" in disabled):
            continue
        out.add(target)
    return sorted(out)


def _read_json(path: Path, budget: Budget) -> tuple[dict[str, object] | None, str, str]:
    """Return ``(document, sha256, error)``; a bad file yields an error, not a crash."""
    try:
        text = sandbox.read_text_confined(path, budget)
    except sandbox.SandboxError as exc:
        return None, "", str(exc)
    digest = sandbox.sha256_text(text)
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as exc:
        return None, digest, f"invalid JSON at line {exc.lineno} column {exc.colno}: {exc.msg}"
    if not isinstance(doc, dict):
        return None, digest, "top-level JSON value is not an object"
    return doc, digest, ""


def discover(index: LooseIndex, budget: Budget) -> OarCatalog:
    """Find every OAR group and submod among the winning config files.

    Only the MO2 winner of each config path is parsed: a lower-priority copy is
    not what the game would read, so parsing it would invent a rule.
    """
    catalog = OarCatalog()
    config_records = [
        r
        for r in index.records.values()
        if r.kind == KIND_OAR_CONFIG and r.key.endswith("/config.json")
    ]
    user_records = {
        r.key: r
        for r in index.records.values()
        if r.kind == KIND_OAR_CONFIG and r.key.endswith("/user.json")
    }

    # Group-level configs first, so submods can quote their group's name.
    group_docs: dict[str, dict[str, object]] = {}
    for record in config_records:
        split = _split_oar_path(record.display)
        if split is None:
            continue
        _, group_dir, submod_dir = split
        if submod_dir:
            continue
        doc, _, error = _read_json(record.winner.absolute, budget)
        if error or doc is None:
            catalog.notes.append(f"{record.display}: {error}")
            continue
        group_docs[sandbox.norm_key(group_dir)] = doc
        group_presets: dict[str, list[ConditionRef]] = {}
        raw_presets = doc.get("conditionPresets")
        for preset in raw_presets if isinstance(raw_presets, list) else []:
            if isinstance(preset, dict) and isinstance(preset.get("name"), str):
                flattened: list[ConditionRef] = []
                _flatten_conditions(preset.get("conditions"), 1, flattened)
                group_presets[preset["name"]] = flattened
        catalog.presets[sandbox.norm_key(group_dir)] = group_presets
        catalog.groups[group_dir] = {
            "name": str(doc.get("name", "")),
            "author": str(doc.get("author", "")),
            "description": str(doc.get("description", "")),
            "providerMod": record.winner.mod,
            "unknownFields": ",".join(sorted(k for k in doc if k not in KNOWN_GROUP_FIELDS)),
        }

    for record in sorted(config_records, key=lambda r: r.key):
        if len(catalog.submods) >= budget.max_submods:
            catalog.truncated = True
            break
        split = _split_oar_path(record.display)
        if split is None:
            catalog.notes.append(
                f"{record.display}: not the expected "
                "OpenAnimationReplacer/<group>/<submod> shape"
            )
            continue
        _, group_dir, submod_dir = split
        if not submod_dir:
            continue  # group config, already handled

        legacy = DAR_MARKER in record.key
        submod = SubMod(
            data_path=submod_dir,
            group_path=group_dir,
            provider_mod=record.winner.mod,
            config_source="config.json",
            legacy=legacy,
        )
        group_meta = group_docs.get(sandbox.norm_key(group_dir), {})
        submod.group_name = str(group_meta.get("name", ""))
        submod.group_author = str(group_meta.get("author", ""))

        config_doc, digest, error = _read_json(record.winner.absolute, budget)
        submod.raw_config_sha256 = digest
        if error or config_doc is None:
            submod.errors.append(error or "config.json unreadable")
            catalog.submods.append(submod)
            continue

        user_key = sandbox.norm_key(f"{submod_dir}/user.json")
        has_user = user_key in user_records

        # Verified precedence: config.json supplies name/description only, and
        # user.json supplies the entire data section (Parsing.cpp:1044-1066).
        _apply_info(submod, config_doc)
        if has_user:
            submod.config_source = "user.json"
            user_doc, user_digest, user_error = _read_json(
                user_records[user_key].winner.absolute, budget
            )
            submod.raw_config_sha256 = f"{digest}+{user_digest}"
            if user_error or user_doc is None:
                submod.errors.append(f"user.json: {user_error or 'unreadable'}")
            else:
                _apply_data(submod, user_doc)
        else:
            _apply_data(submod, config_doc)

        group_presets = catalog.presets.get(sandbox.norm_key(group_dir), {})
        for condition in submod.conditions:
            if not condition.preset:
                continue
            if condition.preset in group_presets:
                submod.preset_conditions.extend(group_presets[condition.preset])
            else:
                submod.missing_presets.append(condition.preset)

        submod.animation_files = _submod_animations(index, submod)
        submod.targets = _targets_for(submod)
        catalog.submods.append(submod)

    return catalog


def _submod_animations(index: LooseIndex, submod: SubMod) -> list[str]:
    """Animation files the submod carries, as paths relative to the submod folder.

    When ``overrideAnimationsFolder`` is set, OAR reads a sibling folder instead
    (Parsing.cpp ``ParseModSubdirectory``), so that folder is used as the root.
    """
    prefix = sandbox.norm_key(_animation_root(submod)) + "/"
    return sorted(
        record.key[len(prefix) :]
        for record in index.records.values()
        if record.key.startswith(prefix) and record.key.endswith(".hkx")
    )
