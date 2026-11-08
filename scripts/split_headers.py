#!/usr/bin/python3

import sys
import re
import io
import os
import glob
import hashlib
import shutil

from pathlib import Path
from typing import TextIO, Callable

#####################################################
# Be sure to list these in ascending order!
# This ensures that non-interface files get the
# latest version available
versions = [
    "0.9.0",
    "0.9.2",
    "0.9.3",
    "0.9.5",
    "0.9.8",
    "0.9.9",
    "0.9.11",
    "0.9.12",
    "0.9.14",
    "0.9.15",
    "0.9.17",
    "0.9.18",
    "0.9.19",
    "0.9.20",
    "1.0.0",
    "1.0.1",
    "1.0.2",
    "1.0.4",
    "1.0.5",
    "1.0.7",
    "1.0.8",
    "1.0.10",
    "1.0.11",
    "1.0.12",
    "1.0.15",
    "1.0.16",
    "1.0.17",
    "1.1.3",
    "1.4.18",
    "1.6.10",
    "1.7.15",
    "1.8.19",
    "1.9.16",
    "1.10.30",
    "1.11.11",
    "1.12.5",
    "1.13.10",
    "1.14.15",
    "1.16.8",
    "1.23.7",
    "2.5.1",
    "2.12.1",
    "2.12.14",
    "2.15.6",
]

driver_versions = [
    "1.1.3b",
]

driver_files = [
    "itrackeddevicedriverprovider",
    "IVRServerDriverHost",
    "ITrackedDeviceServerDriver",
]

interface_exceptions = [
    "itrackeddevicedriverprovider",
]

patches = {
    "driver_IVRServerDriverHost_005",
    "driver_itrackeddevicedriverprovider",
}

#####################################################

# input directory, headers should be located here
input_dir = Path(sys.argv[1])

# output directory to place generated headers in
output_dir = Path(sys.argv[2])
output_dir.mkdir(exist_ok=True)

# List of files we've already written, this prevents us from overwriting newer files
files_written = []

# Valve changed some versions in place. Growing at the end is harmless (an older caller never reaches the new slots), so
# the newest header's copy is kept; anything else shifts slots, and the header to generate from is chosen here, with
# why. Every other change fails the split (check_vtable_prefixes).
PINNED = {
    "IVRTrackedCamera_001": "0.9.12",
}
KNOWN_INPLACE_CHANGES = {
    # 0.9.11 has 17 methods, 0.9.12 19, 0.9.14 18 (SupportsPauseResume gone from slot 13). The 0.9.12 form is the longest:
    # no caller runs off its end, and on x64 a shifted slot only answers "no camera". On 32-bit a 0.9.14 game's
    # GetCameraDistortion lands on IsVideoStreamPaused, which pops fewer bytes: camera apps of that SDK only.
    "IVRTrackedCamera_001": "0.9.14 removed SupportsPauseResume mid-table without a new version",
    # Slot 3 was spelt GetCameraIntrinisics until 1.0.4: renamed in place, same slot and arguments
    "IVRTrackedCamera_003": "GetCameraIntrinisics renamed GetCameraIntrinsics in 1.0.5",
    # The arguments of one method changed without a new version. The newest form is generated, and CVR*.cpp answers
    # it without using the arguments an older caller doesn't pass (on 32-bit the callee still pops the newest form's).
    "IVRChaperone_003": "GetBoundsColor gained flCollisionBoundsFadeDistance in 0.9.12 and pOutputCameraColor in 0.9.14",
    "IVRSettings_001": "Sync(peError) became Sync(bForce, peError) in 0.9.17",
}
methods_by_header = {}  # versioned target -> {header version: [method signatures]}
current_header = None  # the header being split


