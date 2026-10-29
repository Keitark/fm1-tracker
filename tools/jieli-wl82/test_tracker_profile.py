"""Tracker USB profile and offline packaging regressions. No device/session I/O.

Run the final linked candidate with --require-actual. Without that switch,
metadata tests still run while an incomplete actual candidate is reported as
skipped. The retained private V14 is read only; no image or security bytes are
printed or written outside temporary test fixtures.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

import prepare_flash_request as producer

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'firmware/nes'))
import build_stock_image as pack

BUILD = ROOT / 'firmware/usb-diag/build/tracker-workflow20-av'
REQUIRE_ACTUAL = False
_MISSING = object()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_baseline():
    data = pack.V14_BACKUP.read_bytes()
    if len(data) != 0x100000 or sha(data) != pack.V14_SHA:
        raise ValueError('The retained V14 baseline does not match its reviewed digest')
    return data


class ProfileMetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = read_baseline()
        cls.baseline_sha = sha(cls.baseline)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        app = b'profile-check application placeholder'
        (self.folder / 'fm1-usb-diag.app.bin').write_bytes(app)
        (self.folder / 'fm1-usb-diag.elf').write_bytes(b'ELF placeholder; auditor mocked for metadata-only tests')
        self.manifest = dict(
            status='LINKED_USB_DIAGNOSTIC_UNTESTED', mod=True, factory_diag=False,
            peripheral_tests=True, nes_smb1=False, mdx=False,
            usb_audio=True, usb_video=True, mod_usb_audio=True, mod_uvc=True,
            application_sha256=sha(app), source_sha256={}, archive_sha256={},
            vendor_overlay_sources={}, flashable=False,
        )

    def write_manifest(self, changes=None):
        manifest = dict(self.manifest)
        for name, value in (changes or {}).items():
            if value is _MISSING:
                manifest.pop(name, None)
            else:
                manifest[name] = value
        (self.folder / 'build-manifest.json').write_text(json.dumps(manifest), encoding='utf-8')

    def prepare(self):
        return producer.prepare(self.folder, self.baseline_sha, self.baseline)

    def test_matching_tracker_usb_flags_reach_the_mod_elf_audit(self):
        self.write_manifest()
        with patch.object(pack, 'build_candidate', return_value=(self.baseline, {'candidate_sha256': self.baseline_sha})) as build:
            result = self.prepare()
        self.assertEqual(result['sha256'], self.baseline_sha)
        flags = build.call_args.kwargs
        self.assertIs(flags['usb_mod'], True)
        self.assertIs(flags['usb_audio'], True)
        self.assertIs(flags['usb_video'], True)
        self.assertIs(flags['factory_diag'], False)
        self.assertIs(flags['usb_nes'], False)

    def test_missing_mismatched_or_nonboolean_tracker_class_flags_are_rejected(self):
        rejected = [dict.fromkeys(('mod_usb_audio', 'mod_uvc'), _MISSING)]
        for name in ('usb_audio', 'usb_video', 'mod_usb_audio', 'mod_uvc'):
            rejected.append({name: _MISSING})
            rejected.extend({name: value} for value in (False, 0, 1, 'true', None))
        for changes in rejected:
            with self.subTest(fields=tuple(changes), values=tuple(str(v) for v in changes.values())):
                self.write_manifest(changes)
                with patch.object(pack, 'build_candidate') as build, self.assertRaises(ValueError):
                    self.prepare()
                build.assert_not_called()

    def test_factory_claims_and_hybrid_profiles_cannot_use_tracker_admission(self):
        for changes in (
            {'mod': False}, {'mod': 1}, {'mod': 'true'},
            {'factory_diag': True}, {'nes_smb1': True}, {'mdx': True},
            {'status': 'BUILDING_OR_FAILED'},
        ):
            with self.subTest(changes=changes):
                self.write_manifest(changes)
                with patch.object(pack, 'build_candidate') as build, self.assertRaises(ValueError):
                    self.prepare()
                build.assert_not_called()

    def test_factory_uvc_still_requires_explicit_schema3_and_valid_hal(self):
        hal = dict.fromkeys(('buttons', 'encoders', 'volume', 'lcd', 'audio'), True)
        self.manifest.update(mod=False, factory_diag=True, factory_schema=1,
                             profile='FM1_FORGE_FACTORY_DIAG_V1', asset_free=True,
                             external_media_required=False, forge_project_schema=3,
                             forge_hal_access=hal)
        self.write_manifest()
        with patch.object(pack, 'build_candidate', return_value=(self.baseline, {'candidate_sha256': self.baseline_sha})) as build:
            self.prepare()
        self.assertIs(build.call_args.kwargs['factory_diag'], True)
        self.assertIs(build.call_args.kwargs['usb_mod'], False)
        self.assertEqual(build.call_args.kwargs['factory_project_schema'], 3)
        for changes in ({'forge_project_schema': 1}, {'forge_project_schema': 2},
                        {'forge_project_schema': _MISSING}, {'forge_project_schema': True},
                        {'forge_hal_access': dict(hal, audio=1)}, {'mod': True}):
            with self.subTest(fields=tuple(changes)):
                self.write_manifest(changes)
                with patch.object(pack, 'build_candidate') as build, self.assertRaises(ValueError):
                    self.prepare()
                build.assert_not_called()

    def test_direct_packaging_keeps_factory_uvc_schema_gate(self):
        hal = dict.fromkeys(('buttons', 'encoders', 'volume', 'lcd', 'audio'), True)
        for schema, video in ((1, True), (2, True), (3, 1), (3, 'true')):
            with self.subTest(schema=schema, video=video), patch.object(pack, 'audit') as audit, self.assertRaises(ValueError):
                pack.build_candidate(b'', b'', b'', factory_diag=True,
                                     factory_project_schema=schema, factory_hal_access=hal,
                                     usb_video=video)
            audit.assert_not_called()
        for fields in (dict(usb_mod=True, usb_video=True),
                       dict(factory_diag=True, factory_project_schema=3,
                            factory_hal_access=hal, usb_video=True)):
            with self.subTest(fields=tuple(fields)), patch.object(pack, 'audit', side_effect=RuntimeError('reached linked audit')) as audit:
                with self.assertRaisesRegex(RuntimeError, 'reached linked audit'):
                    pack.build_candidate(b'', b'', b'', **fields)
                self.assertTrue(audit.call_args.kwargs['usb_video'])


class ActualTrackerCandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        manifest_file = BUILD / 'build-manifest.json'
        if not manifest_file.is_file():
            reason = 'Actual tracker AV candidate manifest is absent'
        else:
            cls.manifest = json.loads(manifest_file.read_text(encoding='utf-8'))
            reason = None if cls.manifest.get('status') == 'LINKED_USB_DIAGNOSTIC_UNTESTED' else 'Actual tracker AV candidate has not passed its linked audit'
        if reason:
            if REQUIRE_ACTUAL:
                raise ValueError(reason)
            raise unittest.SkipTest(reason)
        for name in ('mod', 'mod_usb_audio', 'mod_uvc', 'usb_audio', 'usb_video'):
            if cls.manifest.get(name) is not True:
                raise ValueError('Actual tracker AV candidate lacks explicit boolean ' + name)
        if cls.manifest.get('factory_diag', False) is not False:
            raise ValueError('Actual tracker candidate is labelled as factory diagnostics')
        cls.baseline = read_baseline()
        cls.baseline_sha = sha(cls.baseline)
        cls.app = (BUILD / 'fm1-usb-diag.app.bin').read_bytes()
        cls.request = producer.prepare(BUILD, cls.baseline_sha, cls.baseline)
        cls.image = base64.b64decode(cls.request['image'], validate=True)

    def copy_build(self, folder, changes=None):
        manifest = json.loads(json.dumps(self.manifest))
        manifest.update(changes or {})
        for name in ('fm1-usb-diag.app.bin', 'fm1-usb-diag.elf'):
            shutil.copyfile(BUILD / name, folder / name)
        (folder / 'build-manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        return manifest

    def test_actual_candidate_passes_audit_and_preserves_every_outside_app_byte(self):
        self.assertEqual(len(self.image), 0x100000)
        self.assertEqual(self.request['sha256'], sha(self.image))
        report = pack.verify_candidate(self.baseline, self.image, self.app)
        self.assertEqual(len(report['checks']), 9)
        self.assertTrue(all(report['checks'].values()), 'A package preservation/CRC check failed')
        for start, end in ((0, 0x4000), (0x4040, 0x4120),
                           (0x4120 + len(self.app), 0x100000)):
            self.assertTrue(self.image[start:end] == self.baseline[start:end],
                            'Candidate changed bytes outside the app/directory scope')
        self.assertIs(report['device_operations_performed'], False)
        self.assertIs(report['flashable'], False)
        self.assertIs(self.manifest.get('flashable'), False)

    def test_linked_usb_classes_cannot_be_hidden_by_relabelling_both_flags(self):
        for generic, explicit in (('usb_audio', 'mod_usb_audio'), ('usb_video', 'mod_uvc')):
            with self.subTest(disabled_class=generic), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                self.copy_build(folder, {generic: False, explicit: False})
                with self.assertRaises(ValueError):
                    producer.prepare(folder, self.baseline_sha, self.baseline)

    def test_tracker_elf_cannot_be_relabelled_as_factory_schema3(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            self.copy_build(folder, dict(mod=False, factory_diag=True, factory_schema=1,
                profile='FM1_FORGE_FACTORY_DIAG_V1', forge_project_schema=3,
                forge_hal_access=dict.fromkeys(('buttons', 'encoders', 'volume', 'lcd', 'audio'), True),
                asset_free=True, external_media_required=False))
            with self.assertRaises(ValueError):
                producer.prepare(folder, self.baseline_sha, self.baseline)

    def test_stale_source_and_changed_app_are_rejected_before_packaging(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            manifest = self.copy_build(folder)
            source = folder / 'bound-input.c'
            source.write_bytes(b'original source\n')
            manifest['source_sha256'][str(source)] = sha(source.read_bytes())
            (folder / 'build-manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
            source.write_bytes(b'changed source\n')
            with patch.object(pack, 'build_candidate') as build, self.assertRaisesRegex(ValueError, 'Stale source'):
                producer.prepare(folder, self.baseline_sha, self.baseline)
            build.assert_not_called()
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            self.copy_build(folder)
            changed = bytearray(self.app)
            changed[0] ^= 1
            (folder / 'fm1-usb-diag.app.bin').write_bytes(changed)
            with patch.object(pack, 'build_candidate') as build, self.assertRaisesRegex(ValueError, 'Stale app'):
                producer.prepare(folder, self.baseline_sha, self.baseline)
            build.assert_not_called()

    def test_current_baseline_changes_outside_app_are_not_overwritten(self):
        changed = bytearray(self.baseline)
        changed[0x95000] ^= 1
        changed = bytes(changed)
        with patch.object(pack, 'build_candidate', return_value=(self.image, {'candidate_sha256': sha(self.image)})), self.assertRaisesRegex(ValueError, 'outside application scope'):
            producer.prepare(BUILD, sha(changed), changed)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--build', type=Path, default=BUILD)
    parser.add_argument('--require-actual', action='store_true')
    options, remaining = parser.parse_known_args()
    BUILD = options.build.resolve()
    REQUIRE_ACTUAL = options.require_actual
    unittest.main(argv=[sys.argv[0], *remaining])
