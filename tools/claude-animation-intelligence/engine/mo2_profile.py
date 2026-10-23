"""Read-only MO2 instance and profile model.

``modlist.txt`` semantics are taken from the project's own verified C++ reader,
``engines/mo2/src/profile.cpp::parse_modlist``: the first line is the HIGHEST
priority, so the list is reversed to make ``index == priority``. ``+`` enables,
``-`` disables, ``*`` marks a foreign/unmanaged entry, ``#`` is a comment, and a
name ending in ``_separator`` is a visual separator, not a data provider.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from . import sandbox
from .sandbox import Budget, ConfinementError, resolve_confined

BYTEARRAY_RE = re.compile(r"^@ByteArray\((.*)\)$", re.DOTALL)

# The engine always loads these, and MO2 never writes them into plugins.txt.
# Source: ModOrganizer2/modorganizer-game_skyrimse src/gameskyrimse.cpp primaryPlugins().
BASE_MASTERS = frozenset(
    {"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"}
)


class ProfileError(Exception):
    """The instance or profile cannot be read as an MO2 layout."""


@dataclass(frozen=True)
class ModEntry:
    """One ``modlist.txt`` row. ``priority`` ascends: higher wins a loose path."""

    name: str
    enabled: bool
    kind: str  # "regular" | "separator" | "foreign"
    priority: int

    @property
    def is_provider(self) -> bool:
        """Only enabled regular mods contribute files to the virtual Data folder."""
        return self.enabled and self.kind == "regular"


@dataclass
class Instance:
    """A resolved MO2 instance: where mods, profiles and overwrite live."""

    ini_path: Path
    base_dir: Path
    mods_dir: Path
    profiles_dir: Path
    overwrite_dir: Path
    game_name: str = ""
    game_path: str = ""
    selected_profile: str = ""

    def profile_names(self) -> list[str]:
        if not self.profiles_dir.is_dir():
            return []
        return sorted(
            entry.name
            for entry in self.profiles_dir.iterdir()
            if entry.is_dir() and (entry / "modlist.txt").is_file()
        )


@dataclass
class Profile:
    """One profile's mod order plus the plugin lists, all read-only."""

    name: str
    directory: Path
    mods: list[ModEntry] = field(default_factory=list)
    plugins: list[tuple[str, bool]] = field(default_factory=list)
    load_order: list[str] = field(default_factory=list)
    archives: list[str] = field(default_factory=list)
    settings: dict[str, str] = field(default_factory=dict)
    notes: list[str] = field(default_factory=list)

    @property
    def providers(self) -> list[ModEntry]:
        """Enabled regular mods in ascending priority, lowest first."""
        return [m for m in self.mods if m.is_provider]

    @property
    def enabled_count(self) -> int:
        return len(self.providers)

    @property
    def disabled_names(self) -> list[str]:
        return [m.name for m in self.mods if not m.enabled and m.kind != "separator"]


def _unwrap_bytearray(value: str) -> str:
    """Qt writes ``@ByteArray(...)`` and escapes backslashes inside it."""
    match = BYTEARRAY_RE.match(value.strip())
    text = match.group(1) if match else value.strip()
    return text.replace("\\\\", "\\")


def parse_ini(text: str) -> dict[str, dict[str, str]]:
    """Minimal INI reader; MO2 writes duplicate-free keys and Qt ``@ByteArray``."""
    sections: dict[str, dict[str, str]] = {"": {}}
    current = ""
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].strip()
            sections.setdefault(current, {})
            continue
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        sections[current][key.strip()] = value.strip()
    return sections


