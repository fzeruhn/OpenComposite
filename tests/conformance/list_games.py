#!/usr/bin/env python3
"""Lists the installed Steam games that use OpenVR, for the test list.

  list_games.py --steam <Steam folder> [--markdown | --json]
  list_games.py --self-test

Reads steamapps/libraryfolders.vdf and each library's appmanifest_*.acf (one it can't read is skipped, with a
warning). For every installed app it finds the openvr_api.dll (or libopenvr_api.so) files under its folder, with their
file version and machine (x86, x64), and notes an action manifest (actions.json, or any *.json under 256 KiB with
"actions" and "default_bindings") and any *.vrmanifest. It looks at most MAX_DEPTH folder levels below an app's
folder and at MAX_FILES files per app, and follows no symbolic links or junctions. Apps with none of these are left
out, and so is SteamVR itself.

--self-test checks all that against the fixtures in data/list_games/.
"""

import argparse
import ctypes
import json
import os
import re
import shutil
import stat
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
MAX_JSON_BYTES = 256 * 1024  # action manifests are small; skips game data files
MAX_DEPTH = 8  # folder levels below an app's folder (Unreal keeps openvr_api.dll 6 down)
MAX_FILES = 20000  # per app
OPENVR_NAMES = {"openvr_api.dll", "libopenvr_api.so"}
STEAMVR_APPID = "250820"
VDF_ESCAPES = {"n": "\n", "t": "\t", "\\": "\\", '"': '"'}


def vdf_unescape(text):
    """KeyValues' escapes: \\n, \\t, \\\\ and \\" (any other backslash stays as it is)"""
    return re.sub(r"\\(.)", lambda m: VDF_ESCAPES.get(m.group(1), m.group(0)), text)


def parse_vdf(text):
    """Valve's KeyValues text format: "key" "value" and "key" { ... }, // comments. Returns nested dicts, with the
    keys in lower case (Steam's own files have had "LibraryFolders" as well as "libraryfolders")."""
    stack = [{}]
    key = None
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"|([{}])|//[^\n]*', text):
        if m.group(2) == "{":
            child = {}
            stack[-1][key if key is not None else ""] = child
            stack.append(child)
            key = None
        elif m.group(2) == "}":
            if len(stack) > 1:
                stack.pop()
            key = None
        elif m.group(1) is not None:  # a quoted string, maybe empty; comments are neither
            value = vdf_unescape(m.group(1))
            if key is None:
                key = value.lower()
            else:
                stack[-1][key] = value
                key = None
    return stack[0]


def folder_key(path):
    """A library folder as compared for duplicates: resolved, and case-blind on Windows"""
    try:
        resolved = Path(path).resolve()
    except OSError:
        resolved = Path(os.path.abspath(path))
    return os.path.normcase(str(resolved))


def libraries(steam):
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    found, seen = [steam], {folder_key(steam)}
    if vdf.is_file():
        data = parse_vdf(vdf.read_text(encoding="utf-8", errors="replace"))
        for entry in data.get("libraryfolders", {}).values():
            if isinstance(entry, dict) and entry.get("path"):
                path = Path(entry["path"])
                if folder_key(path) not in seen:
                    seen.add(folder_key(path))
                    found.append(path)
    return [p for p in found if (p / "steamapps").is_dir()]


