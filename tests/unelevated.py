#!/usr/bin/env python3
"""Runs a command at medium integrity, for OpenXR tests: the OpenXR loader ignores XR_RUNTIME_JSON in a process whose
integrity level is above medium (an elevated process).

  unelevated.py [--env NAME=VALUE]... [--cwd DIR] [--timeout SECONDS] -- COMMAND [ARGS...]

Not elevated (or not on Windows): runs COMMAND directly. At high integrity on Windows: starts COMMAND with a copy of
this process's token lowered to medium integrity (DuplicateTokenEx, SetTokenInformation(TokenIntegrityLevel),
CreateProcessAsUserW, as Microsoft's sample for low-integrity processes does), waits for it and exits with its exit
code. Its output goes to this process's output.
"""

import argparse
import os
import subprocess
import sys


def _win():
    import ctypes
    from ctypes import wintypes
    advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetCurrentProcess.restype = wintypes.HANDLE
    advapi32.GetSidSubAuthorityCount.restype = ctypes.POINTER(ctypes.c_ubyte)
    advapi32.GetSidSubAuthorityCount.argtypes = [ctypes.c_void_p]
    advapi32.GetSidSubAuthority.restype = ctypes.POINTER(wintypes.DWORD)
    advapi32.GetSidSubAuthority.argtypes = [ctypes.c_void_p, wintypes.DWORD]
    advapi32.GetLengthSid.argtypes = [ctypes.c_void_p]
    advapi32.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    return ctypes, wintypes, advapi32, kernel32


def integrity_above_medium():
    if os.name != "nt":
        return False
    ctypes, wintypes, advapi32, kernel32 = _win()
    token = wintypes.HANDLE()
    if not advapi32.OpenProcessToken(kernel32.GetCurrentProcess(), 0x0008, ctypes.byref(token)):  # TOKEN_QUERY
        return False
    buf = (ctypes.c_byte * 64)()
    size = wintypes.DWORD()
    ok = advapi32.GetTokenInformation(token, 25, buf, 64, ctypes.byref(size))  # TokenIntegrityLevel
    kernel32.CloseHandle(token)
    if not ok:
        return False
    sid = ctypes.cast(buf, ctypes.POINTER(ctypes.c_void_p))[0]
    count = advapi32.GetSidSubAuthorityCount(sid)[0]
    level = advapi32.GetSidSubAuthority(sid, count - 1)[0]
    return level > 0x2000  # SECURITY_MANDATORY_MEDIUM_RID


def run_medium(command, env, cwd, timeout):
    import msvcrt
    ctypes, wintypes, advapi32, kernel32 = _win()

    class SID_AND_ATTRIBUTES(ctypes.Structure):
        _fields_ = [("Sid", ctypes.c_void_p), ("Attributes", wintypes.DWORD)]

    class STARTUPINFOW(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR), ("lpDesktop", wintypes.LPWSTR),
                    ("lpTitle", wintypes.LPWSTR), ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
                    ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD), ("dwXCountChars", wintypes.DWORD),
                    ("dwYCountChars", wintypes.DWORD), ("dwFillAttribute", wintypes.DWORD),
                    ("dwFlags", wintypes.DWORD), ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD),
                    ("lpReserved2", ctypes.c_void_p), ("hStdInput", wintypes.HANDLE),
                    ("hStdOutput", wintypes.HANDLE), ("hStdError", wintypes.HANDLE)]

    class PROCESS_INFORMATION(ctypes.Structure):
        _fields_ = [("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE), ("dwProcessId", wintypes.DWORD),
                    ("dwThreadId", wintypes.DWORD)]

    def check(ok, what):
        if not ok:
            raise OSError(ctypes.get_last_error(), what)

    token, dup = wintypes.HANDLE(), wintypes.HANDLE()
    # TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT
    check(advapi32.OpenProcessToken(kernel32.GetCurrentProcess(), 0x0001 | 0x0002 | 0x0008 | 0x0080,
                                    ctypes.byref(token)), "OpenProcessToken")
    # MAXIMUM_ALLOWED, SecurityImpersonation, TokenPrimary
    check(advapi32.DuplicateTokenEx(token, 0x02000000, None, 2, 1, ctypes.byref(dup)), "DuplicateTokenEx")
    sid = ctypes.c_void_p()
    check(advapi32.ConvertStringSidToSidW("S-1-16-8192", ctypes.byref(sid)), "ConvertStringSidToSidW")
    label = SID_AND_ATTRIBUTES(sid, 0x20)  # SE_GROUP_INTEGRITY
    check(advapi32.SetTokenInformation(dup, 25, ctypes.byref(label),
                                       ctypes.sizeof(label) + advapi32.GetLengthSid(sid)), "SetTokenInformation")

    handles = []
    for f in (sys.stdin, sys.stdout, sys.stderr):
        try:
            h = msvcrt.get_osfhandle(f.fileno())
            os.set_handle_inheritable(h, True)
            handles.append(h)
        except (OSError, ValueError, AttributeError):
            handles.append(None)
    sys.stdout.flush()
    sys.stderr.flush()

    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.dwFlags = 0x100  # STARTF_USESTDHANDLES
    si.hStdInput, si.hStdOutput, si.hStdError = handles
    pi = PROCESS_INFORMATION()
    block = ctypes.create_unicode_buffer("".join(f"{k}={v}\0" for k, v in env.items()) + "\0")
    cmdline = ctypes.create_unicode_buffer(subprocess.list2cmdline(command))
    # bInheritHandles, CREATE_UNICODE_ENVIRONMENT
    check(advapi32.CreateProcessAsUserW(dup, None, cmdline, None, None, True, 0x400, block, cwd, ctypes.byref(si),
                                        ctypes.byref(pi)), "CreateProcessAsUserW")
    wait = kernel32.WaitForSingleObject(pi.hProcess, int(timeout * 1000))
    if wait != 0:  # WAIT_TIMEOUT
        kernel32.TerminateProcess(pi.hProcess, 124)
        kernel32.WaitForSingleObject(pi.hProcess, 5000)
        print(f"unelevated.py: timed out after {timeout} s", file=sys.stderr)
        result = 124
    else:
        code = wintypes.DWORD()
        kernel32.GetExitCodeProcess(pi.hProcess, ctypes.byref(code))
        result = code.value
    for h in (pi.hProcess, pi.hThread, dup, token):
        kernel32.CloseHandle(h)
    kernel32.LocalFree(sid)
    return result


def run(command, env_pairs, cwd, timeout):
    env = dict(os.environ)
    env.update(env_pairs)
    if not integrity_above_medium():
        try:
            return subprocess.run(command, env=env, cwd=cwd, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            print(f"unelevated.py: timed out after {timeout} s", file=sys.stderr)
            return 124
    return run_medium(command, env, cwd, timeout)


def main():
    argv = sys.argv[1:]
    if "--" not in argv:
        print(__doc__, file=sys.stderr)
        return 2
    split = argv.index("--")
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--cwd")
    ap.add_argument("--timeout", type=float, default=120)
    args = ap.parse_args(argv[:split])
    env = dict(e.split("=", 1) for e in args.env)
    return run(argv[split + 1:], env, args.cwd, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
