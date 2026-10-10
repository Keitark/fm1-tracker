"""Offline immutable-runtime tests; no UAC, pipe server or device access.

The optional relocation smoke executes only a runtime copied into an owned
temporary directory. Set FM1_FROZEN_RUNTIME_SMOKE=1 to include it; this proves
relocation/isolation, not administrator ownership of that temporary copy.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import flash_session_worker as worker


HERE = Path(__file__).resolve().parent
PS_HELPER = HERE / "frozen-python-runtime.ps1"
PY_HELPER = HERE / "frozen_python_runtime.py"
POWERSHELL = Path(os.environ.get("WINDIR", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
DEPENDENCIES = ("yaml", "tqdm", "crcmod", "colorama", "serial")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def powershell(folder, body, configuration=None, timeout=60):
    """Run only a test harness; never the broker/server or an elevation path."""
    folder = Path(folder)
    config = folder / "test-input.json"
    config.write_text(json.dumps(configuration or {}), encoding="utf-8")
    harness = folder / "test-harness.ps1"
    harness.write_text(
        "$ErrorActionPreference='Stop'\n"
        "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n"
        "$env:PSModulePath=Join-Path $PSHOME 'Modules'\n"
        ". " + ps_quote(PS_HELPER) + "\n"
        "$testConfig=Get-Content -LiteralPath " + ps_quote(config) + " -Raw | ConvertFrom-Json\n"
        "try {\n" + body + "\n"
        "} catch { @{ok=$false;error=$_.Exception.Message} | ConvertTo-Json -Compress -Depth 20 }\n",
        encoding="utf-8")
    environment = dict(os.environ)
    # Windows PowerShell 5 must load its inbox modules even when the outer test
    # process was launched by PowerShell 7 with a different PSModulePath.
    environment["PSModulePath"] = str(POWERSHELL.parent / "Modules")
    process = subprocess.run(
        [str(POWERSHELL), "-NoProfile", "-NonInteractive", "-File", str(harness)],
        capture_output=True, text=True, encoding="utf-8", timeout=timeout,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=environment)
    if process.returncode:
        raise AssertionError(process.stderr)
    return json.loads(process.stdout)


def snapshot(folder, runtime, dependencies, before=""):
    return powershell(folder, before + "\n"
        "$testDependencies=@{}\n"
        "foreach($item in $testConfig.dependencies.PSObject.Properties){$testDependencies[$item.Name]=$item.Value}\n"
        "$entries=@(Get-FM1PythonSnapshot -RuntimeRoot $testConfig.runtime -DependencyRoots $testDependencies)\n"
        "@{ok=$true;entries=$entries} | ConvertTo-Json -Compress -Depth 20",
        {"runtime": str(runtime), "dependencies": {name: str(path) for name, path in dependencies.items()}})


@unittest.skipUnless(sys.platform == "win32", "Windows runtime/ACL contract")
class SnapshotPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fm1-runtime-contract-")
        self.folder = Path(self.temporary.name)
        self.runtime = self.folder / "installed-python"
        self.dependencies = {name: self.folder / "packages" / name for name in DEPENDENCIES}
        for relative in ("python.exe", "pythonw.exe", "python3.dll", "python311.dll",
                         "vcruntime140.dll", "vcruntime140_1.dll", "python311.zip",
                         "Lib/os.py", "Lib/encodings/__init__.py", "Lib/json/__init__.py",
                         "DLLs/_ssl.pyd", "DLLs/libcrypto-3.dll"):
            self.write(self.runtime / relative)
        for name, root in self.dependencies.items():
            self.write(root / "__init__.py")
            self.write(root / "native" / ("_" + name + ".pyd"))
            self.write(root / "LICENSE.txt")

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, path, content=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if content is not None else ("fixture " + path.name).encode())
        return path

    def manifest(self, before=""):
        return snapshot(self.folder, self.runtime, self.dependencies, before)

    def test_manifest_preserves_installed_layout_and_five_dependencies_with_native_files(self):
        result = self.manifest()
        self.assertTrue(result["ok"], result)
        entries = result["entries"]
        relatives = {entry["relative"].replace("\\", "/") for entry in entries}
        self.assertEqual(len(relatives), len(entries), "duplicate destination aliases")
        for relative in ("runtime/python.exe", "runtime/python311.dll", "runtime/Lib/os.py",
                         "runtime/Lib/encodings/__init__.py", "runtime/DLLs/_ssl.pyd"):
            self.assertIn(relative, relatives)
        for name in DEPENDENCIES:
            self.assertIn("lib/" + name + "/__init__.py", relatives)
            self.assertIn("lib/" + name + "/native/_" + name + ".pyd", relatives)
        for entry in entries:
            source = Path(entry["source"])
            self.assertTrue(source.resolve().is_relative_to(self.folder.resolve()))
            self.assertEqual(entry["sha256"], digest(source.read_bytes()))
            relative = Path(entry["relative"])
            self.assertFalse(relative.is_absolute())
            self.assertNotIn("..", relative.parts)

    def test_site_packages_and_bytecode_do_not_enter_the_snapshot(self):
        self.write(self.runtime / "Lib/site-packages/sitecustomize.py", b"raise RuntimeError('outside')")
        self.write(self.runtime / "Lib/__pycache__/os.cpython-311.pyc")
        self.write(self.runtime / "Lib/old.pyc")
        self.write(self.runtime / "Lib/old.pyo")
        self.write(self.dependencies["crcmod"] / "__pycache__/cached.pyc")
        result = self.manifest()
        self.assertTrue(result["ok"], result)
        relatives = [entry["relative"].replace("\\", "/").lower() for entry in result["entries"]]
        self.assertFalse(any("site-packages" in path or "__pycache__" in path or
                             path.endswith((".pyc", ".pyo")) for path in relatives))

    def test_path_configuration_overrides_fail_closed(self):
        for name in ("pyvenv.cfg", "python311._pth", "PYTHON._PTH"):
            with self.subTest(name=name):
                control = self.write(self.runtime / name, b"C:\\mutable\\packages\nimport site\n")
                try:
                    result = self.manifest()
                    self.assertFalse(result["ok"], result)
                    self.assertNotIn("entries", result)
                finally:
                    control.unlink()

    def test_required_python_layout_is_not_optional(self):
        for relative in ("python.exe", "Lib/os.py", "DLLs"):
            with self.subTest(relative=relative):
                source = self.runtime / relative
                saved = source.with_name(source.name + "-saved")
                source.rename(saved)
                try:
                    result = self.manifest()
                    self.assertFalse(result["ok"], result)
                finally:
                    saved.rename(source)

    def test_missing_dependency_never_produces_a_partial_manifest(self):
        roots = dict(self.dependencies)
        roots.pop("serial")
        result = snapshot(self.folder, self.runtime, roots)
        self.assertFalse(result["ok"], result)
        self.assertNotIn("entries", result)

    def test_directory_junction_cannot_enter_runtime_or_dependency_trees(self):
        for link in (self.runtime / "Lib/redirect", self.dependencies["serial"] / "redirect"):
            with self.subTest(link=link.relative_to(self.folder)):
                destination = self.folder / "junction-target"
                destination.mkdir(exist_ok=True)
                self.write(destination / "outside.py")
                made = powershell(self.folder,
                    "New-Item -ItemType Junction -Path " + ps_quote(link) + " -Target " + ps_quote(destination) +
                    " | Out-Null\n@{ok=$true} | ConvertTo-Json -Compress")
                self.assertTrue(made["ok"], made)
                try:
                    result = self.manifest()
                    self.assertFalse(result["ok"], result)
                    self.assertIn("reparse", result["error"].lower())
                finally:
                    # Remove only the junction itself, never its target tree.
                    os.rmdir(link)

    def test_reparse_file_attribute_is_rejected_before_hashing(self):
        unsafe = self.runtime / "DLLs/_ssl.pyd"
        before = """
