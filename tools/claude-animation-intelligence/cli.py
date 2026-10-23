#!/usr/bin/env python3
"""Animation Intelligence Lab - read-only CLI for MO2 animation diagnostics.

Mission CLAUDE-B-ANIM-01. Python 3.11+, standard library only.

This tool never writes outside its own ``--out`` directory. It never launches
Skyrim, MO2, Pandora, Nemesis or FNIS, never loads a DLL or SKSE plugin, and
never deserialises a Havok file.

Exit codes
  0  PASS            - the scan completed within its budgets
  2  INPUT ERROR     - bad arguments, missing instance or profile
  3  PARTIAL         - completed but truncated, or some paths were unreadable
  4  INTERNAL ERROR  - an unexpected failure; the traceback is printed
"""

from __future__ import annotations

import argparse
import ctypes
import json
import statistics
import sys
import time
import traceback
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from engine import (  # noqa: E402
    behavior_outputs,
    bsa_index,
    dar_discovery,
    fingerprint,
    loose_winners,
    mo2_profile,
    oar_discovery,
    oar_rules,
    reports,
    sandbox,
)
from engine import issues as issue_mod  # noqa: E402
from engine.sandbox import Budget, Clock  # noqa: E402

VERSION = "1.0.0"
EXIT_OK = 0
EXIT_INPUT = 2
EXIT_PARTIAL = 3
EXIT_INTERNAL = 4


# --- helpers ---------------------------------------------------------------------


def working_set_bytes() -> int | None:
    """Current working set via the Win32 API; ``None`` off Windows or on failure.

    The argtypes/restype declarations are required: without them ctypes passes
    the ``GetCurrentProcess`` pseudo-handle as a 32-bit int and the call fails.
    """
    if sys.platform != "win32":
        return None

    class Counters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        kernel32.GetCurrentProcess.argtypes = []
        kernel32.K32GetProcessMemoryInfo.restype = wintypes.BOOL
        kernel32.K32GetProcessMemoryInfo.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(Counters),
            wintypes.DWORD,
        ]
        counters = Counters()
        counters.cb = ctypes.sizeof(Counters)
        ok = kernel32.K32GetProcessMemoryInfo(
            kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb
        )
        return int(counters.WorkingSetSize) if ok else None
    except Exception:  # pragma: no cover - diagnostics only, never fatal
        return None


def budget_from_args(args: argparse.Namespace) -> Budget:
    return Budget(
        max_depth=args.max_depth,
        max_files=args.max_files,
        max_json_bytes=args.max_json_bytes,
        max_submods=args.max_submods,
        max_html_rows=args.max_rows,
        max_seconds=args.max_seconds,
    )


def add_budget_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--max-depth", type=int, default=24)
    parser.add_argument("--max-files", type=int, default=400_000)
    parser.add_argument("--max-json-bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--max-submods", type=int, default=20_000)
    parser.add_argument("--max-rows", type=int, default=2_000)
    parser.add_argument("--max-seconds", type=float, default=1_800.0)
    parser.add_argument(
        "--full-walk",
        action="store_true",
        help="also walk top-level textures/, sound/, music/ and calientetools/ "
        "(skipped by default: the engine never loads animations from them)",
    )


