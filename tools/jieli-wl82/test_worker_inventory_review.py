"""Independent offline regressions. No CIM, USB, serial or protected broker access."""
import base64
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import flash_session_worker as worker


TARGET = {'DeviceID': r'\\.\PHYSICALDRIVE3',
          'PNPDeviceID': r'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00&REV_1.00\OFFLINE_TEST'}


class RecordingInput:
    def __init__(self, stream, events):
        self.stream = stream
        self.events = events

    def write(self, text):
        self.events.append(('query', int(text.strip())))
        return self.stream.write(text)

    def __getattr__(self, name):
        return getattr(self.stream, name)


class InventoryFixture(unittest.TestCase):
    def setUp(self):
        worker.close_inventory()
        self.addCleanup(worker.close_inventory)
        self.actual_popen = subprocess.Popen
        self.processes = []
        self.events = []
        self.addCleanup(self.reap_children)

    def reap_children(self):
        for process in self.processes:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            for stream in (process.stdin, process.stdout):
                if stream is not None and not stream.closed:
                    stream.close()

    def child_factory(self, responses=None, mode='normal'):
        # Substitute an isolated Python process for PowerShell. The real pipe
        # transport, reader thread, deadlines, JSON parser and disk() run.
        payload = json.dumps(responses or {})
        script = '\n'.join([
            'import json, sys',
            'responses = json.loads(' + repr(payload) + ')',
            'target = json.loads(' + repr(json.dumps(TARGET)) + ')',
            'mode = ' + repr(mode),
            'for line in sys.stdin:',
            '    sequence = int(line)',
            '    value = responses.get(str(sequence), {"id": sequence, "ok": True, "items": [target]})',
            '    if mode == "stale": value["id"] = 1',
            '    sys.stdout.write(json.dumps(value) + "\\n")',
            '    sys.stdout.flush()',
        ])
        def child(args, **kwargs):
            self.assertEqual(args[0], worker.PS)
            self.assertIn('-NoProfile', args)
            self.assertIn('-NonInteractive', args)
            self.assertEqual(kwargs['stdin'], subprocess.PIPE)
            self.assertEqual(kwargs['stdout'], subprocess.PIPE)
            self.assertEqual(kwargs['stderr'], subprocess.DEVNULL)
            self.assertEqual(kwargs['encoding'], 'utf-8')
            self.assertEqual(kwargs['errors'], 'strict')
            process = self.actual_popen([sys.executable, '-I', '-S', '-u', '-c', script], **kwargs)
            process.stdin = RecordingInput(process.stdin, self.events)
            self.processes.append(process)
            return process
        return child

    def assert_reaped(self):
        self.assertTrue(self.processes)
        for process in self.processes:
            self.assertIsNotNone(process.poll())
            self.assertTrue(process.stdin.closed)
            self.assertTrue(process.stdout.closed)


class DiskProtocolReviewTests(InventoryFixture):
    def test_disk_validates_fresh_results_instead_of_caching_first_identity(self):
        changed = dict(TARGET, DeviceID=r'\\.\PHYSICALDRIVE4')
        responses = {2: {'id': 2, 'ok': True, 'items': [changed]}}
        with patch.object(worker.subprocess, 'Popen', side_effect=self.child_factory(responses)) as spawn:
            self.assertEqual(worker.disk(), TARGET)
            self.assertEqual(worker.disk(), changed)
            self.assertEqual(spawn.call_count, 1)
        worker.close_inventory()
        self.assertEqual(self.events, [('query', 1), ('query', 2)])
        self.assert_reaped()

    def test_stale_sequence_closes_process_without_automatic_restart(self):
        inventory = worker.DiskInventory(timeout=2)
        with patch.object(worker.subprocess, 'Popen', side_effect=self.child_factory(mode='stale')) as spawn:
            self.assertEqual(inventory.query(), [TARGET])
            with self.assertRaisesRegex(ValueError, 'Invalid disk inventory response'):
                inventory.query()
            with self.assertRaisesRegex(ValueError, 'closed'):
                inventory.query()
            self.assertEqual(spawn.call_count, 1)
        self.assert_reaped()


