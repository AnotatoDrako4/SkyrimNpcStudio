"""Download Qt for MSVC from the official online repository and verify every archive.

aqtinstall 3.3.0 does not understand the 2026 split repository layout
(qt6_<ver>/qt6_<ver>_<arch>/), so this script reads Updates.xml directly, downloads the needed
.7z archives, checks each one against the repository's .sha1 file and extracts them.

usage: python -I fetch_qt.py <install-root> [--version 6.11.2] [--cache <dir>]
Result: <install-root>/<version>/msvc2022_64/lib/cmake/Qt6/Qt6Config.cmake
Requires: py7zr (pip install py7zr)
"""
import argparse
import hashlib
import pathlib
import sys
import urllib.request
import xml.etree.ElementTree as ET

import py7zr

REPO = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"
ARCH = "win64_msvc2022_64"
ADDONS = ("qtquick3d", "qtshadertools", "qtquicktimeline", "qtimageformats")
SKIP_ARCHIVES = ("qtdoc-",)


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=300) as response:
        return response.read()


def safe_extract(archive: pathlib.Path, destination: pathlib.Path) -> None:
    with py7zr.SevenZipFile(archive, "r") as z:
        for name in z.getnames():
            parts = pathlib.PurePosixPath(name.replace("\\", "/"))
            if parts.is_absolute() or ".." in parts.parts:
                raise RuntimeError(f"unsafe path {name!r} in {archive.name}")
        z.extractall(path=destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--version", default="6.11.2")
    parser.add_argument("--cache")
    args = parser.parse_args()

    compact = args.version.replace(".", "")
    base = f"{REPO}/qt6_{compact}/qt6_{compact}_msvc2022_64"
    target = pathlib.Path(args.root) / args.version / "msvc2022_64"
    cache = pathlib.Path(args.cache) if args.cache else pathlib.Path(args.root) / "_downloads"
    target.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)

    wanted = {f"qt.qt6.{compact}.{ARCH}"} | {f"qt.qt6.{compact}.addons.{a}.{ARCH}" for a in ADDONS}
    found = set()
    for package in ET.fromstring(fetch(f"{base}/Updates.xml")).findall("PackageUpdate"):
        name = package.findtext("Name")
        if name not in wanted:
            continue
        found.add(name)
        version = package.findtext("Version")
        for archive in filter(None, (a.strip() for a in (package.findtext("DownloadableArchives") or "").split(","))):
            if archive.startswith(SKIP_ARCHIVES):
                continue
            url = f"{base}/{name}/{version}{archive}"
            expected = fetch(url + ".sha1").decode().split()[0].lower()
            local = cache / archive
            if not local.exists() or hashlib.sha1(local.read_bytes()).hexdigest() != expected:
                print(f"download {archive}", flush=True)
                local.write_bytes(fetch(url))
            actual = hashlib.sha1(local.read_bytes()).hexdigest()
            if actual != expected:
                print(f"SHA1 mismatch for {archive}: {actual} != {expected}", file=sys.stderr)
                return 2
            print(f"verified {archive}", flush=True)
            safe_extract(local, target)

    missing = wanted - found
    if missing:
        print(f"packages not found in the repository: {sorted(missing)}", file=sys.stderr)
        return 3
    print(f"Qt {args.version} installed in {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
