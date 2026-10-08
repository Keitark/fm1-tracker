"""Offline inventory transport and flash guard tests; never open USB devices."""
import base64
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import threading
import unittest
from unittest.mock import patch

import flash_session_worker as worker


TARGET = {
    'DeviceID': r'\\.\PHYSICALDRIVE7',
    'PNPDeviceID': r'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00&REV_1.00\OFFLINE_TEST',
}
OTHER = dict(TARGET, DeviceID=r'\\.\PHYSICALDRIVE8')


def child_script(body):
    return ('import json, sys, time\n'
            'for line in sys.stdin:\n'
            '    sequence = int(line)\n' + textwrap.indent(body, '    '))


def emit_items(expression):
    return ("reply = {'id': sequence, 'ok': True, 'items': " + expression + "}\n"
            "print(json.dumps(reply), flush=True)\n")


class OfflineChildren(unittest.TestCase):
    def setUp(self):
        self.children = []
        worker.close_inventory()

    def tearDown(self):
        worker.close_inventory()
        for process in self.children:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            for stream in (process.stdin, process.stdout):
                if stream is not None and not stream.closed:
                    stream.close()

    @contextlib.contextmanager
    def fake_inventory(self, body, timeout=2):
        # Capture the real constructor before patching: every child is a safe
        # Python response fixture, never PowerShell or any device implementation.
        actual_popen = subprocess.Popen
        def launch(args, **kwargs):
            self.assertEqual(args[0], worker.PS)
            self.assertEqual(args[-1], worker.INVENTORY_SCRIPT)
            self.assertIn('-NoProfile', args)
            self.assertIn('-NonInteractive', args)
            self.assertEqual(kwargs['stderr'], subprocess.DEVNULL)
            process = actual_popen(
                [sys.executable, '-I', '-S', '-u', '-c', child_script(body)], **kwargs)
            self.children.append(process)
            return process
        inventory = worker.DiskInventory(timeout=timeout)
        with patch.object(worker.subprocess, 'Popen', side_effect=launch) as constructor:
            try:
                yield inventory, constructor
            finally:
                inventory.close()


