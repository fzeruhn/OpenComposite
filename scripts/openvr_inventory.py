#!/usr/bin/env python3
"""Inventory of the OpenVR interface versions Valve has shipped, against the ones OpenLamina offers.

  openvr_inventory.py --valve-repo <clone of github.com/ValveSoftware/openvr> --write tests/conformance/interfaces.txt
  openvr_inventory.py --check        # every listed version is offered (a GEN_INTERFACE line) or in not_implemented.txt
  openvr_inventory.py --self-test

A blobless clone is enough: git clone --filter=blob:none --no-checkout https://github.com/ValveSoftware/openvr.git
Every tag's headers/openvr.h gives the app-facing versions ("app"); a version found only in headers/openvr_capi.h is
reachable only through the C API ("capi"); one found only in headers/openvr_driver.h is a driver interface and left out.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

VERSION_RE = re.compile(r'"(IVR[A-Za-z]+_[0-9]{3})"')
GEN_RE = re.compile(r'^\s*GEN_INTERFACE\("(\w+)",\s*"(\d{3})"', re.M)
ROOT = Path(__file__).resolve().parent.parent


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace")


def show(repo, tag, path):
    r = git(repo, "show", f"{tag}:{path}")
    return r.stdout if r.returncode == 0 else ""


def tags_in_order(repo):
    tags = [t for t in git(repo, "tag").stdout.split() if t]
    # Order by the date of the tagged commit
    dated = []
    for t in tags:
        ct = git(repo, "log", "-1", "--format=%ct", t).stdout.strip()
        dated.append((int(ct or 0), t))
    return [t for _, t in sorted(dated)]


def inventory(repo):
    """Returns ({version: (kind, first tag, last tag)}, head commit, head date)."""
    found = {}  # version -> [kinds set, first, last]
    for tag in tags_in_order(repo):
        app = set(VERSION_RE.findall(show(repo, tag, "headers/openvr.h")))
        capi = set(VERSION_RE.findall(show(repo, tag, "headers/openvr_capi.h"))) - app
        driver = set(VERSION_RE.findall(show(repo, tag, "headers/openvr_driver.h")))
        for kind, versions in (("app", app), ("capi", capi - driver)):
            for v in versions:
                entry = found.setdefault(v, [set(), tag, tag])
                entry[0].add(kind)
                entry[2] = tag
    result = {}
    for v, (kinds, first, last) in found.items():
        result[v] = ("app" if "app" in kinds else "capi", first, last)
    head = git(repo, "log", "-1", "--format=%H %cs", "HEAD").stdout.split()
    return result, (head[0] if head else "?"), (head[1] if len(head) > 1 else "?")


def version_key(v):
    name, num = v.rsplit("_", 1)
    return name, int(num)


def format_inventory(inv):
    return [f"{v} {kind} {first} {last}" for v, (kind, first, last) in sorted(inv.items(), key=lambda i: version_key(i[0]))]


def write(repo, out):
    inv, commit, date = inventory(repo)
    lines = [f"# OpenVR interface versions Valve has shipped, read from github.com/ValveSoftware/openvr at {commit} ({date})",
             "# by scripts/openvr_inventory.py. Columns: version, app (headers/openvr.h) or capi (C API only), first tag,",
             "# last tag."]
    lines += format_inventory(inv)
    Path(out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return inv


def read_listed(path):
    listed = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        listed[parts[0]] = parts[1] if len(parts) > 1 else ""
    return listed


def offered(root):
    versions = set()
    for cvr in (root / "OpenOVR" / "Reimpl").glob("CVR*.cpp"):
        for name, num in GEN_RE.findall(cvr.read_text(encoding="utf-8", errors="replace")):
            versions.add(f"IVR{name}_{num}")
    return versions


def check(root):
    listed = read_listed(root / "tests" / "conformance" / "interfaces.txt")
    skipped = read_listed(root / "tests" / "conformance" / "not_implemented.txt")
    have = offered(root)
    missing = [v for v in sorted(listed, key=version_key) if v not in have and v not in skipped]
    for v in missing:
        print(f"not offered: {v} ({listed[v]})")
    print(f"{len(listed)} listed, {len(listed) - len(missing)} offered or set aside, {len(missing)} missing")
    return 1 if missing else 0


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        repo = Path(tmp)
        git(repo, "init", "-q")
        git(repo, "config", "user.email", "test@example.invalid")
        git(repo, "config", "user.name", "test")
        headers = repo / "headers"
        headers.mkdir()

        def commit(tag, openvr, capi, driver, when):
            (headers / "openvr.h").write_text(openvr)
            (headers / "openvr_capi.h").write_text(capi)
            (headers / "openvr_driver.h").write_text(driver)
            git(repo, "add", "-A")
            env_date = f"{when} +0000"
            subprocess.run(["git", "-C", str(repo), "commit", "-q", "-m", tag],
                           env={**__import__("os").environ, "GIT_COMMITTER_DATE": env_date, "GIT_AUTHOR_DATE": env_date},
                           check=True)
            git(repo, "tag", tag)

        commit("t1", '"IVRSystem_001" "IVRFoo_001"', "", '"IVRDriverThing_001"', "2020-01-01T00:00:00")
        commit("t2", '"IVRSystem_002"', '"IVRPaths_001" "IVRSystem_002"', '"IVRServerDriverHost_005"',
               "2021-01-01T00:00:00")
        inv, _, _ = inventory(repo)
        got = format_inventory(inv)
        want = ["IVRFoo_001 app t1 t1", "IVRPaths_001 capi t2 t2", "IVRSystem_001 app t1 t1", "IVRSystem_002 app t2 t2"]
        if got != want:
            print("self-test FAILED")
            print("  got: ", got)
            print("  want:", want)
            return 1
        print("self-test passed")
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--valve-repo")
    ap.add_argument("--write")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.write:
        if not args.valve_repo:
            ap.error("--write needs --valve-repo")
        inv = write(args.valve_repo, args.write)
        kinds = [k for k, _, _ in inv.values()]
        print(f"wrote {args.write}: {kinds.count('app')} app, {kinds.count('capi')} capi")
    if args.check:
        return check(ROOT)
    if not (args.write or args.check):
        ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
