#!/usr/bin/env python3
"""Packs OpenComposite's release archives from the release workflow's build artifacts.

    scripts/ci/package.py --version VERSION --artifacts DIR --out DIR [--os windows linux] [--repository OWNER/NAME]

DIR of --artifacts has a folder per build, named as the workflow's artifacts: windows-x64 (vrclient_x64.dll),
windows-x86 (vrclient.dll), linux-x64 and linux-x86 (vrclient.so each). Made in --out:

    OpenComposite-windows.zip    OpenComposite/bin/vrclient_x64.dll and bin/vrclient.dll
    OpenComposite-linux.tar.gz   OpenComposite/bin/linux64/vrclient.so and bin/vrclient.so
    OpenComposite-source.tar.gz  this commit's source, submodules included
    SHA256SUMS.txt               of the three
    RELEASE_NOTES.md             the release's description

Both binary archives also have an empty bin/version.txt (it keeps Steam from rewriting openvrpaths.vrpath while that
names the folder), the licences of OpenComposite and of what is built into it, and SOURCE.txt. Each archive's entries
carry the commit's time, so the same commit packs the same bytes.
"""

import argparse
import gzip
import hashlib
import io
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# OpenComposite's licence and those of what is built into the library (the hand models, glm, the OpenXR loader and its
# jsoncpp): source path -> name in the archive.
LICENCES = {
    "LICENSE.txt": "LICENSE.txt",
    "LICENSE_OPENVR.txt": "LICENSE_OPENVR.txt",
    "LICENSE_INIH.txt": "LICENSE_INIH.txt",
    "LICENCE_BUILD_UTIL.txt": "LICENCE_BUILD_UTIL.txt",
    "assets/LICENSE.txt": "LICENSE_assets.txt",
    "assets/notes.txt": "NOTES_assets.txt",
    "libs/glm/copying.txt": "LICENSE_glm.txt",
    "libs/openxr-sdk/LICENSE": "LICENSE_openxr-sdk.txt",
    "libs/openxr-sdk/LICENSES/LicenseRef-jsoncpp-public-domain.txt": "LICENSE_jsoncpp.txt",
}
# The Linux builds have libunwind built in (the crash backtrace).
LINUX_LICENCES = {"libs/libunwind/COPYING": "LICENSE_libunwind.txt"}

# artifact folder, file in it -> path in the archive
BINARIES = {
    "windows": [
        ("windows-x64", "vrclient_x64.dll", "bin/vrclient_x64.dll"),
        ("windows-x86", "vrclient.dll", "bin/vrclient.dll"),
    ],
    "linux": [
        ("linux-x64", "vrclient.so", "bin/linux64/vrclient.so"),
        ("linux-x86", "vrclient.so", "bin/vrclient.so"),
    ],
}


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


def source_text(version: str, commit: str, repository: str) -> str:
    return (
        f"OpenComposite {version}, commit {commit}\n"
        f"From https://github.com/{repository}, a fork of https://gitlab.com/znixian/OpenOVR.\n"
        "GPL-3.0-or-later (LICENSE.txt); the other licence files are those of what is built into it.\n"
        "Its source, submodules included, is OpenComposite-source.tar.gz, which comes with this archive,\n"
        f"and https://github.com/{repository}/tree/{commit}\n"
        "Built by .github/workflows/build.yml: Release, warnings as errors; Windows with the C runtime linked in,\n"
        "Linux with the crash backtrace.\n"
    )


def binary_files(os_name: str, artifacts: Path, version: str, commit: str, repository: str) -> dict[str, tuple[bytes, int]]:
    """The binary archive's files: path -> (contents, mode)."""
    files: dict[str, tuple[bytes, int]] = {}
    for folder, name, target in BINARIES[os_name]:
        path = artifacts / folder / name
        if not path.is_file():
            sys.exit(f"{path} is missing: the {folder} build's artifact.")
        files[target] = (path.read_bytes(), 0o755)
    files["bin/version.txt"] = (b"", 0o644)
    licences = LICENCES | (LINUX_LICENCES if os_name == "linux" else {})
    for source, target in licences.items():
        path = ROOT / source
        if not path.is_file():
            sys.exit(f"{path} is missing (are the submodules checked out?).")
        files[target] = (path.read_bytes(), 0o644)
    files["SOURCE.txt"] = (source_text(version, commit, repository).encode(), 0o644)
    return files


def directories(paths) -> list[str]:
    found = set()
    for path in paths:
        parts = path.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            found.add("/".join(parts[:i]))
    return sorted(found)


def write_zip(out: Path, prefix: str, files: dict[str, tuple[bytes, int]], mtime: int) -> None:
    stamp = time.gmtime(mtime)[:6]
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(files):
            data, mode = files[path]
            info = zipfile.ZipInfo(f"{prefix}/{path}", date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100000 | mode) << 16
            archive.writestr(info, data)


