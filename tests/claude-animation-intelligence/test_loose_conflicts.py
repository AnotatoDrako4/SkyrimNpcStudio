"""Loose-file precedence: cases M02, M05, M06, M07, M08, M10."""

from __future__ import annotations

import unittest

from _support import MT_IDLE, FixtureCase, build_instance
from engine import issues, loose_winners


class LooseConflictTests(FixtureCase):
    def _index(self, root, profile: str = "TestProfile"):
        instance, prof = self.load(root, profile)
        return loose_winners.build_index(instance, prof, self.budget, include_overwrite=False)

    def test_M02_three_enabled_mods_same_path_exact_winner_and_losers(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {
                "LowMod": {MT_IDLE: b"LOW"},
                "MidMod": {MT_IDLE: b"MID"},
                "HighMod": {MT_IDLE: b"HIGH"},
            },
            ["+HighMod", "+MidMod", "+LowMod"],  # top-down: HighMod wins
        )
        index = self._index(root)
        conflicts = index.conflicts()
        self.assertEqual(len(conflicts), 1)
        record = conflicts[0]
        self.assertEqual(record.winner.mod, "HighMod")
        self.assertEqual([e.mod for e in record.losers], ["MidMod", "LowMod"])
        self.assertFalse(record.tied)

        found = [i for i in loose_winners.conflict_issues(index, self.budget)
                 if i.category == issues.LOOSE_FILE_OVERRIDE]
        self.assertEqual(len(found), 1)
        issue = found[0]
        self.assertEqual(issue.severity, "high")
        self.assertEqual(issue.confidence, issues.PROVEN)
        self.assertEqual(issue.providers[0].role, "winner")
        self.assertEqual(issue.providers[0].mod, "HighMod")
        self.assertFalse(issue.detail["identicalContent"])

    def test_M03_disabled_mod_never_wins_the_file(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"DisabledTop": {MT_IDLE: b"TOP"}, "EnabledLow": {MT_IDLE: b"LOW"}},
            ["-DisabledTop", "+EnabledLow"],
        )
        index = self._index(root)
        record = next(iter(index.records.values()))
        self.assertEqual([e.mod for e in record.entries], ["EnabledLow"])
        self.assertFalse(record.is_conflict, "one provider is not a conflict")

    def test_M05_same_basename_different_path_is_not_an_override(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {
                "ModA": {"meshes/actors/character/animations/mt_idle.hkx": b"A"},
                "ModB": {"meshes/actors/bear/animations/mt_idle.hkx": b"B"},
            },
            ["+ModA", "+ModB"],
        )
        index = self._index(root)
        self.assertEqual(index.conflicts(), [], "different paths must not collide")
        found = loose_winners.conflict_issues(index, self.budget)
        similar = [i for i in found if i.category == issues.SIMILAR_FILENAME]
        self.assertEqual(len(similar), 1)
        self.assertEqual(similar[0].severity, "info")
        self.assertNotIn(issues.LOOSE_FILE_OVERRIDE, {i.category for i in found})

    def test_M06_case_differences_collide_on_windows_semantics(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {
                "UpperMod": {"meshes/actors/character/animations/MT_IDLE.HKX": b"UPPER"},
                "LowerMod": {"meshes/actors/character/animations/mt_idle.hkx": b"LOWER"},
            },
            ["+UpperMod", "+LowerMod"],
        )
        index = self._index(root)
        self.assertEqual(len(index.records), 1, "case-only difference is the same Data path")
        record = next(iter(index.records.values()))
        self.assertTrue(record.is_conflict)
        self.assertEqual(record.winner.mod, "UpperMod")
        spelling = [i for i in loose_winners.conflict_issues(index, self.budget)
                    if i.category == issues.SPELLING_VARIANCE]
        self.assertEqual(len(spelling), 1)
        self.assertEqual(spelling[0].confidence, issues.UNVERIFIABLE)

    def test_M07_two_profiles_do_not_share_winners(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"ModA": {MT_IDLE: b"A"}, "ModB": {MT_IDLE: b"B"}},
            ["+ModA", "+ModB"],
            profile="Alpha",
            extra_profiles={"Beta": ["+ModB", "+ModA"]},
        )
        alpha = self._index(root, "Alpha")
        beta = self._index(root, "Beta")
        self.assertEqual(alpha.conflicts()[0].winner.mod, "ModA")
        self.assertEqual(beta.conflicts()[0].winner.mod, "ModB")
        self.assertNotEqual(
            alpha.conflicts()[0].winner.mod,
            beta.conflicts()[0].winner.mod,
            "each profile must be resolved from its own modlist",
        )

    def test_M08_unicode_and_spaced_mod_names_are_handled(self) -> None:
        names = ["Añoranza Pose Pack", "Mod With Spaces", "日本語モッド"]
        root = build_instance(
            self.tmp / "inst",
            {name: {MT_IDLE: name.encode("utf-8")} for name in names},
            [f"+{name}" for name in names],
        )
        index = self._index(root)
        record = next(iter(index.records.values()))
        self.assertEqual({e.mod for e in record.entries}, set(names))
        self.assertEqual(record.winner.mod, names[0], "first listed name is highest priority")

    def test_M10_bsa_contents_are_not_presented_as_loose_files(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"ArchivedMod": {"Archived.bsa": b"FAKEBSA-not-parsed"}, "LooseMod": {MT_IDLE: b"L"}},
            ["+ArchivedMod", "+LooseMod"],
        )
        index = self._index(root)
        loose_keys = set(index.records)
        self.assertNotIn("archived.bsa", loose_keys, "an archive is not a loose animation")
        self.assertEqual(len(index.archives), 1)
        self.assertEqual(index.archives[0]["mod"], "ArchivedMod")
        limit = [i for i in loose_winners.conflict_issues(index, self.budget)
                 if i.category == issues.BSA_COVERAGE_LIMIT]
        self.assertEqual(len(limit), 1, "the archive limit must be stated, not silently ignored")
        self.assertIn("ONLY inside an archive", limit[0].why_it_matters)

    def test_identical_content_override_is_reported_as_harmless(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"ModA": {MT_IDLE: b"SAME"}, "ModB": {MT_IDLE: b"SAME"}},
            ["+ModA", "+ModB"],
        )
        index = self._index(root)
        issue = next(
            i for i in loose_winners.conflict_issues(index, self.budget)
            if i.category == issues.LOOSE_FILE_OVERRIDE
        )
        self.assertTrue(issue.detail["identicalContent"])
        self.assertIn("harmless", issue.why_it_matters)
        self.assertEqual(
            issue.providers[0].sha256,
            issue.providers[1].sha256,
            "both copies must carry the same recorded digest",
        )

    def test_top_level_textures_and_sound_are_pruned_and_counted(self) -> None:
        stray = "textures/actors/character/animations/stray.hkx"
        root = build_instance(
            self.tmp / "inst",
            {
                "ModA": {
                    MT_IDLE: b"A",
                    stray: b"never loaded by the engine",
                    "sound/fx/a.wav": b"x",
                    "CalienteTools/BodySlide/x.xml": b"x",
                }
            },
            ["+ModA"],
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        self.assertNotIn(stray, {r.display for r in index.records.values()})
        self.assertEqual(index.walk.pruned_dirs, 3, "textures, sound, CalienteTools")
        self.assertEqual(index.walk.skipped, [], "a policy prune is not an access failure")

        full = loose_winners.build_index(
            instance, profile, self.budget, include_overwrite=False, full_walk=True
        )
        self.assertIn(stray, {r.display for r in full.records.values()})
        self.assertEqual(full.walk.pruned_dirs, 0)

    def test_pruning_applies_only_at_a_mods_top_level(self) -> None:
        nested = "meshes/actors/character/animations/sound/mt_idle_sound.hkx"
        root = build_instance(self.tmp / "inst", {"ModA": {nested: b"A"}}, ["+ModA"])
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        self.assertIn(nested, {r.display for r in index.records.values()})

    def test_overwrite_folder_outranks_every_mod_when_included(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"ModA": {MT_IDLE: b"A"}},
            ["+ModA"],
            overwrite={MT_IDLE: b"OVERWRITE"},
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(
            instance, profile, self.budget, include_overwrite=True
        )
        record = next(iter(index.records.values()))
        self.assertEqual(record.winner.mod, "<Overwrite>")


if __name__ == "__main__":
    unittest.main()
