"""Offline compiled checkpoint and isolated SDK-copy regression checks."""
from pathlib import Path
import sys
import unittest

HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
from audit_pre_os import audit_pre_os
from cpu1_guard import audit_application

BUILD=HERE/'build/boot-pre-os-smb1'


class PreOsEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=(BUILD/'fm1-nes-boot-candidate.app.bin').read_bytes()
        cls.symbols={}
        for line in (BUILD/'symbols.txt').read_text().splitlines():
            fields=line.split()
            if len(fields)==4:
                cls.symbols[fields[3]]=(int(fields[0],16),int(fields[1],16),0)

    def check(self,app):
        def read(address,size):
            offset=address-0x02000120
            if offset<0 or offset+size>len(app):raise ValueError('Outside XIP image')
            return app[offset:offset+size]
        return audit_pre_os(self.symbols,read)

    def test_reviewed_placement(self):
        report=self.check(self.app)
        self.assertTrue(report['before_os_init_and_cpu1_launch'])
        self.assertTrue(report['original_os_init_retained'])
        self.assertFalse(report['hardware_verified'])

    def test_every_main_and_wrapper_byte_mutation_rejected(self):
        for name in ('main','__wrap_os_init'):
            address,size,_=self.symbols[name]
            for i in range(size):
                changed=bytearray(self.app)
                changed[address-0x02000120+i]^=1
                with self.subTest(symbol=name,index=i),self.assertRaises(ValueError):
                    self.check(changed)

    def test_sdk_copy_changes_only_one_call_and_adds_declaration(self):
        original=(HERE.parents[1]/'references/source/fw-AC79_AIoT_SDK/apps/common/system/init.c').read_text(encoding='utf-8')
        modified=(BUILD/'init-pre-os.c').read_text(encoding='utf-8')
        self.assertEqual(original.count('    os_init();'),1)
        self.assertEqual(modified,'extern void __wrap_os_init(void);\n'+
                         original.replace('    os_init();','    __wrap_os_init();'))

    def test_cpu1_guard_retained(self):
        report=audit_application(self.app,address=self.symbols['os_start'][0]+0xea,
                                 cpu1_start=self.symbols['cpu1_start'][0],
                                 ready=self.symbols['cpu1_run_flag'][0])
        self.assertTrue(report['restored_after_cpu1_ready'])


if __name__=='__main__':unittest.main(verbosity=2)
