#!/usr/bin/env python3
"""Generates a caller for every method of every OpenVR interface version in tests/conformance/interfaces.txt whose split
header exists (the build's generated/interfaces folder).

  gen_callers.py <generated dir> <interfaces.txt> <unsafe_methods.txt> <output .cpp>

Each caller makes the method's arguments for one argument set (callers_support.h), calls it, and records the return
value and what it wrote into its out-arguments. Which C++ argument maker a parameter gets is decided here from its
type and name; the values come from callers_support.h.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts"))
import libparse  # noqa: E402

SIZE_NAME = re.compile(r"(SizeOf|SizeInBytes|Bytes|Size$|Size[A-Z_]|cb|BufferLen|Len$|Length$)")
COUNT_NAME = re.compile(r"(Count|Capacity|Max)")
HANDLE_TYPE = re.compile(r"(Handle_t|^VRActionHandle_t$|^VRActionSetHandle_t$|^VRInputValueHandle_t$|^PropertyContainerHandle_t$|^SpatialAnchorHandle_t$|^IOBufferHandle_t$|^DriverHandle_t$|^PathHandle_t$)")
INTEGRAL_TYPES = {"uint32_t", "int32_t", "uint64_t", "int64_t", "uint16_t", "int", "unsigned int", "size_t",
                  "TrackedDeviceIndex_t", "DriverId_t", "BoneIndex_t", "ScreenshotHandle_t", "glUInt_t"}


def strip(t):
    """The type without annotation macros, keeping const and pointers."""
    return libparse.convert_type(t, None).strip()


def base_type(t):
    t = strip(t)
    t = t.replace("const ", "").strip()
    return t.rstrip("*& ").strip()


def is_integral(t):
    t = strip(t)
    return "*" not in t and base_type(t) in INTEGRAL_TYPES


UNPARSED = re.compile(r"virtual\s.*?(\w+)\s*\(")


def parse_header(path):
    """The header's methods in order; one libparse can't read (an unnamed parameter) has args None."""
    funcs = []
    with open(path, encoding="latin-1") as f:
        for line in libparse.nice_lines(f):
            try:
                func = libparse.parseline(line, None)
            except AttributeError:
                m = UNPARSED.search(line)
                func = libparse.funcdef_t(return_type="void", name=m.group(1), args=None) if m else None
            if func:
                funcs.append(func)
    return funcs


def load_list(path):
    out = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(line.split())
    return out


def unsafe_for(unsafe, version, method):
    for iface_re, method_re, *_ in unsafe:
        if re.fullmatch(iface_re, version) and re.fullmatch(method_re, method):
            return True
    return False


def plan_args(func):
    """Returns (setup lines, call expressions, record lines) for one function's arguments, or None if unsupported."""
    setup, call, record = [], [], []
    if func.args is None:
        return None
    args = func.args
    used = set()
    for i, a in enumerate(args):
        if i in used:
            continue
        t = strip(a.type)
        bt = base_type(a.type)
        var = f"a{i}"
        nxt = args[i + 1] if i + 1 < len(args) else None
        nxt2 = args[i + 2] if i + 2 < len(args) else None
        if t in ("const char *", "const char*"):
            setup.append(f'callers::String {var}(set, "{a.name}", ctx);')
            call.append(f"{var}.get()")
        elif t.endswith("**"):
            inner = t[:-2].strip()
            setup.append(f"callers::PtrPtr<{inner}> {var}(set);")
            call.append(f"{var}.get()")
            record.append(f'r.out["{a.name}"] = {var}.recorded();')
        elif bt == "Texture_t" and t.endswith("*") and t.startswith("const"):
            setup.append(f"callers::Texture {var}(set, ctx);")
            call.append(f"{var}.get()")
        elif t.endswith("*") and (bt.endswith("_T") or bt.startswith("ID3D") or bt.startswith("IDXGI")):
            # Pointers to Vulkan or D3D objects we can't make: null, or garbage for the invalid set
            setup.append(f"callers::Opaque<{t[:-1].strip()}> {var}(set);")
            call.append(f"{var}.get()")
        elif t.endswith("*") or t.endswith("&"):
            if t.endswith("&"):
                return None
            elem = t[:-1].strip()
            mode = "Single"
            if nxt is not None and is_integral(nxt.type) and SIZE_NAME.search(nxt.name):
                mode = "Bytes"
                if nxt2 is not None and is_integral(nxt2.type) and COUNT_NAME.search(nxt2.name):
                    mode = "BytesAndCount"
            elif nxt is not None and is_integral(nxt.type) and COUNT_NAME.search(nxt.name):
                mode = "Count"
            setup.append(f"callers::Ptr<{elem}> {var}(set, callers::PtrMode::{mode});")
            call.append(f"{var}.get()")
            if mode in ("Bytes", "BytesAndCount"):
                used.add(i + 1)
                call.append(f"{var}.template size_as<{strip(nxt.type)}>()")
            if mode == "Count":
                used.add(i + 1)
                call.append(f"{var}.template count_as<{strip(nxt.type)}>()")
            if mode == "BytesAndCount":
                used.add(i + 2)
                call.append(f"{var}.template count_as<{strip(nxt2.type)}>()")
            if "const" not in t.split("*")[0]:
                record.append(f'r.out["{a.name}"] = {var}.recorded();')
        elif HANDLE_TYPE.search(bt):
            setup.append(f"callers::Handle<{t}> {var}(set);")
            call.append(f"{var}.get()")
        else:
            # Integers, enums, floats and bools as Scalar, structs as ByValue (decided by type traits)
            setup.append(f"callers::Value<{t}> {var}(set);")
            call.append(f"{var}.get()")
    return setup, call, record


