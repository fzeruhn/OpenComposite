#!/usr/bin/env python3
"""Runs oc_conformance over every interface version and named case, each in its own child process, and collects the
records. See README.md.

  run.py --runtime <vrclient dll> --conformance <oc_conformance>
         --xr-runtime none|stand-in|<manifest>|monado:<manifest> --out <dir> [--script <json>] [--expected <json>]
         [--differences <json>] [--expect <json>] [--known-failures <json>] [--allow-skip <regex>] [--only <regex>]
         [--cases <a,b,...>] [--jobs N] [--ci] [--allow-failures] [--real-runtime] [--ini <opencomposite.ini>]

Exit code 0 only if no child was ended by OpenComposite (exit 70), crashed or timed out, and nothing differed from
--expected (beyond --differences) or --expect, except the records --known-failures lists.

A case record with "dump_join" gets the stand-in's dump joined in as "dump" (see join_dump), which --expect fields
named "dump:<path>" check.

monado:<manifest> runs the children on a Monado service that is already running (it isn't started here): the manifest
is their XR_RUNTIME_JSON, with --real-runtime too (which then only skips unsafe_methods.txt's methods). There is no
stand-in dump, so joined dumps are null.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]
import unelevated  # noqa: E402
from list_games import pe_machine  # noqa: E402

ARGSETS = ["valid", "edge", "null", "invalid"]
APPTYPES_VALID_ONLY = [2, 3, 4, 0]  # Overlay, Background, Utility, Other
CASES = ["init-matrix", "init-repeat", "init-other-thread", "lookup-before-init", "unknown-interface",
         "overlay-lifecycle", "input-manifest", "frame-loop", "submit-edges", "applications"]
TIMEOUT = 60
CASE_TIMEOUT = 600  # init-repeat starts OpenComposite 200 times
# SteamVR's answers while vrserver was starting or stopping between children (Retry, AnotherAppLaunching,
# ShuttingDown): a recording keeps them, a comparison skips them
STEAMVR_TRANSIENT = {115, 117, 119}


def load_versions():
    versions = []
    for line in (HERE / "interfaces.txt").read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts and not line.startswith("#") and len(parts) > 1 and parts[1] == "app":
            versions.append(parts[0])
    return versions


def read_jsonl(path):
    records = []
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line:
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    return records


def key_of(r):
    who = r.get("case") or r.get("interface", "?")
    return f'{who}/{r.get("method", "?")}#{r.get("index", "")}/{r.get("apptype", "-")}/{r.get("argset", "-")}'


class Runner:
    def __init__(self, args):
        self.args = args
        self.out = Path(args.out)
        # OC_TEST_NO_STEAMVR: OpenComposite finds no SteamVR or Steam config (its settings, resources and legacy
        # binding files), so a run here answers as on CI's runner, which has none
        self.env = {"OC_NO_MESSAGE_BOX": "1", "OC_LOG_DIR": str(self.out / "logs"),
                    "OC_STORE_DIR": str(self.out / "store"), "OC_TEST_NO_STEAMVR": "1"}
        self.monado = args.xr_runtime.startswith("monado:")
        if self.monado:
            self.manifest = str(Path(args.xr_runtime[len("monado:"):]).resolve())
        elif args.xr_runtime == "stand-in":
            manifest = Path(args.conformance).resolve().parent / "runtime" / "oc_test_runtime.json"
            self.manifest = str(manifest)
        elif args.xr_runtime == "none":
            # A runtime that can't load, so the loader finds none (rather than the PC's active runtime)
            none = self.out / "no_runtime.json"
            none.parent.mkdir(parents=True, exist_ok=True)
            none.write_text(json.dumps({"file_format_version": "1.0.0", "runtime": {
                "name": "no runtime", "library_path": str(self.out / "does_not_exist.dll")}}), encoding="utf-8")
            self.manifest = str(none)
        else:
            self.manifest = args.xr_runtime
        if args.real_runtime and not self.monado:
            self.manifest = None  # SteamVR: no OpenXR involved
        if self.manifest:
            self.env["XR_RUNTIME_JSON"] = self.manifest
        if args.script:
            self.env["OC_TEST_RUNTIME_SCRIPT"] = str(Path(args.script).resolve())
        self.runtime = str(Path(args.runtime).resolve())
        self.conformance = str(Path(args.conformance).resolve())

    def prepare(self):
        if self.monado and not monado_running():
            raise SystemExit("--xr-runtime monado: no Monado service is running (no monado_comp_ipc): start it first")
        if self.monado and not Path(self.manifest).exists():
            raise SystemExit(f"--xr-runtime monado: no manifest {self.manifest}")
        self.out.mkdir(parents=True, exist_ok=True)
        (self.out / "raw").mkdir(exist_ok=True)
        if self.args.ini:
            # Taken out again when the run ends (Runner.cleanup), with any file that was there put back
            target = Path(self.runtime).parent / "opencomposite.ini"
            self.ini_backup = target.read_bytes() if target.exists() else None
            shutil.copy(self.args.ini, target)
        if unelevated.integrity_above_medium() and self.manifest:
            if not self.args.ci:
                return  # children run at medium integrity (unelevated.run_medium)
            # CI's VM is elevated: the loader ignores XR_RUNTIME_JSON, so make the runtime the active one. In both
            # registry views: a 32-bit loader reads the 32-bit one (WOW6432Node)
            for view in ("/reg:64", "/reg:32"):
                subprocess.run(["reg", "add", r"HKLM\SOFTWARE\Khronos\OpenXR\1", "/v", "ActiveRuntime", "/t", "REG_SZ",
                                "/d", self.manifest, "/f", view], check=True, capture_output=True)

    def child(self, extra, raw, timeout=TIMEOUT, env_extra=None):
        cmd = [self.conformance, "--runtime", self.runtime, "--out", str(raw)] + extra
        if self.args.real_runtime:
            cmd.append("--real-runtime")
        env = dict(os.environ)
        env.update(self.env)
        env.update(env_extra or {})
        if unelevated.integrity_above_medium() and not self.args.ci:
            return unelevated.run_medium(cmd, env, None, timeout)
        # In a memory-limited job (unelevated.py): a runaway child fails alone
        return unelevated.run_limited(cmd, env, None, timeout, capture=True)[0]

    @staticmethod
    def take_crash(raw, record):
        """Attaches oc_conformance's crash report (crash_report.cpp), if it wrote one, to the record of the call that
        ended it, and removes the file so the next child starts clean."""
        report = Path(str(raw) + ".crash.txt")
        if report.exists():
            record["crash"] = report.read_text(encoding="utf-8", errors="replace").strip()
            report.unlink()
        return record

    @staticmethod
    def status_of(code):
        return {0: "ok", 70: "ended", 124: "timeout"}.get(code, "crashed")

    def run_interface(self, version, apptype, argset):
        raw = self.out / "raw" / f"{version}-{apptype}-{argset}.jsonl"
        raw.unlink(missing_ok=True)
        start, results, attempts = 0, [], 0
        while attempts < 500:
            attempts += 1
            before = len(read_jsonl(raw))
            code = self.child(["--interface", version, "--apptype", str(apptype), "--argset", argset,
                               "--start-at", str(start)], raw)
            records = read_jsonl(raw)
            new = records[before:]
            for r in new:
                r["status"] = "ok"
            results += new
            if code == 0:
                Path(str(raw) + ".crash.txt").unlink(missing_ok=True)
                break
            if not new and start == 0:
                # Ended before its first record: in Init or the interface lookup
                results.append(self.take_crash(raw, {"interface": version, "apptype": apptype, "argset": argset, "index": -1,
                                "method": "<init or lookup>", "status": self.status_of(code), "exit_code": code}))
                break
            if not new:
                # The first call after the restart ended it again
                results.append(self.take_crash(raw, {"interface": version, "apptype": apptype, "argset": argset, "index": start,
                                "method": f"<call {start}>", "status": self.status_of(code), "exit_code": code}))
                start += 1
                continue
            # The call after the last one recorded is the one that ended the process
            last = max([r.get("index", -1) for r in new] + [start - 1])
            results.append(self.take_crash(raw, {"interface": version, "apptype": apptype, "argset": argset, "index": last + 1,
                            "method": f"<call {last + 1}>", "status": self.status_of(code), "exit_code": code}))
            start = last + 2
            if any(r.get("method") == "<lookup>" for r in new) or last + 1 < 0:
                break
        return results

    def run_case(self, name):
        raw = self.out / "raw" / f"case-{name}.jsonl"
        raw.unlink(missing_ok=True)
        extra = ["--case", name]
        if self.args.manifest:
            extra += ["--manifest", str(Path(self.args.manifest).resolve())]
        # The stand-in runtime's dump of what it was asked (frames ended and so on), for xr_failures.py
        dump = self.out / "raw" / f"case-{name}.dump.json"
        dump.unlink(missing_ok=True)
        code = self.child(extra, raw, CASE_TIMEOUT, {"OC_TEST_RUNTIME_DUMP": str(dump)})
        records = read_jsonl(raw)
        dump_data = None
        if any("dump_join" in r for r in records) and dump.exists():
            try:
                dump_data = json.loads(dump.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                dump_data = None
        for r in records:
            r["status"] = "ok"
            if "dump_join" in r:
                r["dump"] = join_dump(dump_data, r.pop("dump_join"))
        if code != 0:
            records.append(self.take_crash(raw, {"case": name, "method": "<end>", "status": self.status_of(code), "exit_code": code}))
        return records

    def cleanup(self):
        if self.args.ini:
            target = Path(self.runtime).parent / "opencomposite.ini"
            if getattr(self, "ini_backup", None) is not None:
                target.write_bytes(self.ini_backup)
            else:
                target.unlink(missing_ok=True)

    def run(self):
        self.prepare()
        jobs = []
        only = re.compile(self.args.only) if self.args.only else None
        if not self.args.cases_only:
            for v in load_versions():
                if only and not only.fullmatch(v):
                    continue
                for s in ARGSETS:
                    jobs.append(("i", v, 1, s))
                for t in APPTYPES_VALID_ONLY:
                    jobs.append(("i", v, t, "valid"))
        cases = self.args.cases.split(",") if self.args.cases else ([] if only else CASES)
        for c in cases:
            jobs.append(("c", c, None, None))
        results = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.args.jobs) as pool:
            futures = [pool.submit(self.run_interface, j[1], j[2], j[3]) if j[0] == "i" else pool.submit(self.run_case, j[1])
                       for j in jobs]
            for f in futures:
                results += f.result()
        return results


def monado_running():
    """Whether a Monado service is up: its compositor's IPC pipe (Windows) or socket (elsewhere) exists."""
    if os.name == "nt":
        try:
            return any(name.endswith("monado_comp_ipc") for name in os.listdir("\\\\.\\pipe\\"))
        except OSError:
            return False
    return Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "monado_comp_ipc").exists()


