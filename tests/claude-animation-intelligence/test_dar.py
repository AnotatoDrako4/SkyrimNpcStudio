"""Legacy DAR and behaviour-engine observation: cases DP01-DP06."""

from __future__ import annotations

import json
import unittest

from _support import DAR_BASE, OAR_BASE, FixtureCase, build_instance
from engine import behavior_outputs, dar_discovery, hkx_probe, issues, loose_winners, oar_discovery

HKX = bytes.fromhex("57e0e05710c0c010") + b"\x00" * 64


class DarLexerTests(unittest.TestCase):
    """Grammar from Conditions.cpp::CreateConditionFromString."""

    def test_DP02_bare_condition_with_no_parentheses(self) -> None:
        lines = dar_discovery.lex_conditions("IsFemale\n")
        self.assertEqual(lines[0].name, "IsFemale")
        self.assertEqual(lines[0].argument, "")
        self.assertTrue(lines[0].recognised)

    def test_DP02_not_prefix_and_argument_are_separated(self) -> None:
        lines = dar_discovery.lex_conditions('NOT IsActorBase("Skyrim.esm" | 0x000007)\n')
        self.assertTrue(lines[0].negated)
        self.assertEqual(lines[0].name, "IsActorBase")
        self.assertIn("Skyrim.esm", lines[0].argument)

    def test_DP02_trailing_OR_marks_an_or_block(self) -> None:
        lines = dar_discovery.lex_conditions("IsSneaking() OR\nIsRunning()\n")
        self.assertTrue(lines[0].or_continuation)
        self.assertFalse(lines[1].or_continuation)

    def test_DP02_semicolon_is_a_comment_not_a_condition(self) -> None:
        lines = dar_discovery.lex_conditions("; author note\nIsFemale()\n")
        self.assertEqual(lines[0].kind, "comment")
        self.assertEqual(lines[1].kind, "condition")

    def test_DP02_unbalanced_parenthesis_reports_the_exact_line(self) -> None:
        lines = dar_discovery.lex_conditions('IsFemale()\nIsActorBase("Skyrim.esm"\nIsChild()\n')
        bad = [line for line in lines if line.kind == "error"]
        self.assertEqual(len(bad), 1)
        self.assertEqual(bad[0].number, 2)
        self.assertIn("unbalanced parenthesis", bad[0].error)

    def test_DP02_keyword_without_a_function_is_an_error(self) -> None:
        lines = dar_discovery.lex_conditions("NOT\n")
        self.assertEqual(lines[0].kind, "error")
        self.assertIn("no condition function", lines[0].error)

    def test_DP02_unknown_condition_name_is_flagged_as_invalid(self) -> None:
        lines = dar_discovery.lex_conditions("TotallyMadeUpCondition()\n")
        self.assertFalse(lines[0].recognised)
        self.assertIn("InvalidCondition", lines[0].error)

    def test_DP02_legacy_only_names_are_valid_and_marked_legacy(self) -> None:
        lines = dar_discovery.lex_conditions("IsEquippedRightType(1) OR\nIsEquippedLeftType(2)\n")
        for line in lines:
            self.assertTrue(line.recognised, f"{line.name} is a real legacy DAR condition")
            self.assertTrue(line.legacy_only)
            self.assertEqual(line.error, "")

    def test_DP02_IsEquippedShout_alias_resolves(self) -> None:
        lines = dar_discovery.lex_conditions("IsEquippedShout(1)\n")
        self.assertTrue(lines[0].recognised)