function Get-Item {
    param([string]$LiteralPath,[switch]$Force)
    $item=Microsoft.PowerShell.Management\\Get-Item -LiteralPath $LiteralPath -Force
    if($item.FullName -eq UNSAFE){
        return [pscustomobject]@{FullName=$item.FullName;Name=$item.Name;PSIsContainer=$false;
            Attributes=[IO.FileAttributes]::ReparsePoint;Length=$item.Length}
    }
    return $item
}
""".replace("UNSAFE", ps_quote(unsafe))
        result = self.manifest(before)
        self.assertFalse(result["ok"], result)
        self.assertIn("reparse", result["error"].lower())
        self.assertNotIn("entries", result)

    def test_freeze_and_trusted_modes_are_mutually_exclusive_before_preparation(self):
        missing = self.folder / "never-created-baseline.bin"
        process = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-File",
            str(HERE / "start-flash-session.ps1"), "-TrustedLocal", "-FreezePythonRuntime",
            "-PrepareOnly", "-Baseline", str(missing)], capture_output=True, text=True,
            encoding="utf-8", errors="replace",
            timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.assertNotEqual(process.returncode, 0)
        self.assertRegex(process.stderr.lower(), "mutually exclusive|cannot.*combin|choose.*either|cannot.*together")
        self.assertFalse(missing.exists())

    def test_valid_snapshot_destination_stays_beneath_its_root(self):
        result = powershell(self.folder,
            "$destination=Get-FM1SnapshotDestination -Root " + ps_quote(self.folder) +
            " -Relative 'runtime\\Lib\\os.py'\n"
            "@{ok=$true;destination=$destination} | ConvertTo-Json -Compress")
        self.assertTrue(result["ok"], result)
        self.assertEqual(Path(result["destination"]).resolve(), (self.folder / "runtime/Lib/os.py").resolve())

    def test_snapshot_destinations_reject_windows_alias_and_escape_forms(self):
        unsafe = ("", "../escape.py", r"runtime\..\escape.py", r"runtime\.\python.exe",
                  r"C:\mutable\python.exe", r"\Windows\python.exe", r"\\server\share\python.exe",
                  r"runtime\python.exe:payload", "runtime\\python.exe.", "runtime\\python.exe ",
                  "runtime//python.exe", "runtime\\", r"runtime\CON", r"runtime\NUL.dll",
                  r"runtime\LPT1.py", r"runtime\COM9.bin", "runtime\\bad\nname.py")
        result = powershell(self.folder,
            "$checks=@(foreach($relative in $testConfig.paths){\n"
            "try { $null=Get-FM1SnapshotDestination -Root " + ps_quote(self.folder) +
            " -Relative $relative; @{relative=$relative;rejected=$false} }\n"
            "catch { @{relative=$relative;rejected=$true;error=$_.Exception.Message} }\n"
            "})\n@{ok=$true;checks=$checks} | ConvertTo-Json -Compress -Depth 20",
            {"paths": unsafe})
        self.assertTrue(result["ok"], result)
        self.assertEqual(len(result["checks"]), len(unsafe))
        for check in result["checks"]:
            self.assertTrue(check["rejected"], check)
            self.assertIn("snapshot path", check["error"].lower())


@unittest.skipUnless(sys.platform == "win32", "Windows owner/DACL contract")
class ReadOnlyAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fm1-runtime-acl-")
        self.folder = Path(self.temporary.name)
        self.tree = self.folder / "runtime"
        self.tree.mkdir()
        self.child = self.tree / "python.exe"
        self.child.write_bytes(b"synthetic interpreter; never executed")

    def tearDown(self):
        self.temporary.cleanup()

    def audited(self, unsafe_owner=False, unsafe_write=False, deny_write=False):
        body = """