def join_dump(dump, what):
    """What a case record's "dump_join" asks of the stand-in's dump (None without one, as on a real runtime):
      last_projection  the last projection layer: {"frame", "invalid", "views": [{"samples", "samples_error", "rect",
                       "format", "array_index", "invalid", "depth"}]}
      last_frame       {"frame", "layers": every layer of the last frame that had any, as the dump has them}
      counters         frames_ended, frame_order_errors, refresh_rate_requests, images_left_acquired,
                       swapchain_call_order_errors
    """
    if not dump:
        return None
    layers = dump.get("layers", [])
    if what == "last_projection":
        proj = [layer for layer in layers if layer.get("type") == "projection"]
        if not proj:
            return None
        last = proj[-1]
        keep = ("samples", "samples_error", "rect", "format", "array_index", "invalid", "depth")
        return {"frame": last.get("frame"), "invalid": last.get("invalid", []),
                "views": [{k: v.get(k) for k in keep} for v in last.get("views", [])]}
    if what == "last_frame":
        if not layers:
            return None
        frame = layers[-1].get("frame")
        return {"frame": frame, "layers": [layer for layer in layers if layer.get("frame") == frame]}
    if what == "counters":
        return {k: dump.get(k) for k in ("frames_ended", "frame_order_errors", "refresh_rate_requests",
                                         "images_left_acquired", "swapchain_call_order_errors")}
    return {"error": f"unknown dump_join {what}"}