class DarDiscoveryTests(FixtureCase):
    def _scan(self, mods: dict[str, dict[str, bytes | str]], modlist: list[str]):
        root = build_instance(self.tmp / "inst", mods, modlist)
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        dar = dar_discovery.discover(index, self.budget)
        oar = oar_discovery.discover(index, self.budget)
        return index, dar, oar

    def test_DP01_custom_conditions_directory_is_found_with_its_priority(self) -> None:
        _, dar, _ = self._scan(
            {
                "DarMod": {
                    f"{DAR_BASE}/_CustomConditions/68450/_conditions.txt": "NOT IsWeaponDrawn()\n",
                    f"{DAR_BASE}/_CustomConditions/68450/mt_sneakidle.hkx": HKX,
                }
            },
            ["+DarMod"],
        )
        self.assertEqual(len(dar.rule_sets), 1)
        rule = dar.rule_sets[0]
        self.assertEqual(rule.variant, "_CustomConditions")
        self.assertEqual(rule.priority, 68450)
        self.assertEqual(rule.condition_count, 1)
        self.assertEqual(rule.animation_files, ["mt_sneakidle.hkx"])
        self.assertEqual(rule.syntax_errors, [])

    def test_DP01b_actor_base_variant_records_plugin_and_formid(self) -> None:
        _, dar, _ = self._scan(
            {
                "DarMod": {
                    f"{DAR_BASE}/Skyrim.esm/0001A694/_conditions.txt": "IsFemale()\n",
                    f"{DAR_BASE}/Skyrim.esm/0001A694/mt_idle.hkx": HKX,
                }
            },
            ["+DarMod"],
        )
        rule = dar.rule_sets[0]
        self.assertEqual(rule.variant, "actor-base")
        self.assertEqual(rule.plugin_name, "Skyrim.esm")
        self.assertEqual(rule.form_id, "0001A694")
        self.assertIsNone(rule.priority)

    def test_DP02_malformed_conditions_file_yields_a_line_level_diagnosis(self) -> None:
        _, dar, oar = self._scan(
            {
                "DarMod": {
                    f"{DAR_BASE}/_CustomConditions/100/_conditions.txt": (
                        'IsFemale()\nIsActorBase("Skyrim.esm"\nNonsenseCondition()\n'
                    )
                }
            },
            ["+DarMod"],
        )
        found = [
            i
            for i in dar_discovery.dar_issues(dar, oar, self.budget)
            if i.category == issues.CONFIG_ERROR
        ]
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "high")
        self.assertEqual(found[0].confidence, issues.PROVEN)
        self.assertEqual(found[0].detail["faultCount"], 2)
        self.assertEqual([f["line"] for f in found[0].detail["faults"]], [2, 3])

    def test_DP03_migration_candidate_is_never_applied_automatically(self) -> None:
        _, dar, oar = self._scan(
            {
                "DarMod": {
                    f"{DAR_BASE}/_CustomConditions/100/_conditions.txt": "IsFemale()\n",
                    f"{DAR_BASE}/_CustomConditions/100/mt_idle.hkx": HKX,
                },
                "OarMod": {
                    f"{OAR_BASE}/Pack/config.json": json.dumps({"name": "Pack"}),
                    f"{OAR_BASE}/Pack/Sub/config.json": json.dumps(
                        {"name": "Sub", "priority": 100}
                    ),
                    f"{OAR_BASE}/Pack/Sub/mt_idle.hkx": HKX,
                },
            },
            ["+OarMod", "+DarMod"],
        )
        found = [
            i
            for i in dar_discovery.dar_issues(dar, oar, self.budget)
            if i.category == issues.MIGRATION_CANDIDATE and "_CustomConditions" in i.subject
        ]
        self.assertEqual(len(found), 1)
        migration = found[0]
        self.assertEqual(migration.confidence, issues.UNVERIFIABLE)
        self.assertIn("NOT proof", migration.why_it_matters)
        self.assertIn("NOT_APPLIED", migration.action)
        self.assertEqual(migration.detail["sharedFilenames"], ["mt_idle.hkx"])

    def test_DP04_patcher_output_is_read_as_data_and_never_executed(self) -> None:
        index, _, _ = self._scan(
            {
                "FnisMod": {
                    "meshes/actors/character/behaviors/FNIS_Test_Behavior.hkx": HKX,
                    "meshes/actors/character/animations/Test/FNIS_Test_List.txt": (
                        "' FNIS Fores New Idles in Skyrim\nVersion V7.6\n"
                    ),
                }
            },
            ["+FnisMod"],
        )
        report = behavior_outputs.detect(index, self.budget)
        self.assertEqual(report.behavior_files, 1)
        self.assertEqual(report.tools_with_evidence, ["FNIS"])
        kinds = {e.kind for e in report.evidence}
        self.assertIn("filename", kinds)
        self.assertIn("log-line", kinds)

        probe = list(report.probes.values())[0]
        self.assertEqual(probe["headerKind"], hkx_probe.KIND_PACKFILE)
        self.assertIn("not deserialised", probe["note"])

        found = behavior_outputs.behavior_issues(report, index)
        present = [i for i in found if i.category == issues.OUTPUT_PRESENT_UNVERIFIED]
        self.assertEqual(len(present), 1)
        self.assertEqual(present[0].runtime_verification, issues.NOT_VERIFIED_IN_GAME)
        self.assertIn("does not prove", present[0].detail["note"])

    def test_DP05_same_behavior_file_in_two_mods_is_a_file_conflict(self) -> None:
        index, _, _ = self._scan(
            {
                "BehaviorA": {"meshes/actors/character/behaviors/0_master.hkx": HKX + b"A"},
                "BehaviorB": {"meshes/actors/character/behaviors/0_master.hkx": HKX + b"B"},
            },
            ["+BehaviorB", "+BehaviorA"],
        )
        report = behavior_outputs.detect(index, self.budget)
        self.assertEqual(report.conflicts, 1)
        self.assertEqual(report.master_files, ["meshes/actors/character/behaviors/0_master.hkx"])

        found = behavior_outputs.behavior_issues(report, index)
        conflict = [i for i in found if i.category == issues.LOOSE_FILE_OVERRIDE]
        self.assertEqual(len(conflict), 1)
        self.assertTrue(conflict[0].detail["notAnOarRuleConflict"])
        self.assertIn("not an OAR", conflict[0].why_it_matters)
        self.assertEqual(conflict[0].providers[0].mod, "BehaviorB")

    def test_DP06_absence_of_a_patcher_is_a_state_not_an_error(self) -> None:
        index, _, _ = self._scan(
            {"PlainMod": {"meshes/actors/character/animations/mt_idle.hkx": HKX}},
            ["+PlainMod"],
        )
        report = behavior_outputs.detect(index, self.budget)
        self.assertEqual(report.behavior_files, 0)
        self.assertEqual(report.logs_seen, 0)
        self.assertEqual(report.tools_with_evidence, [])

        found = behavior_outputs.behavior_issues(report, index)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].detail["state"], "ABSENT")
        self.assertEqual(found[0].severity, "info")
        self.assertIn("not an error", found[0].why_it_matters)

    def test_DP06b_a_mod_named_after_a_patcher_is_not_evidence(self) -> None:
        """Attribution must come from a real pattern, never from a cosmetic mod name."""
        index, _, _ = self._scan(
            {
                "My Pandora Animations Collection": {
                    "meshes/actors/character/behaviors/custom_behavior.hkx": HKX
                }
            },
            ["+My Pandora Animations Collection"],
        )
        report = behavior_outputs.detect(index, self.budget)
        self.assertEqual(report.behavior_files, 1)
        self.assertEqual(
            report.tools_with_evidence, [], "the mod name alone must not attribute a tool"
        )

    def test_empty_hkx_is_reported_as_empty_not_as_a_valid_animation(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"EmptyMod": {"meshes/actors/character/animations/broken.hkx": b""}},
            ["+EmptyMod"],
        )
        path = root / "mods/EmptyMod/meshes/actors/character/animations/broken.hkx"
        probe = hkx_probe.probe(path)
        self.assertEqual(probe.kind, hkx_probe.KIND_EMPTY)
        self.assertEqual(probe.size, 0)

    def test_unrecognised_header_is_not_called_broken(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"OddMod": {"meshes/actors/character/animations/odd.hkx": b"TAG0" + b"\x00" * 32}},
            ["+OddMod"],
        )
        probe = hkx_probe.probe(root / "mods/OddMod/meshes/actors/character/animations/odd.hkx")
        self.assertEqual(probe.kind, hkx_probe.KIND_UNRECOGNISED)
        self.assertIn("not necessarily a broken file", probe.note)


if __name__ == "__main__":
    unittest.main()
