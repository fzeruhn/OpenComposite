# OpenVR conformance tests

`oc_conformance` loads a `vrclient` DLL, OpenComposite's or SteamVR's, and calls every method of every OpenVR
interface version Valve has shipped (`interfaces.txt`), with four argument sets (valid, edge, null, invalid), under
each application type. It also runs named cases that need real handles or frames (`cases.cpp`). `run.py` runs each
interface version, application type and argument set, and each case, in a child process, and collects what every call
returned and wrote.

Built with the tests (`-DOC_BUILD_TESTS=ON`, or `scripts\ci\build-windows.ps1 -Tests`).

## Running it

```
python tests/conformance/run.py --runtime build/bin/vrclient_x64.dll --conformance build/tests/oc_conformance.exe --xr-runtime stand-in --out out
```

- `--xr-runtime stand-in` runs OpenComposite on `oc_test_runtime` (`tests/runtime`), which needs no headset and can
  be scripted with `--script` (examples in `scripts/`). `none` gives it no OpenXR runtime at all. A manifest path
  uses that runtime (Monado's, say).
- `--only <regex>` limits the interface versions, `--cases a,b` runs only those cases.
- `--expected expected/steamvr-<version>.json --differences differences.json` compares every answer with SteamVR's
  recorded ones (`record-steamvr.ps1`); `--expect <file>` checks exact values for chosen records (`expect/`).
- Each child gets `OC_NO_MESSAGE_BOX=1` (OpenComposite ending a program exits with code 70, without a box),
  `OC_LOG_DIR` and `OC_STORE_DIR` under `--out`, so a run never touches the user's own logs or state.

`out/summary.txt` has the counts per status (`ok`, `ended`: OpenComposite ended the program, `crashed`, `timeout`) and
every record that wasn't ok; `out/results.json` has every record. When a child ends early the runner starts it again
after the call that ended it, so one bad call doesn't hide the rest. When a child crashes, `crash_report.cpp` writes
why (the exception, abort or invalid CRT call) and its stack next to its output, and the runner puts that on the
record (`crash`) and its first line in the summary. A fail-fast inside the DLL (its own CRT's `abort` or `terminate`)
bypasses it: such a crash has no report.

## The SteamVR reference

`record-steamvr.ps1 -Conformance <oc_conformance.exe> -Out <dir>` runs everything against SteamVR with its null
driver (no headset needed) and writes `expected/steamvr-<version>.json`. It backs up the OpenVR paths file and
SteamVR's settings, room setup and app config first, puts every one back afterwards and fails unless their SHA-256s
match; `-WhatIf` only lists them. Close SteamVR, Monado and anything VR first.

`differences.json` lists the differences that are expected between SteamVR's null HMD and OpenComposite (display
geometry, timing and poses, device properties, handle numbers, the PC's own apps and settings, and the app types
OpenComposite doesn't support yet), each with its reason. Every other difference is a bug to fix or to explain there.
An entry matches `interface` and `method` (regexes) and `field`, and, if it has one, `apptype` (a regex of the app type
numbers). Records SteamVR answered while restarting between children (init errors 115, 117, 119) aren't compared.

`scripts/openvr_inventory.py --check` (run by CI) fails unless every version in `interfaces.txt` (every one Valve has
shipped, app-facing or C API only) has a `GEN_INTERFACE` line or a reason in `not_implemented.txt`.

## Failed OpenXR calls

`xr_failures.py --runtime <dll> --conformance <exe> --out <dir>` runs the stand-in runtime with each script in
`scripts/` (a failing `xrCreateInstance`, no headset, a dropped frame, a lost or exiting session, a failed swapchain,
refused binding suggestions) and checks what `expect/xr_failures.json` says OpenComposite does for each: the init
error, `VR_IsHmdPresent`, one `VREvent_Quit`, which `Submit` fails, and that nothing ends or crashes.
`exiting_old_events` polls that quit through IVRSystem_009, whose game passes SDK 0.9.x's 32-byte `VREvent_t`
(case `old-events`): nothing may be written past it. The script
format is described at the top of `tests/runtime/script.h`.

## Elevated shells

An elevated process's OpenXR loader ignores `XR_RUNTIME_JSON`. From an elevated shell `run.py` starts every child at
medium integrity (`tests/unelevated.py`). On CI, where everything is elevated, `--ci` makes the chosen runtime the
machine's active OpenXR runtime instead.

## Methods not called against SteamVR

`unsafe_methods.txt` lists methods that would start programs, write firmware or change SteamVR's own configuration;
with `--real-runtime` (SteamVR) they are recorded as skipped.
