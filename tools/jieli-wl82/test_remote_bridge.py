"""Offline bridge contract tests. All HTTP uses loopback and a fake backend."""
import base64
import hashlib
import http.client
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import uuid

import remote_bridge as bridge


SIZE = 0x100000
TOKEN = 'offline-test-token-' + 'a' * 48
BASELINE = bytes(SIZE)
CANDIDATE = bytearray(BASELINE)
CANDIDATE[0x4000] = 1
CANDIDATE[0x4120] = 2
CANDIDATE = bytes(CANDIDATE)
FIRMWARE = bytes(range(256)) * 4096


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepared(image=CANDIDATE):
    return dict(op='plan', image=base64.b64encode(image).decode(),
                sha256=sha(image), baseline_sha256=sha(BASELINE))


def job(operation='environment', **fields):
    return dict(id=uuid.uuid4().hex, operation=operation, **fields)


class FakeBackend:
    def __init__(self):
        self.calls = []
        self.catalog_installs = []
        self.entered = threading.Event()
        self.release = threading.Event()
        self.release.set()
        self.failure = None
        self.response = None
        self.baseline_data = BASELINE
        self.firmware_data = FIRMWARE

    def status(self):
        return {'device_io': False, 'session_configured': True}

    def execute(self, operation, request=None, loader_state=None, catalog_id=None, entry_method=None):
        if operation in ('switch_app', 'plan_app'):
            self.calls.append((operation, catalog_id, entry_method))
        else:
            self.calls.append((operation, request, loader_state))
        self.entered.set()
        if not self.release.wait(10):
            raise TimeoutError('Offline test gate was not released')
        if self.failure is not None:
            raise self.failure
        if self.response is not None:
            return self.response
        return {'ok': True, 'data': {'operation': operation}}

    def baseline(self):
        return self.baseline_data

    def firmware(self, response):
        if response.get('ok') is not True:
            raise ValueError('No verified firmware')
        return self.firmware_data

    def catalog(self):
        return {'apps': [{'profile': 'nes', 'variants': []}]}

    def install_catalog(self, bundle):
        self.catalog_installs.append(bundle)
        return {'id': bundle['id'], 'device_io': False}


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.backend = FakeBackend()
        self.manager = bridge.JobManager(self.backend, self.root)
        self.server = bridge.make_server(self.manager, TOKEN, port=0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_address[1]

    def tearDown(self):
        self.backend.release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.manager.close()
        self.folder.cleanup()

    def request(self, method, route, body=None, token=TOKEN, headers=None):
        actual_headers = {} if headers is None else dict(headers)
        if token is not None:
            actual_headers['Authorization'] = 'Bearer ' + token
        if isinstance(body, dict):
            body = json.dumps(body).encode()
            actual_headers.setdefault('Content-Type', 'application/json')
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=5)
        try:
            connection.request(method, route, body=body, headers=actual_headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def submit(self, body):
        status, headers, raw = self.request('POST', '/v1/jobs', body)
        self.assertEqual(status, 202, raw)
        response = json.loads(raw)
        self.assertTrue(response['ok'], response)
        return response['data']

    def wait_job(self, identifier):
        deadline = time.monotonic() + 5
        while True:
            metadata = self.manager.get_job(identifier)
            if metadata['status'] in ('succeeded', 'failed', 'unknown'):
                return metadata
            if time.monotonic() > deadline:
                self.fail('Job did not complete: ' + repr(metadata))
            time.sleep(.01)

    def raw_request(self, raw):
        with socket.create_connection(('127.0.0.1', self.port), timeout=5) as connection:
            connection.sendall(raw)
            connection.shutdown(socket.SHUT_WR)
            chunks = []
            while True:
                data = connection.recv(65536)
                if not data:
                    break
                chunks.append(data)
        response = b''.join(chunks)
        self.assertTrue(response.startswith(b'HTTP/'), response)
        return int(response.split(b' ', 2)[1]), response

    def test_api_routes_require_bearer_authentication(self):
        identifier = uuid.uuid4().hex
        for method, route in (('GET', '/v1/status'), ('POST', '/v1/jobs'),
                              ('GET', '/v1/catalog'), ('POST', '/v1/catalog'),
                              ('GET', '/v1/jobs/' + identifier),
                              ('GET', '/v1/baseline'),
                              ('GET', '/v1/jobs/' + identifier + '/firmware')):
            with self.subTest(method=method, route=route):
                status, headers, raw = self.request(method, route, b'{', token=None)
                self.assertEqual(status, 401, raw)
                self.assertFalse(json.loads(raw)['ok'])
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(self.backend.catalog_installs, [])

    def test_authentication_precedes_body_framing_and_json_parsing(self):
        status, raw = self.raw_request(
            b'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\n'
            b'Content-Length: bad\r\nConnection: close\r\n\r\n{')
        self.assertEqual(status, 401, raw)
        self.assertEqual(self.backend.calls, [])

    def test_rejected_bearer_and_success_responses_never_echo_credentials(self):
        rejected = 'secret-rejected-token-' + uuid.uuid4().hex
        status, headers, raw = self.request('GET', '/v1/status', token=rejected)
        self.assertEqual(status, 401, raw)
        self.assertNotIn(rejected.encode(), raw)
        status, headers, raw = self.request('GET', '/v1/status')
        self.assertEqual(status, 200, raw)
        self.assertNotIn(TOKEN.encode(), raw)
        self.assertNotIn(TOKEN, json.dumps(headers))

    def test_status_reports_engine_and_backend_without_executing_jobs(self):
        status, headers, raw = self.request('GET', '/v1/status')
        self.assertEqual(status, 200, raw)
        data = json.loads(raw)['data']
        self.assertIsNone(data['engine']['active'])
        self.assertFalse(data['engine']['blocked_unknown'])
        self.assertFalse(data['device']['device_io'])
        self.assertEqual(self.backend.calls, [])

    def test_job_schema_rejects_paths_commands_and_extra_fields(self):
        for body in (job(command='erase'), job(path='C:\\Windows'),
                     job(arguments=['shell']), job('shell'),
                     dict(id='../../escape', operation='environment'),
                     dict(id=uuid.uuid4().hex.upper(), operation='environment'),
                     {'operation': 'environment'}, [], None):
            with self.subTest(body=body):
                raw_body = json.dumps(body).encode()
                status, headers, raw = self.request('POST', '/v1/jobs', raw_body,
                                                   headers={'Content-Type': 'application/json'})
                self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.calls, [])

    def test_image_job_requires_a_locally_prepared_plan_request(self):
        request = prepared()
        for edit in ({'op': 'flash'}, {'path': 'candidate.bin'},
                     {'command': 'write'}, {'baseline_sha256': 'G' * 64}):
            malformed = dict(request, **edit)
            status, headers, raw = self.request('POST', '/v1/jobs',
                                                job('flash', request=malformed))
            self.assertEqual(status, 400, raw)
        status, headers, raw = self.request('POST', '/v1/jobs', job('flash'))
        self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.calls, [])

    def test_image_request_requires_exact_one_mib(self):
        for image in (CANDIDATE[:-1], CANDIDATE + b'\0'):
            with self.subTest(length=len(image)):
                status, headers, raw = self.request('POST', '/v1/jobs',
                                                    job('plan', request=prepared(image)))
                self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.calls, [])

    def test_image_hash_and_base64_are_verified_before_backend_execution(self):
        for edits in ({'sha256': '0' * 64}, {'image': '!'}, {'sha256': sha(CANDIDATE).upper()}):
            status, headers, raw = self.request('POST', '/v1/jobs',
                                                job('plan', request=dict(prepared(), **edits)))
            self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.calls, [])

    def test_valid_prepared_image_reaches_only_the_selected_backend_operation(self):
        body = job('plan', request=prepared())
        self.submit(body)
        metadata = self.wait_job(body['id'])
        self.assertEqual(metadata['status'], 'succeeded')
        self.assertEqual(self.backend.calls, [('plan', body['request'], None)])

    def test_read_requires_explicit_cold_or_reuse_loader_state(self):
        for body in (job('read_firmware'), job('read_firmware', loader_state='guess'),
                     job('read_firmware', loader_state=True),
                     job('environment', loader_state='cold')):
            status, headers, raw = self.request('POST', '/v1/jobs', body)
            self.assertEqual(status, 400, raw)
        for loader_state in ('cold', 'reuse'):
            body = job('read_firmware', loader_state=loader_state)
            self.submit(body)
            self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')
        self.assertEqual([call[2] for call in self.backend.calls], ['cold', 'reuse'])

    def test_repeated_identical_id_executes_once_and_preserves_receipt(self):
        body = job()
        self.submit(body)
        completed = self.wait_job(body['id'])
        repeated = self.submit(body)
        self.assertEqual(repeated, completed)
        self.assertEqual(self.backend.calls, [('environment', None, None)])

    def test_same_id_with_changed_operation_or_candidate_is_a_conflict(self):
        body = job('plan', request=prepared())
        self.submit(body)
        self.wait_job(body['id'])
        changed = bytearray(CANDIDATE)
        changed[0x4121] = 3
        for replacement in (dict(id=body['id'], operation='environment'),
                            dict(body, request=prepared(bytes(changed)))):
            status, headers, raw = self.request('POST', '/v1/jobs', replacement)
            self.assertEqual(status, 409, raw)
        self.assertEqual(len(self.backend.calls), 1)

    def test_device_jobs_are_serialized_and_busy_submission_is_rejected(self):
        self.backend.release.clear()
        body = job('serial_status')
        self.submit(body)
        self.assertTrue(self.backend.entered.wait(5))
        repeated = self.submit(body)
        self.assertIn(repeated['status'], ('queued', 'running'))
        other = job('observe')
        status, headers, raw = self.request('POST', '/v1/jobs', other)
        self.assertEqual(status, 409, raw)
        self.assertEqual(len(self.backend.calls), 1)
        self.backend.release.set()
        self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')

    def test_client_disconnect_does_not_cancel_or_repeat_the_accepted_job(self):
        self.backend.release.clear()
        body = job('serial_status')
        encoded = json.dumps(body).encode()
        headers = (f'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\n'
                   f'Authorization: Bearer {TOKEN}\r\nContent-Type: application/json\r\n'
                   f'Content-Length: {len(encoded)}\r\nConnection: close\r\n\r\n').encode()
        with socket.create_connection(('127.0.0.1', self.port), timeout=5) as connection:
            connection.sendall(headers + encoded)
            self.assertTrue(self.backend.entered.wait(5))
        self.backend.release.set()
        self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')
        self.assertEqual(len(self.backend.calls), 1)

    def test_request_and_public_metadata_are_persisted_without_image_echo(self):
        body = job('plan', request=prepared())
        submitted = self.submit(body)
        completed = self.wait_job(body['id'])
        directory = self.root / 'jobs' / body['id']
        private = json.loads((directory / 'request.json').read_text(encoding='utf-8'))
        public = json.loads((directory / 'job.json').read_text(encoding='utf-8'))
        self.assertEqual(private, body)
        self.assertEqual(public, completed)
        for metadata in (submitted, completed):
            self.assertNotIn(body['request']['image'], json.dumps(metadata))
            self.assertNotIn('image', metadata)
        status, headers, raw = self.request('GET', '/v1/jobs/' + body['id'])
        self.assertEqual(status, 200, raw)
        self.assertEqual(json.loads(raw)['data'], completed)
        self.assertNotIn(body['request']['image'].encode(), raw)

    def test_backend_known_refusal_is_failed_and_does_not_retry(self):
        self.backend.response = {'ok': False, 'error': 'Known protected-session refusal'}
        body = job('reset')
        self.submit(body)
        self.assertEqual(self.wait_job(body['id'])['status'], 'failed')
        self.submit(body)
        self.assertEqual(len(self.backend.calls), 1)
        self.assertFalse(self.manager.status()['engine']['blocked_unknown'])

    def test_unknown_backend_outcome_latches_device_operations(self):
        self.backend.failure = TimeoutError('Pipe outcome unavailable')
        body = job('reset')
        self.submit(body)
        self.assertEqual(self.wait_job(body['id'])['status'], 'unknown')
        self.assertTrue(self.manager.status()['engine']['blocked_unknown'])
        self.backend.failure = None
        self.assert_unknown_blocks_device_jobs()
        self.assertEqual(len(self.backend.calls), 1)

    def assert_unknown_blocks_device_jobs(self):
        for operation in ('read_firmware', 'serial_status', 'observe', 'enter_uboot',
                          'reset', 'flash', 'retry_flash', 'recover_flash',
                          'switch_app', 'official_updater'):
            fields = {'request': prepared()} if operation in ('flash', 'retry_flash', 'recover_flash') else {}
            if operation == 'read_firmware':
                fields['loader_state'] = 'reuse'
            if operation == 'switch_app':
                fields.update(catalog_id='nes-test', entry_method='serial')
            status, headers, raw = self.request('POST', '/v1/jobs', job(operation, **fields))
            self.assertEqual(status, 409, raw)

    def test_unknown_outcome_preserves_plan_environment_and_inventory_diagnostics(self):
        self.backend.failure = RuntimeError('No terminal reply')
        body = job('reset')
        self.submit(body)
        self.wait_job(body['id'])
        self.backend.failure = None
        for body in (job('environment'), job('plan', request=prepared()),
                     job('plan_app', catalog_id='nes-test')):
            self.submit(body)
            self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')
        status, headers, raw = self.request('GET', '/v1/status')
        self.assertEqual(status, 200, raw)
        self.assertTrue(json.loads(raw)['data']['engine']['blocked_unknown'])

    def test_restart_marks_unfinished_jobs_unknown_and_blocks_device_jobs(self):
        body = job('serial_status')
        self.submit(body)
        self.wait_job(body['id'])
        self.manager.close()
        metadata_path = self.root / 'jobs' / body['id'] / 'job.json'
        original = json.loads(metadata_path.read_text(encoding='utf-8'))
        for previous in ('running', 'queued'):
            metadata = dict(original, status=previous, result=None)
            metadata_path.write_text(json.dumps(metadata), encoding='utf-8')
            self.manager = bridge.JobManager(self.backend, self.root)
            # Restart the HTTP listener with the newly loaded job manager.
            self.server.shutdown()
            self.server.server_close()
            self.thread.join(timeout=5)
            self.server = bridge.make_server(self.manager, TOKEN, port=0)
            self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
            self.thread.start()
            self.port = self.server.server_address[1]
            self.assertEqual(self.manager.get_job(body['id'])['status'], 'unknown')
            self.assert_unknown_blocks_device_jobs()
            self.assertEqual(len(self.backend.calls), 1)
            self.manager.close()
        # Restore a live instance so normal fixture cleanup remains valid.
        self.manager = bridge.JobManager(self.backend, self.root)

    def test_completed_receipt_survives_restart_without_duplicate_execution(self):
        body = job()
        self.submit(body)
        completed = self.wait_job(body['id'])
        self.manager.close()
        self.manager = bridge.JobManager(self.backend, self.root)
        repeated = self.manager.submit(body)
        self.assertEqual(repeated, completed)
        self.assertEqual(len(self.backend.calls), 1)

    def test_state_directory_lock_excludes_another_process(self):
        script = ('import sys\nfrom remote_bridge import JobManager, BridgeError\n'
                  'try:\n    manager = JobManager(None, sys.argv[1])\n'
                  'except BridgeError as error:\n    print(error.status)\n    print(str(error))\n'
                  'else:\n    manager.close()\n    raise SystemExit("Unexpected second owner")\n')
        result = subprocess.run([sys.executable, '-c', script, str(self.root)],
                                cwd=Path(__file__).resolve().parent,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('409', result.stdout)
        self.assertIn('owns', result.stdout)

    def test_baseline_binary_has_fixed_size_type_and_sha256_header(self):
        status, headers, raw = self.request('GET', '/v1/baseline')
        self.assertEqual(status, 200, raw[:100])
        self.assertEqual(raw, BASELINE)
        self.assertEqual(headers['Content-Type'], 'application/octet-stream')
        self.assertEqual(headers['X-FM1-SHA256'], sha(BASELINE))

    def test_firmware_download_requires_successful_read_job(self):
        body = job('read_firmware', loader_state='cold')
        self.submit(body)
        self.wait_job(body['id'])
        status, headers, raw = self.request('GET', '/v1/jobs/' + body['id'] + '/firmware')
        self.assertEqual(status, 200, raw[:100])
        self.assertEqual(raw, FIRMWARE)
        self.assertEqual(headers['X-FM1-SHA256'], sha(FIRMWARE))
        other = job('environment')
        self.submit(other)
        self.wait_job(other['id'])
        status, headers, raw = self.request('GET', '/v1/jobs/' + other['id'] + '/firmware')
        self.assertEqual(status, 409, raw)

    def test_binary_routes_reject_short_backend_artifacts(self):
        self.backend.baseline_data = BASELINE[:-1]
        status, headers, raw = self.request('GET', '/v1/baseline')
        self.assertNotEqual(status, 200, raw[:100])
        body = job('read_firmware', loader_state='reuse')
        self.submit(body)
        self.wait_job(body['id'])
        self.backend.firmware_data = FIRMWARE[:-1]
        status, headers, raw = self.request('GET', '/v1/jobs/' + body['id'] + '/firmware')
        self.assertNotEqual(status, 200, raw[:100])

    def test_artifact_routes_reject_arbitrary_paths(self):
        for route in ('/v1/files/state.json', '/v1/jobs/../../state.json/firmware',
                      '/v1/jobs/%2e%2e/firmware', '/v1/baseline/../state.json'):
            status, headers, raw = self.request('GET', route)
            self.assertEqual(status, 404, raw)
        self.assertEqual(self.backend.calls, [])

    def test_artifact_routes_reject_query_arguments(self):
        status, headers, raw = self.request('GET', '/v1/baseline?path=state.json')
        self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.calls, [])

    def test_duplicate_json_keys_are_rejected_before_job_creation(self):
        identifier = uuid.uuid4().hex
        body = ('{"id":"' + identifier + '","operation":"environment",'
                '"operation":"reset"}').encode()
        status, headers, raw = self.request('POST', '/v1/jobs', body,
                                           headers={'Content-Type': 'application/json'})
        self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.calls, [])

    def test_invalid_or_missing_content_length_is_rejected_before_body_read(self):
        for length in (None, 'bad', '-1'):
            line = '' if length is None else 'Content-Length: ' + length + '\r\n'
            raw = (f'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\n'
                   f'Authorization: Bearer {TOKEN}\r\n{line}Connection: close\r\n\r\n').encode()
            status, response = self.raw_request(raw)
            self.assertEqual(status, 411, response)
        self.assertEqual(self.backend.calls, [])

    def test_oversized_content_length_is_rejected_without_waiting_for_body(self):
        raw = (f'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\n'
               f'Authorization: Bearer {TOKEN}\r\nContent-Length: 67108864\r\n'
               f'Connection: close\r\n\r\n').encode()
        status, response = self.raw_request(raw)
        self.assertEqual(status, 413, response)
        self.assertEqual(self.backend.calls, [])

    def test_transfer_encoding_and_duplicate_content_lengths_are_rejected(self):
        for framing, expected in (('Transfer-Encoding: chunked\r\n', 400),
                                  ('Content-Length: 2\r\nContent-Length: 2\r\n', 411),
                                  ('Content-Length: 2\r\nTransfer-Encoding: chunked\r\n', 400)):
            raw = (f'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\n'
                   f'Authorization: Bearer {TOKEN}\r\n{framing}'
                   f'Connection: close\r\n\r\n{{}}').encode()
            status, response = self.raw_request(raw)
            self.assertEqual(status, expected, response)
        self.assertEqual(self.backend.calls, [])

    def test_truncated_body_is_rejected_without_executing_a_job(self):
        raw = (f'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\n'
               f'Authorization: Bearer {TOKEN}\r\nContent-Length: 100\r\n'
               f'Connection: close\r\n\r\n{{}}').encode()
        status, response = self.raw_request(raw)
        self.assertEqual(status, 400, response)
        self.assertEqual(self.backend.calls, [])

    def test_catalog_listing_uses_authenticated_fixed_route_without_device_operation(self):
        status, headers, raw = self.request('GET', '/v1/catalog')
        self.assertEqual(status, 200, raw)
        self.assertEqual(json.loads(raw)['data'], self.backend.catalog())
        self.assertEqual(self.backend.calls, [])

    def test_catalog_install_accepts_only_packaged_data_without_device_io(self):
        bundle = dict(id='nes-test', profile='nes', title='NES Test', description='Offline fixture',
                      variant='Test ROM', request=prepared())
        status, headers, raw = self.request('POST', '/v1/catalog', bundle)
        self.assertEqual(status, 201, raw)
        self.assertEqual(self.backend.catalog_installs, [bundle])
        self.assertEqual(self.backend.calls, [])
        self.assertNotIn(bundle['request']['image'].encode(), raw)

    def test_catalog_install_rejects_remote_paths_and_unverified_images(self):
        bundle = dict(id='nes-test', profile='nes', title='NES Test', description='Offline fixture',
                      variant='Test ROM', request=prepared())
        for body in (dict(bundle, path='F:\\candidate.bin'), dict(bundle, id='../escape'),
                     dict(bundle, request=dict(prepared(), sha256='0' * 64))):
            status, headers, raw = self.request('POST', '/v1/catalog', body)
            self.assertEqual(status, 400, raw)
        self.assertEqual(self.backend.catalog_installs, [])
        self.assertEqual(self.backend.calls, [])

    def test_switch_app_accepts_only_catalog_slug_and_explicit_entry_method(self):
        body = job('switch_app', catalog_id='nes-test', entry_method='serial')
        self.submit(body)
        self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')
        self.assertEqual(self.backend.calls, [('switch_app', 'nes-test', 'serial')])
        for fields in ({'catalog_id': '../escape', 'entry_method': 'serial'},
                       {'catalog_id': 'nes-test', 'entry_method': 'guess'},
                       {'catalog_id': 'nes-test'},
                       {'catalog_id': 'nes-test', 'entry_method': 'serial', 'path': 'candidate.bin'}):
            status, headers, raw = self.request('POST', '/v1/jobs', job('switch_app', **fields))
            self.assertEqual(status, 400, raw)
        self.assertEqual(len(self.backend.calls), 1)

    def test_plan_app_accepts_only_a_catalog_slug_without_entry_or_write_parameters(self):
        body = job('plan_app', catalog_id='nes-test')
        self.submit(body)
        self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')
        self.assertEqual(self.backend.calls, [('plan_app', 'nes-test', None)])
        for fields in ({'catalog_id': '../escape'}, {},
                       {'catalog_id': 'nes-test', 'entry_method': 'serial'},
                       {'catalog_id': 'nes-test', 'request': prepared()}):
            status, headers, raw = self.request('POST', '/v1/jobs', job('plan_app', **fields))
            self.assertEqual(status, 400, raw)
        self.assertEqual(len(self.backend.calls), 1)

    def test_successful_official_updater_handoff_persistently_blocks_device_operations(self):
        body = job('official_updater')
        self.submit(body)
        self.assertEqual(self.wait_job(body['id'])['status'], 'succeeded')
        engine = self.manager.status()['engine']
        self.assertTrue(engine['updater_handoff'])
        self.assertTrue(engine['blocked_unknown'])
        self.assert_unknown_blocks_device_jobs()
        self.manager.close()
        self.manager = bridge.JobManager(self.backend, self.root)
        self.assertTrue(self.manager.status()['engine']['updater_handoff'])
        self.assertTrue(self.manager.status()['engine']['blocked_unknown'])
        self.assertEqual(len(self.backend.calls), 1)

    def test_unknown_backend_exception_does_not_echo_private_error_text(self):
        self.backend.failure = RuntimeError(TOKEN + ' private-path-and-request-data')
        body = job('reset')
        self.submit(body)
        self.wait_job(body['id'])
        status, headers, raw = self.request('GET', '/v1/jobs/' + body['id'])
        self.assertEqual(status, 200, raw)
        self.assertNotIn(TOKEN.encode(), raw)
        self.assertNotIn(b'private-path-and-request-data', raw)

    def test_duplicate_authorization_headers_are_rejected(self):
        raw = (f'GET /v1/status HTTP/1.1\r\nHost: localhost\r\n'
               f'Authorization: Bearer {TOKEN}\r\nAuthorization: Bearer {TOKEN}\r\n'
               f'Connection: close\r\n\r\n').encode()
        status, response = self.raw_request(raw)
        self.assertEqual(status, 401, response)
        self.assertNotIn(TOKEN.encode(), response)

    def test_expected_continue_authenticates_before_accepting_body(self):
        raw = (b'POST /v1/jobs HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\n'
               b'Content-Length: 100\r\nConnection: close\r\n\r\n')
        status, response = self.raw_request(raw)
        self.assertEqual(status, 401, response)
        self.assertNotIn(b'100 Continue', response)
        self.assertEqual(self.backend.calls, [])


if __name__ == '__main__':
    unittest.main()