def write_tar_gz(out: Path, add) -> None:
    """A gzipped tar without the time and file name a plain tarfile.open('w:gz') puts in the gzip header."""
    with out.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as archive:
            add(archive)


def tar_entry(name: str, mtime: int, mode: int, kind=tarfile.REGTYPE, size=0) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.type = kind
    info.mode = mode
    info.size = size
    info.mtime = mtime
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


def write_binary_tar(out: Path, prefix: str, files: dict[str, tuple[bytes, int]], mtime: int) -> None:
    def add(archive: tarfile.TarFile) -> None:
        archive.addfile(tar_entry(prefix, mtime, 0o755, tarfile.DIRTYPE))
        for folder in directories(files):
            archive.addfile(tar_entry(f"{prefix}/{folder}", mtime, 0o755, tarfile.DIRTYPE))
        for path in sorted(files):
            data, mode = files[path]
            archive.addfile(tar_entry(f"{prefix}/{path}", mtime, mode, size=len(data)), io.BytesIO(data))

    write_tar_gz(out, add)


def write_source_tar(out: Path, prefix: str, mtime: int) -> None:
    paths = sorted(p for p in git("ls-files", "--recurse-submodules", "-z").split("\0") if p)

    def add(archive: tarfile.TarFile) -> None:
        for path in paths:
            full = ROOT / path
            if full.is_symlink():
                info = tar_entry(f"{prefix}/{path}", mtime, 0o777, tarfile.SYMTYPE)
                info.linkname = str(Path(full).readlink()).replace("\\", "/")
                archive.addfile(info)
            elif full.is_file():
                mode = 0o755 if full.stat().st_mode & 0o111 and sys.platform != "win32" else 0o644
                data = full.read_bytes()
                archive.addfile(tar_entry(f"{prefix}/{path}", mtime, mode, size=len(data)), io.BytesIO(data))

    write_tar_gz(out, add)


def notes(version: str, commit: str, repository: str, sums: str) -> str:
    return f"""OpenComposite {version}, built from [{commit[:12]}](https://github.com/{repository}/commit/{commit}).

| File | What |
|---|---|
| `OpenComposite-windows.zip` | `bin\\vrclient_x64.dll` (64-bit games) and `bin\\vrclient.dll` (32-bit) |
| `OpenComposite-linux.tar.gz` | `bin/linux64/vrclient.so` (64-bit games) and `bin/vrclient.so` (32-bit) |
| `OpenComposite-source.tar.gz` | the source of this build, submodules included |
| `SHA256SUMS.txt` | checksums of the three |

**System-wide:** unpack the `OpenComposite` folder somewhere it can stay, and put that folder first in `"runtime"` of `openvrpaths.vrpath` (`%LOCALAPPDATA%\\openvr\\openvrpaths.vrpath` on Windows, `~/.config/openvr/openvrpaths.vrpath` on Linux). SteamVR, started again, can put itself back first.

**One game on Windows:** back up the game's `openvr_api.dll`, then put `vrclient_x64.dll` (or `vrclient.dll` for a 32-bit game) in its place, renamed to `openvr_api.dll`.

**One game on Linux:** `VR_OVERRIDE=/path/to/OpenComposite %command%` in its Steam launch options. Games in the Steam Runtime need more: see the README's [Linux-specific info](https://github.com/{repository}#linux-specific-info).

OpenComposite is GPL-3.0-or-later; each archive has its licences and a `SOURCE.txt`.

```
{sums}```
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--os", nargs="+", choices=["windows", "linux"], default=["windows", "linux"])
    parser.add_argument("--repository", default="fzeruhn/OpenComposite")
    args = parser.parse_args()

    commit = git("rev-parse", "HEAD").strip()
    mtime = int(git("log", "-1", "--format=%ct").strip())
    args.out.mkdir(parents=True, exist_ok=True)

    made = []
    if "windows" in args.os:
        out = args.out / "OpenComposite-windows.zip"
        write_zip(out, "OpenComposite", binary_files("windows", args.artifacts, args.version, commit, args.repository), mtime)
        made.append(out)
    if "linux" in args.os:
        out = args.out / "OpenComposite-linux.tar.gz"
        write_binary_tar(out, "OpenComposite", binary_files("linux", args.artifacts, args.version, commit, args.repository), mtime)
        made.append(out)
    out = args.out / "OpenComposite-source.tar.gz"
    write_source_tar(out, f"OpenComposite-{args.version}", mtime)
    made.append(out)

    sums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in made)
    (args.out / "SHA256SUMS.txt").write_text(sums, newline="\n")
    (args.out / "RELEASE_NOTES.md").write_text(notes(args.version, commit, args.repository, sums), newline="\n")
    for path in made:
        print(f"{path} ({path.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
