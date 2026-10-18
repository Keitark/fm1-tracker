"""Synthetic adopted-read target guard tests; no helper, disk or device access."""
import base64
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import deploy_v14_smb1_via_cli as writer
import flash_session_worker as worker


class VerifiedReadBaselineGuardTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fm1-read-target-guard-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.base = bytes(worker.SIZE)
        image = bytearray(self.base)
        image[0x4000], image[0x5000], image[0x6000] = 1, 2, 3
        self.image = bytes(image)
        self.baseline_target = {'DeviceID': 'fixture-drive-3', 'PNPDeviceID': 'fixture-unit-A'}
        self.target = {'DeviceID': 'fixture-drive-8', 'PNPDeviceID': 'fixture-unit-B'}
        self.initial = {'baseline_file': 'baseline.bin', 'baseline_sha256': worker.sha(self.base),
            'baseline_target': dict(self.baseline_target), 'blocked': False, 'reset_pending': False,
            'needs_observation': False, 'loader_running': False, 'operation': 'idle',
            'target': dict(self.baseline_target)}
        (self.root / 'baseline.bin').write_bytes(self.base)
        self.write_state(self.initial)
        self.helper = self.root / 'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
        self.helper.parent.mkdir(parents=True)
        self.helper_bytes = b'Synthetic helper fixture; never uploaded to hardware'
        self.helper.write_bytes(self.helper_bytes)

    def write_state(self, state):
        (self.root / 'state.json').write_text(json.dumps(state), encoding='utf-8')

    def state(self):
        return json.loads((self.root / 'state.json').read_text(encoding='utf-8'))

    def request(self, operation):
        return {'op': operation, 'image': base64.b64encode(self.image).decode(),
                'sha256': worker.sha(self.image), 'baseline_sha256': worker.sha(self.base)}

    def failed_attempt(self):
        """Real worker receipt contract, with a synthetic failed first sector."""
        run = self.root / 'runs/original-failure'
        run.mkdir(parents=True)
        _, sectors = worker.candidate(self.request('plan'), self.base)
        attempt = {'status': 'failed', 'run': 'runs/original-failure',
            'sha256': worker.sha(self.image), 'baseline_sha256': worker.sha(self.base),
            'sectors': sectors, 'target': dict(self.baseline_target), 'retry_of': None}
        receipt = {'status': 'failed', 'baseline_sha256': worker.sha(self.base),
            'candidate_sha256': worker.sha(self.image), 'verified_sectors': [],
            'current_sector': hex(sectors[0]), 'write_command_attempted': True}
        (run / 'attempt.json').write_text(json.dumps(attempt), encoding='utf-8')
        (run / 'deployment.json').write_text(json.dumps(receipt), encoding='utf-8')
        state = dict(self.initial, blocked=True, failed_operation='flash',
                     error='Synthetic interrupted write', operation='flash', flash_attempt=attempt)
        self.write_state(state)
        return attempt, {path.name: path.read_bytes() for path in run.iterdir()}

    def assert_identity_rejected_before_helper(self, operation):
        before = self.state()
        original_read = Path.read_bytes
        helper_reads = []
        def read(path):
            if path == self.helper:
                helper_reads.append(path)
                raise AssertionError('Identity must reject before reading helper')
            return original_read(path)
        with patch.object(worker, 'ROOT', self.root), \
             patch.object(worker, 'disk', return_value=self.target) as disk, \
             patch.object(worker, 'execute_cli', side_effect=AssertionError('No helper or device command')) as cli, \
             patch.object(writer, 'deploy', side_effect=AssertionError('No deployment')) as deploy, \
             patch.object(Path, 'read_bytes', read):
            with self.assertRaisesRegex(ValueError, 'Verified readback baseline target identity changed'):
                worker.Session(self.root).handle(self.request(operation))
        disk.assert_called_once_with();cli.assert_not_called();deploy.assert_not_called()
        self.assertEqual(helper_reads, [])
        state = self.state()
        self.assertTrue(state['blocked']);self.assertEqual(state['failed_operation'], 'flash')
        self.assertFalse(state['reset_pending'])
        self.assertEqual(state['baseline_target'], self.baseline_target)
        self.assertEqual(state['baseline_file'], before['baseline_file'])
        self.assertEqual(state['baseline_sha256'], before['baseline_sha256'])
        self.assertEqual((self.root / 'baseline.bin').read_bytes(), self.base)
        self.assertEqual(state.get('flash_attempt'), before.get('flash_attempt'))
        failure_run = Path(state['last_run'])
        self.assertFalse((failure_run / 'attempt.json').exists())
        self.assertFalse((failure_run / 'deployment.json').exists())
        failure = json.loads((failure_run / 'failure.json').read_text(encoding='utf-8'))
        self.assertEqual(failure['operation'], operation)
        self.assertIn('baseline target identity changed', failure['error'])
        return state

    def test_normal_flash_rejects_another_pnp_before_attempt_or_helper_and_latches(self):
        state = self.assert_identity_rejected_before_helper('flash')
        self.assertNotIn('flash_attempt', state)
        with patch.object(worker, 'disk', side_effect=AssertionError('No automatic retry')):
            with self.assertRaisesRegex(ValueError, 'Session latched'):
                worker.Session(self.root).handle(self.request('flash'))

    def test_retry_rejects_another_pnp_and_preserves_original_failure_evidence(self):
        attempt, evidence = self.failed_attempt()
        state = self.assert_identity_rejected_before_helper('retry_flash')
        self.assertEqual(state['flash_attempt'], attempt)
        for name, contents in evidence.items():
            self.assertEqual((self.root / attempt['run'] / name).read_bytes(), contents)

    def test_recovery_rejects_another_pnp_before_read_or_upload_and_preserves_original_receipt(self):
        attempt, evidence = self.failed_attempt()
        state = self.assert_identity_rejected_before_helper('recover_flash')
        self.assertEqual(state['flash_attempt'], attempt)
        self.assertNotIn('recovery_preflight_attempt', state)
        for name, contents in evidence.items():
            self.assertEqual((self.root / attempt['run'] / name).read_bytes(), contents)

    def simulated_write(self):
        memory = bytearray(self.base)
        calls = []
        def cli(device, commands, log, mode='write'):
            calls.append((device, list(commands), mode))
            if mode != 'write':
                return
            for command in commands:
                fields = command.split(' ', 3)
                address = int(fields[1])
                if fields[0] == 'write':
                    memory[address:address + 4096] = Path(' '.join(fields[2:])).read_bytes()
                elif fields[0] == 'read':
                    length = int(fields[2])
                    Path(fields[3]).write_bytes(memory[address:address + length])
                else:
                    raise AssertionError('Unexpected synthetic device command')
        with patch.object(worker, 'ROOT', self.root), \
             patch.object(worker, 'HELPER_SHA', worker.sha(self.helper_bytes)), \
             patch.object(worker, 'disk', return_value=self.target), \
             patch.object(worker, 'execute_cli', side_effect=cli), \
             contextlib.redirect_stdout(io.StringIO()):
            result = worker.Session(self.root).handle(self.request('flash'))
        self.assertEqual(bytes(memory), self.image)
        self.assertEqual(result['result']['status'], 'written_and_readback_verified')
        self.assertEqual(result['result']['full_readback_count'], 1)
        self.assertEqual(calls[0], (self.target['DeviceID'], ['help'], 'load'))
        self.assertTrue(all(device == self.target['DeviceID'] for device, _, _ in calls))
        self.assertNotIn('reset', [mode for _, _, mode in calls])
        state = self.state()
        self.assertFalse(state['blocked']);self.assertTrue(state['reset_pending'])
        self.assertTrue(state['needs_observation'])
        self.assertEqual(state['target'], self.target)
        self.assertEqual(state['baseline_sha256'], worker.sha(self.image))
        return state

    def test_same_pnp_allows_normal_write_after_drive_number_changes(self):
        self.target['PNPDeviceID'] = self.baseline_target['PNPDeviceID']
        state = self.simulated_write()
        self.assertNotEqual(state['target']['DeviceID'], self.baseline_target['DeviceID'])
        self.assertEqual(state['baseline_target'], self.baseline_target)

    def test_legacy_baseline_without_target_keeps_normal_write_behavior(self):
        state = dict(self.initial);state.pop('baseline_target');self.write_state(state)
        completed = self.simulated_write()
        self.assertNotIn('baseline_target', completed)
        self.assertEqual(completed['target']['PNPDeviceID'], 'fixture-unit-B')

    def test_legacy_retry_still_requires_original_failed_target_identity(self):
        attempt, _ = self.failed_attempt()
        state = self.state();state.pop('baseline_target');self.write_state(state)
        with patch.object(worker, 'disk', return_value=self.target), \
             patch.object(worker, 'execute_cli', side_effect=AssertionError('No helper or device command')) as cli:
            with self.assertRaisesRegex(ValueError, 'Retry target identity changed'):
                worker.Session(self.root).handle(self.request('retry_flash'))
        cli.assert_not_called()
        self.assertEqual(self.state()['flash_attempt'], attempt)
        self.assertTrue(self.state()['blocked'])

    def test_running_read_helper_gate_precedes_disk_selection_for_all_write_paths(self):
        for operation in ('flash', 'retry_flash', 'recover_flash'):
            with self.subTest(operation=operation):
                if operation == 'flash':
                    self.write_state(self.initial)
                else:
                    if (self.root / 'runs/original-failure').exists():
                        # Preserve our synthetic evidence; only rebuild state.
                        old = self.state()
                        self.write_state(dict(self.initial, blocked=True, failed_operation='flash',
                            operation='flash', error='Synthetic interrupted write', flash_attempt=old['flash_attempt']))
                    else:
                        self.failed_attempt()
                state = self.state();state['loader_running'] = True;self.write_state(state)
                before = (self.root / 'state.json').read_bytes()
                runs = set((self.root / 'runs').glob('*')) if (self.root / 'runs').exists() else set()
                with patch.object(worker, 'disk', side_effect=AssertionError('No disk selection')) as disk, \
                     patch.object(worker, 'execute_cli', side_effect=AssertionError('No helper or device command')) as cli:
                    with self.assertRaisesRegex(ValueError, 'Firmware read left a RAM helper running'):
                        worker.Session(self.root).handle(self.request(operation))
                disk.assert_not_called();cli.assert_not_called()
                self.assertEqual((self.root / 'state.json').read_bytes(), before)
                self.assertEqual(set((self.root / 'runs').glob('*')), runs)


if __name__ == '__main__':
    unittest.main()