def is_link(path):
    """A symbolic link, or on Windows a folder that is a junction (any reparse point): not followed. A file that is a
    reparse point but no link (a cloud placeholder, a deduplicated file) is read as any other."""
    try:
        st = os.lstat(path)
    except OSError:
        return True
    if stat.S_ISLNK(st.st_mode):
        return True
    reparse = bool(getattr(st, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    return reparse and stat.S_ISDIR(st.st_mode)


def pe_machine(path):
    """x86 or x64 from a PE file's header (or ELF's class on Linux)"""
    try:
        with open(path, "rb") as f:
            head = f.read(4096)
    except OSError:
        return "?"
    if head[:4] == b"\x7fELF":
        return "x64" if head[4] == 2 else "x86"
    if head[:2] != b"MZ" or len(head) < 0x40:
        return "?"
    offset = struct.unpack_from("<I", head, 0x3C)[0]
    if offset + 6 > len(head) or head[offset:offset + 4] != b"PE\0\0":
        return "?"
    machine = struct.unpack_from("<H", head, offset + 4)[0]
    return {0x14C: "x86", 0x8664: "x64", 0xAA64: "arm64"}.get(machine, hex(machine))


def file_version(path):
    """The file version from a Windows version resource (Windows only), else empty"""
    if os.name != "nt":
        return ""
    version = ctypes.windll.version
    size = version.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return ""
    buf = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, buf):
        return ""
    info = ctypes.c_void_p()
    length = ctypes.c_uint()
    if not version.VerQueryValueW(buf, "\\", ctypes.byref(info), ctypes.byref(length)) or length.value < 52:
        return ""
    fixed = ctypes.string_at(info.value, 52)
    ms, ls = struct.unpack_from("<II", fixed, 8)
    return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"


def is_action_manifest(path):
    if path.name.lower() == "actions.json":
        return True
    try:
        if path.stat().st_size >= MAX_JSON_BYTES:
            return False
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return False
    return '"actions"' in text and '"default_bindings"' in text


def scan_app(folder):
    dlls, manifests, vrmanifests = [], [], []
    files = 0
    for dirpath, dirnames, filenames in os.walk(folder):
        depth = len(Path(dirpath).relative_to(folder).parts)
        # Deeper folders, links and junctions are left out (a junction may lead anywhere, even back up)
        dirnames[:] = [] if depth >= MAX_DEPTH else sorted(d for d in dirnames if not is_link(Path(dirpath) / d))
        filenames = sorted(filenames)
        if files + len(filenames) > MAX_FILES:
            print(f"warning: {folder} has more than {MAX_FILES} files: the rest are left out", file=sys.stderr)
            filenames = filenames[:MAX_FILES - files]
            dirnames[:] = []
        files += len(filenames)
        for name in filenames:
            lower = name.lower()
            full = Path(dirpath) / name
            if is_link(full):
                continue
            if lower in OPENVR_NAMES:
                dlls.append({"path": str(full.relative_to(folder)), "version": file_version(full), "machine": pe_machine(full)})
            elif lower.endswith(".vrmanifest"):
                vrmanifests.append(str(full.relative_to(folder)))
            elif lower.endswith(".json") and is_action_manifest(full):
                manifests.append(str(full.relative_to(folder)))
        if files >= MAX_FILES:
            break
    return dlls, manifests, vrmanifests


def list_games(steam):
    games = []
    for library in libraries(steam):
        for acf in sorted((library / "steamapps").glob("appmanifest_*.acf")):
            try:
                text = acf.read_text(encoding="utf-8", errors="replace")
            except OSError as e:
                print(f"warning: skipping {acf}: {e}", file=sys.stderr)
                continue
            state = parse_vdf(text).get("appstate", {})
            if not isinstance(state, dict) or state.get("appid") == STEAMVR_APPID:
                continue
            folder = library / "steamapps" / "common" / state.get("installdir", "")
            if not state.get("installdir") or not folder.is_dir():
                continue
            dlls, manifests, vrmanifests = scan_app(folder)
            if not dlls and not manifests and not vrmanifests:
                continue
            games.append({"appid": state.get("appid", ""), "name": state.get("name", folder.name), "library": str(library),
                          "openvr_api": dlls, "action_manifests": manifests, "vrmanifests": vrmanifests})
    games.sort(key=lambda g: g["name"].lower())
    return games


def markdown(games):
    lines = ["| Game (app id) | openvr_api.dll | Input | Graphics API | Upstream issues | Monado simulated headset | Monado worn |",
             "|---|---|---|---|---|---|---|"]
    for g in games:
        # Each version and machine once, with how many copies the game has
        counts = {}
        for d in g["openvr_api"]:
            key = f'{d["version"] or "?"} {d["machine"]}'
            counts[key] = counts.get(key, 0) + 1
        apis = "<br>".join(k + (f" ({n} copies)" if n > 1 else "") for k, n in counts.items()) or "none found"
        if g["action_manifests"]:
            input_kind = "actions"
        elif g["openvr_api"]:
            input_kind = "legacy or unknown"
        else:
            input_kind = "unknown"
        if g["vrmanifests"]:
            input_kind += " (vrmanifest)"
        name = g["name"].replace("|", "\\|")  # a | would end the table's cell
        lines.append(f'| {name} ({g["appid"]}) | {apis} | {input_kind} | unknown | | | |')
    return "\n".join(lines)


def make_junction(link, target):
    """A directory junction (Windows) or symbolic link (elsewhere) to target; False if one can't be made here"""
    try:
        if os.name == "nt":
            import _winapi
            _winapi.CreateJunction(str(target), str(link))
        else:
            os.symlink(target, link, target_is_directory=True)
        return True
    except (OSError, ImportError, AttributeError):
        return False


def self_test():
    """The fixtures (data/list_games/) copied to a temporary folder, with what git can't hold made there: the second
    library's path in the VDF, an .acf that can't be read, deep folders, many files, a large .json and a junction."""
    failures = []

    def check(what, ok):
        print(f'{"ok" if ok else "FAILED"}: {what}')
        if not ok:
            failures.append(what)

    check("VDF escapes", parse_vdf(r'"k" "a\"b\\c\td\ne\x"') == {"k": 'a"b\\c\td\ne\\x'})
    check("VDF keys in any case", parse_vdf('"LibraryFolders" { "0" { "PATH" "x" } }') == {"libraryfolders": {"0": {"path": "x"}}})

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        steam, lib2 = root / "steam", root / "lib2"
        shutil.copytree(HERE / "data" / "list_games" / "steam", steam)
        shutil.copytree(HERE / "data" / "list_games" / "lib2", lib2)
        # The second library listed twice (once spelt another way), and Steam's own folder again; the VDF escapes
        # backslashes
        vdf = steam / "steamapps" / "libraryfolders.vdf"
        escaped = lambda p: str(p).replace("\\", "\\\\")
        vdf.write_text(vdf.read_text(encoding="utf-8").replace("%LIB2%", escaped(lib2))
                       .replace("%LIB2_AGAIN%", escaped(lib2 / "steamapps" / "..")).replace("%STEAM%", escaped(steam)),
                       encoding="utf-8")
        # An .acf that can't be read (a folder of that name)
        (lib2 / "steamapps" / "appmanifest_999.acf").mkdir()
        common = steam / "steamapps" / "common"
        # Deep (102): an openvr_api.dll at the deepest level looked at, and one a level below it
        within = common / "Deep" / Path(*["d"] * MAX_DEPTH)
        beyond = within / "d"
        beyond.mkdir(parents=True)
        (within / "openvr_api.dll").write_bytes(b"MZ")
        (beyond / "libopenvr_api.so").write_bytes(b"\x7fELF\x02")
        # Unreal (107): its openvr_api.dll where UE4 keeps it, 6 levels down
        unreal = common / "Unreal" / "Engine" / "Binaries" / "ThirdParty" / "OpenVR" / "OpenVRv1_5_17" / "Win64"
        unreal.mkdir(parents=True)
        (unreal / "openvr_api.dll").write_bytes(b"MZ")
        # Many files (103): its own actions.json, then more files than are looked at, then an openvr_api.dll
        many = common / "Many"
        (many / "a").mkdir(parents=True)
        (many / "actions.json").write_text("{}", encoding="utf-8")
        for i in range(MAX_FILES + 1):
            (many / "a" / f"f{i}.txt").touch()
        (many / "z").mkdir()
        (many / "z" / "openvr_api.dll").write_bytes(b"MZ")
        # Big (104): a .json of 256 KiB with the manifest's keys isn't read, a small one is
        big = common / "Big"
        big.mkdir()
        (big / "data.json").write_text('{"actions": [], "default_bindings": []}'.ljust(MAX_JSON_BYTES), encoding="utf-8")
        (big / "small.json").write_text('{"actions": [], "default_bindings": []}', encoding="utf-8")
        # Linked (105): a junction (or symbolic link) to a folder with an openvr_api.dll, not followed
        outside = root / "outside"
        outside.mkdir()
        (outside / "openvr_api.dll").write_bytes(b"MZ")
        (common / "Linked").mkdir()
        linked = make_junction(common / "Linked" / "link", outside)

        found = list_games(steam)
        games = {g["appid"]: g for g in found}
        check("both libraries, each once", sorted((g["appid"], g["library"]) for g in found if g["appid"] in ("101", "201"))
              == [("101", str(steam)), ("201", str(lib2))])
        check("an unreadable .acf skipped, the others read", "201" in games)
        check("SteamVR left out", STEAMVR_APPID not in games)
        check(f"openvr_api {MAX_DEPTH} levels down found, deeper not",
              [Path(d["path"]).name for d in games.get("102", {}).get("openvr_api", [])] == ["openvr_api.dll"])
        check("Unreal's openvr_api.dll found",
              [Path(d["path"]).as_posix() for d in games.get("107", {}).get("openvr_api", [])]
              == ["Engine/Binaries/ThirdParty/OpenVR/OpenVRv1_5_17/Win64/openvr_api.dll"])
        check(f"at most {MAX_FILES} files per app", games.get("103", {}).get("openvr_api") == []
              and games.get("103", {}).get("action_manifests") == ["actions.json"])
        check("a .json of 256 KiB not read", games.get("104", {}).get("action_manifests") == ["small.json"])
        if linked:
            check("no junction followed", "105" not in games)
        else:
            print("skipped: no junction or link could be made here")
        check("a | in a name escaped", "| Pipe \\| Game (106) |" in markdown(found))
        check("escaped quotes in a name", games.get("101", {}).get("name") == 'Quote "Game"')
    print(f"{len(failures)} failed")
    return 1 if failures else 0


def main():
    sys.stdout.reconfigure(encoding="utf-8")  # a redirected stdout is in the locale's code page on Windows
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--steam", help="Steam's folder (the one with steamapps)")
    out = ap.add_mutually_exclusive_group()
    out.add_argument("--markdown", action="store_true")
    out.add_argument("--json", action="store_true")
    out.add_argument("--self-test", action="store_true", help="check the fixtures in data/list_games/")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.steam:
        ap.error("--steam is required")
    games = list_games(Path(args.steam))
    if args.json:
        print(json.dumps(games, indent=1))
    else:
        print(markdown(games))
    return 0


if __name__ == "__main__":
    sys.exit(main())