def method_names(text):
    """The virtual methods a section declares, in vtable order: each as its return type, name and argument types
    (without argument names, defaults, const or VR_ annotations), so a signature changed in place counts as a change"""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    sigs = []
    for m in re.finditer(r"\bvirtual\b([^;{(]*?)(~?\w+)\s*\(", text):
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
        args = re.sub(r"\bVR_\w+\s*\([^()]*\)", "", text[m.end():i - 1])
        types = []
        for arg in args.split(","):
            arg = re.sub(r"\bconst\b|\bvr::", "", arg.split("=")[0])
            arg = re.sub(r"\s*([*&])\s*", r"\1", " ".join(arg.split()))
            types.append(re.sub(r"(?<=[\w*&])\s+\w+$|(?<=[*&])\w+$", "", arg))
        ret = re.sub(r"\s*([*&])\s*", r"\1", " ".join(re.sub(r"\bconst\b|\bvr::", "", m.group(1)).split()))
        sigs.append(f"{ret} {m.group(2)}({','.join(t for t in types if t and t != 'void')})")
    return sigs


def check_vtable_prefixes():
    """Fails unless every header's method list for a version is a prefix of the one generated, or is a known change"""
    problems = []
    for target, by_header in sorted(methods_by_header.items()):
        chosen = PINNED.get(target) or next(iter(by_header))  # the newest header comes first
        if chosen not in by_header:
            problems.append(f"{target}: pinned to {chosen}, which has no such section")
            continue
        generated = by_header[chosen]
        for header, methods in by_header.items():
            if methods != generated[:len(methods)] and target not in KNOWN_INPLACE_CHANGES:
                problems.append(f"{target}: {header}'s {len(methods)} methods aren't a prefix of {chosen}'s {len(generated)} "
                                f"(first difference at slot {next(i for i, (a, b) in enumerate(zip(methods + [None], generated + [None])) if a != b)})")
    if problems:
        raise Exception("Valve changed these versions in place, other than at the end of the vtable; pin each one's header "
                        "in PINNED with a KNOWN_INPLACE_CHANGES reason:\n  " + "\n  ".join(problems))

# Matches a comment indicating the start of a new interface (such as "// ivrcompositor.h")
matcher = re.compile(r"^\/\/ ([\w_-]+)\.h$")

# Matches a line giving the version for an interface.
# Formatted like IVR<interface name>_<version number>
versionmatcher = re.compile(r"_Version = \"([\w-]*)\";")


def patch(text, patchfile):
    """
    Patches the provided text using the provided patch file.
    """

    lines = text.splitlines()

    with open(patchfile, "r") as pat:
        patchlines = pat.readlines()

        linenum = 0
        while linenum < len(patchlines):
            line = patchlines[linenum]
            linenum += 1
            line = line.strip()
            if not line:
                continue

            if line[0] == "#":
                continue

            parts = line.split()
            mode = parts[0]
            if mode == "ins":
                start = int(parts[1]) - 1  # start at line 1
                rlen = int(parts[2])
                cpy_section = patchlines[linenum:linenum + rlen]
                linenum += rlen
                lines[start:start] = [s.rstrip('\n') for s in cpy_section]
            elif mode == "del":
                start = int(parts[1]) - 1  # start at line 1
                rlen = int(parts[2])
                del lines[start:start + rlen]
            elif mode == "edit":
                start = int(parts[1]) - 1  # start at line 1
                olen = int(parts[2])
                nlen = int(parts[3])
                cpy_section = patchlines[linenum:linenum + nlen]
                linenum += nlen
                lines[start:start + olen] = [s.rstrip('\n') for s in cpy_section]
            else:
                raise Exception("Unknown patch mode '%s'" % mode)

    return "\n".join(lines)