class TransportTests(OfflineChildren):
    def test_one_process_returns_fresh_results_and_is_reaped(self):
        body = emit_items("[dict(" + repr(TARGET) + ", DeviceID=r'\\\\.\\PHYSICALDRIVE' + str(sequence))]")
        with self.fake_inventory(body) as (inventory, constructor):
            first = inventory.query()
            second = inventory.query()
            self.assertNotEqual(first[0]['DeviceID'], second[0]['DeviceID'])
            self.assertEqual(inventory.sequence, 2)
            self.assertEqual(constructor.call_count, 1)
            inventory.close()
            self.assertIsNotNone(inventory.process.poll())
            self.assertTrue(inventory.process.stdin.closed)
            self.assertTrue(inventory.process.stdout.closed)
            with self.assertRaisesRegex(ValueError, 'closed'):
                inventory.query()
            self.assertEqual(constructor.call_count, 1)

    def test_timeout_closes_reaps_and_never_restarts_or_accepts_late_reply(self):
        body = 'time.sleep(30)\n' + emit_items(repr([TARGET]))
        with self.fake_inventory(body, timeout=.15) as (inventory, constructor):
            with self.assertRaisesRegex(TimeoutError, 'timed out'):
                inventory.query()
            self.assertTrue(inventory.closed)
            self.assertIsNotNone(inventory.process.poll())
            with self.assertRaisesRegex(ValueError, 'closed'):
                inventory.query()
            self.assertEqual(constructor.call_count, 1)

    def test_cleanup_kill_failure_preserves_timeout_without_blocking_reader_close(self):
        body = 'if sequence > 1:\n    time.sleep(30)\n' + emit_items(repr([TARGET]))
        with self.fake_inventory(body) as (inventory, constructor):
            inventory.query()  # Warm startup before the short response deadline.
            inventory.timeout = .15
            process = inventory.process
            actual_kill = process.kill
            kill_attempted = threading.Event()
            errors = []
            def failed_kill():
                kill_attempted.set()
                raise OSError('simulated OS kill failure')
            def query():
                try:
                    inventory.query()
                except Exception as error:
                    errors.append(error)
            thread = threading.Thread(target=query, daemon=True)
            try:
                with patch.object(process, 'kill', side_effect=failed_kill):
                    thread.start()
                    self.assertTrue(kill_attempted.wait(timeout=3))
                    thread.join(timeout=.5)
                    self.assertFalse(thread.is_alive(), 'Cleanup blocked on an active stdout reader')
                    self.assertEqual(len(errors), 1)
                    self.assertIsInstance(errors[0], TimeoutError)
                    self.assertTrue(inventory.closed)
                    self.assertEqual(constructor.call_count, 1)
            finally:
                # Even a failed assertion reaps the fixture; no leaked child.
                if process.poll() is None:
                    actual_kill()
                process.wait(timeout=5)
                thread.join(timeout=5)

    def test_malformed_schema_id_boolean_array_and_query_errors_fail_closed(self):
        cases = [
            ('malformed', "print('{bad json', flush=True)\n"),
            ('non_object', "print('[]', flush=True)\n"),
            ('wrong_id', "print(json.dumps({'id': sequence + 1, 'ok': True, 'items': []}), flush=True)\n"),
            ('boolean_id', "print(json.dumps({'id': True, 'ok': True, 'items': []}), flush=True)\n"),
            ('integer_ok', "print(json.dumps({'id': sequence, 'ok': 1, 'items': []}), flush=True)\n"),
            ('missing_items', "print(json.dumps({'id': sequence, 'ok': True}), flush=True)\n"),
            ('extra_key', "print(json.dumps({'id': sequence, 'ok': True, 'items': [], 'extra': 1}), flush=True)\n"),
            ('object_items', emit_items(repr(TARGET))),
            ('null_items', emit_items('None')),
            ('query_error', "print(json.dumps({'id': sequence, 'ok': False, 'error': 'fixture error'}), flush=True)\n"),
            ('oversized', "print('x' * 65537, flush=True)\n"),
            ('no_newline', "sys.stdout.write('{}'); sys.stdout.flush(); sys.exit(0)\n"),
            ('eof', 'sys.exit(3)\n'),
            ('invalid_utf8', "sys.stdout.buffer.write(b'\\xff\\n'); sys.stdout.flush()\n"),
        ]
        for name, body in cases:
            with self.subTest(name=name), self.fake_inventory(body) as (inventory, constructor):
                with self.assertRaises(Exception):
                    inventory.query()
                self.assertTrue(inventory.closed)
                self.assertIsNotNone(inventory.process.poll())
                with self.assertRaisesRegex(ValueError, 'closed'):
                    inventory.query()
                self.assertEqual(constructor.call_count, 1)

    def test_disk_rejects_ambiguous_or_invalid_identities(self):
        cases = [[], [TARGET, OTHER], [None], [dict(TARGET, extra='bad')],
                 [dict(TARGET, DeviceID=7)], [dict(TARGET, DeviceID='C:')],
                 [dict(TARGET, PNPDeviceID='USBSTOR\\OTHER')]]
        for items in cases:
            with self.subTest(items=items), self.fake_inventory(emit_items(repr(items))) as (inventory, constructor):
                with patch.object(worker, '_inventory', inventory), self.assertRaises(ValueError):
                    worker.disk()
                self.assertEqual(constructor.call_count, 1)