def scan_profile(
    instance: mo2_profile.Instance,
    profile_name: str,
    budget: Budget,
    *,
    full_walk: bool = False,
) -> reports.ScanResult:
    """One profile, one isolated snapshot. No state is shared between calls."""
    started = time.monotonic()
    profile = mo2_profile.load_profile(instance, profile_name, budget)
    clock = Clock(budget)

    index = loose_winners.build_index(instance, profile, budget, clock, full_walk=full_walk)
    oar = oar_discovery.discover(index, budget)
    dar = dar_discovery.discover(index, budget)
    behavior = behavior_outputs.detect(index, budget)

    primary, primary_source = mo2_profile.primary_plugins(instance, budget)
    active = {name.lower() for name, is_active in profile.plugins if is_active}
    listed = {name.lower() for name, _ in profile.plugins}
    # Must run before conflict_issues: it marks which archives were indexed, so the
    # loose-only coverage note only lists archives whose names could not be read.
    archives = bsa_index.index_archives(
        index, budget, loaded_plugins=active | primary, listed_plugins=listed
    )

    found: list[issue_mod.Issue] = []
    found.extend(loose_winners.conflict_issues(index, budget))
    found.extend(
        oar_rules.config_issues(
            oar, profile, budget, primary_plugins=primary, primary_source=primary_source
        )
    )
    found.extend(oar_rules.overlap_issues(oar, budget))
    found.extend(dar_discovery.dar_issues(dar, oar, budget))
    found.extend(behavior_outputs.behavior_issues(behavior, index))
    found.extend(bsa_index.archive_issues(archives, index, budget))

    return reports.ScanResult(
        instance=instance,
        profile=profile,
        fingerprint=fingerprint.profile_fingerprint(instance, profile, budget),
        index=index,
        oar=oar,
        dar=dar,
        behavior=behavior,
        issues=found,
        witness=fingerprint.read_only_witness(instance, profile, budget),
        duration_seconds=time.monotonic() - started,
        archives=archives,
    )


def exit_code_for(result: reports.ScanResult) -> int:
    if result.index.walk.is_truncated or result.oar.truncated or result.dar.truncated:
        return EXIT_PARTIAL
    if result.index.walk.skipped:
        return EXIT_PARTIAL
    return EXIT_OK


def _print_summary(payload: dict[str, object], written: list[Path]) -> None:
    summary = payload.get("summary", {})
    profile = payload.get("profile", {})
    print(f"  profile              : {profile.get('name')}")
    print(f"  fingerprint          : {profile.get('fingerprint')}")
    print(f"  enabled mods         : {profile.get('enabledMods')}")
    print(f"  loose .hkx paths     : {profile.get('countedLooseAnimations')}")
    print(f"  loose file conflicts : {summary.get('loosePathConflicts')}")
    print(f"  OAR candidates       : {summary.get('oarCandidates')}")
    print(f"  OAR overlaps         : {summary.get('oarPotentialOverlaps')}")
    print(f"  DAR rule sets        : {summary.get('darRuleSets')}")
    print(f"  behaviour files      : {summary.get('behaviorOutputs')}")
    print(f"  unknown/unverified   : {summary.get('unknowns')}")
    print(f"  findings             : {len(payload.get('issues', []))}")
    print(f"  runtimeVerification  : {payload.get('runtimeVerification')}")
    for path in written:
        print(f"  wrote                : {path}")


# --- commands --------------------------------------------------------------------


def cmd_version(args: argparse.Namespace) -> int:
    print(f"claude-animation-intelligence {VERSION} (mission {reports.MISSION})")
    print(f"schema {reports.SCHEMA_VERSION}; python {sys.version.split()[0]}")
    print("read-only: never writes outside --out, never executes mods or patchers")
    return EXIT_OK


