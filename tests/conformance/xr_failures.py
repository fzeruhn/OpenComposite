#!/usr/bin/env python3
"""Runs OpenLamina against the stand-in OpenXR runtime with each failure script (scripts/<name>.json) and checks
what expect/xr_failures.json says it should do: start-up errors, dropped frames, session loss, failed swapchains
and binding suggestions. Nothing may end or crash in any of them.

  xr_failures.py --runtime build-tests/bin/vrclient_x64.dll --conformance build-tests/tests/oc_conformance.exe --out <dir>
                 [--only <script>] [--ci]
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
VREVENT_QUIT = 700


def records_of(out):
    return json.loads((out / "results.json").read_text(encoding="utf-8"))["records"]


def check(name, want, out):
    """Returns the list of what didn't match for one script's run in out."""
    misses = []
    records = records_of(out)
    for r in records:
        if r.get("status") != "ok":
            misses.append(f"{r.get('case')}/{r.get('method')}: {r.get('status')} (exit {r.get('exit_code')})")
    by_method = {(r.get("case"), r.get("method")): r for r in records}

    for method, expected in want.get("init_error", {}).items():
        r = by_method.get(("init-matrix", method))
        got = r.get("init_error") if r else None
        if got != expected:
            misses.append(f"init-matrix/{method} init_error: got {got}, expected {expected}")
    if "hmd_present" in want:
        r = by_method.get(("init-matrix", "BIsHmdPresent"))
        got = r.get("ret") if r else None
        if got != want["hmd_present"]:
            misses.append(f"BIsHmdPresent: got {got}, expected {want['hmd_present']}")

    frames = [r for r in records if r.get("case") == "frame-loop" and str(r.get("method", "")).startswith("frame ")]
    if "frames" in want and len(frames) != want["frames"]:
        misses.append(f"frame-loop: {len(frames)} frames recorded, expected {want['frames']}")
    # Every frame-loop run: no swapchain image left acquired, and the call order never broken
    dump_path = out / "raw" / "case-frame-loop.dump.json"
    if dump_path.exists():
        dump = json.loads(dump_path.read_text(encoding="utf-8"))
        if dump.get("images_left_acquired", 0):
            misses.append(f"frame-loop: {dump['images_left_acquired']} swapchain images still acquired when their swapchain went")
        if dump.get("swapchain_call_order_errors"):
            misses.append(f"frame-loop: swapchain calls out of order: {dump['swapchain_call_order_errors']}")
    if "frames_ended_min" in want:
        dump_path = out / "raw" / "case-frame-loop.dump.json"
        dump = json.loads(dump_path.read_text(encoding="utf-8")) if dump_path.exists() else {}
        ended = dump.get("frames_ended", -1)
        if ended < want["frames_ended_min"]:
            misses.append(f"frame-loop: the runtime saw {ended} frames ended, expected at least {want['frames_ended_min']}")
    if "quit_events" in want:
        quits = [int(r["method"].split()[1]) for r in frames for e in r.get("events", []) if e == VREVENT_QUIT]
        if len(quits) != want["quit_events"]:
            misses.append(f"frame-loop: {len(quits)} VREvent_Quit polled (frames {quits}), expected {want['quit_events']}")
        elif "quit_from_frame" in want and quits and quits[0] < want["quit_from_frame"]:
            misses.append(f"frame-loop: VREvent_Quit polled at frame {quits[0]}, expected from frame {want['quit_from_frame']}")
    if "old_event_quits" in want:
        # IVRSystem_009's events (case old-events): the quit arrives, and nothing is written past the 32-byte event
        olds = [r for r in records if r.get("case") == "old-events" and str(r.get("method", "")).startswith("frame ")]
        quits = sum(1 for r in olds for e in r.get("events", []) if e == VREVENT_QUIT)
        overrun = sum(r.get("overrun", 0) for r in olds)
        if len(olds) != 120:
            misses.append(f"old-events: {len(olds)} frames recorded, expected 120")
        if quits != want["old_event_quits"]:
            misses.append(f"old-events: {quits} VREvent_Quit polled, expected {want['old_event_quits']}")
        if overrun:
            misses.append(f"old-events: {overrun} bytes written past the 32-byte event")
    if "failed_submits" in want:
        # The Submit that first makes the swapchain gets the failure (frame 1's left eye restarts the session for
        # the game's graphics API, so that's the right eye); every other Submit works
        answers = [r.get(eye) for r in frames for eye in ("left", "right")]
        failed = want["failed_submits"]
        got = answers.count(failed["answer"])
        if got != failed["count"]:
            misses.append(f"frame-loop: {got} Submits answered {failed['answer']}, expected {failed['count']}")
        others = [a for a in answers if a not in (failed["answer"], want.get("other_submits", 0))]
        if others:
            misses.append(f"frame-loop: Submits also answered {sorted(set(others))}")
    if want.get("manifest_handles"):
        for method in ("GetActionSetHandle", "GetActionHandle trigger", "GetActionHandle pose"):
            r = by_method.get(("input-manifest", method))
            if not r or r.get("ret") != 0:
                misses.append(f"input-manifest/{method}: got {r.get('ret') if r else 'no record'}, expected 0")
    return misses


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runtime", required=True)
    ap.add_argument("--conformance", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only")
    ap.add_argument("--ci", action="store_true", help="passed to run.py (CI runners are elevated)")
    args = ap.parse_args()
    expect = json.loads((HERE / "expect" / "xr_failures.json").read_text(encoding="utf-8"))
    failed = 0
    for name, want in expect.items():
        if name.startswith("#") or (args.only and name != args.only):
            continue
        out = Path(args.out) / name
        cmd = [sys.executable, "-I", str(HERE / "run.py"), "--runtime", args.runtime, "--conformance", args.conformance,
               "--xr-runtime", "stand-in", "--script", str(HERE / "scripts" / f"{name}.json"),
               "--cases", ",".join(want["cases"]), "--allow-failures", "--jobs", "2", "--out", str(out),
               "--manifest", str(HERE.parent / "unit" / "data" / "manifest_basic.json")] + (["--ci"] if args.ci else [])
        subprocess.run(cmd, capture_output=True, text=True)
        misses = check(name, want, out)
        print(f"{name}: {'ok' if not misses else 'FAILED'}")
        for m in misses:
            print(f"  {m}")
        failed += bool(misses)
    print(f"{failed} of the scripts didn't do what expect/xr_failures.json says")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
