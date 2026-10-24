# Acceptance matrix

Mission `CLAUDE-B-ANIM-01`. 36 mandatory cases, all covered by executable tests.
Generated run: `out/claude-animation-intelligence/run-002/test-results.txt`.

Command:

```powershell
python -m unittest discover -s tests/claude-animation-intelligence -p "test_*.py"
```

Result on 2026-10-22 (revision 2): **91 tests, 90 PASS, 0 FAIL, 1 SKIP**. Mission cases: **36/36 PASS**,
one of them (`S02`) with a justified SKIP on its privileged half. A skipped test is never
counted as a pass.

## Group M — MO2 and file precedence

| case | requirement | test | verdict |
|------|-------------|------|---------|
| M01 | modlist order matches the installed MO2 and the winner follows | `test_profile.test_M01_first_line_is_highest_priority`, `test_M01_comments_and_blank_lines_are_not_mods` | PASS |
| M02 | same path in three enabled mods: exact provider and losers | `test_loose_conflicts.test_M02_three_enabled_mods_same_path_exact_winner_and_losers` | PASS |
| M03 | a disabled mod never wins | `test_loose_conflicts.test_M03_disabled_mod_never_wins_the_file`, `test_profile.test_M03_disabled_entry_is_never_a_provider` | PASS |
| M04 | a separator is not a data provider | `test_profile.test_M04_separator_is_not_a_data_provider`, `..._foreign_star_entry_is_flagged_not_dropped`, `..._unknown_marker_is_ignored_rather_than_guessed` | PASS |
| M05 | same basename at a different path is not an override | `test_loose_conflicts.test_M05_same_basename_different_path_is_not_an_override` | PASS |
| M06 | case-insensitive collision on Windows semantics | `test_loose_conflicts.test_M06_case_differences_collide_on_windows_semantics` | PASS |
| M07 | two profiles do not share winners | `test_loose_conflicts.test_M07_two_profiles_do_not_share_winners` | PASS |
| M08 | mod names with spaces, accents and non-Latin scripts | `test_loose_conflicts.test_M08_unicode_and_spaced_mod_names_are_handled` | PASS |
| M09 | a profile with no enabled mods yields an empty result, no synthetic filler | `test_profile.test_M09_profile_with_no_enabled_mods_yields_no_providers` | PASS |
| M10 | a file inside a BSA is never presented as a verified loose file | `test_loose_conflicts.test_M10_bsa_contents_are_not_presented_as_loose_files` | PASS |

## Group O — Open Animation Replacer

| case | requirement | test | verdict |
|------|-------------|------|---------|
| O01 | discovers group and submod in a supported Skyrim OAR path | `test_oar.test_O01_discovers_group_and_submod_in_the_supported_path` | PASS |
| O02 | a correct `config.json` yields priority and conditions | `test_oar.test_O02_extracts_priority_and_flattened_conditions` | PASS |
| O03 | corrupt JSON is one issue and does not abort the scan | `test_oar.test_O03_corrupt_json_is_one_issue_and_does_not_abort_the_scan` | PASS |
| O04 | `user.json` wins only where precedence was verified | `test_oar.test_O04_user_json_replaces_the_data_section_but_not_name` | PASS |
| O05 | a disabled submod is not an active candidate | `test_oar.test_O05_disabled_submod_is_not_an_active_candidate` | PASS |
| O06 | two submods on one clip: potential overlap, no runtime winner asserted | `test_oar.test_O06_two_submods_on_one_clip_overlap_without_a_runtime_winner` (two mods), `test_O06b_overlap_within_one_mod_is_informational_variants` | PASS |
| O07 | a priority tie is a warning, not an arbitrary choice | `test_oar.test_O07_priority_tie_is_a_warning_not_an_arbitrary_choice` | PASS |
| O08 | unknown custom conditions: extension required / UNKNOWN | `test_oar.test_O08_unknown_condition_is_an_extension_requirement_not_an_error`, `test_O08b_legacy_only_condition_names_are_recognised` | PASS |
| O09 | dynamic conditions make an absolute winner impossible | `test_oar.test_O09_dynamic_conditions_never_yield_an_absolute_winner` | PASS |
| O10 | an unsupported field is preserved and marked UNKNOWN | `test_oar.test_O10_unsupported_field_is_preserved_and_marked_unknown` | PASS |

