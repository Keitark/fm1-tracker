"""Offline tracker identity seeds using temporary inputs; no target builds."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

BUILD = Path(__file__).resolve().parents[2] / 'usb-diag/build.py'
sys.path.insert(0, str(BUILD.parent))
spec = importlib.util.spec_from_file_location('fm1_tracker_identity_build', BUILD)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.fixture, self.files = self.make_fixture(self.folder / 'first')

    def make_fixture(self, folder, *, root=None, sdk=None, out=None):
        root = root or folder / 'checkout'
        sdk = sdk or folder / 'vendor-sdk'
        out = out or folder / 'build-output'
        files = dict(source=root / 'firmware/protracker/mod.c',
                     second=root / 'firmware/protracker/routing.c',
                     header=root / 'firmware/protracker/mod.h',
                     script=root / 'firmware/usb-diag/build.py',
                     sdk_header=sdk / 'include/platform.h',
                     archive=sdk / 'lib/libsystem.a',
                     second_archive=sdk / 'lib/libaudio.a',
                     overlay=out / 'sdk_overlay/keyscan.c',
                     generated=out / 'fm1-tracker-build-id.c')
        for name, path in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('fixture ' + name + '\n').encode('ascii'))
        fixture = dict(paths=[files[name] for name in ('source', 'second', 'header', 'script',
                                                      'sdk_header', 'overlay', 'generated')],
                       archives=[files['archive'], files['second_archive']],
                       sources=[files['source'], files['second'], files['overlay'], files['generated']],
                       out=out, speed_sources=[files['second'], files['generated']],
                       defines=['-DFM1_MOD=1', '-DFM1_UVC=1'],
                       flags=['-O2', '-fno-strict-aliasing'],
                       includes=['-I' + str(root / 'firmware/protracker'),
                                 '-I' + str(sdk / 'include'), '-I' + str(out / 'sdk_overlay')],
                       sdk_pin='a' * 40, root=root, sdk=sdk)
        return fixture, files

    def identity(self, **changes):
        return build.tracker_build_identity(**dict(self.fixture, **changes))

    def assert_bytes_change_identity(self, name):
        original, _ = self.identity()
        path = self.files[name]
        path.write_bytes(path.read_bytes() + b'changed input\n')
        changed, _ = self.identity()
        self.assertNotEqual(changed, original)

    def test_repeated_inputs_return_same_lowercase_digest_and_payload(self):
        digest, payload = self.identity()
        self.assertRegex(digest, r'^[0-9a-f]{64}$')
        self.assertEqual(self.identity(), (digest, payload))
        self.assertIn('firmware/protracker/mod.c', payload['inputs'])
        self.assertIn('sdk/include/platform.h', payload['inputs'])
        self.assertIn('$BUILD/sdk_overlay/keyscan.c', payload['inputs'])
        self.assertIn('sdk/lib/libsystem.a', payload['archives'])

    def test_checkout_and_sdk_location_do_not_change_identity(self):
        relocated, _ = self.make_fixture(self.folder / 'relocated-checkout')
        self.assertEqual(build.tracker_build_identity(**relocated), self.identity())

    def test_output_directory_does_not_change_identity(self):
        relocated, _ = self.make_fixture(self.folder / 'second',
                                         root=self.fixture['root'], sdk=self.fixture['sdk'],
                                         out=self.folder / 'different-output-name')
        self.assertEqual(build.tracker_build_identity(**relocated), self.identity())

    def test_generated_identity_source_bytes_are_excluded_from_seed(self):
        original = self.identity()
        self.files['generated'].write_bytes(b'completely different generated identity\n')
        self.assertEqual(self.identity(), original)
        self.assertNotIn('fm1-tracker-build-id.c', json.dumps(original[1]))

    def test_generated_identity_source_need_not_exist_when_seeding(self):
        original = self.identity()
        self.files['generated'].unlink()
        self.assertEqual(self.identity(), original)

    def test_source_bytes_change_identity(self):
        self.assert_bytes_change_identity('source')

    def test_header_bytes_change_identity(self):
        self.assert_bytes_change_identity('header')

    def test_build_script_bytes_change_identity(self):
        self.assert_bytes_change_identity('script')

    def test_sdk_header_bytes_change_identity(self):
        self.assert_bytes_change_identity('sdk_header')

    def test_archive_bytes_change_identity(self):
        self.assert_bytes_change_identity('archive')

    def test_overlay_bytes_change_identity(self):
        self.assert_bytes_change_identity('overlay')

    def test_compiler_flag_values_change_identity(self):
        self.assertNotEqual(self.identity(flags=['-O3', '-fno-strict-aliasing'])[0], self.identity()[0])

    def test_compiler_flag_order_changes_identity(self):
        self.assertNotEqual(self.identity(flags=list(reversed(self.fixture['flags'])))[0], self.identity()[0])

    def test_define_values_change_identity(self):
        self.assertNotEqual(self.identity(defines=['-DFM1_MOD=1', '-DFM1_UVC=0'])[0], self.identity()[0])

    def test_define_order_changes_identity(self):
        self.assertNotEqual(self.identity(defines=list(reversed(self.fixture['defines'])))[0], self.identity()[0])

    def test_include_order_changes_identity(self):
        self.assertNotEqual(self.identity(includes=list(reversed(self.fixture['includes'])))[0], self.identity()[0])

    def test_source_order_changes_identity(self):
        self.assertNotEqual(self.identity(sources=list(reversed(self.fixture['sources'])))[0], self.identity()[0])

    def test_speed_source_selection_changes_identity(self):
        self.assertNotEqual(self.identity(speed_sources=[self.files['source']])[0], self.identity()[0])

    def test_sdk_pin_changes_identity(self):
        self.assertNotEqual(self.identity(sdk_pin='b' * 40)[0], self.identity()[0])

    def test_inventory_iteration_order_does_not_change_identity(self):
        self.assertEqual(self.identity(paths=list(reversed(self.fixture['paths'])),
                                       archives=list(reversed(self.fixture['archives']))), self.identity())

    def test_external_basename_with_different_hashes_is_rejected(self):
        first = self.folder / 'external-one/collision.a'
        second = self.folder / 'external-two/collision.a'
        for path, content in ((first, b'first input'), (second, b'different input')):
            path.parent.mkdir()
            path.write_bytes(content)
        for inventory in ('paths', 'archives'):
            with self.subTest(inventory=inventory), self.assertRaisesRegex(
                    ValueError, r'Ambiguous tracker identity input: .*collision[.]a'):
                self.identity(**{inventory: self.fixture[inventory] + [first, second]})


if __name__ == '__main__':
    unittest.main()