function Get-Acl {
    param([string]$LiteralPath)
    $acl=[Security.AccessControl.DirectorySecurity]::new()
    $owner=[Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $user=[Security.Principal.SecurityIdentifier]::new('S-1-5-11')
    if($LiteralPath -eq CHILD -and OWNER){$owner=$user}
    $acl.SetOwner($owner)
    $acl.SetAccessRuleProtection($true,$false)
    $rights=[Security.AccessControl.FileSystemRights]::ReadAndExecute
    if($LiteralPath -eq CHILD -and WRITE){$rights=[Security.AccessControl.FileSystemRights]::FullControl}
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($user,$rights,'Allow'))
    if(DENY){$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($user,'Write','Deny'))}
    return $acl
}
Assert-FM1ReadOnlyTree -Root ROOT
[IO.File]::WriteAllText(MARKER,'audit passed')
@{ok=$true} | ConvertTo-Json -Compress
"""
        for key, value in {"CHILD": ps_quote(self.child), "ROOT": ps_quote(self.tree),
                           "MARKER": ps_quote(self.folder / "after-audit.txt"),
                           "OWNER": "$true" if unsafe_owner else "$false",
                           "WRITE": "$true" if unsafe_write else "$false",
                           "DENY": "$true" if deny_write else "$false"}.items():
            body = body.replace(key, value)
        return powershell(self.folder, body)

    def test_read_execute_access_is_valid_for_non_administrators(self):
        self.assertTrue(self.audited()["ok"])
        self.assertTrue((self.folder / "after-audit.txt").exists())

    def test_non_administrative_child_owner_blocks_execution_boundary(self):
        result = self.audited(unsafe_owner=True)
        self.assertFalse(result["ok"], result)
        self.assertIn("owner", result["error"].lower())
        self.assertFalse((self.folder / "after-audit.txt").exists())

    def test_user_writable_child_blocks_execution_boundary(self):
        result = self.audited(unsafe_write=True)
        self.assertFalse(result["ok"], result)
        self.assertIn("writable", result["error"].lower())
        self.assertFalse((self.folder / "after-audit.txt").exists())

    def test_deny_write_is_not_mistaken_for_allowed_write(self):
        self.assertTrue(self.audited(deny_write=True)["ok"])

    def test_real_user_owned_temporary_copy_is_not_claimed_immutable(self):
        result = powershell(self.folder,
            "Assert-FM1ReadOnlyTree -Root " + ps_quote(self.tree) +
            "\n@{ok=$true} | ConvertTo-Json -Compress")
        self.assertFalse(result["ok"], result)
        self.assertIn("owner", result["error"].lower())


class WorkerRuntimeModeTests(unittest.TestCase):
    def test_frozen_setting_defaults_to_false_and_requires_an_exact_boolean(self):
        with tempfile.TemporaryDirectory(prefix="fm1-runtime-descriptor-") as folder:
            root = Path(folder)
            self.assertFalse(worker.frozen_runtime(root))
            descriptor = root / "session.json"
            descriptor.write_text("{}", encoding="utf-8")
            self.assertFalse(worker.frozen_runtime(root))
            for value in (False, True):
                descriptor.write_text(json.dumps({"freeze_python_runtime": value}), encoding="utf-8")
                self.assertIs(worker.frozen_runtime(root), value)
            for value in (None, 0, 1, "true", {}, []):
                with self.subTest(value=value):
                    descriptor.write_text(json.dumps({"freeze_python_runtime": value}), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        worker.frozen_runtime(root)

    def test_strict_trusted_and_frozen_child_modes_keep_their_distinct_contracts(self):
        for frozen, trusted, expected in (
            (False, False, ["-I", "-S", "-u"]),
            (False, True, ["-u"]),
            (True, False, ["-I", "-S", "-B", "-u"])):
            with self.subTest(frozen=frozen, trusted=trusted), \
                    patch.object(worker, "frozen_runtime", return_value=frozen), \
                    patch.object(worker, "trusted_local", return_value=trusted):
                self.assertEqual(worker.python_flags(), expected)
        with patch.object(worker, "frozen_runtime", return_value=True), \
                patch.object(worker, "trusted_local", return_value=True):
            with self.assertRaisesRegex(ValueError, "Conflicting runtime modes"):
                worker.python_flags()

    def test_requests_cannot_select_or_override_the_frozen_runtime(self):
        for value in (True, False, "C:\\mutable\\python.exe"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                worker.schema({"op": "status", "freeze_python_runtime": value})

    def test_cli_child_uses_current_frozen_executable_and_isolated_no_cache_flags(self):
        with tempfile.TemporaryDirectory(prefix="fm1-runtime-child-") as folder:
            root = Path(folder)
            executable = root / "runtime/python.exe"
            calls = []
            def fake_run(args, **kwargs):
                calls.append((args, kwargs))
                kwargs["stdout"].write("offline child fixture\n")
                return subprocess.CompletedProcess(args, 0)
            with patch.object(worker, "ROOT", root), \
                    patch.object(worker.sys, "executable", str(executable)), \
                    patch.object(worker, "frozen_runtime", return_value=True), \
                    patch.object(worker, "trusted_local", return_value=False), \
                    patch.object(worker.subprocess, "run", side_effect=fake_run):
                worker.execute_cli("synthetic-device", ["offline-fixture"], root / "child.log")
            self.assertEqual(len(calls), 1)
            args, options = calls[0]
            self.assertEqual(args[:5], [str(executable), "-I", "-S", "-B", "-u"])
            self.assertEqual(options["cwd"], root)
            self.assertTrue(options["check"])


@unittest.skipUnless(sys.platform == "win32" and os.environ.get("FM1_FROZEN_RUNTIME_SMOKE") == "1",
                     "explicit offline copied-runtime smoke")
class RelocatedRuntimeSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="fm1-runtime-relocation-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = Path(cls.temporary.name)
        cls.session = cls.folder / "session"
        cls.session.mkdir()
        dependencies = {}
        for name in DEPENDENCIES:
            spec = importlib.util.find_spec(name)
            if spec is None or spec.origin is None:
                raise AssertionError("installed dependency unavailable: " + name)
            dependencies[name] = Path(spec.origin).parent
        result = snapshot(cls.folder, Path(sys.executable).parent, dependencies)
        if not result["ok"]:
            raise AssertionError(result)
        for entry in result["entries"]:
            target = cls.session / entry["relative"]
            if not target.resolve().is_relative_to(cls.session.resolve()):
                raise AssertionError("snapshot destination escaped temporary session")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(entry["source"], target)
            if digest(target.read_bytes()) != entry["sha256"]:
                raise AssertionError("source changed while copying runtime fixture")
        cls.helper = cls.session / "tools/jieli-wl82/frozen_python_runtime.py"
        cls.helper.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(PY_HELPER, cls.helper)
        cls.python = cls.session / "runtime/python.exe"
        (cls.python.parent / "openssl-empty.cnf").write_bytes(b"")
        for name in ("openssl-modules", "openssl-engines"):
            (cls.python.parent / name).mkdir()

    def probe(self, python=None, flags=("-I", "-S", "-B", "-u"), env=None, cwd=None,
              native_overrides=None):
        environment = dict(os.environ if env is None else env)
        environment.update(OPENSSL_CONF=str(self.python.parent / "openssl-empty.cnf"),
                           OPENSSL_MODULES=str(self.python.parent / "openssl-modules"),
                           OPENSSL_ENGINES=str(self.python.parent / "openssl-engines"))
        environment.pop("OPENSSL_CONF_INCLUDE", None)
        environment.update(native_overrides or {})
        return subprocess.run([str(python or self.python), *flags, str(self.helper),
            "--self-test", str(self.session)], capture_output=True, text=True,
            timeout=60, cwd=cwd or self.folder, env=environment,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

    def test_relocated_interpreter_and_child_use_only_snapshot_paths(self):
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertTrue(data["ok"])
        self.assertFalse(data["device_io"])
        for process in (data, data["child"]):
            self.assertEqual(Path(process["executable"]).resolve(), self.python.resolve())
            self.assertEqual(Path(process["prefix"]).resolve(), self.python.parent.resolve())
            self.assertTrue(process["isolated"])
            self.assertTrue(process["no_site"])
            self.assertTrue(process["no_bytecode"])
            for path in process["paths"]:
                self.assertTrue(Path(path).resolve().is_relative_to(self.session.resolve()), path)
        for name in DEPENDENCIES:
            self.assertTrue(Path(data["modules"][name]).resolve().is_relative_to((self.session / "lib").resolve()))
        system = Path(os.environ["WINDIR"]).resolve()
        allowed_native = (self.session.resolve(), *(system / name for name in ("System32", "SysWOW64", "WinSxS")))
        for process in (data, data["child"]):
            for filename in process["native_modules"]:
                self.assertTrue(any(Path(filename).resolve().is_relative_to(root) for root in allowed_native), filename)
        for name in ("_ctypes", "_ssl", "_hashlib", "zlib", "_sqlite3", "_decimal"):
            if name in data["modules"]:
                self.assertTrue(Path(data["modules"][name]).resolve().is_relative_to(self.python.parent.resolve()))
            else:
                # Windows' installed CPython may build zlib into python311.dll;
                # it has no __file__, and the loaded DLL proof covers its origin.
                self.assertIn(name, sys.builtin_module_names)
        self.assertEqual(list(self.session.rglob("__pycache__")), [])

    def test_mutable_environment_and_working_directory_cannot_supply_imports(self):
        poison = self.folder / "mutable-user-path"
        poison.mkdir(exist_ok=True)
        marker = self.folder / "mutable-import-executed.txt"
        (poison / "sitecustomize.py").write_text("from pathlib import Path\nPath(" + repr(str(marker)) +
            ").write_text('escaped')\n", encoding="utf-8")
        (poison / "yaml.py").write_text("raise RuntimeError('mutable dependency')\n", encoding="utf-8")
        env = dict(os.environ, PYTHONHOME=str(poison), PYTHONPATH=str(poison), PYTHONUSERBASE=str(poison))
        result = self.probe(env=env, cwd=poison)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(marker.exists())
        data = json.loads(result.stdout)
        self.assertNotIn(str(poison), json.dumps(data))

    def test_external_interpreter_cannot_claim_the_frozen_runtime(self):
        result = self.probe(python=sys.executable)
        self.assertNotEqual(result.returncode, 0)

    def test_mutable_openssl_configuration_and_provider_paths_fail_closed(self):
        for name in ("OPENSSL_CONF", "OPENSSL_MODULES", "OPENSSL_ENGINES", "OPENSSL_CONF_INCLUDE"):
            with self.subTest(name=name):
                result = self.probe(native_overrides={name: str(self.folder / "mutable-native-source")})
                self.assertNotEqual(result.returncode, 0)
                self.assertRegex(result.stderr, "Native environment escaped|configuration includes")

    def test_missing_isolation_flags_fail_closed(self):
        try:
            for flags in (("-S", "-B", "-u"), ("-I", "-B", "-u"), ("-I", "-S", "-u")):
                with self.subTest(flags=flags):
                    result = self.probe(flags=flags)
                    self.assertNotEqual(result.returncode, 0)
        finally:
            # The deliberately missing -B command may write stdlib caches before
            # the helper rejects it. Keep that negative fixture from polluting
            # the separate valid-command proof; only our temp session is touched.
            for cache in self.session.rglob("__pycache__"):
                if not cache.resolve().is_relative_to(self.session.resolve()):
                    raise AssertionError("temporary cache escaped owned session")
                shutil.rmtree(cache)


if __name__ == "__main__":
    unittest.main()