class BrokerIdentityTests(OfflineChildren):
    @contextlib.contextmanager
    def session_fixture(self, body, timeout=2):
        with tempfile.TemporaryDirectory() as folder, self.fake_inventory(body, timeout) as (inventory, constructor):
            root = Path(folder)
            baseline = bytes(worker.SIZE)
            image = bytearray(baseline)
            image[0x4000] = 1
            image[0x5000] = 2
            image = bytes(image)
            (root / 'baseline.bin').write_bytes(baseline)
            (root / 'state.json').write_text(json.dumps({
                'baseline_file': 'baseline.bin', 'baseline_sha256': worker.sha(baseline),
                'blocked': False, 'reset_pending': False, 'needs_observation': False,
            }))
            memory = bytearray(baseline)
            calls, writes = [], []
            def cli(device, commands, log, mode='write'):
                calls.append((mode, list(commands)))
                self.assertEqual(device, TARGET['DeviceID'])
                if mode != 'write':
                    return
                for command in commands:
                    bits = command.split(' ', 3)
                    address = int(bits[1])
                    if bits[0] == 'write':
                        data = Path(' '.join(bits[2:])).read_bytes()
                        memory[address:address + len(data)] = data
                        writes.append(address)
                    else:
                        count = int(bits[2])
                        Path(bits[3]).write_bytes(memory[address:address + count])
            request = {'op': 'flash', 'image': base64.b64encode(image).decode(),
                       'sha256': worker.sha(image), 'baseline_sha256': worker.sha(baseline)}
            with patch.object(worker, '_inventory', inventory), \
                 patch.object(worker, 'execute_cli', side_effect=cli), \
                 contextlib.redirect_stdout(io.StringIO()):
                yield root, request, calls, writes, inventory, constructor

    def assert_failed_latch(self, root, request, inventory, constructor):
        state = worker.Session(root).state
        self.assertTrue(state['blocked'])
        self.assertEqual(state['failed_operation'], 'flash')
        self.assertEqual(state['flash_attempt']['status'], 'failed')
        self.assertEqual(state['baseline_sha256'], request['baseline_sha256'])
        self.assertFalse(state['reset_pending'])
        self.assertFalse(state['needs_observation'])
        sequence = inventory.sequence
        with self.assertRaisesRegex(ValueError, 'latched'):
            worker.Session(root).handle(request)
        self.assertEqual(inventory.sequence, sequence)
        self.assertEqual(constructor.call_count, 1)

    def test_identity_change_before_sector_prevents_write_and_latches(self):
        body = emit_items('[' + repr(OTHER) + ' if sequence == 3 else ' + repr(TARGET) + ']')
        with self.session_fixture(body) as (root, request, calls, writes, inventory, constructor):
            with self.assertRaisesRegex(ValueError, 'during write/verify'):
                worker.Session(root).handle(request)
            self.assertEqual([mode for mode, _ in calls], ['load'])
            self.assertEqual(writes, [])
            self.assert_failed_latch(root, request, inventory, constructor)

    def test_identity_change_after_sector_stops_next_sector_and_latches(self):
        body = emit_items('[' + repr(OTHER) + ' if sequence == 4 else ' + repr(TARGET) + ']')
        with self.session_fixture(body) as (root, request, calls, writes, inventory, constructor):
            with self.assertRaisesRegex(ValueError, 'after write/verify'):
                worker.Session(root).handle(request)
            self.assertEqual([mode for mode, _ in calls], ['load', 'write'])
            self.assertEqual(writes, [0x5000])
            self.assert_failed_latch(root, request, inventory, constructor)

    def test_inventory_timeout_after_sector_is_ambiguous_and_not_retried(self):
        body = 'if sequence == 4:\n    time.sleep(30)\n' + emit_items(repr([TARGET]))
        with self.session_fixture(body, timeout=2) as (root, request, calls, writes, inventory, constructor):
            with self.assertRaises(TimeoutError):
                worker.Session(root).handle(request)
            self.assertEqual(writes, [0x5000])
            self.assertTrue(inventory.closed)
            self.assertIsNotNone(inventory.process.poll())
            self.assert_failed_latch(root, request, inventory, constructor)

    def test_success_keeps_every_before_after_query_and_one_full_readback(self):
        with self.session_fixture(emit_items(repr([TARGET]))) as (root, request, calls, writes, inventory, constructor):
            result = worker.Session(root).handle(request)
            self.assertEqual(writes, [0x5000, 0x4000])
            self.assertEqual([mode for mode, _ in calls], ['load', 'write', 'write', 'write'])
            self.assertEqual(inventory.sequence, 8)
            self.assertEqual(constructor.call_count, 1)
            receipt = json.loads((Path(result['log_directory']) / 'deployment.json').read_text())
            self.assertEqual(receipt['full_readback_count'], 1)
            self.assertEqual(receipt['verified_sectors'], ['0x5000', '0x4000'])
            state = worker.Session(root).state
            self.assertFalse(state['blocked'])
            self.assertTrue(state['reset_pending'])
            self.assertTrue(state['needs_observation'])
            self.assertEqual(state['baseline_sha256'], request['sha256'])


if __name__ == '__main__':
    unittest.main()
