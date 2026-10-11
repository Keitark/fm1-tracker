"""Offline import/relocation proof for a protected session's fixed Python copy.

The elevated bootstrap separately verifies hashes and administrator-only ACLs
before executing this file. This self-test opens no device and installs nothing.
"""
import argparse
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys

DEPENDENCIES = ("yaml", "tqdm", "crcmod", "colorama", "serial")


def native_paths(session):
    """Check transitive loaded DLLs as well as importable Python modules."""
    if os.name != "nt":
        return []
    import ctypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.K32EnumProcessModules.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]
    kernel.K32EnumProcessModules.restype = ctypes.c_int
    kernel.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint]
    kernel.GetModuleFileNameW.restype = ctypes.c_uint
    kernel.GetWindowsDirectoryW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint]
    kernel.GetWindowsDirectoryW.restype = ctypes.c_uint
    windows = ctypes.create_unicode_buffer(32768)
    if not kernel.GetWindowsDirectoryW(windows, len(windows)):
        raise RuntimeError("Cannot inspect Windows native module root")
    allowed = [Path(session).resolve(), *(Path(windows.value) / name for name in ("System32", "SysWOW64", "WinSxS"))]
    handles = (ctypes.c_void_p * 4096)()
    needed = ctypes.c_uint()
    if not kernel.K32EnumProcessModules(kernel.GetCurrentProcess(), handles, ctypes.sizeof(handles), ctypes.byref(needed)) or needed.value > ctypes.sizeof(handles):
        raise RuntimeError("Cannot inspect loaded native module paths")
    paths = []
    for handle in handles[:needed.value // ctypes.sizeof(ctypes.c_void_p)]:
        buffer = ctypes.create_unicode_buffer(32768)
        if not kernel.GetModuleFileNameW(handle, buffer, len(buffer)):
            raise RuntimeError("Cannot inspect a loaded native module")
        path = Path(buffer.value).resolve()
        if not any(path.is_relative_to(root) for root in allowed):
            raise RuntimeError(f"Native DLL escaped frozen snapshot/system directories: {path}")
        paths.append(str(path))
    return sorted(paths)


def confined(path, root):
    path, root = Path(path).resolve(), Path(root).resolve()
    if not path.is_relative_to(root):
        raise RuntimeError(f"Path escaped frozen snapshot: {path}")
    return path


def identity(session):
    session = Path(session).resolve()
    runtime = session / "runtime"
    executable = runtime / "python.exe"
    if Path(sys.executable).resolve() != executable.resolve():
        raise RuntimeError("Interpreter is not the derived frozen runtime/python.exe")
    if Path(sys.prefix).resolve() != runtime.resolve() or Path(sys.base_prefix).resolve() != runtime.resolve():
        raise RuntimeError("Python prefix did not relocate into the frozen runtime")
    if not sys.flags.isolated or not sys.flags.no_site or not sys.dont_write_bytecode or "site" in sys.modules:
        raise RuntimeError("Frozen Python requires isolated mode and no site initialization")
    config = runtime / "openssl-empty.cnf"
    if not config.is_file() or config.read_bytes() != b"":
        raise RuntimeError("Frozen Python requires its empty protected OpenSSL configuration")
    for name, expected in (("OPENSSL_CONF", config), ("OPENSSL_MODULES", runtime / "openssl-modules"),
                           ("OPENSSL_ENGINES", runtime / "openssl-engines")):
        if Path(os.environ.get(name, "")).resolve() != expected.resolve():
            raise RuntimeError(f"Native environment escaped frozen snapshot: {name}")
    if os.environ.get("OPENSSL_CONF_INCLUDE"):
        raise RuntimeError("OpenSSL configuration includes are not allowed")
    for path in sys.path:
        if not path:
            raise RuntimeError("Empty/current-directory Python import path")
        confined(path, runtime)
    if os.name == "nt":
        import ctypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
        kernel.GetModuleHandleW.restype = ctypes.c_void_p
        kernel.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint]
        kernel.GetModuleFileNameW.restype = ctypes.c_uint
        handle = kernel.GetModuleHandleW("python311.dll")
        buffer = ctypes.create_unicode_buffer(32768)
        if not handle or not kernel.GetModuleFileNameW(handle, buffer, len(buffer)):
            raise RuntimeError("Cannot inspect loaded Python DLL")
        confined(buffer.value, runtime)
    return {"executable": str(executable), "prefix": sys.prefix,
            "isolated": bool(sys.flags.isolated), "no_site": bool(sys.flags.no_site), "no_bytecode": sys.dont_write_bytecode,
            "paths": list(sys.path)}


def self_test(session, child=False):
    data = identity(session)
    session = Path(session).resolve()
    lib = session / "lib"
    sys.path.insert(0, str(lib))
    modules = {}
    for name in ("os", "json", "pathlib", "subprocess", "_ctypes", "_ssl", "_hashlib", "zlib", "_sqlite3", "_decimal"):
        module = importlib.import_module(name)
        if getattr(module, "__file__", None):
            modules[name] = str(confined(module.__file__, session / "runtime"))
    for name in DEPENDENCIES:
        module = importlib.import_module(name)
        modules[name] = str(confined(module.__file__, lib / name))
    import yaml
    import crcmod
    if yaml.safe_load("value: 7") != {"value": 7}:
        raise RuntimeError("Frozen yaml parser failed")
    if hasattr(yaml, "CLoader") and yaml.load("value: 7", Loader=yaml.CLoader) != {"value": 7}:
        raise RuntimeError("Frozen yaml native parser failed")
    if crcmod.mkCrcFun(0x104C11DB7, initCrc=0, xorOut=0xFFFFFFFF)(b"123456789") != 0xCBF43926:
        raise RuntimeError("Frozen crcmod vector failed")
    # Inspect every imported Python/native module, including transitive imports.
    for name, module in tuple(sys.modules.items()):
        path = getattr(module, "__file__", None)
        if path:
            confined(path, session)
    data.update(ok=True, device_io=False, modules=modules, native_modules=native_paths(session), paths=list(sys.path))
    if not child:
        result = subprocess.run([sys.executable, "-I", "-S", "-B", "-u", str(Path(__file__).resolve()),
                                 "--child-check", str(session)], capture_output=True, text=True, timeout=30, check=True)
        data["child"] = json.loads(result.stdout)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--self-test", metavar="SESSION_ROOT")
    mode.add_argument("--child-check", metavar="SESSION_ROOT")
    args = parser.parse_args()
    print(json.dumps(self_test(args.self_test or args.child_check, child=bool(args.child_check)), sort_keys=True))


if __name__ == "__main__":
    main()
