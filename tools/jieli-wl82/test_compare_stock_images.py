"""Offline regression checks against the saved firmware reference artifacts."""
import hashlib
import unittest

from compare_stock_images import ROOT, application_info, compare, extract_flash


class StockComparisonTests(unittest.TestCase):
    def test_official_applications_match_independent_extractions(self):
        for version, marker in [('v14', 'FM-1_014'), ('v15', 'FM-1_015')]:
            with self.subTest(version=version):
                payload, meta = extract_flash(ROOT / f'references/originals/FM-1-{version}-original.fwsc')
                app = application_info(payload)
                independent = (ROOT / f'references/unpacked/{version}/files/app.bin').read_bytes()
                self.assertTrue(meta['payload_crc_valid'])
                self.assertTrue(app['data_crc_valid'])
                self.assertEqual(app['sha256'], hashlib.sha256(independent).hexdigest())
                self.assertEqual(app['version_markers'], [marker])
                self.assertEqual(app['flash_offset'], '0x4120')

    def test_installed_application_and_shared_boot_region(self):
        path = ROOT / 'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
        if not path.exists():
            self.skipTest('Private backup is not distributed')
        raw = path.read_bytes()
        app = application_info(raw)
        self.assertTrue(app['data_crc_valid'])
        self.assertEqual(app['version_markers'], ['FM-1_010'])
        self.assertEqual(app['sha256'], 'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')
        payload, _ = extract_flash(ROOT / 'references/downloads/FM-1-live-20260926.fwsc')
        result = compare(raw, payload)
        self.assertEqual(result['first_difference'], '0x4000')
        self.assertEqual(raw[:0x4000], payload[:0x4000])
        self.assertFalse(result['exact_match'])

    def test_archive_label_does_not_determine_embedded_version(self):
        payload, _ = extract_flash(ROOT / 'references/downloads/FM-1-v13-community-20260926.fwsc')
        app = application_info(payload)
        self.assertTrue(app['data_crc_valid'])
        self.assertEqual(app['version_markers'], ['FM-1_009'])

    def test_reject_invalid_flash_header(self):
        with self.assertRaisesRegex(ValueError, 'Flash header CRC mismatch'):
            application_info(bytes(4096))


if __name__ == '__main__':
    unittest.main()
