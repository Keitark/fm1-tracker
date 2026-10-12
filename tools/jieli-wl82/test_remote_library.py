"""Offline local library tests; the browser and all device I/O are fake."""
import base64
import copy
import hashlib
import http.client
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock
import uuid

import remote_bridge as bridge
from remote_catalog import Catalog, MAX_SET_BYTES

TOKEN = 'local-library-test-' + 'a' * 48
IMAGE = bytes(0x100000)
SHA = hashlib.sha256(IMAGE).hexdigest()


def bundle(ident='test-nes', title='NES demo'):
    return {'id': ident, 'profile': 'nes', 'title': title,
            'description': 'Offline test package', 'variant': 'Demo',
            'request': {'op': 'plan', 'image': base64.b64encode(IMAGE).decode(),
                        'sha256': SHA, 'baseline_sha256': SHA}}


class CatalogSetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.catalog = Catalog(self.root / 'catalog')

    def tearDown(self):
        self.temp.cleanup()

    def test_named_set_is_durable_stable_and_metadata_only(self):
        packages = [bundle('b'), bundle('a')]
        receipt = self.catalog.install_set('My apps', packages)
        self.assertEqual([item['id'] for item in receipt['members']], ['a', 'b'])
        self.assertFalse(receipt['device_io'])
        self.assertNotIn('request', json.dumps(receipt))
        self.assertNotIn('image', json.dumps(receipt))
        again = Catalog(self.root / 'catalog').install_set('My apps', list(reversed(packages)))
        self.assertEqual(receipt, again)
        self.assertEqual(self.catalog.listing()['sets'], [receipt])

    def test_unicode_display_metadata_receipt_is_readable_with_all_eight_members(self):
        packages = [bundle('unicode-' + str(index), '\U0001f33f' * 80) for index in range(8)]
        for package in packages:
            package['description'] = '\U0001f33f' * 500
            package['variant'] = '\U0001f33f' * 120
        receipt = self.catalog.install_set('譌･譛ｬ隱槭い繝励Μ', packages)
        self.assertEqual(self.catalog.sets(), [receipt])

    def test_entire_set_validation_precedes_writes(self):
        invalid = bundle('bad'); invalid['request']['sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            self.catalog.install_set('No partial set', [bundle('valid'), invalid])
        self.assertEqual(list(self.catalog.folder.iterdir()), [])
        with self.assertRaises(ValueError):
            self.catalog.install_set('Duplicates', [bundle(), bundle()])
        self.assertEqual(list(self.catalog.folder.iterdir()), [])

    def test_immutable_collision_preserves_existing_and_stages_nothing(self):
        original = bundle('fixed')
        self.catalog.install(original)
        changed = bundle('fixed', 'Changed title')
        with self.assertRaises(ValueError):
            self.catalog.install_set('Collision', [bundle('new'), changed])
        self.assertEqual(self.catalog.get('fixed'), original)
        self.assertFalse((self.catalog.folder / 'new.json').exists())
        self.assertFalse((self.catalog.folder / 'sets').exists())

    def test_ordinary_publish_failure_rolls_back_only_new_packages(self):
        self.catalog.install(bundle('existing'))
        real_link = os.link
        def fail_receipt(src, dst):
            if Path(src).name == 'receipt.json':
                raise OSError('offline injected failure')
            real_link(src, dst)
        with mock.patch('remote_catalog.os.link', side_effect=fail_receipt):
            with self.assertRaises(OSError):
                self.catalog.install_set('Rollback', [bundle('existing'), bundle('new')])
        self.assertEqual(self.catalog.get('existing')['id'], 'existing')
        self.assertFalse((self.catalog.folder / 'new.json').exists())
        self.assertEqual(list(self.catalog.folder.glob('.import-*')), [])
        self.assertEqual(self.catalog.sets(), [])

    def test_count_name_controls_and_unknown_fields_are_rejected(self):
        for name, packages in [(' ', [bundle()]), ('bad\nname', [bundle()]),
                               ('x' * 81, [bundle()]), ('Empty', []),
                               ('Too many', [bundle(str(i)) for i in range(9)])]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.catalog.install_set(name, packages)
        extra = bundle(); extra['path'] = '/not/accepted'
        with self.assertRaises(ValueError):
            self.catalog.install_set('Extra', [extra])
        controls = bundle(); controls['title'] = 'bad\x00title'
        with self.assertRaises(ValueError):
            self.catalog.install_set('Controls', [controls])

    def test_links_and_windows_reparse_points_never_followed(self):
        original_lstat = Path.lstat
        target = self.catalog.folder / 'linked.json'
        class Reparse:
            st_mode = 0o100600
            st_file_attributes = 0x400
        def reparse(path):
            if path == target:
                return Reparse()
            return original_lstat(path)
        with mock.patch.object(Path, 'lstat', reparse):
            with self.assertRaises(ValueError):
                self.catalog.install_set('Unsafe', [bundle('linked')])
        outside = self.root / 'outside.json'; outside.write_text(json.dumps(bundle('link')))
        try:
            (self.catalog.folder / 'link.json').symlink_to(outside)
        except OSError:
            return  # Windows without symbolic-link permission is covered above.
        with self.assertRaises(ValueError):
            self.catalog.get('link')
        with self.assertRaises(ValueError):
            self.catalog.install_set('Unsafe', [bundle('link')])

    def test_tampered_or_missing_member_receipt_is_not_listed(self):
        receipt = self.catalog.install_set('Verified', [bundle()])
        (self.catalog.folder / 'test-nes.json').unlink()
        self.assertEqual(self.catalog.sets(), [])
        self.catalog.install(bundle())
        path = self.catalog.folder / 'sets' / (receipt['id'] + '.json')
        changed = copy.deepcopy(receipt); changed['members'][0]['sha256'] = '0' * 64
        path.write_text(json.dumps(changed))
        self.assertEqual(self.catalog.sets(), [])


class LibraryBackend:
    def __init__(self, folder):
        self.catalog_store = Catalog(folder)
        self.device_calls = []

    def catalog(self):
        return self.catalog_store.listing()

    def install_library_set(self, name, bundles):
        return self.catalog_store.install_set(name, bundles)

    def status(self):
        self.device_calls.append('status')
        return {'device_io': False}

    def execute(self, *args, **kwargs):
        self.device_calls.append('execute')
        raise AssertionError('local import must never operate a device')


class LocalImportBridgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.backend = LibraryBackend(self.root / 'catalog')
        self.manager = bridge.JobManager(self.backend, self.root / 'state')
        self.server = bridge.make_server(self.manager, TOKEN, port=0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_port

    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join(5)
        self.manager.close(); self.temp.cleanup()

    def request(self, method, route, body=None, token=TOKEN, headers=None):
        headers = dict(headers or {})
        if token is not None:
            headers['Authorization'] = 'Bearer ' + token
        if isinstance(body, dict):
            body = json.dumps(body).encode(); headers.setdefault('Content-Type', 'application/json')
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=10)
        try:
            connection.request(method, route, body=body, headers=headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def launch(self, ident=None):
        ident = ident or uuid.uuid4().hex
        with mock.patch('remote_bridge.webbrowser.open', return_value=True) as opener:
            status, _, raw = self.request('POST', '/v1/library/open', {'id': ident})
        self.assertEqual(status, 200)
        url = opener.call_args.args[0]
        self.assertTrue(url.startswith('http://127.0.0.1:' + str(self.port) + '/library-import#'))
        scoped = url.split('#')[1]
        self.assertNotIn(scoped.encode(), raw)
        self.assertNotIn(b'127.0.0.1', raw)
        return ident, scoped, json.loads(raw)['data']

    def test_unauth_import_page_has_hashed_csp_and_no_authority(self):
        status, headers, raw = self.request('GET', '/library-import', token=None)
        self.assertEqual(status, 200)
        self.assertIn("script-src 'sha256-", headers['Content-Security-Policy'])
        self.assertNotIn("script-src 'unsafe-inline'", headers['Content-Security-Policy'])
        self.assertEqual(headers['Referrer-Policy'], 'no-referrer')
        self.assertEqual(headers['Cross-Origin-Resource-Policy'], 'same-origin')
        self.assertNotIn(TOKEN.encode(), raw)
        self.assertIn(b'history.replaceState', raw)
        for route in ('/v1/status', '/v1/catalog', '/v1/baseline'):
            self.assertEqual(self.request('GET', route, token=None)[0], 401)

    def test_scoped_token_only_imports_one_successful_set(self):
        ident, scoped, result = self.launch()
        self.assertTrue(result['opened']); self.assertFalse(result['device_io'])
        for route in ('/v1/status', '/v1/catalog', '/v1/baseline', '/v1/library/open/' + ident):
            self.assertEqual(self.request('GET', route, token=scoped)[0], 401)
        self.assertEqual(self.request('POST', '/v1/jobs', {'id': uuid.uuid4().hex, 'operation': 'environment'}, token=scoped)[0], 401)
        self.assertEqual(self.request('POST', '/v1/catalog', {}, token=scoped)[0], 401)
        self.assertEqual(self.request('POST', '/v1/library/open', {'id': uuid.uuid4().hex}, token=scoped)[0], 401)
        invalid = bundle(); invalid['request']['sha256'] = '0' * 64
        self.assertEqual(self.request('POST', '/v1/library/sets', {'name': 'Rejected', 'bundles': [invalid]}, token=scoped)[0], 400)
        status, _, raw = self.request('POST', '/v1/library/sets', {'name': 'My apps', 'bundles': [bundle()]}, token=scoped)
        self.assertEqual(status, 201); self.assertFalse(json.loads(raw)['data']['device_io'])
        self.assertEqual(self.request('POST', '/v1/library/sets', {}, token=scoped)[0], 401)
        catalog = json.loads(self.request('GET', '/v1/catalog')[2])['data']
        self.assertEqual(catalog['sets'][0]['name'], 'My apps')
        self.assertEqual(self.backend.device_calls, [])

    def test_launch_replays_and_restart_never_reopens_browser(self):
        ident, scoped, original = self.launch()
        with mock.patch('remote_bridge.webbrowser.open') as opener:
            result = json.loads(self.request('POST', '/v1/library/open', {'id': ident})[2])['data']
            self.assertTrue(result['opened']); opener.assert_not_called()
            self.manager.close()
            recovered = bridge.JobManager(self.backend, self.root / 'state')
            try:
                self.assertTrue(recovered.open_library({'id': ident}, self.port)['opened'])
                with self.assertRaises(bridge.BridgeError):
                    recovered.check_import_token(scoped)
                opener.assert_not_called()
            finally:
                recovered.close()

    def test_ambiguous_launch_is_durable_and_sanitized(self):
        ident = uuid.uuid4().hex
        with mock.patch('remote_bridge.webbrowser.open', side_effect=RuntimeError('secret path token')) as opener:
            first = json.loads(self.request('POST', '/v1/library/open', {'id': ident})[2])['data']
            again = json.loads(self.request('POST', '/v1/library/open', {'id': ident})[2])['data']
            self.assertEqual(opener.call_count, 1)
        self.assertEqual(first['error'], 'local_import_launch_unknown')
        self.assertFalse(again['opened'])
        self.assertNotIn('secret', json.dumps(first))
        self.assertEqual(json.loads(self.request('GET', '/v1/library/open/' + ident)[2])['data']['error'], 'local_import_launch_unknown')

    def test_persisted_launching_receipt_never_relaunches(self):
        ident = uuid.uuid4().hex; now = bridge.time.time()
        bridge._atomic_json(self.manager.library_launches_dir / (ident + '.json'),
                            {'id': ident, 'status': 'launching', 'created': now, 'expires': now + bridge.IMPORT_TTL})
        with mock.patch('remote_bridge.webbrowser.open') as opener:
            result = self.manager.open_library({'id': ident}, self.port)
            opener.assert_not_called()
        self.assertEqual(result['error'], 'local_import_launch_unknown')
        self.assertFalse(result['opened'])

    def test_expiry_failed_launch_and_active_operation_refusal(self):
        ident, scoped, _ = self.launch()
        future = bridge.time.time() + bridge.IMPORT_TTL + 1
        with mock.patch('remote_bridge.time.time', return_value=future):
            self.assertEqual(self.request('POST', '/v1/library/sets', {}, token=scoped)[0], 401)
            receipt = json.loads(self.request('GET', '/v1/library/open/' + ident)[2])['data']
            self.assertEqual(receipt['error'], 'local_import_session_expired')
        with mock.patch('remote_bridge.webbrowser.open', return_value=False):
            failed = self.manager.open_library({'id': uuid.uuid4().hex}, self.port)
            self.assertEqual(failed['error'], 'local_import_launch_failed')
        self.manager._active = 'f' * 32
        try:
            with mock.patch('remote_bridge.webbrowser.open') as opener:
                self.assertEqual(self.request('POST', '/v1/library/open', {'id': uuid.uuid4().hex})[0], 409)
                self.assertEqual(self.request('POST', '/v1/library/sets', {'name': 'Busy', 'bundles': [bundle()]})[0], 409)
                opener.assert_not_called()
        finally:
            self.manager._active = None
        self.assertEqual(self.backend.device_calls, [])

    def test_cross_origin_and_body_bounds_are_rejected_before_import(self):
        _, scoped, _ = self.launch()
        for headers in ({'Origin':'https://example.invalid'}, {'Sec-Fetch-Site':'cross-site'}):
            self.assertEqual(self.request('POST', '/v1/library/sets', {}, token=scoped, headers=headers)[0], 403)
        self.assertEqual(self.request('POST', '/v1/library/sets', b'', token=scoped,
                                      headers={'Content-Length':str(MAX_SET_BYTES+1)})[0], 413)
        self.assertEqual(self.request('POST', '/v1/library/sets', {'name':'Bad','bundles':[],'path':'secret'}, token=scoped)[0], 400)
        self.assertEqual(self.request('POST', '/v1/library/open', {'id':uuid.uuid4().hex,'url':'https://example.invalid'})[0], 400)
        self.assertEqual(self.backend.catalog_store.sets(), [])

    def test_up_to_eight_bundles_fit_library_limit_but_jobs_keep_old_limit(self):
        body = {'name':'Eight apps','bundles':[bundle('demo-' + str(i)) for i in range(8)]}
        self.assertGreater(len(json.dumps(body)), bridge.MAX_BODY)
        status, _, raw = self.request('POST', '/v1/library/sets', body)
        self.assertEqual(status, 201)
        self.assertEqual(len(json.loads(raw)['data']['members']), 8)
        self.assertEqual(self.request('POST', '/v1/jobs', b'', headers={'Content-Length':str(bridge.MAX_BODY+1)})[0], 413)


if __name__ == '__main__':
    unittest.main()
