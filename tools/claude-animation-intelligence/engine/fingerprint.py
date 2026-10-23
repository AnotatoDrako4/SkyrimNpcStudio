"""Profile fingerprints and artefact manifests.

A fingerprint must not oversell certainty, so what goes into it is documented
and returned alongside the digest.

INCLUDED: the profile name, the ordered (priority, mod name, enabled, kind)
tuples from modlist.txt, and the SHA-256 of modlist.txt, plugins.txt,
loadorder.txt and archives.txt as text.

EXCLUDED, and therefore NOT covered by the digest: the contents of the mod
folders, BSA archives, file timestamps, the game's Data folder, and anything
MO2 holds only in memory. Two scans with the same fingerprint can still differ
if a mod's files changed on disk without the profile changing.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

from . import sandbox
from .mo2_profile import Instance, Profile
from .sandbox import Budget

PROFILE_FILES = ("modlist.txt", "plugins.txt", "loadorder.txt", "archives.txt")

INCLUDED = [
    "profile name",
    "ordered (priority, mod, enabled, kind) tuples from modlist.txt",
    "SHA-256 of modlist.txt, plugins.txt, loadorder.txt, archives.txt",
]
EXCLUDED = [
    "contents of mod folders",
    "contents of BSA/BA2 archives",
    "file modification timestamps",
    "the game Data folder",
    "MO2 in-memory state",
]


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


@dataclass
class Fingerprint:
    digest: str
    profile: str
    file_digests: dict[str, str] = field(default_factory=dict)
    mod_count: int = 0
    enabled_count: int = 0

    def to_json(self) -> dict[str, object]:
        return {
            "fingerprint": self.digest,
            "profile": self.profile,
            "modEntries": self.mod_count,
            "enabledProviders": self.enabled_count,
            "profileFileDigests": self.file_digests,
            "covers": INCLUDED,
            "doesNotCover": EXCLUDED,
        }


def profile_fingerprint(instance: Instance, profile: Profile, budget: Budget) -> Fingerprint:
    """Content-based fingerprint of one profile's mod order and plugin lists."""
    file_digests: dict[str, str] = {}
    for name in PROFILE_FILES:
        path = profile.directory / name
        if not path.is_file():
            continue
        try:
            text = sandbox.read_text_confined(path, budget, max_bytes=16 * 1024 * 1024)
        except sandbox.SandboxError as exc:
            file_digests[name] = f"UNREADABLE: {exc}"
            continue
        file_digests[name] = sandbox.sha256_text(text)

    canonical = json.dumps(
        {
            "profile": profile.name,
            "order": [[m.priority, m.name, m.enabled, m.kind] for m in profile.mods],
            "files": file_digests,
        },
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    )
    return Fingerprint(
        digest=f"sha256:{sandbox.sha256_text(canonical)}",
        profile=profile.name,
        file_digests=file_digests,
        mod_count=len(profile.mods),
        enabled_count=profile.enabled_count,
    )


def read_only_witness(instance: Instance, profile: Profile, budget: Budget) -> dict[str, object]:
    """Digest the MO2 files this run reads, so a re-check can prove nothing changed."""
    witness: dict[str, str] = {}
    targets = [instance.ini_path, *(profile.directory / name for name in PROFILE_FILES)]
    for path in targets:
        if not path.is_file():
            continue
        try:
            digest, complete = sandbox.sha256_file(path, budget)
        except OSError as exc:
            witness[str(path)] = f"UNREADABLE: {exc.strerror or exc}"
            continue
        witness[str(path)] = digest if complete else f"{digest}~partial"
    return {
        "capturedAt": utc_now(),
        "files": witness,
        "claim": (
            "These MO2 files were opened read-only. Re-hash them after the run and compare: "
            "any difference was NOT caused by this tool, which never opens a file for writing "
            "outside its own out/ directory."
        ),
    }


def write_manifest(directory: Path, budget: Budget) -> Path:
    """Write ``manifest.sha256.json`` covering every other file in ``directory``."""
    entries: dict[str, dict[str, object]] = {}
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.name == "manifest.sha256.json":
            continue
        digest, complete = sandbox.sha256_file(path, budget)
        relative = path.relative_to(directory).as_posix()
        entries[relative] = {
            "sha256": digest if complete else f"{digest}~partial",
            "bytes": path.stat().st_size,
        }

    manifest = {
        "schemaVersion": "1.0.0",
        "mission": "CLAUDE-B-ANIM-01",
        "generatedAt": utc_now(),
        "directory": directory.as_posix(),
        "files": entries,
        "verifyWith": (
            "python tools/claude-animation-intelligence/cli.py verify-manifest "
            f"--run {directory.as_posix()}"
        ),
    }
    target = directory / "manifest.sha256.json"
    target.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return target


def verify_manifest(directory: Path, budget: Budget) -> dict[str, object]:
    """Re-hash the artefacts and report matches, mismatches and missing files."""
    manifest_path = directory / "manifest.sha256.json"
    if not manifest_path.is_file():
        return {"ok": False, "error": f"no manifest at {manifest_path}"}
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    matched: list[str] = []
    mismatched: list[dict[str, str]] = []
    missing: list[str] = []
    for relative, expected in manifest.get("files", {}).items():
        path = directory / relative
        if not path.is_file():
            missing.append(relative)
            continue
        digest, complete = sandbox.sha256_file(path, budget)
        actual = digest if complete else f"{digest}~partial"
        if actual == expected.get("sha256"):
            matched.append(relative)
        else:
            mismatched.append(
                {"path": relative, "expected": expected.get("sha256", ""), "actual": actual}
            )
    return {
        "ok": not mismatched and not missing,
        "checked": len(matched) + len(mismatched) + len(missing),
        "matched": len(matched),
        "mismatched": mismatched,
        "missing": missing,
    }
