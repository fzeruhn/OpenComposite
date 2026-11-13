#!/usr/bin/env python3
"""The regression run: a conformance run's records against a recorded baseline.

  regress.py --results <dir>/results.json --baseline <file> [--differences differences.json] [--volatile volatile.json]
             [--accept]

Fails (exit 1) on any record that was ok in the baseline and is something else now (ended, crashed, timed out, or
gone), and on any change of ret, out, init_error or lookup_error, except where differences.json (the known differences
from SteamVR, which include this PC's own values) or volatile.json (values that change from run to run: process ids,
handles) covers that field. New records are reported, not failed. --accept writes the results as the new baseline.

The baseline keeps, per record, its status and a short hash of each compared field (not the values): one record per
line, so a re-accepted baseline diffs readably.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIELDS = ("ret", "out", "init_error", "lookup_error")


def key_of(r):
    who = r.get("case") or r.get("interface", "?")
    return f'{who}/{r.get("method", "?")}#{r.get("index", "")}/{r.get("apptype", "-")}/{r.get("argset", "-")}'


def digest(value):
    return hashlib.sha1(json.dumps(value, sort_keys=True).encode("utf-8")).hexdigest()[:10]


def covered(rules, r, field):
    who = r.get("interface", r.get("case", ""))
    for d in rules:
        if d["field"] != field and not re.fullmatch(d["field"], field):
            continue
        if not re.fullmatch(d["interface"], who) or not re.fullmatch(d["method"], r.get("method", "")):
            continue
        if "apptype" in d and not re.fullmatch(d["apptype"], str(r.get("apptype", ""))):
            continue
        return True
    return False


def summarise(results, rules):
    base = {}
    for r in results:
        if r.get("skipped"):
            continue
        entry = {"s": r.get("status")}
        for f in FIELDS:
            if f in r and not covered(rules, r, f):
                entry[f] = digest(r[f])
        base[key_of(r)] = entry
    return base


def write_baseline(path, base, source):
    lines = ["{", f' "#": "Baseline (regress.py --accept) from {source}: per record its status and a hash of each '
                  'compared field",', ' "records": {']
    keys = sorted(base)
    for i, k in enumerate(keys):
        lines.append(f"  {json.dumps(k)}: {json.dumps(base[k], sort_keys=True)}" + ("," if i + 1 < len(keys) else ""))
    lines += [" }", "}"]
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--differences", default=str(HERE / "differences.json"))
    ap.add_argument("--volatile", default=str(HERE / "volatile.json"))
    ap.add_argument("--accept", action="store_true")
    args = ap.parse_args()

    results = json.loads(Path(args.results).read_text(encoding="utf-8"))["records"]
    rules = []
    for p in (args.differences, args.volatile):
        if p and Path(p).exists():
            rules += [d for d in json.loads(Path(p).read_text(encoding="utf-8")) if isinstance(d, dict) and "field" in d]
    now = summarise(results, rules)

    if args.accept:
        write_baseline(args.baseline, now, Path(args.results).parent.name + "/results.json")
        print(f"baseline written: {len(now)} records to {args.baseline}")
        return 0

    base = json.loads(Path(args.baseline).read_text(encoding="utf-8"))["records"]
    problems, new = [], []
    for k, b in base.items():
        n = now.get(k)
        if n is None:
            if b["s"] == "ok":
                problems.append(f"{k}: ok in the baseline, missing now")
            continue
        if b["s"] == "ok" and n["s"] != "ok":
            problems.append(f"{k}: ok in the baseline, {n['s']} now")
            continue
        for f in FIELDS:
            if f in b and f in n and b[f] != n[f]:
                problems.append(f"{k}: {f} changed")
            elif (f in b) != (f in n) and b["s"] == n["s"] == "ok":
                problems.append(f"{k}: {f} {'gone' if f in b else 'new'}")
    for k in now:
        if k not in base:
            new.append(k)
    print(f"{len(base)} records in the baseline, {len(now)} now; {len(problems)} regressions, {len(new)} new records")
    for p in problems[:200]:
        print("  " + p)
    for k in new[:20]:
        print("  new: " + k)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
