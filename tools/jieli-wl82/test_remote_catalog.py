"""Offline catalog export tests using fake audited request producers."""
import base64
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_remote_catalog as catalog
import remote_catalog


class CatalogExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.root_patch = patch.object(catalog, 'ROOT', self.root)
        self.root_patch.start(); self.addCleanup(self.root_patch.stop)
        self.build = self.root / 'build/selected-profile'
        self.build.mkdir(parents=True)
        self.session = self.root / 'session'
        self.session.mkdir()
        self.baseline = bytes(catalog.worker.SIZE)
        (self.session / 'baseline.bin').write_bytes(self.baseline)
        self.digest = catalog.worker.sha(self.baseline)
        self.state = {'baseline_file': 'baseline.bin', 'baseline_sha256': self.digest,
                      'operation': 'idle', 'blocked': False, 'reset_pending': False, 'needs_observation': False}
        (self.session / 'state.json').write_text(json.dumps(self.state))
        receipt = {'status': 'written_and_readback_verified', 'full_readback_sha256': self.digest,
                   'full_readback_count': 1}
        (self.session / 'baseline-deployment.json').write_text(json.dumps(receipt))
        self.source = self.build / 'profile.c'
        self.source.write_bytes(b'fresh audited source')
        hashes = {str(self.source): catalog.worker.sha(self.source.read_bytes())}
        self.manifest = {'status': 'LINKED_USB_DIAGNOSTIC_UNTESTED', 'nes_smb1': True,
                         'mod': False, 'mdx': False, 'source_sha256': hashes,
                         'archive_sha256': hashes, 'vendor_overlay_sources': hashes}
        self.save_manifest()
        candidate = bytearray(self.baseline)
        candidate[0x4000] = 1
        self.request = {'op': 'plan', 'baseline_sha256': self.digest,
                        'sha256': catalog.worker.sha(candidate),
                        'image': base64.b64encode(candidate).decode('ascii')}
        self.out = self.root / 'tmp/catalog/game.json'

    def save_manifest(self):
        (self.build / 'build-manifest.json').write_text(json.dumps(self.manifest))

    def run_export(self, profile='nes', **changes):
        kwargs = dict(profile=profile, build=self.build, session=self.session, out=self.out,
                      identifier='selected-game', title='Selected game', description='Local private bundle')
        kwargs.update(changes)
        return catalog.export(**kwargs)

    def test_local_profile_uses_audited_prepare_and_exact_session_bytes(self):
        with patch.object(catalog.prepare_flash_request, 'prepare', return_value=self.request) as prepare:
            report = self.run_export()
        prepare.assert_called_once_with(self.build, self.digest, self.baseline)
        self.assertFalse(report['device_io'])
        bundle = json.loads(self.out.read_text())
        self.assertEqual(set(bundle), {'id', 'profile', 'title', 'description', 'variant', 'request'})
        self.assertEqual(bundle['request'], self.request)
        self.assertEqual(bundle['variant'], self.build.name)

    def test_exported_bundle_passes_real_catalog_schema_with_profile_default_description(self):
        with patch.object(catalog.prepare_flash_request, 'prepare', return_value=self.request):
            self.run_export(description='')
        bundle = json.loads(self.out.read_text())
        self.assertEqual(bundle['description'], remote_catalog.DESCRIPTIONS['nes'])
        self.assertEqual(remote_catalog.validate_bundle(bundle), base64.b64decode(self.request['image']))
        installed = remote_catalog.Catalog(self.root / 'installed').install(bundle)
        self.assertEqual(installed['id'], bundle['id'])

    def test_metadata_limits_match_catalog_before_producer(self):
        with patch.object(catalog.prepare_flash_request, 'prepare') as prepare:
            for values in ({'title': 'x' * 81}, {'description': 'x' * 501}):
                with self.subTest(values=values), self.assertRaises(ValueError):
                    self.run_export(**values)
            prepare.assert_not_called()

    def test_mdx_and_protracker_route_to_same_existing_audited_producer(self):
        for profile in ('mdx', 'protracker'):
            self.manifest.update(nes_smb1=False, mdx=profile == 'mdx', mod=profile == 'protracker')
            self.manifest['status'] = 'LINKED_MDX_KARAOKE_UNTESTED' if profile == 'mdx' else 'LINKED_USB_DIAGNOSTIC_UNTESTED'
            self.save_manifest()
            with self.subTest(profile=profile), patch.object(catalog.prepare_flash_request, 'prepare', return_value=self.request) as prepare:
                self.run_export(profile, out=self.root / ('tmp/catalog/' + profile + '.json'))
                prepare.assert_called_once_with(self.build, self.digest, self.baseline)

    def test_wrong_profile_stale_source_or_pending_session_refuses_before_prepare(self):
        with patch.object(catalog.prepare_flash_request, 'prepare') as prepare:
            with self.assertRaisesRegex(ValueError, 'standalone MOD editor'):
                self.run_export('protracker')
            self.source.write_bytes(b'changed source')
            with self.assertRaisesRegex(ValueError, 'Stale source'):
                self.run_export()
            self.state['reset_pending'] = True
            (self.session / 'state.json').write_text(json.dumps(self.state))
            with self.assertRaisesRegex(ValueError, 'idle verified'):
                self.run_export()
            prepare.assert_not_called()

    def test_existing_output_or_public_path_refuses_before_prepare(self):
        self.out.parent.mkdir(parents=True)
        self.out.write_text('original')
        with patch.object(catalog.prepare_flash_request, 'prepare') as prepare:
            with self.assertRaisesRegex(ValueError, 'never overwritten'):
                self.run_export()
            with self.assertRaisesRegex(ValueError, 'tmp directory'):
                self.run_export(out=self.root / 'public.json')
            prepare.assert_not_called()
        self.assertEqual(self.out.read_text(), 'original')

    def test_buddha_refuses_stock_tracker_boot_song(self):
        self.manifest.update(nes_smb1=False, mod=True, mod_default_song_sha256='0' * 64,
                             mod_default_song_bytes=999)
        self.save_manifest()
        folder = self.root / 'firmware/protracker/build/buddha'
        folder.mkdir(parents=True)
        song = b'exact NAMO.MOD example'
        (folder / 'NAMO.MOD').write_bytes(song)
        (folder / 'manifest.json').write_text(json.dumps({'sha256': catalog.worker.sha(song), 'mod_bytes': len(song)}))
        with patch.object(catalog.prepare_flash_request, 'prepare') as prepare:
            with self.assertRaisesRegex(ValueError, 'current tracker build is not Buddha'):
                self.run_export('buddha')
            prepare.assert_not_called()

    def test_buddha_requires_verified_embedded_array_and_song_hash(self):
        folder = self.root / 'firmware/protracker/build/buddha'
        folder.mkdir(parents=True)
        song = b'NAMO'
        (folder / 'NAMO.MOD').write_bytes(song)
        (folder / 'manifest.json').write_text(json.dumps({'sha256': catalog.worker.sha(song), 'mod_bytes': len(song)}))
        source = self.build / 'embedded_default_mod.c'
        source.write_text('const unsigned char fm1_mod_default_song[] = {0x4e,0x41,0x4d,0x4f,};')
        self.manifest.update(nes_smb1=False, mod=True, mod_default_song_sha256=catalog.worker.sha(song),
                             mod_default_song_bytes=len(song))
        self.manifest['source_sha256'][str(source)] = catalog.worker.sha(source.read_bytes())
        self.save_manifest()
        with patch.object(catalog.prepare_flash_request, 'prepare', return_value=self.request):
            self.run_export('buddha')
        self.assertEqual(json.loads(self.out.read_text())['profile'], 'buddha')
        source.write_text('const unsigned char fm1_mod_default_song[] = {0x42,};')
        self.manifest['source_sha256'][str(source)] = catalog.worker.sha(source.read_bytes())
        with self.assertRaisesRegex(ValueError, 'differs from exact NAMO'):
            catalog.verify_buddha_boot(self.manifest)

    def test_audited_producer_refusal_and_wrong_baseline_cannot_create_bundle(self):
        with patch.object(catalog.prepare_flash_request, 'prepare', side_effect=ValueError('stale producer artifact')):
            with self.assertRaisesRegex(ValueError, 'stale producer artifact'):
                self.run_export()
        bad_request = dict(self.request, baseline_sha256='0' * 64)
        with patch.object(catalog.prepare_flash_request, 'prepare', return_value=bad_request):
            with self.assertRaisesRegex(ValueError, 'Stale baseline'):
                self.run_export()
        self.assertFalse(self.out.exists())

    def test_doom_invokes_exact_cli_and_reads_only_its_private_request_path(self):
        repository = self.root / 'doom'
        build = repository / 'build/target-candidate'
        producer = repository / 'tools/prepare_flash_request.py'
        producer.parent.mkdir(parents=True); producer.write_text('reviewed offline producer')
        (repository / 'TARGET_CANDIDATE.md').write_text('Doom candidate')
        build.mkdir(parents=True)
        request_path = repository / 'build/flash-request/hash/flash-request.json'
        request_path.parent.mkdir(parents=True); request_path.write_text(json.dumps(self.request))
        completed = types.SimpleNamespace(returncode=0, stderr='', stdout=json.dumps({
            'status': 'offline_plan_ready', 'device_operations_performed': False, 'request': str(request_path)}))
        with patch.object(catalog.subprocess, 'run', return_value=completed) as run:
            request = catalog.prepare_doom(build, self.session)
        self.assertEqual(request, self.request)
        self.assertEqual(run.call_args.args[0], [sys.executable, str(producer), '--fm1-root', str(self.root),
                                                '--session', str(self.session), '--build', str(build)])
        self.assertNotIn('shell', run.call_args.kwargs)

    def local_mdx(self):
        app, elf = b'exact local MDX app', b'fake audited ELF'
        (self.build / 'fm1-usb-diag.app.bin').write_bytes(app)
        (self.build / 'fm1-usb-diag.elf').write_bytes(elf)
        self.manifest.update(nes_smb1=False, mdx=True, profile='MDX_KARAOKE_CANDIDATE',
                             peripheral_tests=True, application_sha256=catalog.worker.sha(app),
                             application_bytes=len(app))
        self.save_manifest()
        return app, elf

    def test_current_local_mdx_schema_uses_full_unchanged_static_package_audit(self):
        app, elf = self.local_mdx()
        reference = self.root / 'stock.bin'; reference.write_bytes(self.baseline)
        candidate = base64.b64decode(self.request['image'])
        build_candidate = unittest.mock.Mock(return_value=(candidate, {'candidate_sha256': self.request['sha256']}))
        pack = types.SimpleNamespace(V14_BACKUP=reference, build_candidate=build_candidate)
        with patch.dict(sys.modules, {'build_stock_image': pack}):
            self.run_export('mdx')
        build_candidate.assert_called_once_with(self.baseline, elf, app, require_peripherals=False,
            usb_only=True, usb_peripheral_tests=True, usb_nes=False, usb_mdx=True,
            usb_audio=False, usb_mod=False, rom='rom50')

    def test_local_mdx_audit_refusal_or_protected_byte_change_cannot_export(self):
        self.local_mdx()
        reference = self.root / 'stock.bin'; reference.write_bytes(self.baseline)
        pack = types.SimpleNamespace(V14_BACKUP=reference,
                                     build_candidate=unittest.mock.Mock(side_effect=ValueError('Wrong static layout')))
        with patch.dict(sys.modules, {'build_stock_image': pack}):
            with self.assertRaisesRegex(ValueError, 'Wrong static layout'):
                self.run_export('mdx')
        candidate = bytearray(base64.b64decode(self.request['image'])); candidate[-1] = 42
        pack.build_candidate = unittest.mock.Mock(return_value=(candidate, {'candidate_sha256': catalog.worker.sha(candidate)}))
        with patch.dict(sys.modules, {'build_stock_image': pack}):
            with self.assertRaisesRegex(ValueError, 'boot/config/reserved'):
                self.run_export('mdx')
        self.assertFalse(self.out.exists())

    def external_mdx(self):
        app = b'exact external MDX app'
        (self.build / 'fm1-mdx.app.bin').write_bytes(app)
        producer = self.root / 'external-mdx/scripts/build_target.py'
        producer.parent.mkdir(parents=True); producer.write_bytes(b'exact producer')
        self.manifest.update(status='LINKED_MDX_KARAOKE_UNTESTED', nes_smb1=False,
                             application_sha256=catalog.worker.sha(app), application_bytes=len(app), usb_audio=True)
        self.manifest['source_sha256'][str(producer)] = catalog.worker.sha(producer.read_bytes())
        self.save_manifest()
        reference = self.root / 'stock.bin'; reference.write_bytes(self.baseline)
        candidate = base64.b64decode(self.request['image'])
        pack = types.SimpleNamespace(V14_BACKUP=reference, BACKUP=reference, AREA=0x4000,
            decode_boot=unittest.mock.Mock(), _replace_application=unittest.mock.Mock(return_value=candidate),
            verify_candidate=unittest.mock.Mock(return_value={'candidate_sha256': self.request['sha256']}))
        return app, producer.parents[1], pack

    def test_external_mdx_preserves_exact_audit_then_all_stock_container_checks(self):
        app, repository, pack = self.external_mdx()
        with patch.dict(sys.modules, {'build_stock_image': pack}), \
                patch.object(catalog, 'audit_external_mdx', return_value={'static_audit': 'passed'}) as audit:
            self.run_export('mdx')
        audit.assert_called_once_with(repository, self.build, self.manifest)
        pack.decode_boot.assert_called_once_with(self.baseline)
        pack._replace_application.assert_called_once_with(self.baseline, app)
        pack.verify_candidate.assert_called_once_with(self.baseline, base64.b64decode(self.request['image']), app)

    def test_external_mdx_refused_audit_or_changed_reserved_bytes_stops_export(self):
        app, repository, pack = self.external_mdx()
        with patch.dict(sys.modules, {'build_stock_image': pack}), \
                patch.object(catalog, 'audit_external_mdx', side_effect=ValueError('Exact external audit refused')):
            with self.assertRaisesRegex(ValueError, 'Exact external audit refused'):
                self.run_export('mdx')
        pack._replace_application.assert_not_called()
        candidate = bytearray(base64.b64decode(self.request['image'])); candidate[-1] = 9
        pack._replace_application.return_value = candidate
        pack.verify_candidate.return_value = {'candidate_sha256': catalog.worker.sha(candidate)}
        with patch.dict(sys.modules, {'build_stock_image': pack}), \
                patch.object(catalog, 'audit_external_mdx', return_value={'static_audit': 'passed'}):
            with self.assertRaisesRegex(ValueError, 'boot/config/reserved'):
                self.run_export('mdx')
        self.assertFalse(self.out.exists())


if __name__ == '__main__':
    unittest.main()