def write_interface(target: str, result: str, usingiface: bool, interface_checker: Callable, out_dir: Path):
    """
    Writes an interface out to a file.
    target - The name of the interface and resulting file.
    result - The text to write to the interface file.
    usingiface - Is this a versioned interface?
    interface_checker - Function to check if we want to write out this interface.
    out_dir - The directory to put the interface file in.
    """
    if interface_checker:
        target = interface_checker(target, usingiface)
        if not target:
            return True

    # Generated file name for this interface
    filename = target + ".h"
    outfile = out_dir / filename

    # Process the file line-by-line to remove the include guard - we've added a pragma once
    lines = result.split("\n")
    hit_include_guard = False
    hit_include_guard_end = False
    for i in range(len(lines) - 1, 0, -1):
        line = lines[i]

        # Filter out the _OPENVR_API include guard, and _INCLUDE_ include guards
        if "_OPENVR_API" in line or "_INCLUDE_" in line:
            # Only count the ifdef itself that set the flag
            if "#ifndef" in line:
                assert not hit_include_guard
                hit_include_guard = True

            # And also keep track of whether we've found the end of the include guard
            if "#endif" in line:
                assert not hit_include_guard_end
                hit_include_guard_end = True

            del lines[i]
            continue

    # If we've got an unterminated include guard, remove the last ifdef:
    if hit_include_guard and not hit_include_guard_end:
        for i in range(len(lines) - 1, 0, -1):
            line = lines[i]
            if line == "#endif":
                # Replace it with a comment, in case this ever does the wrong thing
                lines[i] = "// Uncommented include guard ifdef was here, removed while splitting"
                break
            elif line:
                # Stop on any non-empty lines
                break

    result = "\n".join(lines)

    # Don't delete this file, since it gets overwritten
    # if filename in files_to_delete:
    #   files_to_delete.remove(filename)

    if usingiface:
        methods_by_header.setdefault(target, {})[current_header] = method_names(result)
        if target in PINNED and PINNED[target] != current_header:
            return  # only the pinned header's copy is generated

    if filename in files_written:
        # This file is already written by a later version
        return

    files_written.append(filename)

    # Fix up the namespaces
    # If this is a versioned namespace (usingiface = True), we place it in its own custom namespace
    if usingiface:
        # The SDK's accessor (VR_INTERFACE IVRFoo *VR_CALLTYPE VRFoo();) is extern "C": two versions declaring it in
        # their own namespaces with different return types don't compile together, and nothing here calls it
        result = re.sub(r"^[ \t]*VR_INTERFACE\s+(?:vr::)?\w+\s*\*\s*VR_CALLTYPE\s+VR\w+\s*\(\s*\)\s*;[ \t]*$",
                        "\t// (the SDK's extern \"C\" accessor was here, removed while splitting)", result, flags=re.M)
        result = result.replace("%%REPLACE%NS%START%%", "namespace vr\n{\nnamespace " + target)
        result = result + "} // Close custom namespace\n"
        result = result.replace("vr::", "")
    else:
        result = result.replace("%%REPLACE%NS%START%%", "namespace vr")

    # Apply any manual patches (their line numbers are the section's own, so before anything is inserted)
    if target in patches:
        result = patch(result, input_dir / f"patches/{target}.ipatch")

    # Types the old versions use that the newest vrtypes.h no longer has (the driver interfaces need none)
    if usingiface and not target.startswith("driver_"):
        result = result.replace("#pragma once\n", "#pragma once\n#include \"deleted_types.h\"\n", 1)

    # if nooverwrite and os.path.isfile(outfile):
    #   # Check the hashes of the files, in case the headers have been modified, a new
    #   #  header has been added, or anything like that.
    #   current = hashlib.md5()
    #   with open(outfile, "rb") as fi:
    #       current.update(fi.read())

    #   new = hashlib.md5()
    #   new.update(result.encode())

    #   if current.digest() == new.digest():
    #       return
    #   else:
    #       print("Hash changed, cannot skip " + outfile)

    print(f"Writing to: {outfile}")
    with open(outfile, "wb") as outfile:
        outfile.write(result.encode())


