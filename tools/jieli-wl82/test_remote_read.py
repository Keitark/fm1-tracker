"""Offline protected remote read/status tests. No USB or serial hardware I/O."""
import copy
import base64
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flash_session_worker as worker


class ReadFirmwareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.baseline = bytes(worker.SIZE)
        self.current = bytes([42]) * worker.SIZE
        (self.root / 'baseline.bin').write_bytes(self.baseline)
        self.target = {'DeviceID': r'\\.\PHYSICALDRIVE3', 'PNPDeviceID': 'verified-device'}
        self.initial = {'baseline_file': 'baseline.bin', 'baseline_sha256': worker.sha(self.baseline),
                        'blocked': False, 'reset_pending': False, 'needs_observation': False,
                        'operation': 'idle', 'target': copy.deepcopy(self.target)}
        self.save(self.initial)
        helper = self.root / 'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
        helper.parent.mkdir(parents=True)
        self.helper_bytes = b'pinned offline helper'
        helper.write_bytes(self.helper_bytes)
        self.calls = []
        self.images = [self.current, self.current]

    def save(self, state):
        (self.root / 'state.json').write_text(json.dumps(state), encoding='utf-8')

    def cli(self, device, commands, log, mode='write'):
        self.calls.append((device, commands, mode))
        self.assertEqual(device, self.target['DeviceID'])
        # No final export is visible while a read is still being executed.
        self.assertFalse(list((self.root / 'runs').glob('*/firmware.bin')))
        if mode == 'load':
            self.assertEqual(commands, ['help'])
        else:
            self.assertEqual(mode, 'write')  # Existing guarded CLI mode; read commands only.
            self.assertEqual(len(commands), 1)
            prefix, path = commands[0].rsplit(' ', 1)
            self.assertEqual(prefix, f'read 0 {worker.SIZE}')
            Path(path).write_bytes(self.images.pop(0))

    def run_read(self, loader='reuse', disk_values=None, helper_sha=None):
        disk_patch = patch.object(worker, 'disk', return_value=self.target) if disk_values is None else \
            patch.object(worker, 'disk', side_effect=disk_values)
        with disk_patch, patch.object(worker, 'ROOT', self.root), \
                patch.object(worker, 'HELPER_SHA', helper_sha or worker.sha(self.helper_bytes)), \
                patch.object(worker, 'execute_cli', side_effect=self.cli):
            return worker.Session(self.root).handle({'op': 'read_firmware', 'loader_state': loader})

    def test_reuse_reads_twice_finalizes_private_export_and_preserves_baseline(self):
        result = self.run_read()['result']
        self.assertEqual(result['size'], worker.SIZE)
        self.assertEqual(result['sha256'], worker.sha(self.current))
        self.assertEqual(result['readback_count'], 2)
        self.assertEqual(result['loader_state'], 'reuse')
        self.assertFalse(result['chip_key_queried'])
        self.assertFalse(result['flash_array_program_or_erase_commands_sent'])
        self.assertFalse(result['reset_requested'])
        self.assertTrue(result['includes_device_configuration'])
        self.assertTrue(result['loader_online_may_change_nor_protection'])
        self.assertEqual((self.root / result['file']).read_bytes(), self.current)
        self.assertEqual([mode for _, _, mode in self.calls], ['write', 'write'])
        state = worker.Session(self.root).state
        self.assertEqual(state['baseline_file'], self.initial['baseline_file'])
        self.assertEqual(state['baseline_sha256'], self.initial['baseline_sha256'])
        self.assertEqual((self.root / 'baseline.bin').read_bytes(), self.baseline)
        self.assertFalse(state['blocked'])
        self.assertFalse(state['reset_pending'])
        self.assertTrue(state['loader_running'])

    def test_read_running_helper_blocks_later_flash_without_reupload(self):
        self.run_read()
        image = bytearray(self.baseline)
        image[0x4000] = 1
        request = {'op': 'flash', 'baseline_sha256': worker.sha(self.baseline),
                   'sha256': worker.sha(image), 'image': base64.b64encode(image).decode('ascii')}
        with patch.object(worker, 'disk') as disk, patch.object(worker, 'execute_cli') as cli:
            with self.assertRaisesRegex(ValueError, 'RAM helper running'):
                worker.Session(self.root).handle(request)
            disk.assert_not_called(); cli.assert_not_called()

    def test_read_running_helper_refuses_another_cold_upload(self):
        self.run_read()
        with patch.object(worker, 'disk') as disk, patch.object(worker, 'execute_cli') as cli:
            with self.assertRaisesRegex(ValueError, 'Known RAM helper'):
                worker.Session(self.root).handle({'op': 'read_firmware', 'loader_state': 'cold'})
            disk.assert_not_called(); cli.assert_not_called()

    def test_cold_loads_pinned_helper_once_before_two_reads(self):
        self.run_read('cold')
        self.assertEqual([mode for _, _, mode in self.calls], ['load', 'write', 'write'])

    def test_missing_or_extra_fields_and_invalid_loader_state_send_no_io(self):
        for request in ({'op': 'read_firmware'}, {'op': 'read_firmware', 'loader_state': 'auto'},
                        {'op': 'read_firmware', 'loader_state': False},
                        {'op': 'read_firmware', 'loader_state': 'reuse', 'path': 'anything'},
                        {'op': 'serial_status', 'command': 'UBOOT'}):
            with self.subTest(request=request), patch.object(worker, 'disk') as disk, \
                    patch.object(worker, 'execute_cli') as cli, patch.object(worker, 'serial_action') as serial:
                with self.assertRaises(ValueError):
                    worker.Session(self.root).handle(request)
                disk.assert_not_called(); cli.assert_not_called(); serial.assert_not_called()

    def test_pending_reset_observation_or_blocked_send_no_io(self):
        for flag in ('reset_pending', 'needs_observation', 'blocked'):
            state = dict(self.initial, **{flag: True})
            self.save(state)
            with self.subTest(flag=flag), patch.object(worker, 'disk') as disk, \
                    patch.object(worker, 'execute_cli') as cli:
                with self.assertRaises(ValueError):
                    worker.Session(self.root).handle({'op': 'read_firmware', 'loader_state': 'reuse'})
                disk.assert_not_called(); cli.assert_not_called()
            self.assertEqual(worker.Session(self.root).state, state)

    def test_bad_baseline_hash_send_no_io(self):
        self.save(dict(self.initial, baseline_sha256='0' * 64))
        with patch.object(worker, 'disk') as disk, patch.object(worker, 'execute_cli') as cli:
            with self.assertRaisesRegex(ValueError, 'Stored baseline damaged'):
                worker.Session(self.root).handle({'op': 'read_firmware', 'loader_state': 'reuse'})
            disk.assert_not_called(); cli.assert_not_called()

    def test_cold_helper_hash_failure_latches_without_loader_io(self):
        with self.assertRaisesRegex(ValueError, 'Helper hash mismatch'):
            self.run_read('cold', helper_sha='0' * 64)
        self.assertEqual(self.calls, [])
        self.assertTrue(worker.Session(self.root).state['blocked'])
        self.assertFalse(list((self.root / 'runs').glob('*/firmware.bin')))

    def test_changed_recorded_pnp_identity_fails_before_helper_or_read(self):
        changed = dict(self.target, PNPDeviceID='different-device')
        with self.assertRaisesRegex(ValueError, 'target identity changed'):
            self.run_read('cold', disk_values=[changed])
        self.assertEqual(self.calls, [])
        self.assertTrue(worker.Session(self.root).state['blocked'])

    def test_disk_renumbering_with_same_pnp_is_allowed(self):
        self.target['DeviceID'] = r'\\.\PHYSICALDRIVE7'
        self.run_read()
        self.assertEqual(len(self.calls), 2)

    def test_transport_identity_change_stops_without_second_read(self):
        changed = dict(self.target, DeviceID=r'\\.\PHYSICALDRIVE9')
        with self.assertRaisesRegex(ValueError, 'after firmware read'):
            self.run_read(disk_values=[self.target, self.target, changed])
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(worker.Session(self.root).state['blocked'])
        self.assertFalse(list((self.root / 'runs').glob('*/firmware.bin')))

    def test_short_read_stops_without_second_read(self):
        self.images = [self.current[:-1], self.current]
        with self.assertRaisesRegex(ValueError, 'Short full firmware read'):
            self.run_read()
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(worker.Session(self.root).state['blocked'])
        self.assertEqual(worker.Session(self.root).state['baseline_sha256'], self.initial['baseline_sha256'])
        self.assertFalse(list((self.root / 'runs').glob('*/firmware.bin')))

    def test_mismatching_second_read_latches_without_export_or_baseline_promotion(self):
        self.images[1] = self.current[:-1] + b'!'
        with self.assertRaisesRegex(ValueError, 'Firmware reads differ'):
            self.run_read()
        self.assertEqual(len(self.calls), 2)
        state = worker.Session(self.root).state
        self.assertTrue(state['blocked'])
        self.assertEqual(state['failed_operation'], 'read_firmware')
        self.assertFalse(state['reset_pending'])
        self.assertEqual(state['baseline_sha256'], self.initial['baseline_sha256'])
        self.assertFalse(list((self.root / 'runs').glob('*/firmware.bin')))


