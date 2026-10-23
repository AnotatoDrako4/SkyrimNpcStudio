"""Bounded, read-only BSA name-table reader and archive analysis.

Only the directory of an archive is read: the 36-byte header, the folder records,
the file-record blocks and the file-name table. File DATA is never read, never
decompressed and never extracted.

Format evidence. The layout follows the Skyrim archive format (UESP "Skyrim
Mod:Archive File Format"); it was then verified EMPIRICALLY on 2026-10-23 against
all 110 archives of the user's enabled mods plus the official
``Skyrim - Animations.bsa``: for every one, the position reached after parsing
equals the value implied by the header counts exactly. All of them are version 105
(Skyrim SE). Version 104 (Skyrim LE, 16-byte folder records) is accepted but was
not present on this machine, so it is marked as not empirically verified.

What may be claimed about archive contents:

* A loose file wins over an archive entry at the same path (MO2 mod-information
  documentation). Reported with confidence ``likely``, not ``proven``.
* Between two archives the winner is decided by the load order of the plugins
  that load them, NOT by MO2 mod priority, so no archive-vs-archive winner is ever
  stated.
* An archive loads only when its associated plugin is loaded (or it is listed in
  an ini resource list, which this tool does not read). That association is a
  best-effort name match and is reported as detail, never as a finding.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

from . import issues
from .loose_winners import KIND_ANIMATION, KIND_BEHAVIOR, LooseIndex, classify
from .sandbox import Budget, norm_key

MAGIC = b"BSA\x00"
HEADER = struct.Struct("<4sIIIIIIIHH")
FOLDER_RECORD = {105: struct.Struct("<QIIQ"), 104: struct.Struct("<QII")}
FILE_RECORD_SIZE = 16
FLAG_DIR_NAMES = 0x1
FLAG_FILE_NAMES = 0x2
VERIFIED_VERSIONS = frozenset({105})

MAX_INDEX_BYTES = 32 * 1024 * 1024  # an archive directory larger than this is refused
MAX_FILES_PER_ARCHIVE = 2_000_000

SOURCE_FORMAT = (
    "UESP Skyrim Mod:Archive File Format; verified empirically on 110 installed v105 "
    "archives (parsed end offset == header-implied offset)"
)
SOURCE_MO2 = "https://github.com/ModOrganizer2/modorganizer/wiki/Mod-information-window"

ANIMATION_KINDS = (KIND_ANIMATION, KIND_BEHAVIOR)


@dataclass
class BsaIndex:
    """The directory of one archive, or the reason it could not be read."""

    version: int = 0
    flags: int = 0
    paths: list[str] = field(default_factory=list)
    error: str = ""

    @property
    def ok(self) -> bool:
        return not self.error


def read_bsa_names(path: Path, budget: Budget | None = None) -> BsaIndex:
    """Read an archive's file names, refusing anything that does not add up.

    Every count from the header is checked against the file size before it is used,
    and the final position must equal the header-implied end of the name table.
    A mismatch means the layout is not the one verified, so the archive is refused
    rather than half-read.
    """
    try:
        size = path.stat().st_size
        with path.open("rb") as handle:
            head = handle.read(HEADER.size)
            if len(head) < HEADER.size:
                return BsaIndex(error="file shorter than a BSA header")
            (magic, version, offset, aflags, folders, files, folder_name_len,
             file_name_len, _fflags, _pad) = HEADER.unpack(head)
            if magic != MAGIC:
                return BsaIndex(error=f"not a Skyrim BSA (magic {magic!r})")
            record = FOLDER_RECORD.get(version)
            if record is None:
                return BsaIndex(version=version, error=f"unsupported BSA version {version}")
            if not (aflags & FLAG_DIR_NAMES and aflags & FLAG_FILE_NAMES):
                return BsaIndex(
                    version=version, flags=aflags,
                    error="archive does not embed directory and file names",
                )
            if files > MAX_FILES_PER_ARCHIVE:
                return BsaIndex(version=version, error=f"{files} files exceeds the per-archive limit")

            expected_end = (
                offset
                + folders * record.size
                + folders + folder_name_len
                + files * FILE_RECORD_SIZE
                + file_name_len
            )
            if offset != HEADER.size or expected_end > size:
                return BsaIndex(version=version, error="header counts point past the end of file")
            if expected_end > MAX_INDEX_BYTES:
                return BsaIndex(version=version, error="archive directory exceeds the read budget")

            handle.seek(offset)
            counts = [record.unpack(handle.read(record.size))[1] for _ in range(folders)]
            if sum(counts) != files:
                return BsaIndex(version=version, error="folder file counts do not sum to the header total")

            folder_names: list[str] = []
            for count in counts:
                length = handle.read(1)
                if not length:
                    return BsaIndex(version=version, error="truncated folder name")
                raw = handle.read(length[0])
                folder_names.append(raw.rstrip(b"\x00").decode("cp1252", errors="strict"))
                handle.seek(count * FILE_RECORD_SIZE, 1)

            blob = handle.read(file_name_len)
            names = blob.split(b"\x00")
            if handle.tell() != expected_end or len(names) < files:
                return BsaIndex(version=version, error="parsed layout does not match the verified format")
    except UnicodeDecodeError as exc:
        return BsaIndex(error=f"name table is not cp1252 text at byte {exc.start}")
    except (OSError, struct.error) as exc:
        return BsaIndex(error=f"unreadable archive: {exc}")

    paths: list[str] = []
    cursor = 0
    for folder, count in zip(folder_names, counts):
        for name in names[cursor : cursor + count]:
            paths.append(f"{folder}\\{name.decode('cp1252')}".replace("\\", "/"))
        cursor += count
    return BsaIndex(version=version, flags=aflags, paths=paths)


def _plugin_candidates(archive_name: str) -> list[str]:
    """``<plugin>.bsa`` and ``<plugin> - <suffix>.bsa`` belong to ``<plugin>``.

    A plugin name may itself contain " - " (``Anchor SE - Johnskyrim.esp`` owns
    ``Anchor SE - Johnskyrim - Textures.bsa``), so every " - " prefix is a candidate.
    Splitting only at the first one left 4 of 110 real archives unmatched.
    """
    stem = archive_name.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    parts = stem.split(" - ")
    bases = [" - ".join(parts[:i]) for i in range(len(parts), 0, -1)]
    return [f"{base}{ext}".lower() for base in bases for ext in (".esp", ".esm", ".esl")]


@dataclass
class ArchiveRecord:
    mod: str
    priority: int
    path: str
    absolute: Path
    bytes: int
    index: BsaIndex
    plugin_loaded: str = "unknown"  # "yes" | "no" | "unknown"
    relevant: dict[str, str] = field(default_factory=dict)  # norm key -> kind


@dataclass
class ArchiveCatalog:
    archives: list[ArchiveRecord] = field(default_factory=list)

    @property
    def indexed(self) -> list[ArchiveRecord]:
        return [a for a in self.archives if a.index.ok]

    @property
    def failed(self) -> list[ArchiveRecord]:
        return [a for a in self.archives if not a.index.ok]

    def relevant_paths(self) -> dict[str, list[ArchiveRecord]]:
        out: dict[str, list[ArchiveRecord]] = {}
        for archive in self.indexed:
            for key in archive.relevant:
                out.setdefault(key, []).append(archive)
        return out

    def summary(self) -> dict[str, object]:
        rel = self.relevant_paths()
        return {
            "archives": len(self.archives),
            "indexed": len(self.indexed),
            "failed": len(self.failed),
            "versions": sorted({a.index.version for a in self.indexed}),
            "animationPathsInArchives": len(rel),
            "archivesWithAnimations": sum(1 for a in self.indexed if a.relevant),
        }


def index_archives(
    loose: LooseIndex,
    budget: Budget,
    loaded_plugins: frozenset[str] | set[str] | None = None,
    listed_plugins: frozenset[str] | set[str] | None = None,
) -> ArchiveCatalog:
    """Read the name table of every archive the loose walk found, and mark each one
    as indexed in ``loose.archives`` so the loose-only coverage note stays accurate."""
    catalog = ArchiveCatalog()
    for entry in loose.archives:
        absolute = Path(str(entry["absolute"]))
        index = read_bsa_names(absolute, budget)
        record = ArchiveRecord(
            mod=str(entry["mod"]),
            priority=int(entry["priority"]),  # type: ignore[arg-type]
            path=str(entry["path"]),
            absolute=absolute,
            bytes=int(entry["bytes"]),  # type: ignore[arg-type]
            index=index,
        )
        entry["indexed"] = index.ok
        if loaded_plugins is not None:
            candidates = _plugin_candidates(record.path)
            if any(c in loaded_plugins for c in candidates):
                record.plugin_loaded = "yes"
            elif listed_plugins is not None and any(c in listed_plugins for c in candidates):
                record.plugin_loaded = "no"
        for archived in index.paths:
            kind = classify(archived)
            if kind in ANIMATION_KINDS:
                record.relevant[norm_key(archived)] = kind
        catalog.archives.append(record)
    return catalog


def archive_issues(catalog: ArchiveCatalog, loose: LooseIndex, budget: Budget) -> list[issues.Issue]:
    """Archive-only animations, loose-over-archive shadows, archive-vs-archive overlaps."""
    out: list[issues.Issue] = []
    rel = catalog.relevant_paths()

    archive_only = sorted(k for k in rel if k not in loose.records)
    shadowed = sorted(k for k in rel if k in loose.records)
    overlapping = sorted(k for k, owners in rel.items() if len({a.path for a in owners}) > 1)

    def refs(owners: list[ArchiveRecord]) -> list[issues.ProviderRef]:
        return [
            issues.ProviderRef(mod=a.mod, priority=a.priority, path=a.path, bytes=a.bytes,
                               role=f"archive (plugin loaded: {a.plugin_loaded})")
            for a in owners
        ]

    if archive_only:
        owners_by_path = {a.path: a for key in archive_only for a in rel[key]}
        out.append(
            issues.Issue(
                category=issues.ARCHIVE_ONLY_ANIMATION,
                subject=f"{len(archive_only)} animation path(s) exist only inside archives",
                what_was_observed=(
                    f"{len(archive_only)} animation or behaviour path(s) are supplied only by "
                    "BSA archives, with no loose copy in any enabled mod, e.g. "
                    + "; ".join(archive_only[:5])
                ),
                why_it_matters=(
                    "These were invisible before archive names were read. They load only if "
                    "the archive's plugin is loaded; the plugin association is shown per "
                    "archive and is a best-effort name match."
                ),
                action="Informational. No action unless one of these clips misbehaves.",
                confidence=issues.PROVEN,
                runtime_verification=issues.STATIC_FILE_FACT,
                providers=refs([owners_by_path[p] for p in sorted(owners_by_path)])[:30],
                source_reference=SOURCE_FORMAT,
                detail={"paths": archive_only[: budget.max_html_rows], "count": len(archive_only)},
            )
        )

    for key in shadowed[: budget.max_html_rows]:
        record = loose.records[key]
        out.append(
            issues.Issue(
                category=issues.LOOSE_OVERRIDES_ARCHIVE,
                subject=record.display,
                what_was_observed=(
                    f"A loose copy from {record.winner.mod} and an archived copy in "
                    + ", ".join(sorted({a.path for a in rel[key]}))
                    + " share this path."
                ),
                why_it_matters=(
                    "Loose files take precedence over archive contents in MO2, so the loose "
                    "copy is the one deployed. Usually intended, e.g. a patch replacing an "
                    "archived clip."
                ),
                action="Informational. Remove the loose copy only if you want the archived one.",
                confidence=issues.LIKELY,
                runtime_verification=issues.NOT_VERIFIED_IN_GAME,
                providers=[
                    issues.ProviderRef(mod=record.winner.mod, priority=record.winner.priority,
                                       path=record.winner.raw_relative, bytes=record.winner.size,
                                       role="loose")
                ] + refs(rel[key]),
                source_reference=SOURCE_MO2,
            )
        )

    for key in overlapping[: budget.max_html_rows]:
        owners = rel[key]
        out.append(
            issues.Issue(
                category=issues.ARCHIVE_PATH_OVERLAP,
                subject=key,
                what_was_observed=(
                    f"{len({a.path for a in owners})} archives contain this path: "
                    + ", ".join(sorted({a.path for a in owners}))
                ),
                why_it_matters=(
                    "Between archives the winner follows the load order of the plugins that "
                    "load them, not MO2 mod priority, so no winner is stated here."
                ),
                action="Check plugin load order if the wrong version of this clip appears.",
                confidence=issues.UNVERIFIABLE,
                runtime_verification=issues.NOT_VERIFIED_IN_GAME,
                providers=refs(owners),
                source_reference=SOURCE_FORMAT,
            )
        )

    for archive in catalog.failed:
        out.append(
            issues.Issue(
                category=issues.UNVERIFIED_FORMAT,
                subject=archive.path,
                what_was_observed=f"{archive.mod}: {archive.index.error}",
                why_it_matters=(
                    "This archive's names could not be read with the verified layout, so any "
                    "animation inside it is not in this report."
                ),
                action="Informational. The archive was not modified.",
                confidence=issues.UNVERIFIABLE,
                providers=[issues.ProviderRef(mod=archive.mod, priority=archive.priority,
                                              path=archive.path, bytes=archive.bytes)],
                source_reference=SOURCE_FORMAT,
            )
        )

    unverified_versions = sorted({a.index.version for a in catalog.indexed} - VERIFIED_VERSIONS)
    if unverified_versions:
        out.append(
            issues.Issue(
                category=issues.UNVERIFIED_FORMAT,
                subject="archive version not verified on this machine",
                what_was_observed=f"Archives of version(s) {unverified_versions} were read.",
                why_it_matters="Only version 105 was verified empirically; treat their names with care.",
                action="Informational.",
                confidence=issues.UNVERIFIABLE,
                source_reference=SOURCE_FORMAT,
            )
        )
    return out
