"""Open Animation Replacer analysis: cases O01-O10.

Every expectation traces to ``ersh1/OpenAnimationReplacer@main``, not to the
Fallout 4 port, whose schema may differ.
"""

from __future__ import annotations

import json
import unittest

from _support import OAR_BASE, FixtureCase, build_instance
from engine import issues, loose_winners, mo2_profile, oar_discovery, oar_rules

HKX = bytes.fromhex("57e0e05710c0c010") + b"\x00" * 64


def group(name: str = "Pack") -> dict[str, object]:
    return {"name": name, "author": "fixture", "description": "synthetic"}


def submod(name: str, priority: int | None = 100, **extra: object) -> dict[str, object]:
    doc: dict[str, object] = {"name": name}
    if priority is not None:
        doc["priority"] = priority
    doc.update(extra)
    return doc


def pack(files: dict[str, object]) -> dict[str, bytes | str]:
    """Turn a {relative-path: json-or-bytes} map into mod file contents."""
    out: dict[str, bytes | str] = {}
    for relative, content in files.items():
        if isinstance(content, (bytes, bytearray)):
            out[relative] = bytes(content)
        else:
            out[relative] = json.dumps(content, indent=2)
    return out


class OarTests(FixtureCase):
    def _catalog(self, mod_files: dict[str, bytes | str], mod_name: str = "OarMod"):
        root = build_instance(self.tmp / "inst", {mod_name: mod_files}, [f"+{mod_name}"])
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        return index, profile, oar_discovery.discover(index, self.budget)

    def test_O01_discovers_group_and_submod_in_the_supported_path(self) -> None:
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group("Fixture Pack"),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod("Sub One", 300),
                    f"{OAR_BASE}/Pack/Sub/female/mt_idle.hkx": HKX,
                }
            )
        )
        self.assertEqual(len(catalog.groups), 1)
        self.assertEqual(len(catalog.submods), 1)
        found = catalog.submods[0]
        self.assertEqual(found.name, "Sub One")
        self.assertEqual(found.group_name, "Fixture Pack")
        self.assertEqual(found.animation_files, ["female/mt_idle.hkx"])

    def test_O02_extracts_priority_and_flattened_conditions(self) -> None:
        conditions = [
            {"condition": "IsFemale", "requiredVersion": "1.0.0.0"},
            {
                "condition": "OR",
                "requiredVersion": "1.0.0.0",
                "negated": True,
                "Conditions": [
                    {"condition": "IsSneaking", "requiredVersion": "1.0.0.0"},
                    {
                        "condition": "HasSpell",
                        "requiredVersion": "1.0.0.0",
                        "Spell": {"pluginName": "Ghost.esp", "formID": "1A2B"},
                    },
                ],
            },
        ]
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod(
                        "Sub", 6001925, conditions=conditions
                    ),
                }
            )
        )
        found = catalog.submods[0]
        self.assertEqual(found.priority, 6001925)
        self.assertEqual(
            [c.name for c in found.conditions], ["IsFemale", "OR", "IsSneaking", "HasSpell"]
        )
        self.assertTrue(next(c for c in found.conditions if c.name == "OR").negated)
        self.assertEqual(found.referenced_plugins, ["Ghost.esp"])
        self.assertEqual(found.classification, oar_discovery.CONDITIONAL_RUNTIME_UNKNOWN)

    def test_O03_corrupt_json_is_one_issue_and_does_not_abort_the_scan(self) -> None:
        files = pack(
            {
                f"{OAR_BASE}/Pack/config.json": group(),
                f"{OAR_BASE}/Pack/Healthy/config.json": submod("Healthy", 100),
            }
        )
        files[f"{OAR_BASE}/Pack/Broken/config.json"] = '{"name": "Broken", "priority": 1,,}'
        _, profile, catalog = self._catalog(files)

        self.assertEqual(len(catalog.submods), 2, "the healthy submod must still be parsed")
        broken = next(s for s in catalog.submods if "Broken" in s.data_path)
        healthy = next(s for s in catalog.submods if "Healthy" in s.data_path)
        self.assertFalse(broken.valid)
        self.assertTrue(healthy.valid)
        self.assertEqual(broken.classification, oar_discovery.INVALID_CONFIG)
        self.assertIn("invalid JSON at line", broken.errors[0])

        found = oar_rules.config_issues(catalog, profile, self.budget)
        invalid = [i for i in found if i.category == issues.INVALID_CONFIG]
        self.assertEqual(len(invalid), 1)
        self.assertEqual(invalid[0].severity, "high")

    def test_O04_user_json_replaces_the_data_section_but_not_name(self) -> None:
        """Verified in Parsing.cpp::ParseModSubdirectory lines 1025-1066."""
        _, profile, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod(
                        "Author Name",
                        300,
                        description="author text",
                        conditions=[{"condition": "IsSprinting", "requiredVersion": "1.0.0.0"}],
                    ),
                    f"{OAR_BASE}/Pack/Sub/user.json": submod("USER NAME IGNORED", 4242),
                }
            )
        )
        found = catalog.submods[0]
        self.assertEqual(found.config_source, "user.json")
        self.assertEqual(found.name, "Author Name", "name comes from config.json (kInfoOnly)")
        self.assertEqual(found.description, "author text")
        self.assertEqual(found.priority, 4242, "priority comes from user.json (kDataOnly)")
        self.assertEqual(
            found.conditions, [], "user.json replaces the whole data section, not per field"
        )
        override = [
            i
            for i in oar_rules.config_issues(catalog, profile, self.budget)
            if i.category == issues.USER_JSON_OVERRIDE
        ]
        self.assertEqual(len(override), 1)
        self.assertEqual(override[0].confidence, issues.PROVEN)

    def test_O05_disabled_submod_is_not_an_active_candidate(self) -> None:
        _, profile, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/On/config.json": submod("On", 100),
                    f"{OAR_BASE}/Pack/Off/config.json": submod("Off", 200, disabled=True),
                }
            )
        )
        self.assertEqual(len(catalog.submods), 2)
        self.assertEqual([s.name for s in catalog.active], ["On"])
        off = next(s for s in catalog.submods if s.name == "Off")
        self.assertEqual(off.classification, oar_discovery.DISABLED_BY_CONFIG)
        disabled = [
            i
            for i in oar_rules.config_issues(catalog, profile, self.budget)
            if i.category == issues.DISABLED_BY_CONFIG
        ]
        self.assertEqual(len(disabled), 1)
        self.assertEqual(disabled[0].severity, "info")

    def _two_mod_catalog(self, files_a: dict[str, object], files_b: dict[str, object]):
        root = build_instance(
            self.tmp / "inst", {"PackA": pack(files_a), "PackB": pack(files_b)},
            ["+PackA", "+PackB"],
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        return oar_discovery.discover(index, self.budget)

    def test_O06_two_submods_on_one_clip_overlap_without_a_runtime_winner(self) -> None:
        catalog = self._two_mod_catalog(
            {
                f"{OAR_BASE}/A/config.json": group("A"),
                f"{OAR_BASE}/A/High/config.json": submod("High", 900),
                f"{OAR_BASE}/A/High/female/mt_walk.hkx": HKX,
            },
            {
                f"{OAR_BASE}/B/config.json": group("B"),
                f"{OAR_BASE}/B/Low/config.json": submod("Low", 100),
                f"{OAR_BASE}/B/Low/female/mt_walk.hkx": HKX,
            },
        )
        overlaps = oar_rules.overlap_issues(catalog, self.budget)
        self.assertEqual(len(overlaps), 1)
        overlap = overlaps[0]
        self.assertEqual(overlap.category, issues.STATIC_OVERLAP)
        self.assertTrue(overlap.detail["crossMod"])
        self.assertEqual(overlap.detail["providerMods"], ["PackA", "PackB"])
        # Both candidates are unconditional, so confidence is "likely" -> "medium".
        self.assertEqual(overlap.confidence, issues.LIKELY)
        self.assertEqual(overlap.severity, "medium")
        self.assertEqual(overlap.runtime_verification, issues.RUNTIME_CONDITION_DEPENDENT)
        self.assertEqual([c["name"] for c in overlap.detail["candidates"]], ["High", "Low"])
        self.assertEqual(overlap.providers[0].role, "candidate-highest")
        self.assertNotIn("WINNER_CONFIRMED", overlap.what_was_observed)
        self.assertIn("cannot be determined by reading files", overlap.why_it_matters)

    def test_O06b_overlap_within_one_mod_is_informational_variants(self) -> None:
        """58 of 71 real overlaps were one author's random variants; they must not drown
        the 13 cross-mod ones."""
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Random1/config.json": submod("Random 1", 902),
                    f"{OAR_BASE}/Pack/Random1/bow_sprint.hkx": HKX,
                    f"{OAR_BASE}/Pack/Random2/config.json": submod("Random 2", 903),
                    f"{OAR_BASE}/Pack/Random2/bow_sprint.hkx": HKX,
                }
            )
        )
        overlap = oar_rules.overlap_issues(catalog, self.budget)[0]
        self.assertEqual(overlap.category, issues.INTRA_MOD_OVERLAP)
        self.assertEqual(overlap.severity, "info")
        self.assertFalse(overlap.detail["crossMod"])
        self.assertIn("same mod", overlap.why_it_matters)
        self.assertEqual(overlap.runtime_verification, issues.RUNTIME_CONDITION_DEPENDENT)

    def test_O07_priority_tie_is_a_warning_not_an_arbitrary_choice(self) -> None:
        catalog = self._two_mod_catalog(
            {
                f"{OAR_BASE}/A/config.json": group("A"),
                f"{OAR_BASE}/A/A/config.json": submod("A", 500),
                f"{OAR_BASE}/A/A/female/mt_sprint.hkx": HKX,
            },
            {
                f"{OAR_BASE}/B/config.json": group("B"),
                f"{OAR_BASE}/B/B/config.json": submod("B", 500),
                f"{OAR_BASE}/B/B/female/mt_sprint.hkx": HKX,
            },
        )
        found = oar_rules.overlap_issues(catalog, self.budget)
        self.assertEqual(len(found), 1)
        tie = found[0]
        self.assertEqual(tie.category, issues.PRIORITY_TIE)
        self.assertEqual(tie.confidence, issues.UNVERIFIABLE)
        self.assertEqual(tie.detail["priority"], 500)
        self.assertEqual(len(tie.providers), 2)

    def test_O07b_tie_inside_one_mod_is_informational(self) -> None:
        """Real case: weapon-specific jump submods of one mod all at priority 99999."""
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Dagger/config.json": submod("Dagger jump", 99999),
                    f"{OAR_BASE}/Pack/Dagger/mt_jumpfast.hkx": HKX,
                    f"{OAR_BASE}/Pack/Sword/config.json": submod("Sword jump", 99999),
                    f"{OAR_BASE}/Pack/Sword/mt_jumpfast.hkx": HKX,
                }
            )
        )
        found = oar_rules.overlap_issues(catalog, self.budget)
        self.assertEqual([i.category for i in found], [issues.INTRA_MOD_OVERLAP])
        self.assertEqual(found[0].severity, "info")
        self.assertIn("share priority 99999", found[0].what_was_observed)

    def test_O06c_variants_folder_targets_the_animation_it_replaces(self) -> None:
        """Parsing.cpp ConvertVariantsPath: _variants_X is one replacement for X.hkx."""
        catalog = self._two_mod_catalog(
            {
                f"{OAR_BASE}/A/config.json": group("A"),
                f"{OAR_BASE}/A/Sub/config.json": submod("Variants", 300),
                f"{OAR_BASE}/A/Sub/female/_variants_mt_runforward/v1.hkx": HKX,
                f"{OAR_BASE}/A/Sub/female/_variants_mt_runforward/v2.hkx": HKX,
            },
            {
                f"{OAR_BASE}/B/config.json": group("B"),
                f"{OAR_BASE}/B/Sub/config.json": submod("Plain", 100),
                f"{OAR_BASE}/B/Sub/female/mt_runforward.hkx": HKX,
            },
        )
        variants = next(s for s in catalog.submods if s.name == "Variants")
        self.assertEqual(variants.targets, ["female/mt_runforward.hkx"])
        overlaps = oar_rules.overlap_issues(catalog, self.budget)
        self.assertEqual(len(overlaps), 1, "the variants folder must collide with the plain clip")
        self.assertEqual(overlaps[0].subject, "female/mt_runforward.hkx")
        self.assertTrue(overlaps[0].detail["crossMod"])

    def test_O06d_animation_disabled_in_replacementanimdatas_is_not_a_target(self) -> None:
        sub_dir = f"{OAR_BASE}/Pack/Sub"
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{sub_dir}/config.json": submod(
                        "Sub", 100,
                        replacementAnimDatas=[{
                            "projectName": "DefaultFemale",
                            "path": "data\\" + sub_dir.replace("/", "\\") + "\\female\\mt_walk.hkx",
                            "disabled": True,
                        }],
                    ),
                    f"{sub_dir}/female/mt_walk.hkx": HKX,
                    f"{sub_dir}/female/mt_idle.hkx": HKX,
                }
            )
        )
        found = catalog.submods[0]
        self.assertEqual(found.targets, ["female/mt_idle.hkx"])
        self.assertEqual(len(found.disabled_animation_keys), 1)

    def test_O06e_replacementanimdatas_are_settings_not_targets(self) -> None:
        """Regression: a submod carrying replacementAnimDatas used them as targets, so it
        never overlapped a submod without them (ReplacerMods.cpp LoadAnimData)."""
        catalog = self._two_mod_catalog(
            {
                f"{OAR_BASE}/A/config.json": group("A"),
                f"{OAR_BASE}/A/Sub/config.json": submod(
                    "WithSettings", 300,
                    replacementAnimDatas=[{"projectName": "DefaultFemale",
                                           "path": "data\\x\\female\\mt_idle.hkx"}],
                ),
                f"{OAR_BASE}/A/Sub/female/mt_idle.hkx": HKX,
            },
            {
                f"{OAR_BASE}/B/config.json": group("B"),
                f"{OAR_BASE}/B/Sub/config.json": submod("WithoutSettings", 100),
                f"{OAR_BASE}/B/Sub/female/mt_idle.hkx": HKX,
            },
        )
        overlaps = oar_rules.overlap_issues(catalog, self.budget)
        self.assertEqual([o.subject for o in overlaps], ["female/mt_idle.hkx"])

    def test_preset_conditions_count_for_references_and_extensions(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {
                "OarMod": pack(
                    {
                        f"{OAR_BASE}/Pack/config.json": {
                            "name": "Pack",
                            "conditionPresets": [{
                                "name": "Base",
                                "conditions": [{
                                    "condition": "HasMagicEffect",
                                    "requiredVersion": "1.0.0.0",
                                    "Magic effect": {"pluginName": "Aroused.esm", "formID": "1"},
                                }],
                            }],
                        },
                        f"{OAR_BASE}/Pack/Sub/config.json": submod(
                            "Sub", 100,
                            conditions=[{"condition": "PRESET", "requiredVersion": "2.2.0.0",
                                         "Preset": "Base"}],
                        ),
                    }
                )
            },
            ["+OarMod"],
            plugins=["*SomeMod.esp"],
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        catalog = oar_discovery.discover(index, self.budget)
        found = catalog.submods[0]
        self.assertEqual(len(found.preset_conditions), 1)
        self.assertEqual(found.referenced_plugins, ["Aroused.esm"])
        missing = [i for i in oar_rules.config_issues(catalog, profile, self.budget)
                   if i.category == issues.MISSING_REFERENCE]
        self.assertEqual(missing[0].detail["absentPlugins"], ["Aroused.esm"])

    def test_missing_preset_is_a_config_error(self) -> None:
        _, profile, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod(
                        "Sub", 100,
                        conditions=[{"condition": "PRESET", "requiredVersion": "2.2.0.0",
                                     "Preset": "DoesNotExist"}],
                    ),
                }
            )
        )
        self.assertEqual(catalog.submods[0].missing_presets, ["DoesNotExist"])
        errors = [i for i in oar_rules.config_issues(catalog, profile, self.budget)
                  if i.category == issues.CONFIG_ERROR]
        self.assertEqual(len(errors), 1)
        self.assertEqual(errors[0].detail["missingPresets"], ["DoesNotExist"])

    def test_O08_unknown_condition_is_an_extension_requirement_not_an_error(self) -> None:
        _, profile, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod(
                        "Sub",
                        100,
                        conditions=[
                            {
                                "condition": "SomeThirdPartyCondition",
                                "requiredVersion": "1.0.0.0",
                                "requiredPlugin": "Mystery.dll",
                            }
                        ],
                    ),
                }
            )
        )
        found = catalog.submods[0]
        self.assertEqual(found.classification, oar_discovery.CUSTOM_CONDITION_REQUIRES_PLUGIN)
        self.assertTrue(found.valid, "an unknown condition must not invalidate the submod")
        self.assertEqual(found.conditions[0].required_plugin, "Mystery.dll")

        unknown = [
            i
            for i in oar_rules.config_issues(catalog, profile, self.budget)
            if i.category == issues.UNKNOWN_EXTENSION
        ]
        self.assertEqual(len(unknown), 1)
        self.assertEqual(unknown[0].confidence, issues.UNVERIFIABLE)

    def test_O08b_legacy_only_condition_names_are_recognised(self) -> None:
        """OAR registers hidden legacy factories; flagging them would be a false positive."""
        self.assertIn("IsEquippedRightType", oar_discovery.LEGACY_ONLY_CONDITIONS)
        self.assertIn("IsEquippedLeftType", oar_discovery.LEGACY_ONLY_CONDITIONS)
        self.assertIn("IsActorValueLessThan", oar_discovery.LEGACY_ONLY_CONDITIONS)
        self.assertTrue(oar_discovery.LEGACY_ONLY_CONDITIONS <= oar_discovery.KNOWN_CONDITIONS)
        self.assertFalse(oar_discovery.LEGACY_ONLY_CONDITIONS & oar_discovery.BUILTIN_CONDITIONS)

    def test_O09_dynamic_conditions_never_yield_an_absolute_winner(self) -> None:
        dynamic = [
            {"condition": "IsInCombat", "requiredVersion": "1.0.0.0"},
            {
                "condition": "IsRace",
                "requiredVersion": "1.0.0.0",
                "Race": {"pluginName": "Skyrim.esm", "formID": "13746"},
            },
            {"condition": "Random", "requiredVersion": "2.3.0.0"},
            {"condition": "IsEquippedType", "requiredVersion": "1.0.0.0"},
        ]
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/A/config.json": submod("A", 900, conditions=dynamic),
                    f"{OAR_BASE}/Pack/A/female/mt_run.hkx": HKX,
                    f"{OAR_BASE}/Pack/B/config.json": submod("B", 100, conditions=dynamic),
                    f"{OAR_BASE}/Pack/B/female/mt_run.hkx": HKX,
                }
            )
        )
        overlap = oar_rules.overlap_issues(catalog, self.budget)[0]
        self.assertEqual(overlap.confidence, issues.UNVERIFIABLE)
        self.assertEqual(overlap.runtime_verification, issues.RUNTIME_CONDITION_DEPENDENT)
        self.assertEqual(overlap.detail["unconditionalCount"], 0)
        for candidate in overlap.detail["candidates"]:
            self.assertEqual(
                candidate["classification"], oar_discovery.CONDITIONAL_RUNTIME_UNKNOWN
            )

    def test_O10_unsupported_field_is_preserved_and_marked_unknown(self) -> None:
        _, profile, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod(
                        "Sub", 100, someFutureFlagNobodyKnows=True, anotherOne="keep me"
                    ),
                }
            )
        )
        found = catalog.submods[0]
        self.assertTrue(found.valid, "an unknown field must not invalidate a submod")
        self.assertEqual(
            sorted(set(found.unknown_fields)), ["anotherOne", "someFutureFlagNobodyKnows"]
        )
        unsupported = [
            i
            for i in oar_rules.config_issues(catalog, profile, self.budget)
            if i.category == issues.UNSUPPORTED_FIELD
        ]
        self.assertEqual(len(unsupported), 1)
        self.assertEqual(unsupported[0].confidence, issues.UNVERIFIABLE)
        self.assertEqual(
            unsupported[0].detail["fields"], ["anotherOne", "someFutureFlagNobodyKnows"]
        )

    def test_missing_integer_priority_invalidates_the_submod(self) -> None:
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/NoPriority/config.json": submod("No Priority", None),
                    # A string priority is not an int, so OAR rejects it too.
                    f"{OAR_BASE}/Pack/StringPriority/config.json": {
                        "name": "Str",
                        "priority": "5",
                    },
                }
            )
        )
        self.assertEqual(len(catalog.submods), 2)
        for found in catalog.submods:
            self.assertEqual(found.classification, oar_discovery.INVALID_CONFIG)
            self.assertIn("priority", found.errors[0])
        self.assertEqual(catalog.active, [])

    def test_override_animations_folder_redirects_the_animation_root(self) -> None:
        _, _, catalog = self._catalog(
            pack(
                {
                    f"{OAR_BASE}/Pack/config.json": group(),
                    f"{OAR_BASE}/Pack/Sub/config.json": submod(
                        "Sub", 100, overrideAnimationsFolder="SharedAnims"
                    ),
                    f"{OAR_BASE}/Pack/SharedAnims/female/mt_idle.hkx": HKX,
                }
            )
        )
        self.assertEqual(catalog.submods[0].animation_files, ["female/mt_idle.hkx"])

    def _missing_refs(self, referenced: list[str], plugins: list[str], primary=None):
        """Scan one submod whose conditions reference ``referenced``; return MISSING_REFERENCE."""
        conditions = [
            {
                "condition": "HasSpell",
                "requiredVersion": "1.0.0.0",
                "Spell": {"pluginName": name, "formID": "1"},
            }
            for name in referenced
        ]
        root = build_instance(
            self.tmp / "inst",
            {
                "OarMod": pack(
                    {
                        f"{OAR_BASE}/Pack/config.json": group(),
                        f"{OAR_BASE}/Pack/Sub/config.json": submod(
                            "Sub", 100, conditions=conditions
                        ),
                    }
                )
            },
            ["+OarMod"],
            plugins=plugins,
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        catalog = oar_discovery.discover(index, self.budget)
        kwargs = {}
        if primary is not None:
            kwargs = {"primary_plugins": primary, "primary_source": "base masters + Skyrim.ccc"}
        return [
            i
            for i in oar_rules.config_issues(catalog, profile, self.budget, **kwargs)
            if i.category == issues.MISSING_REFERENCE
        ]

    def test_missing_reference_flags_an_installed_but_disabled_plugin(self) -> None:
        """Regression: loadorder.txt listed every plugin, so a disabled one counted as loaded."""
        found = self._missing_refs(["Frostfall.esp"], ["*Skyrim.esm", "Frostfall.esp"])
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].detail["disabledPlugins"], ["Frostfall.esp"])
        self.assertEqual(found[0].detail["absentPlugins"], [])
        self.assertIn("installed but disabled", found[0].what_was_observed)

    def test_missing_reference_ignores_an_unstarred_creation_club_primary_plugin(self) -> None:
        """Real case: ccQDRSSE001-SurvivalMode.esl is unstarred in plugins.txt but listed
        in Skyrim.ccc, so MO2 force-loads it (gameskyrimse.cpp primaryPlugins)."""
        primary = frozenset(mo2_profile.BASE_MASTERS | {"ccqdrsse001-survivalmode.esl"})
        found = self._missing_refs(
            ["ccQDRSSE001-SurvivalMode.esl"],
            ["ccQDRSSE001-SurvivalMode.esl"],
            primary=primary,
        )
        self.assertEqual(found, [], "a Skyrim.ccc plugin is loaded even without a star")

    def test_missing_reference_never_flags_a_base_master(self) -> None:
        """MO2 omits Skyrim.esm and the DLC masters from plugins.txt; they always load."""
        found = self._missing_refs(["Skyrim.esm", "Dragonborn.esm"], ["*SomeMod.esp"])
        self.assertEqual(found, [])

    def test_missing_reference_without_skyrim_ccc_downgrades_cc_plugins(self) -> None:
        found = self._missing_refs(["ccBGSSSE001-Fish.esm"], ["*SomeMod.esp"])
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].confidence, issues.UNVERIFIABLE)

    def test_missing_plugin_reference_is_likely_not_proven(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {
                "OarMod": pack(
                    {
                        f"{OAR_BASE}/Pack/config.json": group(),
                        f"{OAR_BASE}/Pack/Sub/config.json": submod(
                            "Sub",
                            100,
                            conditions=[
                                {
                                    "condition": "HasSpell",
                                    "requiredVersion": "1.0.0.0",
                                    "Spell": {"pluginName": "NotInstalled.esp", "formID": "1"},
                                }
                            ],
                        ),
                    }
                )
            },
            ["+OarMod"],
            plugins=["*Skyrim.esm", "*Dawnguard.esm"],
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        catalog = oar_discovery.discover(index, self.budget)
        missing = [
            i
            for i in oar_rules.config_issues(catalog, profile, self.budget)
            if i.category == issues.MISSING_REFERENCE
        ]
        self.assertEqual(len(missing), 1)
        self.assertEqual(missing[0].confidence, issues.LIKELY)
        self.assertEqual(missing[0].detail["missingPlugins"], ["NotInstalled.esp"])


if __name__ == "__main__":
    unittest.main()
