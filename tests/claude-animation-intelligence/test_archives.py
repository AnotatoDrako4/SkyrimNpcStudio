"""BSA name-table reading and archive analysis.

Synthetic archives carry a real header and directory but no file data: the reader
never touches data, so none is needed to exercise it.
"""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from _support import MT_IDLE, FixtureCase, build_instance
from engine import bsa_index, issues, loose_winners
from engine.sandbox import Budget


def make_bsa(folders: dict[str, list[str]], version: int = 105, flags: int = 0x3) -> bytes:
    """Build a minimal archive directory in the verified Skyrim layout."""
    record = bsa_index.FOLDER_RECORD[version]
    names = list(folders)
    file_count = sum(len(v) for v in folders.values())
    folder_name_len = sum(len(n) + 1 for n in names)
    file_name_len = sum(len(f) + 1 for v in folders.values() for f in v)
    out = bytearray(
        struct.pack("<4sIIIIIIIHH", b"BSA\x00", version, 36, flags, len(names), file_count,
                    folder_name_len, file_name_len, 0, 0)
    )
    for name in names:
        count = len(folders[name])
        out += record.pack(0, count, 0, 0) if version == 105 else record.pack(0, count, 0)
    for name in names:
        encoded = name.encode("cp1252") + b"\x00"
        out += bytes([len(encoded)]) + encoded
        out += b"\x00" * (16 * len(folders[name]))
    for name in names:
        for file in folders[name]:
            out += file.encode("cp1252") + b"\x00"
    return bytes(out)


ANIM_DIR = "meshes\\actors\\character\\animations"


class ReaderTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="anim-lab-bsa-")
        self.tmp = Path(self._tmp.name)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, name: str, data: bytes) -> Path:
        path = self.tmp / name
        path.write_bytes(data)
        return path

    def test_A01_reads_names_from_a_v105_directory(self) -> None:
        path = self._write("a.bsa", make_bsa({ANIM_DIR: ["mt_idle.hkx", "mt_walk.hkx"],
                                              "textures\\x": ["a.dds"]}))
        index = bsa_index.read_bsa_names(path)
        self.assertTrue(index.ok, index.error)
        self.assertEqual(index.version, 105)
        self.assertIn("meshes/actors/character/animations/mt_idle.hkx", index.paths)
        self.assertEqual(len(index.paths), 3)

    def test_A02_bad_magic_ba2_and_unsupported_versions_are_errors_not_crashes(self) -> None:
        good = make_bsa({"a": ["b.nif"]})
        cases = {
            "notbsa.bsa": b"PK\x03\x04" + b"\x00" * 64,
            "fallout.ba2": b"BTDX" + b"\x00" * 64,
            "oblivion.bsa": good[:4] + struct.pack("<I", 103) + good[8:],
            "short.bsa": b"BSA\x00\x69",
        }
        for name, data in cases.items():
            with self.subTest(name=name):
                index = bsa_index.read_bsa_names(self._write(name, data))
                self.assertFalse(index.ok)
                self.assertEqual(index.paths, [])

    def test_A03_counts_pointing_past_eof_are_refused_before_reading(self) -> None:
        data = bytearray(make_bsa({ANIM_DIR: ["mt_idle.hkx"]}))
        struct.pack_into("<I", data, 20, 1_000_000)  # fileCount far beyond the file
        index = bsa_index.read_bsa_names(self._write("lying.bsa", bytes(data)))
        self.assertFalse(index.ok)
        self.assertIn("past the end", index.error)

    def test_A03b_layout_mismatch_is_refused_rather_than_half_read(self) -> None:
        data = bytearray(make_bsa({ANIM_DIR: ["mt_idle.hkx", "mt_walk.hkx"]}))
        struct.pack_into("<I", data, 36 + 8, 1)  # folder count says 1, header says 2
        index = bsa_index.read_bsa_names(self._write("mismatch.bsa", bytes(data)))
        self.assertFalse(index.ok)

    def test_A03c_archive_without_embedded_names_is_reported(self) -> None:
        index = bsa_index.read_bsa_names(
            self._write("nonames.bsa", make_bsa({ANIM_DIR: ["x.hkx"]}, flags=0x1))
        )
        self.assertFalse(index.ok)
        self.assertIn("names", index.error)

    def test_A08_v104_layout_is_read(self) -> None:
        path = self._write("le.bsa", make_bsa({ANIM_DIR: ["mt_idle.hkx"]}, version=104))
        index = bsa_index.read_bsa_names(path)
        self.assertTrue(index.ok, index.error)
        self.assertEqual(index.version, 104)

    def test_plugin_candidates_cover_suffixed_archive_names(self) -> None:
        self.assertIn("foo.esp", bsa_index._plugin_candidates("Foo - Textures.bsa"))
        self.assertIn("foo.esm", bsa_index._plugin_candidates("Foo.bsa"))

    def test_plugin_candidates_handle_plugin_names_containing_a_dash(self) -> None:
        """Real case: 'Anchor SE - Johnskyrim.esp' owns 'Anchor SE - Johnskyrim - Textures.bsa'."""
        candidates = bsa_index._plugin_candidates("Anchor SE - Johnskyrim - Textures.bsa")
        self.assertIn("anchor se - johnskyrim.esp", candidates)
        self.assertIn("anchor se.esp", candidates)
        self.assertIn("anchor se - johnskyrim - textures.esp", candidates)

    def test_A11_directory_larger_than_budget_is_refused(self) -> None:
        path = self._write("big.bsa", make_bsa({ANIM_DIR: [f"c{i:05d}.hkx" for i in range(2000)]}))
        original = bsa_index.MAX_INDEX_BYTES
        try:
            bsa_index.MAX_INDEX_BYTES = 1024
            index = bsa_index.read_bsa_names(path, Budget())
        finally:
            bsa_index.MAX_INDEX_BYTES = original
        self.assertFalse(index.ok)
        self.assertIn("budget", index.error)


