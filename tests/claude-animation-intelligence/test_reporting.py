"""Report generation: cases S05 (HTML escaping) and S07 (determinism)."""

from __future__ import annotations

import json
import re
import unittest

from _support import MT_IDLE, FixtureCase, build_instance
from engine import (
    behavior_outputs,
    dar_discovery,
    fingerprint,
    issues,
    loose_winners,
    oar_discovery,
    reports,
)

XSS_MOD = "<img src=x onerror=alert(1)>"
XSS_SCRIPT_MOD = "</script><script>alert('pwned')</script>"


class ReportTests(FixtureCase):
    def _result(self, mods: dict[str, dict[str, bytes | str]], modlist: list[str]):
        root = build_instance(self.tmp / "inst", mods, modlist)
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        oar = oar_discovery.discover(index, self.budget)
        dar = dar_discovery.discover(index, self.budget)
        behavior = behavior_outputs.detect(index, self.budget)
        found = loose_winners.conflict_issues(index, self.budget)
        found.extend(behavior_outputs.behavior_issues(behavior, index))
        return reports.ScanResult(
            instance=instance,
            profile=profile,
            fingerprint=fingerprint.profile_fingerprint(instance, profile, self.budget),
            index=index,
            oar=oar,
            dar=dar,
            behavior=behavior,
            issues=found,
            duration_seconds=0.0,
        )

    def _hostile_result(self, hostile: str):
        """Inject hostile markup where a real mod author actually controls the text.

        Windows forbids ``<`` and ``>`` in a folder name, so the realistic vectors
        are an OAR submod ``name`` (arbitrary author-supplied JSON) and a
        ``modlist.txt`` row, which is free text naming a folder that may not exist.
        """
        oar_base = "meshes/actors/character/animations/OpenAnimationReplacer"
        mods = {
            "HostileOar": {
                f"{oar_base}/Pack/config.json": json.dumps({"name": hostile}),
                f"{oar_base}/Pack/Sub/config.json": json.dumps({"name": hostile}),
            },
            "Plain": {MT_IDLE: b"B"},
        }
        root = build_instance(
            self.tmp / "inst", mods, [f"+{hostile}", "+HostileOar", "+Plain"]
        )
        instance, profile = self.load(root)
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        oar = oar_discovery.discover(index, self.budget)
        found = loose_winners.conflict_issues(index, self.budget)
        from engine import oar_rules

        found.extend(oar_rules.config_issues(oar, profile, self.budget))
        return reports.ScanResult(
            instance=instance,
            profile=profile,
            fingerprint=fingerprint.profile_fingerprint(instance, profile, self.budget),
            index=index,
            oar=oar,
            dar=dar_discovery.discover(index, self.budget),
            behavior=behavior_outputs.detect(index, self.budget),
            issues=found,
        )

    def test_S05_markup_from_a_mod_author_cannot_escape_the_json_island(self) -> None:
        result = self._hostile_result(XSS_MOD)
        page = reports.render_html(reports.build_payload(result), "escaping test")

        island = re.search(
            r'<script id="report-data" type="application/json">(.*?)</script>', page, re.S
        )
        self.assertIsNotNone(island, "the JSON island must be present and well formed")
        raw = island.group(1)
        self.assertNotIn("<", raw, "no raw '<' may survive inside the data island")
        self.assertNotIn(">", raw, "no raw '>' may survive inside the data island")
        self.assertIn("\\u003c", raw, "the hostile text must be present but escaped")
        # The data is still intact once parsed the way the browser parses it.
        parsed = json.loads(raw)
        self.assertTrue(
            any(XSS_MOD in json.dumps(i) for i in parsed["issues"]),
            "escaping must not corrupt the value",
        )

    def test_S05b_a_closing_script_tag_from_a_mod_author_is_neutralised(self) -> None:
        result = self._hostile_result(XSS_SCRIPT_MOD)
        page = reports.render_html(reports.build_payload(result), "escaping test")
        self.assertEqual(
            page.count("</script>"), 2, "only the two real script tags may close a script"
        )
        self.assertNotIn("<script>alert(", page)
        self.assertNotIn("onerror=", page)

    def test_S05c_the_report_uses_no_html_injection_sink_and_no_remote_resource(self) -> None:
        result = self._result({"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        page = reports.render_html(reports.build_payload(result), "offline test")
        for sink in (".innerHTML", ".outerHTML", "insertAdjacentHTML", "document.write", "eval("):
            self.assertNotIn(sink, page, f"{sink} must never appear in the report")
        self.assertNotIn("<link", page)
        self.assertNotIn('src="http', page)
        self.assertIn("textContent", page, "rendering must go through textContent")

    def test_S05d_the_skeleton_escapes_the_profile_name(self) -> None:
        # '&' is legal in a Windows folder name and must still be escaped in HTML.
        root = build_instance(
            self.tmp / "inst",
            {"ModA": {MT_IDLE: b"A"}},
            ["+ModA"],
            profile="Evil&Profile",
        )
        instance, profile = self.load(root, "Evil&Profile")
        index = loose_winners.build_index(instance, profile, self.budget, include_overwrite=False)
        result = reports.ScanResult(
            instance=instance,
            profile=profile,
            fingerprint=fingerprint.profile_fingerprint(instance, profile, self.budget),
            index=index,
            oar=oar_discovery.discover(index, self.budget),
            dar=dar_discovery.discover(index, self.budget),
            behavior=behavior_outputs.detect(index, self.budget),
        )
        page = reports.render_html(reports.build_payload(result), "escape")
        self.assertIn("Evil&amp;Profile", page)
        self.assertNotIn("<strong>Evil&Profile", page)

    def test_S07_two_runs_on_a_stable_fixture_produce_identical_json(self) -> None:
        mods = {
            "ModA": {MT_IDLE: b"A"},
            "ModB": {MT_IDLE: b"B"},
            "ModC": {"meshes/actors/character/behaviors/0_master.hkx": b"C"},
        }
        modlist = ["+ModA", "+ModB", "+ModC"]
        first = reports.build_payload(self._result(mods, modlist))
        second = reports.build_payload(self._result(mods, modlist))

        for payload in (first, second):
            payload.pop("generatedAt")
            payload["summary"].pop("durationSeconds")
            payload.pop("readOnlyWitness", None)
            payload["profile"].pop("fingerprintDetail", None)
            payload["instance"] = {}
            payload["behavior"]["headerProbes"] = {}
            payload["fileConflicts"] = [
                {k: v for k, v in row.items() if k != "winner"} for row in payload["fileConflicts"]
            ]

        self.assertEqual(
            json.dumps(first, sort_keys=True),
            json.dumps(second, sort_keys=True),
            "identical inputs must yield byte-identical output",
        )

    def test_S07b_issue_ids_are_stable_and_derived_from_evidence(self) -> None:
        mods = {"ModA": {MT_IDLE: b"A"}, "ModB": {MT_IDLE: b"B"}}
        first = self._result(mods, ["+ModA", "+ModB"])
        second = self._result(mods, ["+ModA", "+ModB"])
        self.assertEqual(
            [i.id for i in issues.sort_issues(first.issues)],
            [i.id for i in issues.sort_issues(second.issues)],
        )
        override = next(i for i in first.issues if i.category == issues.LOOSE_FILE_OVERRIDE)
        self.assertTrue(override.id.startswith("loose_file_override-"))
        self.assertEqual(len(override.id.split("-")[-1]), 12)

    def test_S07c_findings_are_sorted_most_severe_first(self) -> None:
        result = self._result(
            {"ModA": {MT_IDLE: b"A"}, "ModB": {MT_IDLE: b"B"}}, ["+ModA", "+ModB"]
        )
        ordered = reports.build_payload(result)["issues"]
        rank = {"high": 0, "medium": 1, "low": 2, "info": 3}
        severities = [rank[i["severity"]] for i in ordered]
        self.assertEqual(severities, sorted(severities))

    def test_S07d_the_browser_renders_payload_order_without_resorting(self) -> None:
        """Regression: a JS re-sort with `SEV_ORDER[x] || 9` pushed every high row last.

        Found by opening the real report in Chrome on 2026-10-22: the 7 high-severity
        rows rendered at positions 267-273 of 274. Order has one source of truth,
        engine/issues.py sort_issues, so renderIssues must not sort at all.
        """
        script = (reports.WEB_DIR / "report.js").read_text(encoding="utf-8")
        body = re.search(r"function renderIssues\(\)\s*\{(.*?)\n  \}\n", script, re.S)
        self.assertIsNotNone(body, "renderIssues must exist")
        self.assertNotIn(".sort(", body.group(1), "renderIssues must keep payload order")
        self.assertNotIn("|| 9", script, "a falsy-zero fallback must not return")

    def test_S07e_a_behaviour_conflict_is_reported_once_with_unique_ids(self) -> None:
        """Regression: the real profile showed 6 file conflicts but 7 overrides, because
        a behaviour .hkx conflict was emitted by both loose_winners and behavior_outputs."""
        behavior = "meshes/actors/character/_1stperson/behaviors/mt_behavior.hkx"
        result = self._result(
            {
                "BehaviorA": {behavior: b"A"},
                "BehaviorB": {behavior: b"B"},
                "AnimA": {MT_IDLE: b"1"},
                "AnimB": {MT_IDLE: b"2"},
            },
            ["+BehaviorA", "+BehaviorB", "+AnimA", "+AnimB"],
        )
        payload = reports.build_payload(result)
        ids = [i["id"] for i in payload["issues"]]
        self.assertEqual(len(ids), len(set(ids)), "every finding id must be unique")
        overrides = [i for i in payload["issues"] if i["category"] == issues.LOOSE_FILE_OVERRIDE]
        self.assertEqual(len(overrides), len(payload["fileConflicts"]))
        on_behavior = [i for i in overrides if i["subject"] == behavior]
        self.assertEqual(len(on_behavior), 1)
        self.assertTrue(on_behavior[0]["detail"]["notAnOarRuleConflict"])

    def test_recommendation_wording_is_not_one_findings_winner(self) -> None:
        """A grouped recommendation must hold for every member, not quote one winner."""
        result = self._result(
            {
                "WinnerOne": {MT_IDLE: b"1", "meshes/actors/character/animations/mt_walk.hkx": b"x"},
                "LoserOne": {MT_IDLE: b"2"},
                "WinnerTwo": {"meshes/actors/character/animations/mt_walk.hkx": b"y"},
            },
            ["+WinnerTwo", "+WinnerOne", "+LoserOne"],
        )
        recs = reports.build_payload(result)["recommendations"]
        override = next(r for r in recs if r["category"] == issues.LOOSE_FILE_OVERRIDE)
        self.assertEqual(override["count"], 2)
        self.assertNotIn("WinnerOne", override["action"])
        self.assertNotIn("WinnerTwo", override["action"])
        self.assertIn("Reversible", override["action"])

    def test_the_payload_carries_the_mandatory_claim_limits(self) -> None:
        result = self._result({"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        payload = reports.build_payload(result)
        self.assertEqual(payload["runtimeVerification"], issues.NOT_VERIFIED_IN_GAME)
        self.assertEqual(payload["schemaVersion"], "1.0.0")
        self.assertEqual(payload["mission"], "CLAUDE-B-ANIM-01")
        self.assertTrue(payload["profile"]["readOnly"])
        joined = " ".join(payload["limitations"])
        for claim in (
            "SCANNED_STATICALLY is not SUPPORTED_IN_GAME",
            "HKX_FILE_PRESENT is not PLAYABLE_ANIMATION",
            "CONDITIONS_PARSED is not OAR_RUNTIME_WINNER",
            "PANDORA_OUTPUT_PRESENT is not PANDORA_PATCH_SUCCESS",
            "MO2_LOOSE_FILE_WINNER is not PROVEN_GAME_RESULT",
            "TESTS_PASS is not UI_INTEGRATED",
        ):
            self.assertIn(claim, joined)

    def test_every_evidence_entry_names_a_concrete_source(self) -> None:
        result = self._result({"ModA": {MT_IDLE: b"A"}}, ["+ModA"])
        evidence = reports.build_payload(result)["evidence"]
        self.assertGreaterEqual(len(evidence), 8)
        for entry in evidence:
            self.assertTrue(entry["claim"])
            self.assertTrue(entry["source"])
            self.assertTrue(
                any(
                    marker in entry["source"]
                    for marker in ("::", "github.com", "empirical", ".cpp", ".h")
                ),
                f"source must be checkable: {entry['source']}",
            )

    def test_conflict_index_lists_winner_and_overridden_roles(self) -> None:
        result = self._result(
            {"ModA": {MT_IDLE: b"A"}, "ModB": {MT_IDLE: b"B"}}, ["+ModA", "+ModB"]
        )
        index_doc = reports.conflict_index(result)
        self.assertEqual(len(index_doc["paths"]), 1)
        providers = index_doc["paths"][0]["providers"]
        self.assertEqual(providers[0]["role"], "winner")
        self.assertEqual(providers[0]["mod"], "ModA")
        self.assertEqual(providers[1]["role"], "overridden")
        self.assertIn("Loose files only", index_doc["note"])

    def test_recommendations_are_manual_and_mention_reversibility(self) -> None:
        result = self._result(
            {"ModA": {MT_IDLE: b"A"}, "ModB": {MT_IDLE: b"B"}}, ["+ModA", "+ModB"]
        )
        recommendations = reports.build_payload(result)["recommendations"]
        self.assertTrue(recommendations)
        joined = " ".join(r["action"] for r in recommendations)
        self.assertIn("Reversible", joined)
        for entry in recommendations:
            self.assertTrue(entry["issueIds"], "a recommendation must link to its evidence")


if __name__ == "__main__":
    unittest.main()