def split_header(headerfile: TextIO, interface_checker: Callable = None,
                 out_dir: Path = output_dir / "interfaces", imports=None):
    """
    Splits the provided headerfile into its interfaces.
    headerfile - The header file to read from. Should be a file object.
    interface_checker - Function to check if an interface should be written out.
        Passed to write_interface.
    out_dir - Directory to output interfaces to.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    outbuff = io.StringIO()
    targetfile = None
    usingiface = False
    iface_excluded = False

    if not imports:
        imports = io.StringIO()
        imports.write("#pragma once\n")

    for line in headerfile:
        niceline = line.rstrip("\n")

        # "// End" closes the last interface. What follows is the SDK's tail (VR_Init, COpenVRContext and the old enum
        # names), which would otherwise land in the last section; nothing uses it from there
        if niceline.strip() == "// End":
            break

        # check if this line is signifiying the start of a new interface file
        match = matcher.match(niceline)

        # check if this line is giving us a version
        vmatch = versionmatcher.search(niceline)

        # this is the start of a new interface file
        if match:

            # Write out the previous interface file, unless this was the first one
            if targetfile:
                rejected = write_interface(targetfile, outbuff.getvalue(),
                                           usingiface, interface_checker, out_dir)

                # Add the import, unless it's a versioned interface
                if not usingiface and not iface_excluded and not rejected:
                    imports.write("#include \"" + targetfile + ".h\"\n")

                outbuff = io.StringIO()
                outbuff.write(imports.getvalue())
                usingiface = False

            # Grab the name of the next interface
            targetfile = match.group(1)
            iface_excluded = targetfile in interface_exceptions

        # Found version info
        if vmatch and not iface_excluded:
            assert not usingiface, "Cannot have multiple interfaces in one file"
            targetfile = vmatch.group(1)
            usingiface = True

        if niceline == "namespace vr":
            outbuff.write("%%REPLACE%NS%START%%\n")
        else:
            outbuff.write(line)

    # Write the last interface
    if targetfile:
        write_interface(targetfile, outbuff.getvalue(), usingiface, interface_checker, out_dir)

    return imports


def driver_filter(fi, iface):
    # Chop off the _xxx version suffix from interfaces for the purposes of comparing them
    if re.match(r".*_\d\d\d", fi):
        name = fi[:-4]
    else:
        name = fi
    # print(fi)
    if name in driver_files:
        return "driver_" + fi

    return False


(output_dir / "interfaces").mkdir(parents=True, exist_ok=True)
shutil.copyfile(input_dir / "deleted_types.h", output_dir / "interfaces" / "deleted_types.h")

# Go through the list of interface versions backwards.
# This allows us to build the newest version of each interface without overwriting them
imports_by_version = {}
DRIVER_IMPORTS_FROM = "0.9.12"
for version in versions[::-1]:
    feed = f"openvr-{version}.h"
    print("Reading: " + feed)
    current_header = version

    with open(input_dir / feed, "r") as headerfile:
        imports_by_version[version] = split_header(headerfile)
# The driver header's interfaces include what the 0.9.12 header sets up; the 0.9.0–0.9.11 headers have fewer shared
# sections, and would change the driver interfaces
appapi_imports = imports_by_version[DRIVER_IMPORTS_FROM]

# Does the same thing with driver versions.
for version in driver_versions[::-1]:
    feed = "openvr-%s-driver.h" % version
    print("Reading: " + feed)
    current_header = version + "-driver"

    # Note we have to use Latin-1, since EVRSkeletalTrackingLevel's first comment includes
    # a character outside of the plain ASCII range
    with open(input_dir / feed, "r", encoding="latin-1") as headerfile:
        split_header(headerfile, driver_filter, imports=appapi_imports)

check_vtable_prefixes()

# for fi in files_to_delete:
#    os.remove(fi)

# link custom interfaces
#os.symlink(input_dir / "custom_interfaces", output_dir / "custom_interfaces")