class LiveStatusTests(unittest.TestCase):
    def test_running_dirty_tracker_and_faults_are_reported_without_stop(self):
        sent = []
        class Port:
            def __init__(self, **kwargs):
                pass
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def open(self):
                pass
            def write(self, data):
                sent.append(data)
            def flush(self):
                pass
        module = types.SimpleNamespace(Serial=Port)
        sample = ('MOD status ready=1 running=1 dirty=1 export=0 order=0 row=3 ch=4 sample=1 '
                  'lcd=0 keys=2 audio=0 frames=99 underruns=1\n')
        with tempfile.TemporaryDirectory() as temp, patch.dict(sys.modules, {'serial': module}), \
                patch.object(worker, 'serial_port', return_value='COM42'), \
                patch.object(worker, 'capture', side_effect=['', 'FM1DIAG/1 MOD-EDITOR/1 RAM-TRANSFER UBOOT=SERIAL COMMIT=BLOCKED\n'] + [sample] * 4):
            result = worker.serial_action('serial_status', Path(temp))
            self.assertEqual(result['profile'], 'MOD-EDITOR/1')
            self.assertEqual(result['samples'][0]['running'], 1)
            self.assertEqual(result['samples'][0]['dirty'], 1)
            self.assertEqual(result['samples'][0]['keys'], 2)
            self.assertTrue(result['diagnostic_only'])
            self.assertFalse(result['boot_verified'])
        self.assertEqual(sent, [b'HELLO\n'] + [b'MOD STATUS\n'] * 4)

    def test_live_status_does_not_clear_existing_failure_or_observation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = {'operation': 'flash', 'blocked': True, 'failed_operation': 'flash',
                     'error': 'Completed failure', 'reset_pending': False, 'needs_observation': True}
            (root / 'state.json').write_text(json.dumps(state))
            with patch.object(worker, 'serial_action', return_value={'diagnostic_only': True}):
                answer = worker.Session(root).handle({'op': 'serial_status'})
            self.assertTrue(answer['result']['diagnostic_only'])
            self.assertEqual(worker.Session(root).state, state)

    def test_live_status_absent_cdc_does_not_introduce_failure_latch(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = {'operation': 'idle', 'blocked': False, 'needs_observation': True}
            (root / 'state.json').write_text(json.dumps(state))
            with patch.object(worker, 'serial_action', side_effect=ValueError('No CDC')):
                with self.assertRaisesRegex(ValueError, 'No CDC'):
                    worker.Session(root).handle({'op': 'serial_status'})
            self.assertEqual(worker.Session(root).state, state)

    def test_pending_operation_blocks_serial_before_open(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'state.json').write_text(json.dumps({'operation': 'flash', 'blocked': True}))
            with patch.object(worker, 'serial_action') as serial:
                with self.assertRaisesRegex(ValueError, 'operation pending'):
                    worker.Session(root).handle({'op': 'serial_status'})
                serial.assert_not_called()

    def test_complete_status_fields_required_but_fault_values_are_data(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete live status'):
            worker.live_status_samples('MOD status running=1\n', 'MOD-EDITOR/1')
        with self.assertRaisesRegex(ValueError, 'Live firmware status missing'):
            worker.live_status_samples('TEST STATUS ready=1 busy=0', 'NES')
        sample = 'MDX running=0 pending=1 frames=0 underruns=4 error=-1 lcd=2 keys=3 audio=4\n'
        self.assertEqual(worker.live_status_samples(sample, 'MDX-KARAOKE/1')[0]['error'], -1)


if __name__ == '__main__':
    unittest.main()
