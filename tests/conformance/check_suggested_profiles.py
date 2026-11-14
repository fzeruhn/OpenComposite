#!/usr/bin/env python3
"""Checks that inputEvaluator=steamvr suggests bindings for the same interaction
profiles as direct. A profile the game's bindings give nothing must not become one the runtime may pick because of
OpenLamina's own raw inputs (direct leaves such a profile out, so the runtime doesn't choose khr/simple over a
controller the game has bindings for).

  check_suggested_profiles.py <dump under direct> <dump under steamvr>

The dumps are the stand-in runtime's (run.py writes raw/case-<case>.dump.json). Exit code 0 if the profiles match.
"""

import json
import sys
from pathlib import Path


def profiles(path):
    dump = json.loads(Path(path).read_text(encoding="utf-8"))
    return {profile for profile, paths in dump.get("suggested_bindings", {}).items() if paths}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    direct, steamvr = profiles(sys.argv[1]), profiles(sys.argv[2])
    if not direct:
        print("no suggested bindings under direct: nothing to compare")
        return 1
    for p in sorted(direct - steamvr):
        print(f"only under direct: {p}")
    for p in sorted(steamvr - direct):
        print(f"only under steamvr: {p}")
    print(f"{len(direct)} profiles under direct, {len(steamvr)} under steamvr")
    return 0 if direct == steamvr else 1


if __name__ == "__main__":
    sys.exit(main())