MISSING = object()


def dump_path(value, path):
    """Follows a path such as views[0].samples.tl into a joined dump; MISSING where it leads nowhere."""
    for part in re.findall(r"[^.\[\]]+|\[\d+\]", path):
        if part.startswith("["):
            i = int(part[1:-1])
            if not isinstance(value, list) or i >= len(value):
                return MISSING
            value = value[i]
        else:
            if not isinstance(value, dict) or part not in value:
                return MISSING
            value = value[part]
    return value


def expect_key(r):
    who = r.get("case") or r.get("interface", "?")
    return f'{who}/{r.get("method")}/{r.get("apptype", "-")}/{r.get("argset", "-")}'


def compare(results, expected_path, differences_path, arch):
    expected = {key_of(r): r for r in json.loads(Path(expected_path).read_text(encoding="utf-8"))["records"]}
    allowed = json.loads(Path(differences_path).read_text(encoding="utf-8")) if differences_path else []
    allowed = [d for d in allowed if d.get("arch", arch) == arch]
    diffs = []
    for r in results:
        e = expected.get(key_of(r))
        if not e or e.get("status") != "ok" or r.get("status") != "ok" or e.get("skipped") or r.get("skipped"):
            continue
        if e.get("init_error") in STEAMVR_TRANSIENT:
            continue  # SteamVR was restarting between children: no reference for this record
        for field in ("ret", "out", "init_error", "lookup_error"):
            if r.get(field) == e.get(field):
                continue
            # An entry may also name the app types it's for ("apptype": a regex of their numbers)
            if any(re.fullmatch(d["interface"], r.get("interface", r.get("case", ""))) and re.fullmatch(d["method"], r.get("method", ""))
                   and d["field"] == field and ("apptype" not in d or re.fullmatch(d["apptype"], str(r.get("apptype", ""))))
                   for d in allowed):
                continue
            diffs.append(f'{key_of(r)} {field}: got {json.dumps(r.get(field))}, SteamVR {json.dumps(e.get(field))}')
    return diffs