class AnalysisTests(FixtureCase):
    def _scan(self, mods, modlist, loaded=None, listed=None):
        root = build_instance(self.tmp / "inst", mods, modlist)
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        catalog = bsa_index.index_archives(index, self.budget, loaded, listed)
        return index, catalog, bsa_index.archive_issues(catalog, index, self.budget)

    def test_A04_archive_only_animation_becomes_visible(self) -> None:
        index, catalog, found = self._scan(
            {"Packed": {"Packed.bsa": make_bsa({ANIM_DIR: ["mt_sneak.hkx"]})}}, ["+Packed"]
        )
        self.assertEqual(catalog.summary()["animationPathsInArchives"], 1)
        only = [i for i in found if i.category == issues.ARCHIVE_ONLY_ANIMATION]
        self.assertEqual(len(only), 1)
        self.assertEqual(only[0].detail["count"], 1)
        self.assertNotIn(
            "meshes/actors/character/animations/mt_sneak.hkx",
            {r.display for r in index.records.values()},
            "M10: an archived path is never presented as a loose file",
        )

    def test_A05_loose_copy_shadows_archive_with_likely_confidence(self) -> None:
        _, _, found = self._scan(
            {
                "Packed": {"Packed.bsa": make_bsa({ANIM_DIR: ["mt_idle.hkx"]})},
                "Loose": {MT_IDLE: b"L"},
            },
            ["+Loose", "+Packed"],
        )
        shadow = [i for i in found if i.category == issues.LOOSE_OVERRIDES_ARCHIVE]
        self.assertEqual(len(shadow), 1)
        self.assertEqual(shadow[0].confidence, issues.LIKELY)
        self.assertEqual(shadow[0].providers[0].role, "loose")
        self.assertEqual([i for i in found if i.category == issues.ARCHIVE_ONLY_ANIMATION], [])

    def test_A06_two_archives_with_one_path_never_get_a_winner(self) -> None:
        _, _, found = self._scan(
            {
                "PackA": {"PackA.bsa": make_bsa({ANIM_DIR: ["mt_run.hkx"]})},
                "PackB": {"PackB.bsa": make_bsa({ANIM_DIR: ["mt_run.hkx"]})},
            },
            ["+PackA", "+PackB"],
        )
        overlap = [i for i in found if i.category == issues.ARCHIVE_PATH_OVERLAP]
        self.assertEqual(len(overlap), 1)
        self.assertEqual(overlap[0].confidence, issues.UNVERIFIABLE)
        self.assertIn("plugin", overlap[0].why_it_matters)
        self.assertNotIn("winner", {p.role for p in overlap[0].providers})

    def test_A07_plugin_association_marks_loaded_and_disabled_archives(self) -> None:
        _, catalog, _ = self._scan(
            {
                "On": {"On.bsa": make_bsa({ANIM_DIR: ["a.hkx"]})},
                "Off": {"Off - Animations.bsa": make_bsa({ANIM_DIR: ["b.hkx"]})},
                "Orphan": {"Orphan.bsa": make_bsa({ANIM_DIR: ["c.hkx"]})},
            },
            ["+On", "+Off", "+Orphan"],
            loaded={"on.esp", "skyrim.esm"},
            listed={"on.esp", "off.esp"},
        )
        state = {a.path: a.plugin_loaded for a in catalog.archives}
        self.assertEqual(state["On.bsa"], "yes")
        self.assertEqual(state["Off - Animations.bsa"], "no")
        self.assertEqual(state["Orphan.bsa"], "unknown")

    def test_A09_coverage_note_lists_only_archives_that_could_not_be_read(self) -> None:
        index, catalog, found = self._scan(
            {
                "Good": {"Good.bsa": make_bsa({ANIM_DIR: ["a.hkx"]})},
                "Bad": {"Bad.bsa": b"BTDX" + b"\x00" * 40},
            },
            ["+Good", "+Bad"],
        )
        self.assertEqual(len(catalog.failed), 1)
        unverified = [i for i in found if i.category == issues.UNVERIFIED_FORMAT]
        self.assertEqual([i.subject for i in unverified], ["Bad.bsa"])
        limit = [
            i for i in loose_winners.conflict_issues(index, self.budget)
            if i.category == issues.BSA_COVERAGE_LIMIT
        ]
        self.assertEqual(len(limit), 1)
        self.assertEqual([a["path"] for a in limit[0].detail["archives"]], ["Bad.bsa"])

    def test_A10_v104_archives_are_flagged_as_not_empirically_verified(self) -> None:
        _, _, found = self._scan(
            {"Old": {"Old.bsa": make_bsa({ANIM_DIR: ["a.hkx"]}, version=104)}}, ["+Old"]
        )
        flagged = [i for i in found if i.subject == "archive version not verified on this machine"]
        self.assertEqual(len(flagged), 1)


if __name__ == "__main__":
    unittest.main()
