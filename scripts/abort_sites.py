#!/usr/bin/env python3
"""The places where OpenComposite can end a program (hard stops), and the check that no new one comes in.

  abort_sites.py --list               write tests/abort_sites.csv: file, line, function, macro, message, return_type
  abort_sites.py --write-allowlist    write scripts/fatal_sites.txt from what's there now (reason "to convert")
  abort_sites.py --check              exit 1 if a hard stop isn't covered by scripts/fatal_sites.txt
  abort_sites.py --self-test

fatal_sites.txt: one line per function and macro, "file|function|macro|count|reason". A function may have up to count
uses of that macro; a new one, or one in a function not listed, fails the check.
"""

import argparse
import re
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIRS = ["OpenOVR", "OCOVR", "DrvOpenXR"]
MACROS = ["STUBBED", "OOVR_ABORT_T", "OOVR_ABORTF", "OOVR_ABORT", "OOVR_FAILED_XR_ABORT", "OOVR_FALSE_ABORT",
          "OOVR_FAILED_DX_ABORT", "OOVR_FAILED_VK_ABORT", "ERR", "LINUX_STUBBED", "XR_STUBBED", "OC_FATAL",
          # Config.cpp's ABORT (exit()), and DX11Compositor::ThrowIfFailed and `throw` (an uncaught exception ends it)
          "ABORT", "ThrowIfFailed"]
# The C++ library's and Windows' own, also as std:: or :: (a message box stalls a game whose window is in the headset)
CALLS = ["exit", "_Exit", "abort", "terminate", "ExitProcess", "TerminateProcess", "MessageBoxA", "MessageBoxW"]
USE = re.compile(r"(?<![\w#.:>])(" + "|".join(MACROS) + r")\s*\(|(?<![\w#.:>])(?:std::|::)?(" + "|".join(CALLS) +
                 r")\s*\(|(?<![\w#])(throw)\b")
FUNC = re.compile(r"^(?!\s)(?!(?:if|for|while|switch|return|else|do|case|#|//|typedef|namespace|class|struct|enum)\b)"
                  r".*?\b((?:\w+::)*~?\w+)\s*\([^;]*$")
SKIP_FILES = {"OpenOVR/logging.h", "OpenOVR/Misc/SoftFail.h", "OpenOVR/BaseCommon.h", "OpenOVR/Misc/xrutil.h"}


def scan(root):
    sites = []
    for d in DIRS:
        base = root / d
        if not base.exists():
            continue
        for path in sorted(list(base.rglob("*.cpp")) + list(base.rglob("*.h"))):
            rel = path.relative_to(root).as_posix()
            if rel in SKIP_FILES:
                continue
            func, ret = "<file>", ""
            in_block = False
            for n, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                stripped = line.strip()
                if in_block:
                    if "*/" in stripped:
                        in_block = False
                    continue
                if stripped.startswith("/*") and "*/" not in stripped:
                    in_block = True
                    continue
                if stripped.startswith("//") or stripped.startswith("#define") or stripped.startswith("*"):
                    continue
                m = FUNC.match(line)
                if m:
                    func = m.group(1)
                    ret = " ".join(line[:m.start(1)].split())
                code = line.split("//", 1)[0]
                for use in USE.finditer(code):
                    macro = use.group(1) or use.group(2) or use.group(3)
                    if macro == "ThrowIfFailed" and "::ThrowIfFailed" in code:
                        continue  # its definition, not a use
                    message = code[use.end():].strip().rstrip(";").rstrip(")")[:100]
                    sites.append((rel, n, func, macro, message, ret))
    return sites


def read_allowlist(path):
    allowed = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) >= 4:
                allowed[(parts[0], parts[1], parts[2])] = int(parts[3])
    return allowed


def counts(sites):
    return Counter((s[0], s[2], s[3]) for s in sites)


def check(root, allowlist):
    allowed = read_allowlist(allowlist)
    bad = []
    for key, n in sorted(counts(scan(root)).items()):
        if n > allowed.get(key, 0):
            bad.append(f"{key[0]}: {n} x {key[2]} in {key[1]} (allowed {allowed.get(key, 0)})")
    for b in bad:
        print("not allowed:", b)
    print(f"{sum(counts(scan(root)).values())} hard stops, {len(bad)} not allowed")
    return 1 if bad else 0


def write_allowlist(root, path):
    lines = ["# Hard stops that may stay, checked by scripts/abort_sites.py --check: file|function|macro|count|reason"]
    for (f, func, macro), n in sorted(counts(scan(root)).items()):
        lines.append(f"{f}|{func}|{macro}|{n}|to convert")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "OpenOVR").mkdir()
        (root / "OpenOVR" / "a.cpp").write_text(
            "int Foo::Bar(int x)\n{\n\tSTUBBED();\n}\n// OOVR_ABORT(\"comment\");\nvoid Baz()\n{\n"
            "\tOOVR_ABORTF(\"x %d\", 1);\n\tOOVR_SOFT_ABORT(\"soft\");\n\tstd::terminate();\n\tapp->exit(0);\n}\n")
        sites = scan(root)
        got = [(s[2], s[3]) for s in sites]
        want = [("Foo::Bar", "STUBBED"), ("Baz", "OOVR_ABORTF"), ("Baz", "terminate")]
        if got != want:
            print("self-test FAILED: got", got, "want", want)
            return 1
        allow = root / "fatal.txt"
        write_allowlist(root, allow)
        if check(root, allow) != 0:
            print("self-test FAILED: a fresh allowlist doesn't pass")
            return 1
        (root / "OpenOVR" / "a.cpp").write_text((root / "OpenOVR" / "a.cpp").read_text() + "void New()\n{\n\tERR(\"x\");\n}\n")
        if check(root, allow) != 1:
            print("self-test FAILED: a new hard stop passes")
            return 1
    print("self-test passed")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--write-allowlist", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    allowlist = ROOT / "scripts" / "fatal_sites.txt"
    if args.self_test:
        return self_test()
    if args.list:
        out = ROOT / "tests" / "abort_sites.csv"
        rows = ["file,line,function,macro,message,return_type"]
        for f, n, func, macro, msg, ret in scan(ROOT):
            rows.append(",".join([f, str(n), func, macro, '"' + msg.replace('"', "'") + '"', '"' + ret + '"']))
        out.write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {out}: {len(rows) - 1} hard stops")
    if args.write_allowlist:
        write_allowlist(ROOT, allowlist)
        print(f"wrote {allowlist}")
    if args.check:
        return check(ROOT, allowlist)
    return 0


if __name__ == "__main__":
    sys.exit(main())