def cmd_scan(args: argparse.Namespace) -> int:
    budget = budget_from_args(args)
    try:
        instance = mo2_profile.load_instance(Path(args.instance), budget)
    except (mo2_profile.ProfileError, sandbox.SandboxError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return EXIT_INPUT

    available = instance.profile_names()
    if args.profile not in available:
        print(
            f"ERROR: profile {args.profile!r} not found. Available: {', '.join(available)}",
            file=sys.stderr,
        )
        return EXIT_INPUT

    try:
        result = scan_profile(instance, args.profile, budget, full_walk=args.full_walk)
    except (mo2_profile.ProfileError, sandbox.SandboxError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return EXIT_INPUT

    out_dir = Path(args.out)
    payload = reports.build_payload(result)
    written = [
        reports.write_json(out_dir, "result.json", payload),
        reports.write_json(out_dir, "conflict-index.json", reports.conflict_index(result)),
        reports.write_html(out_dir, payload, f"Animation Intelligence - {result.profile.name}"),
    ]
    written.append(fingerprint.write_manifest(out_dir, budget))

    print(f"scan complete in {result.duration_seconds:.2f}s")
    _print_summary(payload, written)
    return exit_code_for(result)


def _comparison(a: reports.ScanResult, b: reports.ScanResult) -> dict[str, object]:
    """Delta between two independently scanned snapshots. No cache is shared."""
    mods_a = {m.name for m in a.profile.providers}
    mods_b = {m.name for m in b.profile.providers}

    conflicts_a = {r.key: r for r in a.index.conflicts()}
    conflicts_b = {r.key: r for r in b.index.conflicts()}

    winner_changed = []
    for key in sorted(set(conflicts_a) & set(conflicts_b)):
        winner_a = conflicts_a[key].winner.mod
        winner_b = conflicts_b[key].winner.mod
        if winner_a != winner_b:
            winner_changed.append(
                {"path": conflicts_a[key].display, "winnerA": winner_a, "winnerB": winner_b}
            )

    return {
        "profileA": a.profile.name,
        "profileB": b.profile.name,
        "fingerprintA": a.fingerprint.digest,
        "fingerprintB": b.fingerprint.digest,
        "modsOnlyInA": sorted(mods_a - mods_b),
        "modsOnlyInB": sorted(mods_b - mods_a),
        "sharedMods": len(mods_a & mods_b),
        "conflictsOnlyInA": sorted(
            conflicts_a[k].display for k in set(conflicts_a) - set(conflicts_b)
        ),
        "conflictsOnlyInB": sorted(
            conflicts_b[k].display for k in set(conflicts_b) - set(conflicts_a)
        ),
        "winnerChanged": winner_changed,
        "oarCandidatesA": len(a.oar.active),
        "oarCandidatesB": len(b.oar.active),
        "darRuleSetsA": len(a.dar.rule_sets),
        "darRuleSetsB": len(b.dar.rule_sets),
        "note": (
            "Each profile was loaded and walked in its own snapshot; no index, cache or "
            "issue list is shared between them. Differing fingerprints prove the inputs "
            "differed."
        ),
    }


def cmd_compare(args: argparse.Namespace) -> int:
    budget = budget_from_args(args)
    try:
        instance = mo2_profile.load_instance(Path(args.instance), budget)
    except (mo2_profile.ProfileError, sandbox.SandboxError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return EXIT_INPUT

    available = instance.profile_names()
    missing = [p for p in (args.profile_a, args.profile_b) if p not in available]
    if missing:
        print(
            f"ERROR: profile(s) not found: {', '.join(missing)}. "
            f"Available: {', '.join(available)}",
            file=sys.stderr,
        )
        return EXIT_INPUT

    result_a = scan_profile(instance, args.profile_a, budget, full_walk=args.full_walk)
    result_b = scan_profile(instance, args.profile_b, budget, full_walk=args.full_walk)
    comparison = _comparison(result_a, result_b)

    out_dir = Path(args.out)
    payload_a = reports.build_payload(result_a, comparison=comparison)
    payload_b = reports.build_payload(result_b, comparison=comparison)
    written = [
        reports.write_json(out_dir, "result.json", payload_a),
        reports.write_json(out_dir, f"result-{args.profile_b}.json", payload_b),
        reports.write_json(out_dir, "conflict-index.json", reports.conflict_index(result_a)),
        reports.write_json(out_dir, "comparison.json", comparison),
        reports.write_html(
            out_dir, payload_a, f"Animation Intelligence - {args.profile_a} vs {args.profile_b}"
        ),
    ]
    written.append(fingerprint.write_manifest(out_dir, budget))

    print(f"compared {args.profile_a} vs {args.profile_b}")
    print(f"  mods only in {args.profile_a}: {len(comparison['modsOnlyInA'])}")
    print(f"  mods only in {args.profile_b}: {len(comparison['modsOnlyInB'])}")
    print(f"  shared enabled mods         : {comparison['sharedMods']}")
    print(f"  winner changed              : {len(comparison['winnerChanged'])}")
    print(f"  fingerprint A               : {comparison['fingerprintA']}")
    print(f"  fingerprint B               : {comparison['fingerprintB']}")
    for path in written:
        print(f"  wrote                       : {path}")

    return max(exit_code_for(result_a), exit_code_for(result_b))


def cmd_inspect_oar(args: argparse.Namespace) -> int:
    """Validate an OAR pack in an isolated folder, with no MO2 instance involved."""
    budget = budget_from_args(args)
    root = Path(args.root)
    if not root.is_dir():
        print(f"ERROR: {root} is not a directory", file=sys.stderr)
        return EXIT_INPUT

    index = loose_winners.LooseIndex()
    clock = Clock(budget)
    for relative, absolute in sandbox.walk_files(
        root, budget, index.walk, clock, want=lambda rel: loose_winners.classify(rel) is not None
    ):
        kind = loose_winners.classify(relative)
        if kind is None or kind == loose_winners.KIND_ARCHIVE:
            continue
        try:
            size = absolute.stat().st_size
        except OSError:
            continue
        key = sandbox.norm_key(relative)
        record = index.records.setdefault(
            key, loose_winners.PathRecord(key=key, display=relative, kind=kind)
        )
        record.entries.append(
            loose_winners.Entry(
                mod="<pack>", priority=0, absolute=absolute, raw_relative=relative, size=size
            )
        )

    catalog = oar_discovery.discover(index, budget)
    empty_profile = mo2_profile.Profile(name="<none>", directory=root)
    found = oar_rules.config_issues(catalog, empty_profile, budget)
    found.extend(oar_rules.overlap_issues(catalog, budget))

    payload = {
        "schemaVersion": reports.SCHEMA_VERSION,
        "mission": reports.MISSION,
        "generatedAt": fingerprint.utc_now(),
        "mode": "inspect-oar",
        "root": str(root),
        "oar": oar_rules.summarise(catalog),
        "submods": [s.to_json() for s in catalog.submods],
        "groups": catalog.groups,
        "issues": [i.to_json() for i in issue_mod.sort_issues(found)],
        "notes": catalog.notes,
        "runtimeVerification": issue_mod.NOT_VERIFIED_IN_GAME,
    }
    out_dir = Path(args.out)
    written = [reports.write_json(out_dir, "oar-inspection.json", payload)]
    written.append(fingerprint.write_manifest(out_dir, budget))

    print(f"inspected {root}")
    print(f"  groups           : {len(catalog.groups)}")
    print(f"  submods          : {len(catalog.submods)}")
    print(f"  active candidates: {len(catalog.active)}")
    print(f"  findings         : {len(found)}")
    for path in written:
        print(f"  wrote            : {path}")
    return EXIT_PARTIAL if index.walk.is_truncated else EXIT_OK


FIXTURE_MARKER = ".anim-lab-synthetic-fixture"


def _make_perf_fixture(root: Path, mods: int, per_mod: int) -> None:
    """Create a synthetic tree of .hkx paths. Fictional names, reproducible layout.

    Callers must have checked that ``root`` is new or empty; the marker file lets a
    later benchmark prove it is running on a synthetic tree, never a real instance.
    """
    root.mkdir(parents=True, exist_ok=True)
    (root / FIXTURE_MARKER).write_text(
        "synthetic benchmark fixture generated by claude-animation-intelligence\n",
        encoding="utf-8",
    )
    base = root / "mods"
    for mod_index in range(mods):
        anim_dir = base / f"PerfMod{mod_index:04d}" / "meshes/actors/character/animations"
        anim_dir.mkdir(parents=True, exist_ok=True)
        for file_index in range(per_mod):
            # Every 4th file reuses a shared name so real conflicts exist to resolve.
            name = (
                f"shared{file_index % 7:03d}.hkx"
                if file_index % 4 == 0
                else f"anim{mod_index:04d}_{file_index:04d}.hkx"
            )
            (anim_dir / name).write_bytes(bytes.fromhex("57e0e05710c0c010") + b"\x00" * 64)

    profiles = root / "profiles" / "Perf"
    profiles.mkdir(parents=True, exist_ok=True)
    rows = "\n".join(f"+PerfMod{i:04d}" for i in reversed(range(mods)))
    profiles.joinpath("modlist.txt").write_text(
        "# synthetic benchmark fixture\n" + rows + "\n", encoding="utf-8"
    )
    (root / "overwrite").mkdir(exist_ok=True)
    (root / "ModOrganizer.ini").write_text(
        "[General]\ngameName=Skyrim Special Edition\n"
        "selected_profile=@ByteArray(Perf)\n\n"
        f"[Settings]\nbase_directory={root.as_posix()}\n",
        encoding="utf-8",
    )


def cmd_benchmark(args: argparse.Namespace) -> int:
    budget = budget_from_args(args)
    root = Path(args.fixture)
    if not (root / FIXTURE_MARKER).is_file():
        # Never write into, or benchmark against, a directory we did not create.
        if root.exists() and (not root.is_dir() or any(root.iterdir())):
            print(
                f"ERROR: {root} is not empty and has no {FIXTURE_MARKER} marker. The "
                "benchmark only runs on synthetic fixtures it generated itself; pass a "
                "new or empty directory.",
                file=sys.stderr,
            )
            return EXIT_INPUT
        print(f"generating synthetic fixture under {root}")
        _make_perf_fixture(root, args.mods, args.files_per_mod)

    instance = mo2_profile.load_instance(root, budget)
    profile_name = instance.profile_names()[0]

    samples: list[dict[str, object]] = []
    for run in range(args.repetitions):
        rss_before = working_set_bytes()
        started = time.perf_counter()
        result = scan_profile(instance, profile_name, budget, full_walk=args.full_walk)
        scan_seconds = time.perf_counter() - started

        html_started = time.perf_counter()
        payload = reports.build_payload(result)
        html_text = reports.render_html(payload, "benchmark")
        html_seconds = time.perf_counter() - html_started

        samples.append(
            {
                "run": run + 1,
                "cache": "cold" if run == 0 else "warm",
                "scanSeconds": round(scan_seconds, 4),
                "htmlSeconds": round(html_seconds, 4),
                "htmlBytes": len(html_text.encode("utf-8")),
                "filesWalked": result.index.walk.files_seen,
                "conflicts": len(result.index.conflicts()),
                "workingSetBytesBefore": rss_before,
                "workingSetBytesAfter": working_set_bytes(),
            }
        )
        print(
            f"  run {run + 1}/{args.repetitions} ({samples[-1]['cache']}): "
            f"scan {scan_seconds:.3f}s, html {html_seconds:.3f}s, "
            f"{result.index.walk.files_seen} files"
        )

    def stats(key: str, rows: list[dict[str, object]]) -> dict[str, float]:
        values = sorted(float(r[key]) for r in rows)  # type: ignore[arg-type]
        if not values:
            return {}
        index95 = min(len(values) - 1, int(round(0.95 * (len(values) - 1))))
        return {
            "min": round(values[0], 4),
            "median": round(statistics.median(values), 4),
            "p95": round(values[index95], 4),
            "max": round(values[-1], 4),
            "samples": len(values),
        }

    warm = [s for s in samples if s["cache"] == "warm"]
    cold = [s for s in samples if s["cache"] == "cold"]
    report = {
        "schemaVersion": reports.SCHEMA_VERSION,
        "mission": reports.MISSION,
        "generatedAt": fingerprint.utc_now(),
        "fixture": {
            "root": str(root),
            "mods": args.mods,
            "filesPerMod": args.files_per_mod,
            "synthetic": True,
        },
        "environment": {
            "python": sys.version.split()[0],
            "platform": sys.platform,
            "workingSetBytes": working_set_bytes(),
        },
        "samples": samples,
        "scanSecondsCold": stats("scanSeconds", cold),
        "scanSecondsWarm": stats("scanSeconds", warm),
        "htmlSecondsWarm": stats("htmlSeconds", warm),
        "note": (
            "Cold and warm cache runs are reported separately. Timings are filesystem "
            "bound and will differ on other hardware."
        ),
    }
    out_dir = Path(args.out)
    written = [reports.write_json(out_dir, "benchmark.json", report)]
    written.append(fingerprint.write_manifest(out_dir, budget))
    for path in written:
        print(f"  wrote: {path}")
    return EXIT_OK


def cmd_verify_manifest(args: argparse.Namespace) -> int:
    outcome = fingerprint.verify_manifest(Path(args.run), Budget())
    print(json.dumps(outcome, indent=2))
    if outcome.get("error"):
        return EXIT_INPUT
    return EXIT_OK if outcome.get("ok") else EXIT_PARTIAL


def cmd_selftest(args: argparse.Namespace) -> int:
    """Run the mission's unittest suite from the CLI."""
    import unittest

    tests_dir = Path(__file__).resolve().parents[2] / "tests" / "claude-animation-intelligence"
    sys.path.insert(0, str(tests_dir))
    suite = unittest.TestLoader().discover(str(tests_dir), pattern="test_*.py")
    outcome = unittest.TextTestRunner(verbosity=2 if args.verbose else 1).run(suite)
    return EXIT_OK if outcome.wasSuccessful() else EXIT_PARTIAL


# --- argument parsing ------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="claude-animation-intelligence",
        description=(
            "Read-only diagnostics for Skyrim SE animation mods in a Mod Organizer 2 "
            "profile: loose .hkx precedence, OAR and DAR rules, behaviour-engine output, "
            "and an offline HTML report. Nothing is modified or executed."
        ),
        epilog="Exit codes: 0 PASS, 2 input error, 3 partial/truncated, 4 internal error.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("version", help="print version and guarantees").set_defaults(func=cmd_version)

    scan = sub.add_parser("scan", help="analyse one MO2 profile (read-only)")
    scan.add_argument("--instance", required=True, help="folder holding ModOrganizer.ini")
    scan.add_argument("--profile", required=True)
    scan.add_argument("--out", required=True, help="output directory for this run")
    add_budget_flags(scan)
    scan.set_defaults(func=cmd_scan)

    compare = sub.add_parser("compare", help="compare two profiles, both read-only")
    compare.add_argument("--instance", required=True)
    compare.add_argument("--profile-a", required=True)
    compare.add_argument("--profile-b", required=True)
    compare.add_argument("--out", required=True)
    add_budget_flags(compare)
    compare.set_defaults(func=cmd_compare)

    inspect = sub.add_parser("inspect-oar", help="validate an OAR pack folder in isolation")
    inspect.add_argument("--root", required=True)
    inspect.add_argument("--out", required=True)
    add_budget_flags(inspect)
    inspect.set_defaults(func=cmd_inspect_oar)

    bench = sub.add_parser("benchmark", help="synthetic performance run (fixtures only)")
    bench.add_argument("--fixture", required=True)
    bench.add_argument("--out", required=True)
    bench.add_argument("--repetitions", type=int, default=5)
    bench.add_argument("--mods", type=int, default=60)
    bench.add_argument("--files-per-mod", type=int, default=100)
    add_budget_flags(bench)
    bench.set_defaults(func=cmd_benchmark)

    verify = sub.add_parser("verify-manifest", help="re-hash a run directory")
    verify.add_argument("--run", required=True)
    verify.set_defaults(func=cmd_verify_manifest)

    selftest = sub.add_parser("selftest", help="run the mission test suite")
    selftest.add_argument("--verbose", action="store_true")
    selftest.set_defaults(func=cmd_selftest)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return EXIT_PARTIAL
    except sandbox.SandboxError as exc:
        print(f"ERROR (sandbox refusal): {exc}", file=sys.stderr)
        return EXIT_INPUT
    except Exception:  # noqa: BLE001 - report, never hide, an internal failure
        traceback.print_exc()
        return EXIT_INTERNAL


if __name__ == "__main__":
    raise SystemExit(main())