def parse_modlist(text: str) -> list[ModEntry]:
    """Parse ``modlist.txt`` into ascending-priority entries.

    Unrecognised leading characters are dropped rather than guessed at, so a
    hand-edited file cannot invent a mod.
    """
    top_down: list[tuple[str, bool, str]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        marker, name = line[0], line[1:].strip()
        if not name:
            continue
        if marker == "+":
            enabled, kind = True, "regular"
        elif marker == "-":
            enabled, kind = False, "regular"
        elif marker == "*":
            enabled, kind = True, "foreign"
        else:
            continue
        if kind == "regular" and name.endswith("_separator"):
            kind = "separator"
        top_down.append((name, enabled, kind))

    top_down.reverse()  # first line is highest priority -> index becomes priority
    return [
        ModEntry(name=name, enabled=enabled, kind=kind, priority=index)
        for index, (name, enabled, kind) in enumerate(top_down)
    ]


def parse_plugins_txt(text: str) -> list[tuple[str, bool]]:
    out: list[tuple[str, bool]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        active = line.startswith("*")
        name = line[1:].strip() if active else line
        if name:
            out.append((name, active))
    return out


def parse_plain_list(text: str) -> list[str]:
    return [x.strip() for x in text.splitlines() if x.strip() and not x.strip().startswith("#")]


# The order in which primaryPlugins() lists the base masters.
BASE_MASTER_ORDER = ("skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm")


def load_order(instance: Instance, profile: Profile, budget: Budget) -> list[str]:
    """Lower-cased plugin load order: base masters, Skyrim.ccc entries, then the
    starred plugins in plugins.txt order.

    GameSkyrimSE declares LoadOrderMechanism::PluginsTxt (gameskyrimse.cpp), so the
    position in plugins.txt is the load position, after the primary plugins.
    """
    order: list[str] = list(BASE_MASTER_ORDER)
    seen = set(order)
    if instance.game_path:
        ccc = Path(instance.game_path) / "Skyrim.ccc"
        if ccc.is_file():
            try:
                text = sandbox.read_text_confined(ccc, budget, max_bytes=1024 * 1024)
            except sandbox.SandboxError:
                text = ""
            for line in text.splitlines():
                name = line.strip().lower()
                if name and name not in seen:
                    order.append(name)
                    seen.add(name)
    for name, active in profile.plugins:
        lowered = name.lower()
        if active and lowered not in seen:
            order.append(lowered)
            seen.add(lowered)
    return order


def primary_plugins(instance: Instance, budget: Budget) -> tuple[frozenset[str], str]:
    """Plugins the game loads regardless of ``plugins.txt``, lower-cased.

    MO2's ``GameSkyrimSE::primaryPlugins()`` is the five base masters plus every line
    of ``<gameDir>/Skyrim.ccc`` (Creation Club content). Such a plugin can appear in
    plugins.txt WITHOUT a ``*`` and still be loaded, so "not starred" alone must
    never be read as "not loaded". Returns the set and a note naming its source.
    """
    names = set(BASE_MASTERS)
    if not instance.game_path:
        return frozenset(names), "base masters only: gamePath missing from ModOrganizer.ini"
    ccc = Path(instance.game_path) / "Skyrim.ccc"
    if not ccc.is_file():
        return frozenset(names), f"base masters only: no Skyrim.ccc at {ccc}"
    try:
        text = sandbox.read_text_confined(ccc, budget, max_bytes=1024 * 1024)
    except sandbox.SandboxError as exc:
        return frozenset(names), f"base masters only: Skyrim.ccc unreadable ({exc})"
    names.update(line.strip().lower() for line in text.splitlines() if line.strip())
    return frozenset(names), f"base masters + {ccc}"


def load_instance(instance_arg: Path | str, budget: Budget) -> Instance:
    """Resolve an instance from a directory holding ModOrganizer.ini, or the ini itself."""
    target = Path(instance_arg)
    ini_path = target if target.is_file() else target / "ModOrganizer.ini"
    if not ini_path.is_file():
        raise ProfileError(f"no ModOrganizer.ini at {ini_path}")

    ini = parse_ini(sandbox.read_text_confined(ini_path, budget, max_bytes=8 * 1024 * 1024))
    general = ini.get("General", {})
    settings = ini.get("Settings", {})

    base_raw = _unwrap_bytearray(settings.get("base_directory", ""))
    base_dir = Path(base_raw) if base_raw else ini_path.parent
    if not base_dir.is_dir():
        raise ProfileError(f"base_directory {base_dir} does not exist")

    def sub(key: str, default_name: str) -> Path:
        raw = _unwrap_bytearray(settings.get(key, ""))
        if not raw:
            return base_dir / default_name
        return Path(raw.replace("%BASE_DIR%", str(base_dir)))

    return Instance(
        ini_path=ini_path.resolve(),
        base_dir=base_dir.resolve(),
        mods_dir=sub("mod_directory", "mods"),
        profiles_dir=sub("profiles_directory", "profiles"),
        overwrite_dir=sub("overwrite_directory", "overwrite"),
        game_name=_unwrap_bytearray(general.get("gameName", "")),
        game_path=_unwrap_bytearray(general.get("gamePath", "")),
        selected_profile=_unwrap_bytearray(general.get("selected_profile", "")),
    )


def load_profile(instance: Instance, name: str, budget: Budget) -> Profile:
    """Load one profile by name, confined under the instance's profiles directory."""
    if not name or any(sep in name for sep in ("/", "\\")) or name in (".", ".."):
        raise ProfileError(f"invalid profile name {name!r}")
    try:
        directory = resolve_confined(instance.profiles_dir, name)
    except ConfinementError as exc:
        raise ProfileError(str(exc)) from exc
    if not directory.is_dir():
        raise ProfileError(f"profile {name!r} does not exist under {instance.profiles_dir}")

    modlist = directory / "modlist.txt"
    if not modlist.is_file():
        raise ProfileError(f"profile {name!r} has no modlist.txt")

    profile = Profile(name=name, directory=directory)
    profile.mods = parse_modlist(
        sandbox.read_text_confined(modlist, budget, max_bytes=16 * 1024 * 1024)
    )

    for filename, target in (
        ("plugins.txt", profile.plugins),
        ("loadorder.txt", profile.load_order),
        ("archives.txt", profile.archives),
    ):
        path = directory / filename
        if not path.is_file():
            continue
        try:
            text = sandbox.read_text_confined(path, budget, max_bytes=8 * 1024 * 1024)
        except sandbox.SandboxError as exc:
            profile.notes.append(f"{filename}: {exc}")
            continue
        if filename == "plugins.txt":
            target.extend(parse_plugins_txt(text))  # type: ignore[arg-type]
        else:
            target.extend(parse_plain_list(text))  # type: ignore[arg-type]

    settings_ini = directory / "settings.ini"
    if settings_ini.is_file():
        try:
            parsed = parse_ini(sandbox.read_text_confined(settings_ini, budget))
            profile.settings = dict(parsed.get("General", {}))
        except sandbox.SandboxError as exc:
            profile.notes.append(f"settings.ini: {exc}")

    return profile
