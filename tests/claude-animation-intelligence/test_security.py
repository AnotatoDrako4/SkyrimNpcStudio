"""Confinement, budgets and exit codes: cases S01-S04, S06, S08-S10."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

from _support import MT_IDLE, OAR_BASE, TOOL_ROOT, FixtureCase, build_instance, write_file
from engine import fingerprint, loose_winners, mo2_profile, oar_discovery, sandbox
from engine.sandbox import Budget, ConfinementError, WalkReport

CLI = TOOL_ROOT / "cli.py"


class ConfinementTests(FixtureCase):
    def test_S01_parent_traversal_is_refused(self) -> None:
        root = self.tmp / "root"
        (root / "inside").mkdir(parents=True)
        for attempt in ("../outside", "..", "inside/../../outside", "a/../../b"):
            with self.subTest(attempt=attempt):
                with self.assertRaises(ConfinementError):
                    sandbox.resolve_confined(root, attempt)

    def test_S01b_absolute_and_unc_paths_are_refused(self) -> None:
        root = self.tmp / "root"
        root.mkdir()
        for attempt in ("C:/Windows/System32", "//server/share/file.hkx", "\\\\server\\share"):
            with self.subTest(attempt=attempt):
                with self.assertRaises(ConfinementError):
                    sandbox.resolve_confined(root, attempt)

    def test_S01c_a_legitimate_relative_path_still_resolves(self) -> None:
        root = self.tmp / "root"
        write_file(root, "meshes/anim.hkx", b"x")
        resolved = sandbox.resolve_confined(root, "meshes/anim.hkx")
        self.assertTrue(resolved.is_file())
        self.assertTrue(str(resolved).startswith(str(Path(os.path.realpath(root)))))

    def test_S02_directory_symlink_is_skipped_not_followed(self) -> None:
        """On Windows a symlink needs privileges; without them this SKIPs with a reason."""
        outside = self.tmp / "outside"
        write_file(outside, "secret.hkx", b"should never be read")
        root = self.tmp / "root"
        write_file(root, "meshes/actors/character/animations/real.hkx", b"ok")
        link = root / "meshes/actors/character/animations/linked"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(
                "SKIP (justified): creating a directory symlink needs Windows privileges "
                f"this process does not have: {exc}"
            )

        report = WalkReport()
        seen = [rel for rel, _ in sandbox.walk_files(root, self.budget, report)]
        self.assertIn("meshes/actors/character/animations/real.hkx", seen)
        self.assertFalse(
            any("secret.hkx" in rel for rel in seen), "a symlink must not be traversed"
        )
        self.assertTrue(any("reparse point" in s["reason"] for s in report.skipped))

    def test_S02b_reparse_point_detection_is_false_for_a_plain_directory(self) -> None:
        plain = self.tmp / "plain"
        plain.mkdir()
        self.assertFalse(sandbox.is_reparse_point(plain))


class BudgetTests(FixtureCase):
    def test_S03_oversized_json_is_refused_without_reading_it_all(self) -> None:
        root = self.tmp / "root"
        big = write_file(root, "huge.json", "x" * 5000)
        with self.assertRaises(sandbox.BudgetError) as caught:
            sandbox.read_text_confined(big, Budget(max_json_bytes=1000))
        self.assertIn("over the 1000 byte limit", str(caught.exception))

    def test_S03b_oversized_json_in_a_scan_is_an_issue_not_a_crash(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {
                "FatMod": {
                    f"{OAR_BASE}/Pack/config.json": '{"name":"Pack"}',
                    f"{OAR_BASE}/Pack/Sub/config.json": '{"name":"Sub","priority":1,"pad":"'
                    + "x" * 3000
                    + '"}',
                }
            },
            ["+FatMod"],
        )
        instance, profile = self.load(root)
        tight = Budget(max_json_bytes=500, max_files=5000)
        index = loose_winners.build_index(instance, profile, tight, include_overwrite=False)
        catalog = oar_discovery.discover(index, tight)
        self.assertTrue(catalog.submods)
        self.assertFalse(catalog.submods[0].valid)
        self.assertIn("over the 500 byte limit", catalog.submods[0].errors[0])

    def test_S04_file_budget_marks_truncated_and_stops(self) -> None:
        files = {f"meshes/actors/character/animations/anim{i:03d}.hkx": b"x" for i in range(200)}
        root = build_instance(self.tmp / "inst", {"BigMod": files}, ["+BigMod"])
        instance, profile = self.load(root)
        tight = Budget(max_files=25)
        index = loose_winners.build_index(instance, profile, tight, include_overwrite=False)
        self.assertTrue(index.walk.is_truncated)
        self.assertEqual(index.walk.files_seen, 25)
        self.assertLessEqual(len(index.records), 25)

        found = loose_winners.conflict_issues(index, tight)
        truncated = [i for i in found if i.category == "TRUNCATED"]
        self.assertEqual(len(truncated), 1)
        self.assertEqual(truncated[0].severity, "medium")
        self.assertIn("Absence of a finding proves nothing", truncated[0].why_it_matters)

    def test_S04b_depth_budget_marks_truncated(self) -> None:
        root = self.tmp / "deep"
        write_file(root, "a/" * 30 + "anim.hkx", b"x")
        report = WalkReport()
        seen = list(sandbox.walk_files(root, Budget(max_depth=5), report))
        self.assertEqual(seen, [])
        self.assertTrue(any("depth limit" in r for r in report.truncated))

    def test_S06_invalid_utf8_text_is_refused_rather_than_silently_repaired(self) -> None:
        root = self.tmp / "root"
        broken = write_file(root, "broken.txt", b"\xff\xfe\x00bad bytes")
        with self.assertRaises(sandbox.DecodeError) as caught:
            sandbox.read_text_confined(broken, self.budget)
        self.assertIn("not valid UTF-8", str(caught.exception))

    def test_S06b_utf8_bom_is_accepted(self) -> None:
        root = self.tmp / "root"
        path = write_file(root, "bom.txt", b"\xef\xbb\xbfIsFemale()\n")
        self.assertEqual(sandbox.read_text_confined(path, self.budget), "IsFemale()\n")

    def test_S06c_a_missing_mod_folder_is_reported_not_fatal(self) -> None:
        root = build_instance(self.tmp / "inst", {"Real": {MT_IDLE: b"r"}}, ["+Ghost", "+Real"])
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        self.assertEqual(index.missing_mod_dirs, ["Ghost"])
        access = [
            i for i in loose_winners.conflict_issues(index, self.budget)
            if i.category == "ACCESS_ERROR"
        ]
        self.assertEqual(len(access), 1)
        self.assertEqual(access[0].detail["mods"], ["Ghost"])


class SnapshotAndManifestTests(FixtureCase):
    def test_S08_two_profiles_produce_independent_snapshots(self) -> None:
        root = build_instance(
            self.tmp / "inst",
            {"ModA": {MT_IDLE: b"A"}, "ModB": {MT_IDLE: b"B"}},
            ["+ModA", "+ModB"],
            profile="Alpha",
            extra_profiles={"Beta": ["+ModB", "+ModA"]},
        )
        instance = mo2_profile.load_instance(root, self.budget)
        alpha = mo2_profile.load_profile(instance, "Alpha", self.budget)
        beta = mo2_profile.load_profile(instance, "Beta", self.budget)
        index_a = loose_winners.build_index(instance, alpha, self.budget, include_overwrite=False)
        index_b = loose_winners.build_index(instance, beta, self.budget, include_overwrite=False)

        self.assertIsNot(index_a.records, index_b.records)
        self.assertEqual(index_a.conflicts()[0].winner.mod, "ModA")
        self.assertEqual(index_b.conflicts()[0].winner.mod, "ModB")

        fp_a = fingerprint.profile_fingerprint(instance, alpha, self.budget)
        fp_b = fingerprint.profile_fingerprint(instance, beta, self.budget)
        self.assertNotEqual(fp_a.digest, fp_b.digest, "different order must change the digest")

    def test_S08b_the_same_profile_yields_a_stable_fingerprint(self) -> None:
        root = build_instance(self.tmp / "inst", {"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        instance, profile = self.load(root)
        first = fingerprint.profile_fingerprint(instance, profile, self.budget)
        second = fingerprint.profile_fingerprint(instance, profile, self.budget)
        self.assertEqual(first.digest, second.digest)
        self.assertTrue(first.digest.startswith("sha256:"))

    def test_S09_manifest_is_regenerable_and_detects_tampering(self) -> None:
        out = self.tmp / "run"
        out.mkdir()
        (out / "result.json").write_text('{"a": 1}', encoding="utf-8")
        (out / "report.html").write_text("<!doctype html><html></html>", encoding="utf-8")

        manifest = fingerprint.write_manifest(out, self.budget)
        self.assertTrue(manifest.is_file())
        first = fingerprint.verify_manifest(out, self.budget)
        self.assertTrue(first["ok"])
        self.assertEqual(first["matched"], 2)

        (out / "result.json").write_text('{"a": 2}', encoding="utf-8")
        after = fingerprint.verify_manifest(out, self.budget)
        self.assertFalse(after["ok"])
        self.assertEqual(len(after["mismatched"]), 1)
        self.assertEqual(after["mismatched"][0]["path"], "result.json")

        (out / "report.html").unlink()
        self.assertIn("report.html", fingerprint.verify_manifest(out, self.budget)["missing"])

    def test_S09b_read_only_witness_covers_the_mo2_files_opened(self) -> None:
        root = build_instance(self.tmp / "inst", {"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        instance, profile = self.load(root)
        witness = fingerprint.read_only_witness(instance, profile, self.budget)
        names = [Path(p).name for p in witness["files"]]
        self.assertIn("ModOrganizer.ini", names)
        self.assertIn("modlist.txt", names)
        for digest in witness["files"].values():
            self.assertEqual(len(digest), 64)


class ExitCodeTests(FixtureCase):
    """S10: the CLI must distinguish PASS, input error, partial and internal failure."""

    def _run(self, *argv: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(CLI), *argv],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

    def test_S10_version_returns_zero(self) -> None:
        done = self._run("version")
        self.assertEqual(done.returncode, 0)
        self.assertIn("CLAUDE-B-ANIM-01", done.stdout)

    def test_S10b_a_clean_scan_returns_zero(self) -> None:
        root = build_instance(self.tmp / "inst", {"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        done = self._run(
            "scan", "--instance", str(root), "--profile", "TestProfile",
            "--out", str(self.tmp / "out"),
        )
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("NOT_VERIFIED_IN_GAME", done.stdout)
        self.assertTrue((self.tmp / "out" / "result.json").is_file())
        self.assertTrue((self.tmp / "out" / "report.html").is_file())

    def test_S10c_a_missing_instance_returns_the_input_error_code(self) -> None:
        done = self._run(
            "scan", "--instance", str(self.tmp / "nope"), "--profile", "X",
            "--out", str(self.tmp / "out"),
        )
        self.assertEqual(done.returncode, 2)
        self.assertIn("no ModOrganizer.ini", done.stderr)

    def test_S10d_an_unknown_profile_returns_the_input_error_code(self) -> None:
        root = build_instance(self.tmp / "inst", {"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        done = self._run(
            "scan", "--instance", str(root), "--profile", "NoSuchProfile",
            "--out", str(self.tmp / "out"),
        )
        self.assertEqual(done.returncode, 2)
        self.assertIn("not found", done.stderr)
        self.assertIn("TestProfile", done.stderr)

    def test_S10e_a_truncated_scan_returns_the_partial_code(self) -> None:
        files = {f"meshes/actors/character/animations/a{i:03d}.hkx": b"x" for i in range(60)}
        root = build_instance(self.tmp / "inst", {"BigMod": files}, ["+BigMod"])
        done = self._run(
            "scan", "--instance", str(root), "--profile", "TestProfile",
            "--out", str(self.tmp / "out"), "--max-files", "10",
        )
        self.assertEqual(done.returncode, 3, done.stdout + done.stderr)

    def test_S10f_verify_manifest_reports_a_mismatch_as_partial(self) -> None:
        out = self.tmp / "run"
        out.mkdir()
        (out / "result.json").write_text("{}", encoding="utf-8")
        fingerprint.write_manifest(out, self.budget)
        self.assertEqual(self._run("verify-manifest", "--run", str(out)).returncode, 0)
        (out / "result.json").write_text('{"changed": true}', encoding="utf-8")
        self.assertEqual(self._run("verify-manifest", "--run", str(out)).returncode, 3)

    def test_S10h_benchmark_refuses_a_real_instance_and_writes_nothing(self) -> None:
        """Regression from an audit: benchmark used to generate files into, or scan, any
        --fixture directory lacking ModOrganizer.ini, including a real MO2 folder."""
        real = build_instance(self.tmp / "real", {"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        before = sorted(p.relative_to(real).as_posix() for p in real.rglob("*"))
        done = self._run("benchmark", "--fixture", str(real), "--out", str(self.tmp / "o"),
                         "--repetitions", "1")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("synthetic fixtures", done.stderr)
        after = sorted(p.relative_to(real).as_posix() for p in real.rglob("*"))
        self.assertEqual(before, after, "a refused benchmark must not create any file")

        profile_dir = real / "profiles" / "TestProfile"
        done = self._run("benchmark", "--fixture", str(profile_dir), "--out",
                         str(self.tmp / "o2"), "--repetitions", "1")
        self.assertEqual(done.returncode, 2)
        self.assertFalse((profile_dir / "ModOrganizer.ini").exists())

    def test_S10i_benchmark_generates_into_an_empty_dir_and_reuses_it(self) -> None:
        fixture = self.tmp / "fresh"
        args = ["benchmark", "--fixture", str(fixture), "--out", str(self.tmp / "o"),
                "--repetitions", "1", "--mods", "3", "--files-per-mod", "4"]
        first = self._run(*args)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertTrue((fixture / ".anim-lab-synthetic-fixture").is_file())
        second = self._run(*args)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertNotIn("generating", second.stdout, "a marked fixture is reused, not rebuilt")

    def test_S10g_verify_manifest_without_a_manifest_is_an_input_error(self) -> None:
        empty = self.tmp / "empty"
        empty.mkdir()
        self.assertEqual(self._run("verify-manifest", "--run", str(empty)).returncode, 2)


if __name__ == "__main__":
    unittest.main()