Extra, not substituting for the above: `test_missing_integer_priority_invalidates_the_submod`,
`test_override_animations_folder_redirects_the_animation_root`,
`test_missing_plugin_reference_is_likely_not_proven`.

## Group D/P — legacy DAR and behaviour engines

| case | requirement | test | verdict |
|------|-------------|------|---------|
| DP01 | a legacy DAR directory is found | `test_dar.test_DP01_custom_conditions_directory_is_found_with_its_priority`, `test_DP01b_actor_base_variant_records_plugin_and_formid` | PASS |
| DP02 | a malformed `_conditions.txt` gives a concrete line diagnosis | 9 lexer tests in `test_dar.DarLexerTests` plus `test_DP02_malformed_conditions_file_yields_a_line_level_diagnosis` | PASS |
| DP03 | a DAR to OAR migration candidate is never applied automatically | `test_dar.test_DP03_migration_candidate_is_never_applied_automatically` | PASS |
| DP04 | Pandora output is read as data, never executed | `test_dar.test_DP04_patcher_output_is_read_as_data_and_never_executed` | PASS |
| DP05 | the same behaviour HKX in two mods is a file conflict, not an OAR rule conflict | `test_dar.test_DP05_same_behavior_file_in_two_mods_is_a_file_conflict` | PASS |
| DP06 | absence of Pandora/Nemesis is an absence state, not a false error | `test_dar.test_DP06_absence_of_a_patcher_is_a_state_not_an_error`, `test_DP06b_a_mod_named_after_a_patcher_is_not_evidence` | PASS |

## Group S — security and reporting

| case | requirement | test | verdict |
|------|-------------|------|---------|
| S01 | `..` blocked | `test_security.test_S01_parent_traversal_is_refused`, `test_S01b_absolute_and_unc_paths_are_refused`, `test_S01c_a_legitimate_relative_path_still_resolves` | PASS |
| S02 | symlink/junction escape blocked, or justified SKIP on Windows | `test_security.test_S02_directory_symlink_is_skipped_not_followed` (**SKIP**: `[WinError 1314] El cliente no dispone de un privilegio requerido` — creating a directory symlink needs `SeCreateSymbolicLinkPrivilege`), `test_S02b_reparse_point_detection_is_false_for_a_plain_directory` (PASS) | PASS with justified SKIP |
| S03 | an oversized JSON is rejected without exhaustion | `test_security.test_S03_oversized_json_is_refused_without_reading_it_all`, `test_S03b_oversized_json_in_a_scan_is_an_issue_not_a_crash` | PASS |
| S04 | a massive tree applies budgets and marks TRUNCATED | `test_security.test_S04_file_budget_marks_truncated_and_stops`, `test_S04b_depth_budget_marks_truncated` | PASS |
| S05 | malicious HTML/JS content is escaped | `test_reporting.test_S05_markup_from_a_mod_author_cannot_escape_the_json_island`, `test_S05b_a_closing_script_tag_from_a_mod_author_is_neutralised`, `test_S05c_the_report_uses_no_html_injection_sink_and_no_remote_resource`, `test_S05d_the_skeleton_escapes_the_profile_name` | PASS |
| S06 | an inaccessible file destroys neither the report nor the session | `test_security.test_S06_invalid_utf8_text_is_refused_rather_than_silently_repaired`, `test_S06b_utf8_bom_is_accepted`, `test_S06c_a_missing_mod_folder_is_reported_not_fatal` | PASS |
| S07 | deterministic JSON across two runs on a stable fixture | `test_reporting.test_S07_two_runs_on_a_stable_fixture_produce_identical_json`, `test_S07b_issue_ids_are_stable_and_derived_from_evidence`, `test_S07c_findings_are_sorted_most_severe_first`, `test_S07d_the_browser_renders_payload_order_without_resorting`, `test_S07e_a_behaviour_conflict_is_reported_once_with_unique_ids` | PASS |
| S08 | profile comparison does not contaminate caches or results | `test_security.test_S08_two_profiles_produce_independent_snapshots`, `test_S08b_the_same_profile_yields_a_stable_fingerprint` | PASS |
| S09 | the SHA-256 manifest is regenerable and checkable | `test_security.test_S09_manifest_is_regenerable_and_detects_tampering`, `test_S09b_read_only_witness_covers_the_mo2_files_opened` | PASS |
| S10 | the CLI returns the right exit code for PASS, input error, partial and internal failure | `test_security.test_S10_version_returns_zero`, `test_S10b_a_clean_scan_returns_zero`, `test_S10c_a_missing_instance_returns_the_input_error_code`, `test_S10d_an_unknown_profile_returns_the_input_error_code`, `test_S10e_a_truncated_scan_returns_the_partial_code`, `test_S10f_verify_manifest_reports_a_mismatch_as_partial`, `test_S10g_verify_manifest_without_a_manifest_is_an_input_error` | PASS |

