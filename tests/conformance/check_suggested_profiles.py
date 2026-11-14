#!/usr/bin/env python3
"""Checks that inputEvaluator=steamvr suggests bindings for the same interaction
profiles as direct. A profile the game's bindings give nothing must not become one the runtime may pick because of
OpenComposite's own raw inputs (direct leaves such a profile out, so the runtime doesn't choose khr/simple over a
controller the game has bindings for).

  check_suggested_profiles.py <dump under direct> <dump under steamvr>
  check_suggested_profiles.py --vector-shapes <dump>

The dumps are the stand-in runtime's (run.py writes raw/case-<case>.dump.json). Exit code 0 if the profiles match.

--vector-shapes checks one dump's suggestions instead: a vector2 action (vector2 and vector3 ones in OpenVR) is
suggested only onto a two-axis component (a thumbstick, trackpad or joystick), never onto a float or boolean one,
which a runtime may answer by refusing all of that profile's suggestions.
"""

import json
import sys
from pathlib import Path


def profiles(path):
    dump = json.loads(Path(path).read_text(encoding="utf-8"))
    return {profile for profile, paths in dump.get("suggested_bindings", {}).items() if paths}


XR_ACTION_TYPE_VECTOR2F_INPUT = 3
TWO_AXIS = {"thumbstick", "trackpad", "joystick"}


def vector_shapes(path):
    dump = json.loads(Path(path).read_text(encoding="utf-8"))
    bad, vectors = [], 0
    for profile, entries in sorted(dump.get("suggested_actions", {}).items()):
        for e in entries:
            if e.get("type") != XR_ACTION_TYPE_VECTOR2F_INPUT:
                continue
            vectors += 1
            if e["path"].rsplit("/", 1)[-1] not in TWO_AXIS:
                bad.append(f"{profile}: {e['action']} on {e['path']}")
    for b in bad:
        print(f"vector action on a component of another shape: {b}")
    print(f"{vectors} vector suggestions, {len(bad)} onto another shape")
    if not vectors:
        print("no vector suggestions: nothing checked")
        return 1
    return 1 if bad else 0


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--vector-shapes":
        return vector_shapes(sys.argv[2])
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