def main():
    gen_dir, interfaces_txt, unsafe_txt, out_cpp = map(Path, sys.argv[1:5])
    versions = [line[0] for line in load_list(interfaces_txt) if len(line) > 1 and line[1] == "app"]
    unsafe = load_list(unsafe_txt) if unsafe_txt.exists() else []
    # Versions answered "not found" on purpose; their headers (SteamVR-internal ones) needn't compile on their own
    skipped_file = interfaces_txt.parent / "not_implemented.txt"
    skipped = {line[0] for line in load_list(skipped_file)} if skipped_file.exists() else set()
    versions = [v for v in versions if v not in skipped]
    iface_dir = gen_dir / "interfaces"

    out = ["// Generated by tests/conformance/gen_callers.py: do not edit.", '#include "callers_support.h"', ""]
    tables = []
    for version in versions:
        header = iface_dir / f"{version}.h"
        if not header.exists():
            continue
        name = version.rsplit("_", 1)[0]  # IVRSystem
        out.append(f'#include "{version}.h"')
        funcs = parse_header(header)
        body = [f"namespace vr {{ namespace {version} {{", "namespace {"]
        fnames = []
        for index, func in enumerate(funcs):
            fn = f"call_{index}"
            plan = plan_args(func)
            body.append(f"CallRecord {fn}(void* p, ArgSet set, CallContext& ctx)")
            body.append("{")
            body.append("\t(void)set; (void)ctx;")
            body.append(f'\tCallRecord r; r.method = "{func.name}"; r.index = {index};')
            if unsafe_for(unsafe, version, func.name):
                body.append('\tif (ctx.againstRealRuntime) { r.skipped = "unsafe"; return r; }')
            if plan is None:
                body.append('\tr.skipped = "unsupported signature"; (void)p; return r;')
                body.append("}")
                fnames.append(fn)
                continue
            setup, call, record = plan
            body += ["\t" + s for s in setup]
            invoke = f"(({name}*)p)->{func.name}({', '.join(call)})"
            if strip(func.return_type) == "void":
                body.append(f"\t{invoke};")
            else:
                body.append(f"\tr.ret = callers::Record({invoke});")
            body += ["\t" + s for s in record]
            body.append("\treturn r;")
            body.append("}")
            fnames.append(fn)
        body.append("} // namespace")
        body.append(f"const std::vector<CallerFn> callers_table = {{ {', '.join(fnames)} }};")
        body.append("} }")
        out += body
        tables.append(version)

    out.append("")
    out.append("const std::vector<CallerFn>* FindCallers(const char* interfaceVersion)")
    out.append("{")
    out.append("\tstd::string v = interfaceVersion ? interfaceVersion : \"\";")
    for version in tables:
        out.append(f'\tif (v == "{version}") return &vr::{version}::callers_table;')
    out.append("\treturn nullptr;")
    out.append("}")
    out.append("")
    out.append("std::vector<std::string> GeneratedVersions()")
    out.append("{")
    out.append("\treturn { " + ", ".join(f'"{v}"' for v in tables) + " };")
    out.append("}")
    out_cpp.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(out) + "\n"
    if not out_cpp.exists() or out_cpp.read_text(encoding="utf-8") != text:
        out_cpp.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
