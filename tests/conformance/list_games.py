#!/usr/bin/env python3
"""Lists the installed Steam games that use OpenVR, for the test list.

  list_games.py --steam <Steam folder> [--markdown | --json]

Reads steamapps/libraryfolders.vdf and each library's appmanifest_*.acf. For every installed app it finds the
openvr_api.dll (or libopenvr_api.so) files under its folder, with their file version and machine (x86, x64), and
notes an action manifest (actions.json, or any small *.json with "actions" and "default_bindings") and any
*.vrmanifest. Apps with none of these are left out.
"""

import argparse
import ctypes
import json
import os
import re
import struct
import sys
from pathlib import Path

MAX_JSON_BYTES = 2 * 1024 * 1024  # action manifests are small; skips game data files
OPENVR_NAMES = {"openvr_api.dll", "libopenvr_api.so"}


def parse_vdf(text):
    """Valve's KeyValues text format: "key" "value" and "key" { ... }, // comments. Returns nested dicts."""
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
            value = m.group(1).replace("\\\\", "\\")
            if key is None:
                key = value
            else:
                stack[-1][key] = value
                key = None
    return stack[0]


def libraries(steam):
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    found = [steam]
    if vdf.is_file():
        data = parse_vdf(vdf.read_text(encoding="utf-8", errors="replace"))
        for entry in data.get("libraryfolders", {}).values():
            if isinstance(entry, dict) and entry.get("path"):
                path = Path(entry["path"])
                if path not in found:
                    found.append(path)
    return [p for p in found if (p / "steamapps").is_dir()]


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
        if path.stat().st_size > MAX_JSON_BYTES:
            return False
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return False
    return '"actions"' in text and '"default_bindings"' in text


def scan_app(folder):
    dlls, manifests, vrmanifests = [], [], []
    for dirpath, dirnames, filenames in os.walk(folder):
        for name in filenames:
            lower = name.lower()
            full = Path(dirpath) / name
            if lower in OPENVR_NAMES:
                dlls.append({"path": str(full.relative_to(folder)), "version": file_version(full), "machine": pe_machine(full)})
            elif lower.endswith(".vrmanifest"):
                vrmanifests.append(str(full.relative_to(folder)))
            elif lower.endswith(".json") and is_action_manifest(full):
                manifests.append(str(full.relative_to(folder)))
    return dlls, manifests, vrmanifests


def list_games(steam):
    games = []
    for library in libraries(steam):
        for acf in sorted((library / "steamapps").glob("appmanifest_*.acf")):
            state = parse_vdf(acf.read_text(encoding="utf-8", errors="replace")).get("AppState", {})
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
        lines.append(f'| {g["name"]} ({g["appid"]}) | {apis} | {input_kind} | unknown | | | |')
    return "\n".join(lines)


def main():
    sys.stdout.reconfigure(encoding="utf-8")  # a redirected stdout is in the locale's code page on Windows
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--steam", required=True, help="Steam's folder (the one with steamapps)")
    out = ap.add_mutually_exclusive_group()
    out.add_argument("--markdown", action="store_true")
    out.add_argument("--json", action="store_true")
    args = ap.parse_args()
    games = list_games(Path(args.steam))
    if args.json:
        print(json.dumps(games, indent=1))
    else:
        print(markdown(games))
    return 0


if __name__ == "__main__":
    sys.exit(main())