def check_expect(results, expect_path, allow_skip=None):
    """The mismatches with an --expect file, as (record key, text). A field "dump:<path>" is looked up in the record's
    joined dump. A skipped record passes only if its case matches allow_skip (a regex)."""
    expect = json.loads(Path(expect_path).read_text(encoding="utf-8"))
    by_key = {expect_key(r): r for r in results}
    misses = []
    for k, fields in expect.items():
        if k.startswith("#"):
            continue
        r = by_key.get(k)
        if r is None:
            misses.append((k, f"{k}: no such record"))
            continue
        if r.get("skipped"):
            if not (allow_skip and re.fullmatch(allow_skip, r.get("case", ""))):
                misses.append((k, f'{k}: skipped ({r["skipped"]})'))
            continue
        for f, v in fields.items():
            got = dump_path(r.get("dump"), f[len("dump:"):]) if f.startswith("dump:") else r.get(f)
            if got != v:
                shown = "nothing" if got is MISSING else json.dumps(got)
                misses.append((k, f"{k} {f}: got {shown}, expected {json.dumps(v)}"))
    return misses


def load_known(path):
    """--known-failures: {"<record key as in --expect>": "why", ...}; "#" keys are comments."""
    if not path:
        return {}
    return {k: v for k, v in json.loads(Path(path).read_text(encoding="utf-8")).items() if not k.startswith("#")}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runtime", required=True)
    ap.add_argument("--conformance", required=True)
    ap.add_argument("--xr-runtime", default="stand-in")
    ap.add_argument("--script")
    ap.add_argument("--out", required=True)
    ap.add_argument("--expected")
    ap.add_argument("--differences")
    ap.add_argument("--expect")
    ap.add_argument("--known-failures", help="records (keyed as in --expect) that may fail; any other failure fails the run")
    ap.add_argument("--allow-skip", help="a regex of the cases whose skipped records pass --expect")
    ap.add_argument("--only")
    ap.add_argument("--cases")
    ap.add_argument("--cases-only", action="store_true", help="only the cases of --cases, no interfaces")
    ap.add_argument("--manifest", help="the action manifest for the input-manifest case")
    ap.add_argument("--ini", help="an opencomposite.ini to put next to the DLL")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--ci", action="store_true")
    ap.add_argument("--allow-failures", action="store_true")
    ap.add_argument("--real-runtime", action="store_true")
    args = ap.parse_args()
    if args.cases and not args.only:
        args.cases_only = True

    runner = Runner(args)
    try:
        results = runner.run()
    finally:
        runner.cleanup()
    out = Path(args.out)
    (out / "results.json").write_text(json.dumps({"runtime": args.runtime, "records": results}, indent=1),
                                      encoding="utf-8")
    counts = {}
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    known = load_known(args.known_failures)
    known_hit = set()
    bad = [r for r in results if r["status"] in ("ended", "crashed", "timeout")]
    lines = [f"{k}: {v}" for k, v in sorted(counts.items())] + [""]
    problems = 0
    for r in bad:
        line = f'{r["status"]}: {key_of(r)} (exit {r.get("exit_code")})' + (f' - {r["crash"].splitlines()[0]}' if r.get("crash") else "")
        if expect_key(r) in known:
            known_hit.add(expect_key(r))
            line = "known: " + line
        else:
            problems += 1
        lines.append(line)
    if args.expected:
        diffs = compare(results, args.expected, args.differences, pe_machine(args.runtime))
        lines += ["", f"differences from {args.expected}: {len(diffs)}"] + diffs
        problems += len(diffs)
    if args.expect:
        misses = check_expect(results, args.expect, args.allow_skip)
        new = [text for k, text in misses if k not in known]
        old = [text for k, text in misses if k in known]
        known_hit.update(k for k, _ in misses if k in known)
        lines += ["", f"mismatches with {args.expect}: {len(new)}"] + new
        if old:
            lines += ["", f"known failures ({args.known_failures}): {len(old)}"] + old
        problems += len(new)
    if known:
        # A listed record that passes now: time to take it off the list
        fixed = sorted(k for k in known if k not in known_hit
                       and any(expect_key(r) == k and not r.get("skipped") for r in results))
        if fixed:
            lines += ["", f"known failures that pass now: {len(fixed)}"] + fixed
    (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:40]))
    if problems and not args.allow_failures:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
