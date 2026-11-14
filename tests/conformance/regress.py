#!/usr/bin/env python3
"""The regression run: a conformance run's records against a recorded baseline.

  regress.py --results <dir>/results.json --baseline <file> [--baseline <overlay>]... [--volatile volatile.json]
             [--accept]
  regress.py --self-test

Fails (exit 1) on any record that was ok in the baseline and is something else now (ended, crashed, timed out, or
gone), and on any change of an answer (every field but the record's identity and run bookkeeping: ret, out, the errors
and what cases record), except where volatile.json covers that field (values that change from run to run: process
ids, handles, timing). Every other answer is pinned, also those that differ from
SteamVR's (differences.json): they're OpenComposite's own, and mustn't change unnoticed. New records are reported,
not failed. --accept writes the results as the new baseline.

The baseline keeps, per record, its status and a short hash of each compared field, and the value itself ("#v", a key no record field has) for a
field whose JSON is under VALUE_BYTES bytes: one record per line, so a re-accepted baseline diffs readably, and a
changed field is shown with its old and new values (a baseline accepted before values were kept shows the old hash).

A later --baseline overrides the earlier ones per record (null: no such record), so a build whose answers differ in
a few records (32-bit, where pointers are shorter) keeps only those. With several, --accept writes the last one: the
records that differ from the earlier files.
"""

import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Not answers: which call the record is, and how the run went
BOOKKEEPING = {"case", "interface", "method", "index", "apptype", "argset", "status", "exit_code", "skipped", "crash", "callers"}
# A baseline entry's own keys, beside the fields' hashes
ENTRY_KEYS = {"s", "#v"}
VALUE_BYTES = 200


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
        values = {}
        for f in r:
            if f not in BOOKKEEPING and not covered(rules, r, f):
                entry[f] = digest(r[f])
                if len(json.dumps(r[f], sort_keys=True)) < VALUE_BYTES:
                    values[f] = r[f]
        if values:
            entry["#v"] = values
        base[key_of(r)] = entry
    return base


def write_baseline(path, base, source, over=()):
    what = f"the records that differ from {', '.join(Path(p).name for p in over)}, each with" if over else "per record"
    lines = ["{", f' "#": "Baseline (regress.py --accept) from {source}: {what} its status, a hash of each '
                  f'compared field and (#v) the value of each under {VALUE_BYTES} bytes",', ' "records": {']
    keys = sorted(base)
    for i, k in enumerate(keys):
        lines.append(f"  {json.dumps(k)}: {json.dumps(base[k], sort_keys=True)}" + ("," if i + 1 < len(keys) else ""))
    lines += [" }", "}"]
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def shown(entry, f):
    """A field as a regression shows it: its value if the entry keeps it, else its hash"""
    values = entry.get("#v", {})
    return json.dumps(values[f], sort_keys=True) if f in values else f"(hash {entry[f]})"


def same_entry(a, b):
    """Whether two baseline entries pin the same answers (the values kept beside the hashes don't count)"""
    if a is None or b is None:
        return a is b
    return {k: v for k, v in a.items() if k != "#v"} == {k: v for k, v in b.items() if k != "#v"}


def compare(base, now):
    """The regressions of now against base, and the records new in now"""
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
        for f in sorted((set(b) | set(n)) - ENTRY_KEYS):
            if f in b and f in n and b[f] != n[f]:
                problems.append(f"{k}: {f} changed: {shown(b, f)} -> {shown(n, f)}")
            elif (f in b) != (f in n) and b["s"] == n["s"] == "ok":
                problems.append(f"{k}: {f} {'gone' if f in b else 'new'}")
    for k in now:
        if k not in base:
            new.append(k)
    return problems, new


def self_test():
    """A baseline accepted from one run, then a run with a short and a long field changed: the short one is shown
    with its values, the long one with its hashes; a value kept in the baseline doesn't make an unchanged hash differ"""
    failures = []

    def check(what, ok):
        print(f'{"ok" if ok else "FAILED"}: {what}')
        if not ok:
            failures.append(what)

    long_a, long_b = "a" * VALUE_BYTES, "b" * VALUE_BYTES
    first = [{"interface": "IVRSystem_022", "method": "GetFloat", "index": 1, "apptype": 1, "argset": "valid", "status": "ok",
              "ret": 0.5, "out": {"pError": 0}},
             {"interface": "IVRSystem_022", "method": "GetLong", "index": 2, "apptype": 1, "argset": "valid", "status": "ok",
              "ret": long_a}]
    second = json.loads(json.dumps(first))
    second[0]["ret"] = 0.75
    second[1]["ret"] = long_b
    with tempfile.TemporaryDirectory() as tmp:
        base_path = Path(tmp) / "baseline.json"
        write_baseline(base_path, summarise(first, []), "first/results.json")
        base = json.loads(base_path.read_text(encoding="utf-8"))["records"]
        check("short values kept beside their hashes", base[key_of(first[0])].get("#v") == {"ret": 0.5, "out": {"pError": 0}})
        check(f"values of {VALUE_BYTES} bytes or more not kept", "#v" not in base[key_of(first[1])])
        problems, _ = compare(base, summarise(second, []))
        check("a changed short field shown with its old and new values",
              any(p.endswith("ret changed: 0.5 -> 0.75") for p in problems))
        check("a changed long field shown with its hashes",
              any(f"ret changed: (hash {digest(long_a)}) -> (hash {digest(long_b)})" in p for p in problems))
        check("an unchanged field not reported", not any(": out " in p for p in problems))
        check("nothing else", len(problems) == 2)
        # A baseline from before values were kept: the same answers are no regression, a changed one shows the hash
        old = {k: {f: v for f, v in e.items() if f != "#v"} for k, e in base.items()}
        check("an older baseline without values still matches", compare(old, summarise(first, []))[0] == [])
        check("an older baseline shows the old hash",
              any(p.endswith(f"ret changed: (hash {digest(0.5)}) -> 0.75") for p in compare(old, summarise(second, []))[0]))
        check("a re-accepted overlay leaves out records whose answers are the same", same_entry(old[key_of(first[0])], base[key_of(first[0])]))
    print(f"{len(failures)} failed")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results")
    ap.add_argument("--baseline", action="append")
    ap.add_argument("--volatile", default=str(HERE / "volatile.json"))
    ap.add_argument("--accept", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.results or not args.baseline:
        ap.error("--results and --baseline are required")

    results = json.loads(Path(args.results).read_text(encoding="utf-8"))["records"]
    rules = []
    if args.volatile and Path(args.volatile).exists():
        rules = [d for d in json.loads(Path(args.volatile).read_text(encoding="utf-8"))
                 if isinstance(d, dict) and {"interface", "method", "field"} <= d.keys()]
    now = summarise(results, rules)

    def merged(paths):
        base = {}
        for p in paths:
            base.update(json.loads(Path(p).read_text(encoding="utf-8"))["records"])
        return {k: v for k, v in base.items() if v is not None}

    if args.accept:
        target, earlier = args.baseline[-1], merged(args.baseline[:-1])
        records = {k: v for k, v in now.items() if not same_entry(earlier.get(k), v)}
        records.update({k: None for k in earlier if k not in now})
        write_baseline(target, records, Path(args.results).parent.name + "/results.json", args.baseline[:-1])
        print(f"baseline written: {len(records)} records to {target}")
        return 0

    base = merged(args.baseline)
    problems, new = compare(base, now)
    print(f"{len(base)} records in the baseline, {len(now)} now; {len(problems)} regressions, {len(new)} new records")
    for p in problems[:200]:
        print("  " + p)
    for k in new[:20]:
        print("  new: " + k)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
