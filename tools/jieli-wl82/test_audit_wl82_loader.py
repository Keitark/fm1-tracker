"""Offline exact-blob checks; no SCSI handles or USB commands."""
import hashlib
import unittest

import audit_wl82_loader as audit


class LoaderAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw=audit.SOURCE.read_bytes()
        cls.code=audit.decode(cls.raw)

    def test_decode_known_transport_chunks(self):
        self.assertEqual(hashlib.sha256(self.code).hexdigest(),audit.DECODED_SHA)
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),audit.SHA)

    def test_reject_changed_or_truncated_blob(self):
        for raw in (self.raw[:-1],bytes([self.raw[0]^1])+self.raw[1:]):
            with self.assertRaisesRegex(ValueError,'Unreviewed WL82'):
                audit.decode(raw)

    def test_exact_dispatch_and_callbacks(self):
        report=audit.inspect_protocol(self.code)
        self.assertEqual(report['handlers']['FB01_sector_erase'],'0x1c04458')
        self.assertEqual(report['handlers']['FC0A_online'],'0x1c0462c')
        self.assertEqual(report['nor_callbacks']['write'],'0x1c05430')
        self.assertEqual(report['sector_erase_spi_opcode'],'0x20')
        self.assertEqual(report['FC0D_dispatch_target'],'0x1c04b60')
        self.assertFalse(report['FC0D_set_flash_commands_handled_by_reviewed_vendor_dispatch'])

    def test_known_side_effects_and_unverified_status_not_hidden(self):
        report=audit.inspect_protocol(self.code)
        self.assertTrue(report['online_device_may_write_status_registers'])
        for field in ('erase_reply_proves_completion',
                      'program_callback_result_checked_by_handler',
                      'FC03_handled_by_reviewed_vendor_dispatch',
                      'host_ready_poll_qualified','read_callback_checks_busy_wait_result',
                      'physical_write_verified','physical_restore_verified'):
            self.assertFalse(report[field])

    def test_changed_decoded_instruction_or_table_rejected(self):
        for address in (0x1c04b2a,0x1c05244,0x1c041c0,0x1c07964,
                        0x1c045d6,0x1c045e2,0x1c045f4,0x1c04626,0x1c0551c):
            changed=bytearray(self.code)
            changed[address-audit.BASE]^=1
            with self.subTest(address=hex(address)), self.assertRaisesRegex(ValueError,'Unreviewed decoded'):
                audit.inspect_protocol(changed)

    def test_proposed_hook_does_not_use_vendor_copy_path(self):
        report=audit.inspect_protocol(self.code)
        self.assertEqual(report['handlers']['FB06_memory_write'],'0x1c045aa')
        review=report['memory_write_review']
        start=int(review['vendor_copy_excluded_start'],16)
        end=int(review['vendor_copy_excluded_end_exclusive'],16)
        self.assertTrue(start <= 0x1c04318 < end)
        self.assertFalse(start <= 0x1c60000 < end)
        self.assertTrue(review['proposed_status_hook_uses_excluded_path'])
        self.assertEqual(review['excluded_path_return_flag'],0)
        for field in ('ROM_fallback_behavior_qualified',
                      'end_to_end_cache_coherence_qualified',
                      'live_status_patch_installation_qualified'):
            self.assertFalse(review[field])


if __name__=='__main__':
    unittest.main()
