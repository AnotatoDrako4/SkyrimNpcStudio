"""Confined, read-only filesystem access with explicit budgets.

Every path this lab touches goes through :func:`resolve_confined`. Nothing is ever
opened for writing here, and directory walks refuse to cross reparse points
(junctions / symlinks) so a mod folder cannot redirect the scan outside the
instance it belongs to.
"""

from __future__ import annotations

import hashlib
import os
import time
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path, PurePath
from typing import Callable, Iterator

FILE_ATTRIBUTE_REPARSE_POINT = 0x400


class SandboxError(Exception):
    """Base class for every refusal raised by this module."""


class ConfinementError(SandboxError):
    """A path resolved outside the authorised root."""


class BudgetError(SandboxError):
    """A declared budget would be exceeded."""


class DecodeError(SandboxError):
    """A text file is not valid UTF-8 and must not be silently repaired."""


@dataclass(frozen=True)
class Budget:
    """Guardrails. Hitting one yields a ``TRUNCATED`` marker, never a fake result."""

    max_depth: int = 24
    max_files: int = 400_000
    max_json_bytes: int = 4 * 1024 * 1024
    max_submods: int = 20_000
    max_html_rows: int = 2_000
    max_seconds: float = 1_800.0
    max_hash_bytes: int = 32 * 1024 * 1024

    def scaled(self, **kwargs: object) -> "Budget":
        merged: dict[str, object] = {f: getattr(self, f) for f in self.__dataclass_fields__}
        merged.update(kwargs)
        return Budget(**merged)  # type: ignore[arg-type]


@dataclass
class WalkReport:
    """What a walk actually managed to see, and what it refused to look at."""

    files_seen: int = 0
    dirs_seen: int = 0
    pruned_dirs: int = 0  # deliberately not walked by policy; NOT an access failure
    truncated: list[str] = field(default_factory=list)
    skipped: list[dict[str, str]] = field(default_factory=list)

    @property
    def is_truncated(self) -> bool:
        return bool(self.truncated)

    def note_truncation(self, reason: str) -> None:
        if reason not in self.truncated:
            self.truncated.append(reason)

    def note_skip(self, path: str, reason: str) -> None:
        self.skipped.append({"path": path, "reason": reason})


def is_unc(path: "os.PathLike[str] | str") -> bool:
    text = str(path)
    return text.startswith("\\\\") or text.startswith("//")


def is_reparse_point(path: "os.PathLike[str] | str") -> bool:
    """True for a junction, symlink or other reparse point, without following it."""
    try:
        st = os.lstat(path)
    except OSError:
        return False
    if getattr(st, "st_file_attributes", 0) & FILE_ATTRIBUTE_REPARSE_POINT:
        return True
    return os.path.islink(path)


def norm_key(relative: str) -> str:
    """MO2 treats Data paths as case-insensitive with either separator.

    The key is casefolded and NFC-normalised so ``Meshes\\MT_Idle.HKX`` and
    ``meshes/mt_idle.hkx`` collide, which is what the game's VFS does.
    """
    unified = relative.replace("\\", "/").strip("/")
    while "//" in unified:
        unified = unified.replace("//", "/")
    return unicodedata.normalize("NFC", unified).casefold()


def spelling_variance(raw_paths: list[str]) -> str:
    """Classify how several raw spellings of one normalised key differ.

    ``"none"`` when they are byte-identical, ``"case"`` when they differ only in
    letter case, ``"unicode"`` when NFC normalisation is what makes them equal.
    Reported as information, never as a conflict: whether Windows treats them as
    one file depends on the filesystem, which reading alone cannot prove.
    """
    distinct = set(raw_paths)
    if len(distinct) < 2:
        return "none"
    if len({p.casefold() for p in distinct}) == 1:
        return "case"
    if len({unicodedata.normalize("NFC", p).casefold() for p in distinct}) == 1:
        return "unicode"
    return "mixed"


def _parts_normcase(path: Path) -> tuple[str, ...]:
    return tuple(os.path.normcase(part) for part in path.parts)


