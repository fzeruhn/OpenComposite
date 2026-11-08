from dataclasses import dataclass
from pathlib import Path
import re
import typing
from typing import List

import libparse

# Ugly hack to get around a recursive import only needed for type checking
if typing.TYPE_CHECKING:
    from stubs.interface_spec import InterfaceSpec


class InterfaceDef:
    """
    A class representing a specific OpenVR interface (such as IVRSystem, IVRCompositor, etc)
    """
    name: str
    version: str
    flags: List[str]
    spec: 'InterfaceSpec'
    functions: List['Function']

    # Output directory for interfaces
    # set from the InterfaceSpec class (in interface_spec.py)
    INTERFACES_DIR = None

    # Input directory for headers (openvr-[version].h)
    HEADERS_DIR = None

    def __init__(self, name, version, flags, spec):
        assert self.INTERFACES_DIR is not None
        assert self.HEADERS_DIR is not None
        self.name = name
        self.version = version
        self.flags = flags
        self.spec = spec
        self.functions = _read_headers(self)
        print(version, name, flags)
        super().__init__()
        pass

    def varname(self):
        """
        Name of the interface.
        """
        return self.name

    def has_flag(self, flag):
        return flag in self.flags

    def header_filename(self):
        return f"interfaces/{self.interface_v()}.h"

    def header_prefix(self):
        return self.INTERFACES_DIR

    def basename(self):
        return "Base" + self.name

    def base_header(self):
        return self.basename() + ".h"

    def getter_name(self):
        return self.basename()

    def proxy_class_name(self):
        """
        The name of the proxy class that will help to generate this interface - just appends CVR to the name.
        """
        return f"CVR{self.name}_{self.version}"

    def interface(self):
        """
        Gets the name of the interface as seen in OpenVR - just appends IVR to the name.
        """
        return f"IVR{self.name}"

    def interface_v(self):
        """
        Gets the name of the interface with the version attached.
        """
        return f"{self.interface()}_{self.version}"

    def namespace(self):
        """
        Get the namespace in which this interface class resides, such as vr::IVRSystem_020
        """
        return "vr::" + self.interface_v()

    def version_variable(self):
        return f"{self.namespace()}::{self.interface()}_Version"

    # Throw an error to make sure we don't accidentally write this instance to a file
    def __str__(self):
        raise Exception("repl")


class CustomInterface(InterfaceDef):
    def header_prefix(self):
        return self.HEADERS_DIR

    def header_filename(self):
        return f"custom_interfaces/{self.interface_v()}.h"


class DriverInterface(InterfaceDef):
    def interface_v(self):
        return "driver_%s_%s" % (self.interface(), self.version)


class APIInterface(InterfaceDef):
    def header_filename(self):
        return "API/I%s_%s.h" % (self.name, self.version)

    def proxy_class_name(self):
        return "CVROC%s_%s" % (self.name, self.version)

    def basename(self):
        return "OCBase" + self.name

    def interface(self):
        return "IVROC%s" % self.name

    def getter_name(self):
        return "BaseOC" + self.name

    def varname(self):
        return "OC" + self.name

    def namespace(self):
        return "ocapi::" + self.interface_v()

    def base_header(self):
        return "API/" + super().base_header()


@dataclass
class Function:
    name: str
    return_type: str
    args: List[libparse.arg_t]
    user_implemented: bool = False
    const: bool = False

    def args_str(self):
        """
        Returns a type-name string of the arguments, as you'd find in a function declaration
        """
        return ", ".join(f.type + " " + f.name for f in self.args)

    def args_names(self):
        """
        Returns a comma-separated list of the argument names, for passing all the arguments
        from this function into another function
        """
        return ", ".join([a.name for a in self.args])


def _read_headers(interface: InterfaceDef) -> List[Function]:
    filename = interface.header_prefix() / interface.header_filename()
    context = dict(_get_global_context())
    context.update(deleted_types_context().get(interface.interface_v(), {}))
    libparse.read_context(context, filename, interface.namespace())

    funcs = []
    with open(filename) as f:
        for line in libparse.nice_lines(f):
            func = libparse.parseline(line, context)
            if not func:
                continue

            user_funcs = interface.spec.manual_functions.get(interface.proxy_class_name(), None)
            user_defined = user_funcs is not None and (func.name in user_funcs)

            func = Function(func.name, func.return_type, func.args, user_defined, func.const)
            funcs.append(func)

    # A method the parser can't read would shift every later one in the vtable: count the declarations to be sure
    text = re.sub(r"/\*.*?\*/", "", Path(filename).read_text(encoding="latin-1"), flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    declared = len(re.findall(r"\bvirtual\b", text))
    if declared != len(funcs):
        raise Exception(f"{filename}: {declared} virtual methods declared, but {len(funcs)} parsed: "
                        f"{[f.name for f in funcs]}")

    return funcs


# The global context - that's all the types from vrtypes.h and similar files
_global_ctx = None


def _get_global_context():
    global _global_ctx
    if _global_ctx is not None:
        return _global_ctx

    _global_ctx = dict()
    headers_dir = InterfaceDef.INTERFACES_DIR
    libparse.read_context(_global_ctx, headers_dir / "interfaces/public_vrtypes.h", "vr")
    libparse.read_context(_global_ctx, headers_dir / "interfaces/vrtypes.h", "vr")
    # IVRTrackedCamera_001's frame types (SDK 0.9.11's shared camera section)
    libparse.read_context(_global_ctx, headers_dir / "interfaces/vrtrackedcameratypes.h", "vr")
    _global_ctx.update(deleted_types_context()[None])
    return _global_ctx


_deleted_ctx = None


def deleted_types_context():
    """The names deleted_types.h defines for the old versions: under None the ones in vr, under "IVRFoo_NNN" the
    ones in that version's own namespace block (vr::IVRFoo_NNN::Name), which no other version sees."""
    global _deleted_ctx
    if _deleted_ctx is not None:
        return _deleted_ctx

    text = (InterfaceDef.INTERFACES_DIR / "interfaces/deleted_types.h").read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    names = r"\b(?:struct|enum|union)\s+(\w+)\s*\{|\btypedef\b[^;{]*?(\w+)\s*;|\busing\s+(?:\w+::)*(\w+)\s*;"

    def found(body):
        return [next(g for g in m.groups() if g) for m in re.finditer(names, body)]

    _deleted_ctx = {None: {}}
    blocks = re.compile(r"\bnamespace\s+(IVR\w+_\d{3})\s*\{(.*?)\n\}", flags=re.S)
    for m in blocks.finditer(text):
        _deleted_ctx[m.group(1)] = {n: f"vr::{m.group(1)}::{n}" for n in found(m.group(2))}
    for n in found(blocks.sub("", text)):
        _deleted_ctx[None][n] = "::" + n if n.startswith("EGraphicsAPIConvention") else "vr::" + n
    return _deleted_ctx
