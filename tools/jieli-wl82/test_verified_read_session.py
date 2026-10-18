"""Synthetic file-only migration evidence tests; no UAC or device access."""
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('verified_read_session', HERE / 'verified_read_session.py')
verified = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verified)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class VerifiedReadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.run = self.root / 'runs' / ('a' * 32)
        self.run.mkdir(parents=True)
        self.old = bytes([0x35]) * verified.SIZE
        self.image = bytes([0x57]) * verified.SIZE
        self.target = dict(DeviceID=r'\\.\PHYSICALDRIVE7', PNPDeviceID='SYNTHETIC-UNIT-A')
        self.descriptor = dict(root=str(self.root), server_pid=424242, pipe='synthetic-pipe',
                               owner_sid='S-1-5-21-TEST', trusted_local=False, freeze_python_runtime=True)
        self.receipt = dict(sha256=digest(self.image), size=verified.SIZE, readback_count=2,
                            loader_state='cold', file=str(self.run.relative_to(self.root) / 'firmware.bin'),
                            target=dict(self.target), chip_key_queried=False,
                            flash_array_program_or_erase_commands_sent=False, reset_requested=False,
                            includes_device_configuration=True, loader_online_may_change_nor_protection=True)
        self.state = dict(baseline_file='baseline.bin', baseline_sha256=digest(self.old),
                          operation='idle', blocked=False, reset_pending=False, loader_running=True,
                          last_run=str(self.run), last_result=dict(self.receipt))
        (self.root / 'baseline.bin').write_bytes(self.old)
        (self.run / 'firmware.bin').write_bytes(self.image)
        (self.run / 'readback-2.bin').write_bytes(self.image)
        self.write('session.json', self.descriptor)
        self.write('state.json', self.state)
        self.write('ready.json', dict(ready=True, server_pid=424242))
        self.write('baseline-deployment.json', dict(status='written_and_readback_verified',
                                                    full_readback_count=1,
                                                    full_readback_sha256=digest(self.old)))
        self.write(self.run.relative_to(self.root) / 'firmware.json', self.receipt)
        self.stop()

    def write(self, relative, value):
        (self.root / relative).write_text(json.dumps(value), encoding='utf-8')

    def save_read(self):
        self.state['last_result'] = dict(self.receipt)
        self.write(self.run.relative_to(self.root) / 'firmware.json', self.receipt)
        self.write('state.json', self.state)

    def stop(self, *, structured=False, explicit_quit=True):
        stamp = datetime.now(timezone.utc).isoformat()
        (self.root / 'stopped.txt').write_text(stamp, encoding='utf-8')
        if structured:
            self.write('stopped.json', dict(root=str(self.root), pipe=self.descriptor['pipe'],
                                          server_pid=self.descriptor['server_pid'],
                                          explicit_quit=explicit_quit, stopped_at=stamp))

    def plan(self, **kwargs):
        return verified.plan_verified_read_session(self.root, pid_exists=lambda pid: False, **kwargs)

    def test_exact_double_read_promotes_explicit_image_preserving_loader_and_all_evidence(self):
        before = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        plan = self.plan()
        initial = plan['initial_state']
        self.assertEqual(initial['baseline_sha256'], digest(self.image))
        self.assertTrue(initial['loader_running'])
        self.assertFalse(initial['blocked'])
        self.assertFalse(initial['reset_pending'])
        self.assertEqual(initial['baseline_target'], self.target)
        self.assertEqual(initial['target'], self.target)
        copied = {item['relative'].replace('\\', '/'): item for item in plan['files']}
        self.assertIn('baseline-readback.json', copied)
        self.assertNotIn('baseline-deployment.json', copied)
        self.assertEqual(copied['readback-source/baseline.bin']['sha256'], digest(self.old))
        self.assertIn('readback-source/baseline-deployment.json', copied)
        for item in copied.values():
            self.assertEqual(item['sha256'], digest(Path(item['source']).read_bytes()))
        self.assertEqual(before, {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

    def test_prepare_only_accepts_live_idle_source_without_querying_process(self):
        (self.root / 'stopped.txt').unlink()
        plan = verified.plan_verified_read_session(
            self.root, require_stopped=False,
            pid_exists=lambda pid: self.fail('PrepareOnly must not query/stop the helper'))
        self.assertFalse(plan['source']['stopped_proof'])
        with self.assertRaises(FileNotFoundError):
            self.plan()

    def test_existing_helper_pid_rejects_actual_migration(self):
        with self.assertRaisesRegex(ValueError, 'PID still exists'):
            verified.plan_verified_read_session(self.root, pid_exists=lambda pid: True)

    def test_stopped_marker_must_follow_saved_source_state(self):
        old = (datetime.now(timezone.utc) - timedelta(seconds=10)).isoformat()
        (self.root / 'stopped.txt').write_text(old, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'predates'):
            self.plan()

    def test_structured_marker_requires_same_helper_and_explicit_quit(self):
        self.stop(structured=True)
        self.assertTrue(self.plan()['source']['stopped_proof'])
        self.stop(structured=True, explicit_quit=False)
        with self.assertRaisesRegex(ValueError, 'quit marker'):
            self.plan()
        self.stop(structured=True)
        marker = json.loads((self.root / 'stopped.json').read_text())
        marker['server_pid'] += 1
        self.write('stopped.json', marker)
        with self.assertRaisesRegex(ValueError, 'quit marker'):
            self.plan()

    def test_saved_receipt_cannot_be_replaced_independently(self):
        self.receipt['loader_state'] = 'reuse'
        self.write(self.run.relative_to(self.root) / 'firmware.json', self.receipt)
        with self.assertRaisesRegex(ValueError, 'Saved result differs'):
            self.plan()

    def test_double_images_must_match_receipt_and_each_other(self):
        (self.run / 'readback-2.bin').write_bytes(self.old)
        with self.assertRaisesRegex(ValueError, 'Double-read images'):
            self.plan()
        (self.run / 'readback-2.bin').write_bytes(self.image)
        self.receipt['sha256'] = digest(self.old)
        self.save_read()
        with self.assertRaisesRegex(ValueError, 'Double-read images'):
            self.plan()

    def test_original_baseline_and_deployment_receipt_remain_required(self):
        (self.root / 'baseline.bin').write_bytes(self.image)
        with self.assertRaisesRegex(ValueError, 'Original baseline hash'):
            self.plan()
        (self.root / 'baseline.bin').write_bytes(self.old)
        self.write('baseline-deployment.json', dict(status='failed', full_readback_sha256=digest(self.old)))
        with self.assertRaisesRegex(ValueError, 'verified deployment receipt'):
            self.plan()

    def test_original_single_readback_count_and_legacy_twice_receipt_contract(self):
        self.write('baseline-deployment.json', dict(status='written_and_readback_verified',
                                                    full_readback_count=True,
                                                    full_readback_sha256=digest(self.old)))
        with self.assertRaisesRegex(ValueError, 'single-readback'):
            self.plan()
        # Retain the launcher’s historic twice-read receipt contract.
        self.write('baseline-deployment.json', dict(status='written_and_twice_readback_verified',
                                                    full_readback_sha256=digest(self.old)))
        self.assertEqual(self.plan()['baseline_sha256'], digest(self.image))

    def test_loader_gate_must_not_be_lost_or_forged(self):
        for value in (False, 1, None):
            with self.subTest(value=value):
                self.state['loader_running'] = value
                self.write('state.json', self.state)
                with self.assertRaisesRegex(ValueError, 'retained loader gate'):
                    self.plan()

    def test_failed_reset_observation_or_pending_flash_rejects_migration(self):
        for key, value in (('blocked', True), ('reset_pending', True), ('operation', 'read_firmware'),
                           ('needs_observation', True), ('failed_operation', 'read_firmware'),
                           ('error', 'interrupted'), ('flash_attempt', {'pending': True}),
                           ('recovery_preflight_attempt', {'pending': True})):
            with self.subTest(key=key):
                state = dict(self.state, **{key: value})
                self.write('state.json', state)
                with self.assertRaises(ValueError):
                    self.plan()
        self.write('state.json', dict(self.state, failed_operation='', error=''))
        self.assertTrue(self.plan()['initial_state']['loader_running'])

    def test_receipt_is_a_read_without_key_reset_program_or_erase(self):
        for key, value in (('readback_count', 1), ('size', True), ('chip_key_queried', True),
                           ('reset_requested', True), ('flash_array_program_or_erase_commands_sent', True),
                           ('includes_device_configuration', False), ('loader_state', 'guessed')):
            with self.subTest(key=key):
                original = self.receipt[key]
                self.receipt[key] = value
                self.save_read()
                with self.assertRaises(ValueError):
                    self.plan()
                self.receipt[key] = original

    def test_target_pin_cannot_conflict_with_read_identity(self):
        self.state['target'] = dict(self.target, PNPDeviceID='SYNTHETIC-OTHER-UNIT')
        self.write('state.json', self.state)
        with self.assertRaisesRegex(ValueError, 'target identity differs'):
            self.plan()

    def test_image_path_run_and_descriptor_are_anchored(self):
        self.receipt['file'] = 'baseline.bin'
        self.save_read()
        with self.assertRaisesRegex(ValueError, 'image path mismatch'):
            self.plan()
        self.state['last_run'] = str(self.root / 'runs' / '..' / 'runs' / ('a' * 32))
        self.write('state.json', self.state)
        with self.assertRaisesRegex(ValueError, 'path segment'):
            self.plan()
        self.state['last_run'] = str(self.run)
        self.write('state.json', self.state)
        self.descriptor['root'] = str(self.root / 'another')
        self.write('session.json', self.descriptor)
        with self.assertRaisesRegex(ValueError, 'root mismatch'):
            self.plan()

    def test_ready_pid_must_be_same_recorded_helper(self):
        self.write('ready.json', dict(ready=True, server_pid=424243))
        with self.assertRaisesRegex(ValueError, 'Ready helper identity'):
            self.plan()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows reparse evidence contract')
    def test_junction_read_run_rejected_before_reading_redirected_evidence(self):
        link = self.root / 'runs' / ('b' * 32)
        ps = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
        quoted_link = str(link).replace("'", "''")
        quoted_run = str(self.run).replace("'", "''")
        subprocess.run([ps, '-NoProfile', '-Command',
                        f"$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path '{quoted_link}' -Value '{quoted_run}' | Out-Null"],
                       capture_output=True, timeout=15, check=True)
        self.state['last_run'] = str(link)
        self.write('state.json', self.state)
        with self.assertRaisesRegex(ValueError, 'Reparse evidence'):
            self.plan()

    def test_source_runtime_modes_and_unknown_state_fields_fail_closed(self):
        self.descriptor['trusted_local'] = True
        self.write('session.json', self.descriptor)
        with self.assertRaisesRegex(ValueError, 'Trusted-local'):
            self.plan()
        self.descriptor['trusted_local'] = False
        self.write('session.json', self.descriptor)
        self.state['new_unknown_pending_latch'] = True
        self.write('state.json', self.state)
        with self.assertRaisesRegex(ValueError, 'Unsupported source state'):
            self.plan()

    def test_duplicate_json_field_and_boolean_numeric_alias_rejected(self):
        raw = json.dumps(self.state)
        (self.root / 'state.json').write_text(raw[:-1] + ',"blocked":false}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Duplicate evidence'):
            self.plan()
        self.state['last_result']['chip_key_queried'] = 0
        self.write('state.json', self.state)
        with self.assertRaisesRegex(ValueError, 'Saved result differs'):
            self.plan()

    def test_cli_prepare_only_emits_pinned_plan_without_changing_source(self):
        (self.root / 'stopped.txt').unlink()
        result = subprocess.run([sys.executable, '-I', '-S', '-B', str(HERE / 'verified_read_session.py'),
                                 str(self.root), '--prepare-only'], capture_output=True, text=True,
                                encoding='utf-8', timeout=15, check=True)
        self.assertEqual(json.loads(result.stdout)['baseline_sha256'], digest(self.image))
        self.assertFalse((self.root / 'stopped.txt').exists())

    @unittest.skipUnless(sys.platform == 'win32', 'Windows launcher contract')
    def test_launcher_conflicting_modes_and_overrides_fail_before_source_or_uac(self):
        ps = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
        cases = [
            (['-ResumeFailedSession', 'missing', '-ResumeVerifiedReadSession', 'missing'], 'mutually exclusive'),
            (['-ResumeVerifiedReadSession', 'missing', '-Baseline', 'missing'], 'do not override'),
            (['-ResumeVerifiedReadSession', 'missing', '-BaselineSha256', 'a' * 64], 'do not override'),
            (['-ResumeVerifiedReadSession', 'missing', '-TrustedLocal'], 'TrustedLocal is not supported'),
            (['-ResumeVerifiedReadSession', str(self.root)], 'protected FM-1 session'),
        ]
        for args, expected in cases:
            with self.subTest(args=args):
                result = subprocess.run([ps, '-NoProfile', '-File', str(HERE / 'start-flash-session.ps1'),
                                         '-PrepareOnly', *args], capture_output=True, text=True,
                                        encoding='utf-8', errors='replace', timeout=15)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)


if __name__ == '__main__':
    unittest.main()