class WorkerInventoryIntegrationReviewTests(InventoryFixture):
    def setUp(self):
        super().setUp()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = bytes(worker.SIZE)
        self.memory = bytearray(self.base)
        self.writes = []
        self.io_states = []
        (self.root / 'baseline.bin').write_bytes(self.base)
        self.initial = dict(baseline_file='baseline.bin', baseline_sha256=worker.sha(self.base),
                            blocked=False, reset_pending=False, needs_observation=False, operation='idle')
        (self.root / 'state.json').write_text(json.dumps(self.initial), encoding='utf-8')
        helper = self.root / 'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
        helper.parent.mkdir(parents=True)
        self.helper = b'Offline pinned helper; never uploaded'
        helper.write_bytes(self.helper)
        self.actual_session = worker.Session
        self.image = self.make_image([0x5000, 0x6000])

    def make_image(self, app_sectors):
        image = bytearray(self.base)
        image[0x4000] = 1
        image[worker.APP_START] = 2
        for address in app_sectors:
            image[address] = 3
        return bytes(image)

    def request(self, operation='flash'):
        return dict(op=operation, image=base64.b64encode(self.image).decode(),
                    sha256=worker.sha(self.image), baseline_sha256=worker.sha(self.base))

    def cli(self, device, commands, log, mode='write'):
        self.assertEqual(device, TARGET['DeviceID'])
        self.events.append(('cli', tuple(commands), mode))
        state = self.actual_session(self.root).state
        self.io_states.append(state)
        self.assertTrue(state['blocked'])
        self.assertFalse(state['reset_pending'])
        if mode == 'load':
            self.assertEqual(commands, ['help'])
            return
        self.assertEqual(mode, 'write')
        for command in commands:
            bits = command.split(' ', 3)
            address = int(bits[1])
            if bits[0] == 'write':
                self.writes.append(address)
                self.memory[address:address + 4096] = Path(' '.join(bits[2:])).read_bytes()
            elif bits[0] == 'read':
                size = int(bits[2])
                Path(bits[3]).write_bytes(self.memory[address:address + size])
            else:
                self.fail('Unexpected device command: ' + bits[0])

    def run_main(self, request, responses=None, mode='normal'):
        input_path = self.root / 'request.json'
        input_path.write_text(json.dumps(request), encoding='utf-8')
        captured = io.StringIO()
        with patch.object(worker, 'ROOT', self.root), \
             patch.object(worker, 'HELPER_SHA', worker.sha(self.helper)), \
             patch.object(worker, 'trusted_local', return_value=True), \
             patch.object(worker, 'Session', side_effect=lambda root=self.root: self.actual_session(root)), \
             patch.object(worker, 'execute_cli', side_effect=self.cli), \
             patch.object(worker, 'environment_info', return_value={'device_io': False}), \
             patch.object(worker, 'serial_action', return_value={'diagnostic_only': True}), \
             patch.object(worker.sys, 'argv', ['offline-worker', str(input_path)]), \
             patch.object(worker.sys, 'path', list(sys.path)), \
             patch.object(worker.subprocess, 'Popen', side_effect=self.child_factory(responses, mode)) as spawn, \
             contextlib.redirect_stdout(captured):
            worker.main()
        return json.loads(captured.getvalue().splitlines()[-1]), spawn.call_count

    def assert_failed_closed(self):
        state = self.actual_session(self.root).state
        self.assertTrue(state['blocked'])
        self.assertFalse(state['reset_pending'])
        self.assertEqual(state['baseline_sha256'], worker.sha(self.base))
        self.assertEqual((self.root / 'baseline.bin').read_bytes(), self.base)
        self.assertEqual(state['failed_operation'], 'flash')
        self.assertEqual(state['flash_attempt']['status'], 'failed')

    def test_139_sector_flash_uses_one_child_and_282_fresh_queries(self):
        self.image = self.make_image(range(0x5000, 0x8f000, 4096))
        result, starts = self.run_main(self.request())
        self.assertTrue(result['ok'], result)
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, list(range(0x5000, 0x8f000, 4096)) + [0x4000])
        queries = [event[1] for event in self.events if event[0] == 'query']
        self.assertEqual(queries, list(range(1, 283)))
        cli_events = [event for event in self.events if event[0] == 'cli']
        self.assertEqual(len(cli_events), 141)
        state = self.actual_session(self.root).state
        self.assertFalse(state['blocked'])
        self.assertTrue(state['reset_pending'])
        self.assertEqual(state['baseline_sha256'], worker.sha(self.image))
        receipt = json.loads((self.root / state['flash_attempt']['run'] / 'deployment.json').read_text())
        self.assertEqual(receipt['full_readback_count'], 1)
        self.assertEqual(len(receipt['verified_sectors']), 139)
        self.assertIsNone(worker._inventory)
        self.assert_reaped()

    def test_every_sector_and_final_read_have_fresh_queries_before_and_after(self):
        result, starts = self.run_main(self.request())
        self.assertTrue(result['ok'], result)
        kinds = [event[0] for event in self.events]
        self.assertEqual(kinds, ['query', 'cli', 'query'] + ['query', 'cli', 'query'] * 4)
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, [0x5000, 0x6000, 0x4000])
        self.assert_reaped()

    def test_changed_identity_before_first_sector_prevents_all_writes_and_latches(self):
        changed = dict(TARGET, DeviceID=r'\\.\PHYSICALDRIVE8')
        result, starts = self.run_main(self.request(), {3: {'id': 3, 'ok': True, 'items': [changed]}})
        self.assertFalse(result['ok'])
        self.assertIn('during write/verify', result['error'])
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, [])
        self.assert_failed_closed()
        self.assert_reaped()

    def test_changed_identity_after_sector_prevents_remaining_writes_and_baseline_promotion(self):
        changed = dict(TARGET, PNPDeviceID=TARGET['PNPDeviceID'] + '_CHANGED')
        result, starts = self.run_main(self.request(), {4: {'id': 4, 'ok': True, 'items': [changed]}})
        self.assertFalse(result['ok'])
        self.assertIn('after write/verify', result['error'])
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, [0x5000])
        self.assert_failed_closed()
        self.assert_reaped()

    def test_changed_pnp_identity_after_final_full_read_prevents_baseline_promotion(self):
        changed = dict(TARGET, PNPDeviceID=TARGET['PNPDeviceID'] + '_CHANGED')
        result, starts = self.run_main(self.request(), {10: {'id': 10, 'ok': True, 'items': [changed]}})
        self.assertFalse(result['ok'])
        self.assertIn('after write/verify', result['error'])
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, [0x5000, 0x6000, 0x4000])
        commands = [event[1] for event in self.events if event[0] == 'cli'][-1]
        self.assertEqual(len(commands), 1)
        self.assertTrue(commands[0].startswith('read 0 ' + str(worker.SIZE) + ' '))
        self.assertEqual([event[1] for event in self.events if event[0] == 'query'], list(range(1, 11)))
        self.assert_failed_closed()
        self.assertIsNone(worker._inventory)
        self.assert_reaped()

    def test_invalid_inventory_after_sector_latches_and_blocks_automatic_retry(self):
        result, starts = self.run_main(self.request(), {4: {'id': 4, 'ok': True, 'items': []}})
        self.assertFalse(result['ok'])
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, [0x5000])
        self.assert_failed_closed()
        repeated, restarts = self.run_main(self.request())
        self.assertFalse(repeated['ok'])
        self.assertIn('latched', repeated['error'])
        self.assertEqual(restarts, 0)
        self.assertEqual(self.writes, [0x5000])
        self.assert_reaped()

    def test_stale_protocol_during_flash_latches_without_write_or_restart(self):
        result, starts = self.run_main(self.request(), mode='stale')
        self.assertFalse(result['ok'])
        self.assertIn('Invalid disk inventory response', result['error'])
        self.assertEqual(starts, 1)
        self.assertEqual(self.writes, [])
        self.assert_failed_closed()
        self.assert_reaped()

    def test_offline_and_serial_diagnostics_start_no_inventory_child(self):
        for request in ({'op': 'status'}, {'op': 'environment'}, self.request('plan'), {'op': 'serial_status'}):
            with self.subTest(operation=request['op']):
                result, starts = self.run_main(request)
                self.assertTrue(result['ok'], result)
                self.assertEqual(starts, 0)
        self.assertEqual(self.processes, [])
        self.assertEqual(self.writes, [])

    def test_invalid_candidate_is_rejected_before_inventory_launch_or_latch(self):
        request = dict(self.request(), sha256='0' * 64)
        result, starts = self.run_main(request)
        self.assertFalse(result['ok'])
        self.assertEqual(starts, 0)
        self.assertEqual(self.actual_session(self.root).state, self.initial)
        self.assertEqual(self.writes, [])


if __name__ == '__main__':
    unittest.main()
