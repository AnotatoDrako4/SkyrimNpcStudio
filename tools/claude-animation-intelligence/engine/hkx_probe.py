"""Bounded HKX metadata probe.

This reads the size and the FIRST 16 BYTES of an ``.hkx`` file and nothing else.
There is no Havok deserialiser here, by design: parsing untrusted binary from
downloaded mods is exactly the risk this mission forbids.

Header magic was verified empirically against the user's installed animation and
behaviour files on 2026-10-22; every sampled Skyrim SE ``.hkx`` begins with the
classic Havok packfile magic ``57 E0 E0 57 10 C0 C0 10``.

HKX_FILE_PRESENT is not PLAYABLE_ANIMATION. A recognised header proves only that
the first bytes look like a Havok container; it says nothing about whether the
file is intact, targets the right skeleton, or plays in game.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .sandbox import Budget

HAVOK_PACKFILE_MAGIC = bytes.fromhex("57e0e05710c0c010")
HEADER_BYTES = 16

KIND_PACKFILE = "havok-packfile"
KIND_UNRECOGNISED = "unrecognised"
KIND_UNREADABLE = "unreadable"
KIND_EMPTY = "empty"

_NOTE = (
    "Header recognition only. The file was not deserialised, so this says nothing "
    "about whether the animation is valid or plays in game."
)


@dataclass(frozen=True)
class HkxProbe:
    """What can honestly be said about an .hkx without parsing it."""

    size: int
    header_hex: str
    kind: str
    note: str = _NOTE

    def to_json(self) -> dict[str, object]:
        return {
            "bytes": self.size,
            "headerHex": self.header_hex,
            "headerKind": self.kind,
            "note": self.note,
        }


def probe(path: Path, budget: Budget | None = None) -> HkxProbe:
    """Read the size and first 16 bytes. Never raises for an unreadable file."""
    try:
        size = path.stat().st_size
    except OSError as exc:
        return HkxProbe(0, "", KIND_UNREADABLE, f"stat failed: {exc.strerror or exc}")
    if size == 0:
        return HkxProbe(0, "", KIND_EMPTY, "zero-byte file; it cannot contain an animation")
    try:
        with path.open("rb") as handle:
            head = handle.read(HEADER_BYTES)
    except OSError as exc:
        return HkxProbe(size, "", KIND_UNREADABLE, f"read failed: {exc.strerror or exc}")

    kind = KIND_PACKFILE if head.startswith(HAVOK_PACKFILE_MAGIC) else KIND_UNRECOGNISED
    note = _NOTE
    if kind == KIND_UNRECOGNISED:
        note = (
            "The first bytes do not match the Havok packfile magic seen in this "
            "installation. That may be a different Havok container version, not "
            "necessarily a broken file. " + _NOTE
        )
    return HkxProbe(size=size, header_hex=head.hex(), kind=kind, note=note)