## Beyond the 36

| extra requirement | status | evidence |
|---|---|---|
| read-only integration with a real MO2 profile | DONE | `scan` on `BodyChangeNG-Test` and `Default` at `D:\Apps` (base `F:\Mods Skyrim`): 621 and 606 enabled mods, 2 429 loose `.hkx`, 6 loose conflicts, 212 OAR candidates, 71 overlaps (13 across mods, 58 within one mod), 17 DAR rule sets, 32 behaviour files. |
| synthetic benchmark, 5+ repetitions, median and p95, cold vs warm, memory | DONE | `out/claude-animation-intelligence/bench-001/benchmark.json`: 6 230 files per run, cold 1.96 s, warm median 1.79 s / p95 1.92 s, HTML median 0.0091 s, working set 48.9 MB. |
| BSA archive names (revision 4) | DONE | 14 tests in `test_archives.py` (corrupt, truncated, wrong magic, BA2, unsupported version, budget, archive-only, loose-over-archive, archive overlap without winner, plugin association, coverage note, v104 flag); real profile 110/110 archives indexed. |
| OAR targets, presets, archive precedence, progress (revision 5) | DONE | `test_O06c` (variants collapse), `test_O06d` (disabled animation), `test_O06e` (replacementAnimDatas are not targets), `test_O07b` (same-mod tie is info), preset reference and missing-preset tests, `test_A06b` (load-order winner), `test_load_order_*`, `test_S10j` (progress JSON lines). |
| before/after hashes prove zero writes into MO2 | DONE | `readOnlyWitness` in `result.json` re-hashed after the run: 5 of 5 MO2 files unchanged. No file under `F:\Mods Skyrim\profiles\*` has a modification time from today. |
| open `report.html` in a browser and exercise search, filters and navigation | DONE (revision 2) | Opened in Chrome via chrome-devtools from `file://`: zero console messages, exactly one network request (the file itself). Search `Gulan0` gave 37/274 rows, all matching; adding severity `high` gave 4; clicking a row expanded its evidence and provider table; Reset restored 274/274; the OAR panel shows `UNKNOWN` on all 71 rows; the comparison section renders in `compare-001`; light and dark schemes and a 390 px viewport were checked. Screenshots in `out/claude-animation-intelligence/run-002/screenshots/` and `compare-001/screenshots/`. This found three defects, all fixed and pinned by tests (see STATUS.md). |
