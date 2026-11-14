#!/usr/bin/env python3
"""Checks that converting bindings to other controllers only adds bindings: every binding the stand-in was
suggested without the conversion (OC_NO_BINDING_CONVERSION=1) it is suggested with it too.

  check_bindings_superset.py <dump without conversion> <dump with conversion>

The dumps are the stand-in runtime's (run.py writes raw/case-<case>.dump.json). Exit code 0 if nothing was lost.
"""

import json
import sys
from pathlib import Path


def bindings(path):
    dump = json.loads(Path(path).read_text(encoding="utf-8"))
    return {(profile, binding) for profile, paths in dump.get("suggested_bindings", {}).items() for binding in paths}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    without, with_conversion = bindings(sys.argv[1]), bindings(sys.argv[2])
    if not without:
        print("no suggested bindings in the dump without the conversion: nothing to compare")
        return 1
    lost = sorted(without - with_conversion)
    added = sorted(with_conversion - without)
    for profile, binding in lost:
        print(f"lost: {profile} {binding}")
    for profile, binding in added:
        print(f"added: {profile} {binding}")
    print(f"{len(without)} bindings without the conversion, {len(with_conversion)} with it; {len(lost)} lost, {len(added)} added")
    return 1 if lost else 0


if __name__ == "__main__":
    sys.exit(main())
