"""Offline laptop adapter tests. No real USB, PnP, serial or named pipe access."""
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import uuid

import remote_backend as backend


SIZE = 0x100000
BASELINE = bytes(SIZE)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepared():
    return dict(op='plan', image=base64.b64encode(BASELINE).decode(),
                sha256=sha(BASELINE), baseline_sha256=sha(BASELINE))


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.repo = self.root / 'repo'
        self.client = self.repo / 'tools/jieli-wl82/flash-session-client.ps1'
        self.client.parent.mkdir(parents=True)
        self.client.write_text('# Offline fixture only', encoding='utf-8')
        self.session = self.root / 'session'
        self.session.mkdir()
        (self.session / 'session.json').write_text(
            json.dumps({'root': str(self.session)}), encoding='utf-8')
        (self.session / 'baseline.bin').write_bytes(BASELINE)
        self.write_state({'baseline_file': 'baseline.bin', 'baseline_sha256': sha(BASELINE),
                          'blocked': False, 'reset_pending': False, 'operation': 'idle'})
        self.adapter = backend.WindowsBackend(self.repo, self.session)

    def tearDown(self):
        self.folder.cleanup()

    def write_state(self, state):
        (self.session / 'state.json').write_text(json.dumps(state), encoding='utf-8')

    def firmware_response(self, data=BASELINE):
        relative = 'runs/' + uuid.uuid4().hex + '/firmware.bin'
        path = self.session / relative
        path.parent.mkdir(parents=True)
        path.write_bytes(data)
        return {'ok': True, 'data': {'result': {
            'size': SIZE, 'readback_count': 2, 'file': relative, 'sha256': sha(data)}}}

    def bundle(self, image=None):
        if image is None:
            candidate = bytearray(BASELINE)
            candidate[0x4000] = 1
            candidate[0x4120] = 2
            image = bytes(candidate)
        request = dict(op='plan', image=base64.b64encode(image).decode(),
                       sha256=sha(image), baseline_sha256=sha(BASELINE))
        return dict(id='nes-test', profile='nes', title='NES Test', description='Offline fixture',
                    variant='Test ROM', request=request)

    def test_constructor_requires_the_fixed_protected_client(self):
        self.client.unlink()
        with self.assertRaisesRegex(ValueError, 'flash-session-client'):
            backend.WindowsBackend(self.repo, self.session)

    def test_constructor_rejects_session_descriptor_for_another_directory(self):
        (self.session / 'session.json').write_text(
            json.dumps({'root': str(self.root / 'other')}), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'another directory'):
            backend.WindowsBackend(self.repo, self.session)

    def test_no_session_returns_known_refusal_without_a_subprocess(self):
        adapter = backend.WindowsBackend(self.repo)
        with patch.object(backend.subprocess, 'run', side_effect=AssertionError('No pipe access')):
            response = adapter.execute('observe')
        self.assertFalse(response['ok'])
        self.assertIn('No protected session', response['error'])

    def test_execute_uses_fixed_client_and_keeps_image_request_as_local_data(self):
        request = prepared()
        captured = {}
        def run(args, **kwargs):
            captured['args'] = args
            captured['kwargs'] = kwargs
            path = Path(args[args.index('-RequestFile') + 1])
            captured['path'] = path
            self.assertEqual(json.loads(path.read_text(encoding='utf-8')), request)
            return SimpleNamespace(stdout='{"ok":true,"data":{"device_io":false}}\n', returncode=0)
        with patch.object(backend.subprocess, 'run', side_effect=run) as call:
            result = self.adapter.execute('plan', request=request)
        self.assertTrue(result['ok'])
        self.assertEqual(call.call_count, 1)
        args = captured['args']
        self.assertEqual(args[0], backend.PS)
        self.assertEqual(args[args.index('-File') + 1], str(self.client))
        self.assertEqual(args[args.index('-SessionRoot') + 1], str(self.session))
        self.assertEqual(args[args.index('-Operation') + 1], 'plan')
        self.assertNotIn('-Command', args)
        self.assertNotIn(request['image'], args)
        self.assertFalse(captured['path'].exists())
        self.assertGreater(captured['kwargs']['timeout'], 1200)

    def test_flash_operation_keeps_embedded_request_in_offline_plan_form(self):
        request = prepared()
        def run(args, **kwargs):
            self.assertEqual(args[args.index('-Operation') + 1], 'flash')
            path = Path(args[args.index('-RequestFile') + 1])
            self.assertEqual(json.loads(path.read_text(encoding='utf-8'))['op'], 'plan')
            return SimpleNamespace(stdout='{"ok":true}\n', returncode=0)
        with patch.object(backend.subprocess, 'run', side_effect=run) as call:
            self.adapter.execute('flash', request=request)
        self.assertEqual(call.call_count, 1)

    def test_read_requires_explicit_loader_state_and_forwards_only_fixed_option(self):
        for state in ('cold', 'reuse'):
            with self.subTest(state=state), patch.object(backend.subprocess, 'run',
                    return_value=SimpleNamespace(stdout='{"ok":true}\n', returncode=0)) as call:
                self.adapter.execute('read_firmware', loader_state=state)
                args = call.call_args.args[0]
                self.assertEqual(args[-2:], ['-LoaderState', state])
                self.assertNotIn('-RequestFile', args)
                self.assertEqual(call.call_count, 1)
        with patch.object(backend.subprocess, 'run', side_effect=AssertionError('No pipe access')):
            for state in (None, 'guess', True):
                with self.assertRaises(ValueError):
                    self.adapter.execute('read_firmware', loader_state=state)
            with self.assertRaises(ValueError):
                self.adapter.execute('observe', loader_state='cold')

    def test_unsupported_operation_or_request_fields_never_reach_subprocess(self):
        with patch.object(backend.subprocess, 'run', side_effect=AssertionError('No command execution')):
            for operation in ('shell', 'erase', 'quit', 'status'):
                with self.subTest(operation=operation), self.assertRaises(ValueError):
                    self.adapter.execute(operation)
            for changes in ({'op': 'flash'}, {'path': 'candidate.bin'},
                            {'command': 'erase'}, {'sha256': '0' * 64}, {'image': '!'}):
                with self.subTest(changes=changes), self.assertRaises(ValueError):
                    self.adapter.execute('flash', request=dict(prepared(), **changes))
            with self.assertRaises(ValueError):
                self.adapter.execute('observe', request=prepared())

    def test_known_broker_refusal_json_is_returned_without_retry(self):
        response = {'ok': False, 'error': 'Blocked by previous failed flash'}
        proc = SimpleNamespace(stdout='client progress\n' + json.dumps(response) + '\n', returncode=1)
        with patch.object(backend.subprocess, 'run', return_value=proc) as call:
            self.assertEqual(self.adapter.execute('reset'), response)
        self.assertEqual(call.call_count, 1)

    def test_missing_definite_broker_response_is_an_unknown_outcome_without_retry(self):
        for output in ('', 'client failed\n', '{"ok":"true"}\n', '[]\n'):
            with self.subTest(output=output), patch.object(backend.subprocess, 'run',
                    return_value=SimpleNamespace(stdout=output, returncode=1)) as call:
                with self.assertRaisesRegex(RuntimeError, 'outcome unknown'):
                    self.adapter.execute('reset')
                self.assertEqual(call.call_count, 1)

    def test_pipe_timeout_propagates_without_automatic_retry(self):
        with patch.object(backend.subprocess, 'run',
                          side_effect=subprocess.TimeoutExpired('offline fixture', 1245)) as call:
            with self.assertRaises(subprocess.TimeoutExpired):
                self.adapter.execute('reset')
        self.assertEqual(call.call_count, 1)

    def test_baseline_download_verifies_full_length_and_recorded_sha256(self):
        self.assertEqual(self.adapter.baseline(), BASELINE)
        (self.session / 'baseline.bin').write_bytes(BASELINE[:-1])
        with self.assertRaisesRegex(ValueError, 'exactly 1 MiB'):
            self.adapter.baseline()
        (self.session / 'baseline.bin').write_bytes(BASELINE)
        self.write_state({'baseline_file': 'baseline.bin', 'baseline_sha256': '0' * 64})
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            self.adapter.baseline()

    def test_baseline_relative_path_cannot_escape_the_selected_session(self):
        for path in ('../outside.bin', str(self.root / 'outside.bin')):
            self.write_state({'baseline_file': path, 'baseline_sha256': sha(BASELINE)})
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.adapter.baseline()

    def test_firmware_download_requires_two_verified_full_readbacks(self):
        response = self.firmware_response()
        self.assertEqual(self.adapter.firmware(response), BASELINE)
        for field, value in (('readback_count', 1), ('size', SIZE - 1)):
            modified = json.loads(json.dumps(response))
            modified['data']['result'][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'incomplete'):
                self.adapter.firmware(modified)
        with self.assertRaisesRegex(ValueError, 'completed verified'):
            self.adapter.firmware({'ok': False})

    def test_firmware_download_rechecks_artifact_hash_after_successful_job(self):
        response = self.firmware_response()
        path = self.session / response['data']['result']['file']
        altered = bytearray(BASELINE)
        altered[7] = 42
        path.write_bytes(altered)
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            self.adapter.firmware(response)

    def test_firmware_download_accepts_only_the_fixed_run_artifact_name(self):
        response = self.firmware_response()
        for path in ('baseline.bin', '../baseline.bin', 'runs/../firmware.bin',
                     'runs/' + uuid.uuid4().hex + '/request.json',
                     str(self.session / response['data']['result']['file'])):
            modified = json.loads(json.dumps(response))
            modified['data']['result']['file'] = path
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'Unexpected firmware artifact'):
                self.adapter.firmware(modified)

    def test_status_inventory_opens_no_device_transport_or_pipe(self):
        ports = SimpleNamespace(comports=lambda: [
            SimpleNamespace(device='COM_FAKE', vid=0x3654, pid=0x5155,
                            description='Offline FM-1', serial_number='offline'),
            SimpleNamespace(device='COM_OTHER', vid=1, pid=2,
                            description='Other', serial_number='other')])
        modules = {'serial.tools': SimpleNamespace(list_ports=ports)}
        with patch.dict('sys.modules', modules), patch.object(backend.sys, 'platform', 'win32'), \
             patch.object(backend.subprocess, 'run',
                          return_value=SimpleNamespace(stdout='[]', returncode=0)) as call:
            result = self.adapter.status()
        self.assertFalse(result['device_io'])
        self.assertEqual([p['port'] for p in result['serial_ports']], ['COM_FAKE'])
        self.assertTrue(result['session_configured'])
        self.assertFalse(result['session']['blocked'])
        self.assertEqual(call.call_count, 1)
        args = call.call_args.args[0]
        self.assertIn('Get-CimInstance Win32_DiskDrive', args[-1])
        self.assertNotIn('-File', args)
        self.assertNotIn('-Operation', args)

    def test_catalog_package_ids_are_immutable(self):
        bundle = self.bundle()
        self.adapter.install_catalog(bundle)
        self.adapter.install_catalog(bundle)
        altered = dict(bundle, title='Different package metadata')
        with self.assertRaisesRegex(ValueError, 'immutable'):
            self.adapter.install_catalog(altered)
        self.assertEqual(self.adapter.packages.get(bundle['id']), bundle)

    def test_catalog_listing_omits_payload_and_reports_device_compatibility(self):
        bundle = self.bundle()
        self.adapter.install_catalog(bundle)
        listing = self.adapter.catalog()
        entry = next(app for app in listing['apps'] if app['profile'] == 'nes')['variants'][0]
        self.assertTrue(entry['ready'])
        self.assertEqual(entry['sha256'], bundle['request']['sha256'])
        self.assertNotIn(bundle['request']['image'], json.dumps(listing))

    def test_catalog_packages_cannot_rebase_boot_config_or_reserved_bytes(self):
        for address in (0, 0x4040, 0xee000, SIZE - 1):
            candidate = bytearray(BASELINE)
            candidate[0x4000] = 1
            candidate[address] = 3
            bundle = self.bundle(bytes(candidate))
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, 'boot/config/reserved'):
                backend.rebase(bundle, BASELINE)

    def test_plan_app_uses_only_offline_plan_and_keeps_device_inventory_closed(self):
        bundle = self.bundle()
        self.adapter.install_catalog(bundle)
        with patch.object(self.adapter, '_pipe', return_value={'ok': True, 'data': {'device_io': False}}) as call, \
             patch.object(self.adapter, 'status', side_effect=AssertionError('No device inventory')):
            result = self.adapter.execute('plan_app', catalog_id='nes-test')
        self.assertTrue(result['ok'])
        call.assert_called_once_with('plan', bundle['request'])

    def test_plan_app_of_current_package_has_empty_sectors_and_no_pipe_operation(self):
        self.adapter.install_catalog(self.bundle(BASELINE))
        with patch.object(self.adapter, '_pipe', side_effect=AssertionError('No device operation')):
            result = self.adapter.execute('plan_app', catalog_id='nes-test')
        self.assertTrue(result['ok'])
        self.assertEqual(result['data']['sectors'], [])
        self.assertFalse(result['data']['device_io'])

    def test_plan_app_rejects_entry_or_image_parameters_before_pipe_access(self):
        with patch.object(self.adapter, '_pipe', side_effect=AssertionError('No device operation')):
            for fields in ({'entry_method': 'serial'}, {'loader_state': 'cold'}, {'request': prepared()}):
                with self.subTest(fields=list(fields)), self.assertRaises(ValueError):
                    self.adapter.execute('plan_app', catalog_id='nes-test', **fields)

    def test_switch_in_uboot_runs_plan_flash_one_reset_observation_and_profile_check(self):
        self.adapter.install_catalog(self.bundle())
        calls = []
        def pipe(operation, request=None):
            calls.append(operation)
            if operation == 'serial_status':
                return {'ok': True, 'data': {'result': {'profile': 'NES'}}}
            return {'ok': True}
        with patch.object(self.adapter, '_pipe', side_effect=pipe), \
             patch.object(self.adapter, 'status', return_value={'uboot_disks': [{'DeviceID': 'fake'}]}):
            result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='already_uboot')
        self.assertTrue(result['ok'], result)
        self.assertEqual(calls, ['plan', 'flash', 'reset', 'observe', 'serial_status'])
        self.assertTrue(result['data']['written_readback_verified'])
        self.assertTrue(result['data']['serial_boot_verified'])
        self.assertFalse(result['data']['physical_acceptance'])

    def test_switch_from_running_app_uses_existing_serial_uboot_transition(self):
        self.adapter.install_catalog(self.bundle())
        calls = []
        def pipe(operation, request=None):
            calls.append(operation)
            return {'ok': True, 'data': {'result': {'profile': 'NES'}}}
        with patch.object(self.adapter, '_pipe', side_effect=pipe), \
             patch.object(self.adapter, 'status', return_value={'uboot_disks': []}):
            result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='serial')
        self.assertTrue(result['ok'], result)
        self.assertEqual(calls, ['plan', 'enter_uboot', 'flash', 'reset', 'observe', 'serial_status'])

    def test_switch_refuses_missing_or_multiple_uboot_devices_without_flashing(self):
        self.adapter.install_catalog(self.bundle())
        for disks in ([], [{'DeviceID': 'one'}, {'DeviceID': 'two'}]):
            with self.subTest(disks=disks), patch.object(self.adapter, '_pipe',
                    return_value={'ok': True}) as call, \
                 patch.object(self.adapter, 'status', return_value={'uboot_disks': disks}):
                result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='already_uboot')
                self.assertFalse(result['ok'])
                self.assertEqual([c.args[0] for c in call.call_args_list], ['plan'])

    def test_switch_stops_after_each_known_failed_step(self):
        self.adapter.install_catalog(self.bundle())
        sequence = ['plan', 'enter_uboot', 'flash', 'reset', 'observe', 'serial_status']
        for failed in ('plan', 'enter_uboot', 'flash', 'observe', 'serial_status'):
            calls = []
            def pipe(operation, request=None):
                calls.append(operation)
                return {'ok': operation != failed, 'data': {'result': {'profile': 'NES'}}}
            with self.subTest(failed=failed), patch.object(self.adapter, '_pipe', side_effect=pipe), \
                 patch.object(self.adapter, 'status', return_value={'uboot_disks': []}):
                result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='serial')
                self.assertFalse(result['ok'])
                self.assertEqual(calls, sequence[:sequence.index(failed) + 1])

    def test_switch_reset_known_disconnect_is_observed_once_without_second_reset(self):
        self.adapter.install_catalog(self.bundle())
        calls = []
        def pipe(operation, request=None):
            calls.append(operation)
            return {'ok': operation != 'reset', 'data': {'result': {'profile': 'NES'}}}
        with patch.object(self.adapter, '_pipe', side_effect=pipe), \
             patch.object(self.adapter, 'status', return_value={'uboot_disks': [{'DeviceID': 'fake'}]}):
            result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='already_uboot')
        self.assertTrue(result['ok'], result)
        self.assertEqual(calls.count('reset'), 1)
        self.assertEqual(calls.count('observe'), 1)
        self.assertFalse(result['data']['reset_response_ok'])

    def test_switch_unknown_reset_outcome_stops_before_observation_or_retry(self):
        self.adapter.install_catalog(self.bundle())
        calls = []
        def pipe(operation, request=None):
            calls.append(operation)
            if operation == 'reset':
                raise RuntimeError('Protected pipe outcome unknown')
            return {'ok': True}
        with patch.object(self.adapter, '_pipe', side_effect=pipe), \
             patch.object(self.adapter, 'status', return_value={'uboot_disks': [{'DeviceID': 'fake'}]}):
            with self.assertRaisesRegex(RuntimeError, 'outcome unknown'):
                self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='already_uboot')
        self.assertEqual(calls, ['plan', 'flash', 'reset'])

    def test_switch_requires_actual_selected_profile_in_live_hello(self):
        self.adapter.install_catalog(self.bundle())
        with patch.object(self.adapter, '_pipe',
                          return_value={'ok': True, 'data': {'result': {'profile': 'DOOM-FM1/1'}}}), \
             patch.object(self.adapter, 'status', return_value={'uboot_disks': [{'DeviceID': 'fake'}]}):
            result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='already_uboot')
        self.assertFalse(result['ok'])
        self.assertIn('did not match', result['error'])

    def test_switch_refuses_latched_busy_or_unobserved_protected_session(self):
        clean = json.loads((self.session / 'state.json').read_text(encoding='utf-8'))
        for changes in ({'blocked': True}, {'reset_pending': True}, {'needs_observation': True},
                        {'operation': 'flash'}, {'loader_running': True}):
            self.write_state(dict(clean, **changes))
            with self.subTest(changes=changes), patch.object(self.adapter, '_pipe',
                    side_effect=AssertionError('No protected device operation')):
                result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='serial')
                self.assertFalse(result['ok'])

    def test_switch_of_already_current_package_is_a_device_free_noop(self):
        self.adapter.install_catalog(self.bundle(BASELINE))
        with patch.object(self.adapter, '_pipe', side_effect=AssertionError('No protected device operation')), \
             patch.object(self.adapter, 'status', side_effect=AssertionError('No inventory needed')):
            result = self.adapter.execute('switch_app', catalog_id='nes-test', entry_method='serial')
        self.assertTrue(result['ok'], result)
        self.assertTrue(result['data']['already_current'])
        self.assertFalse(result['data']['device_io'])

    def test_official_updater_requires_locally_configured_executable_and_hash(self):
        updater = self.root / 'M-UPGRADE-FM1.exe'
        updater.write_bytes(b'Offline fake executable; never launched')
        adapter = backend.WindowsBackend(self.repo, self.session,
                    official_updater=updater, official_updater_sha256='0' * 64)
        with patch.object(backend.sys, 'platform', 'win32'), \
             patch.object(backend.subprocess, 'Popen', side_effect=AssertionError('No execution')):
            self.assertFalse(self.adapter.execute('official_updater')['ok'])
            self.assertFalse(adapter.execute('official_updater')['ok'])
            with self.assertRaises(ValueError):
                adapter.execute('official_updater', request=prepared())

    def test_official_updater_handoff_launches_only_verified_local_executable(self):
        updater = self.root / 'M-UPGRADE-FM1.exe'
        updater.write_bytes(b'Offline fake executable; never launched')
        adapter = backend.WindowsBackend(self.repo, self.session,
                    official_updater=updater, official_updater_sha256=sha(updater.read_bytes()))
        with patch.object(backend.sys, 'platform', 'win32'), \
             patch.object(backend.subprocess, 'Popen', return_value=SimpleNamespace(pid=1234)) as launch:
            result = adapter.execute('official_updater')
        self.assertTrue(result['ok'], result)
        self.assertTrue(result['data']['handoff'])
        self.assertFalse(result['data']['written_verified'])
        launch.assert_called_once_with([str(updater)], cwd=updater.parent)

    def test_official_updater_refuses_latched_or_unobserved_protected_session(self):
        updater = self.root / 'M-UPGRADE-FM1.exe'
        updater.write_bytes(b'Offline fake executable; never launched')
        adapter = backend.WindowsBackend(self.repo, self.session,
                    official_updater=updater, official_updater_sha256=sha(updater.read_bytes()))
        clean = json.loads((self.session / 'state.json').read_text(encoding='utf-8'))
        for changes in ({'blocked': True}, {'reset_pending': True}, {'needs_observation': True},
                        {'operation': 'flash'}):
            self.write_state(dict(clean, **changes))
            with self.subTest(changes=changes), patch.object(backend.sys, 'platform', 'win32'), \
                 patch.object(backend.subprocess, 'Popen', side_effect=AssertionError('No updater launch')):
                self.assertFalse(adapter.execute('official_updater')['ok'])


if __name__ == '__main__':
    unittest.main()
