"""Offline transformation, archive scope, and linked-code mutation checks."""
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import cpu1_guard as guard
BUILD = HERE/'build/boot-cpu1-guard-smb1'


class GuardTests(unittest.TestCase):
    def test_only_guard_ir_insertions(self):
        original = (BUILD/'cpu1-guard/port.original.ll').read_text()
        changed = guard.transform(original)
        self.assertEqual(changed, (BUILD/'cpu1-guard/port.guarded.ll').read_text())
        self.assertEqual(changed.replace(guard.SAVE, '').replace(guard.RESTORE, ''), original)
        for invalid in (changed, original.replace('@EnableOtherCpu()', '@Other()'),
                        original.replace('while.end:', 'unexpected:')):
            with self.assertRaises(ValueError):
                guard.transform(invalid)

    def test_only_port_archive_member_changes(self):
        source = HERE.parents[1]/'references/source/fw-AC79_AIoT_SDK/cpu/wl82/liba/system.a'
        before = guard.archive_members(source.read_bytes())
        after = guard.archive_members((BUILD/'cpu1-guard/system-cpu1-guard.a').read_bytes())
        self.assertEqual([x[0] for x in before], [x[0] for x in after])
        self.assertEqual([a[0] for a, b in zip(before, after) if a != b], ['port.c.o'])

    def test_linked_guard_and_every_byte_mutation(self):
        app = (BUILD/'fm1-nes-boot-candidate.app.bin').read_bytes()
        self.assertTrue(guard.audit_application(app)['restored_after_cpu1_ready'])
        offset = guard.GUARD_ADDRESS - 0x02000120
        for index in range(len(guard.GUARD_CODE)):
            broken = bytearray(app); broken[offset+index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                guard.audit_application(broken)


if __name__ == '__main__':
    unittest.main(verbosity=2)