def resolve_confined(root: Path, relative: "str | PurePath" = "") -> Path:
    """Resolve ``relative`` under ``root``, refusing anything that escapes it.

    Raises :class:`ConfinementError` for ``..`` traversal, absolute or UNC
    escapes, and for any real path that lands outside ``root``.
    """
    rel = PurePath(str(relative).replace("\\", "/")) if str(relative) else PurePath()
    if rel.parts:
        if rel.is_absolute() or is_unc(str(rel)):
            raise ConfinementError(f"absolute or UNC path not allowed: {relative!r}")
        if any(part == ".." for part in rel.parts):
            raise ConfinementError(f"parent traversal not allowed: {relative!r}")

    root_real = Path(os.path.realpath(root))
    candidate = root_real.joinpath(*rel.parts) if rel.parts else root_real
    candidate_real = Path(os.path.realpath(candidate))

    root_parts = _parts_normcase(root_real)
    cand_parts = _parts_normcase(candidate_real)
    if cand_parts[: len(root_parts)] != root_parts:
        raise ConfinementError(f"path escapes root {root_real}: {candidate_real}")
    if is_unc(candidate_real) and not is_unc(root_real):
        raise ConfinementError(f"UNC escape from local root: {candidate_real}")
    return candidate_real


class Clock:
    """Monotonic deadline shared by a whole scan."""

    def __init__(self, budget: Budget) -> None:
        self.budget = budget
        self.started = time.monotonic()

    @property
    def elapsed(self) -> float:
        return time.monotonic() - self.started

    @property
    def expired(self) -> bool:
        return self.elapsed > self.budget.max_seconds


def walk_files(
    root: Path,
    budget: Budget,
    report: WalkReport,
    clock: "Clock | None" = None,
    want: "Callable[[str], bool] | None" = None,
    prune_dir: "Callable[[str], bool] | None" = None,
) -> Iterator[tuple[str, Path]]:
    """Yield ``(relative_posix_path, absolute_path)`` for regular files under ``root``.

    Reparse-point directories are skipped and recorded rather than followed: on
    Windows we cannot cheaply prove where a junction lands, and the mission
    forbids reading outside the instance.
    """
    clock = clock or Clock(budget)
    try:
        root_real = Path(os.path.realpath(root))
    except OSError as exc:
        report.note_skip(str(root), f"unreadable root: {exc.strerror or exc}")
        return
    if not root_real.is_dir():
        return

    stack: list[tuple[Path, str, int]] = [(root_real, "", 0)]
    while stack:
        current, rel_prefix, depth = stack.pop()
        if clock.expired:
            report.note_truncation("time budget exhausted")
            return
        if depth > budget.max_depth:
            report.note_truncation(f"depth limit {budget.max_depth}")
            continue
        try:
            entries = sorted(os.scandir(current), key=lambda e: e.name, reverse=True)
        except OSError as exc:
            report.note_skip(rel_prefix or ".", f"unreadable directory: {exc.strerror or exc}")
            continue
        report.dirs_seen += 1
        for entry in entries:
            rel = f"{rel_prefix}/{entry.name}" if rel_prefix else entry.name
            try:
                if entry.is_dir(follow_symlinks=False):
                    if is_reparse_point(entry.path):
                        report.note_skip(rel, "reparse point (junction/symlink) not followed")
                        continue
                    if prune_dir is not None and prune_dir(rel):
                        report.pruned_dirs += 1
                        continue
                    stack.append((Path(entry.path), rel, depth + 1))
                    continue
                if not entry.is_file(follow_symlinks=False):
                    continue
                if is_reparse_point(entry.path):
                    report.note_skip(rel, "reparse point file not followed")
                    continue
            except OSError as exc:
                report.note_skip(rel, f"stat failed: {exc.strerror or exc}")
                continue
            if want is not None and not want(rel):
                continue
            if report.files_seen >= budget.max_files:
                report.note_truncation(f"file limit {budget.max_files}")
                return
            report.files_seen += 1
            yield rel, Path(entry.path)


def read_text_confined(path: Path, budget: Budget, *, max_bytes: "int | None" = None) -> str:
    """Read UTF-8 (BOM tolerated) and refuse to guess at broken bytes."""
    limit = budget.max_json_bytes if max_bytes is None else max_bytes
    size = path.stat().st_size
    if size > limit:
        raise BudgetError(f"{path.name} is {size} bytes, over the {limit} byte limit")
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise DecodeError(f"{path.name} is not valid UTF-8 at byte {exc.start}") from exc


def sha256_file(path: Path, budget: Budget) -> tuple[str, bool]:
    """Stream a SHA-256. Returns ``(digest, complete)``; partial beyond the budget."""
    digest = hashlib.sha256()
    read = 0
    complete = True
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 256)
            if not chunk:
                break
            if read + len(chunk) > budget.max_hash_bytes:
                digest.update(chunk[: budget.max_hash_bytes - read])
                complete = False
                break
            digest.update(chunk)
            read += len(chunk)
    return digest.hexdigest(), complete


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()
