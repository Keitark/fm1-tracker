"""Offline private-image update/restore model; never opens a device."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import plan_flash_update as planner


class FlashPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not planner.pack.BACKUP.exists():raise unittest.SkipTest('Private backup required')
        elf,cls.app=planner.pack.load_built_profile('peripherals')
        cls.original=planner.pack.BACKUP.read_bytes()
        cls.candidate,_=planner.pack.build_candidate(cls.original,elf,cls.app)
        cls.plan=planner.build_plan(cls.original,cls.candidate,cls.app)
        cls.allowed=[int(s['offset'],16) for s in cls.plan['sector_order']]
        cls.operations=list(planner.model_operations(cls.original,cls.candidate,cls.allowed))

    def test_whole_sectors_and_metadata_last(self):
        self.assertEqual(self.allowed[-1],0x4000)
        self.assertEqual(len(self.allowed),len(set(self.allowed)))
        self.assertTrue(all(0x4000<=i<0x30000 for i in self.allowed))
        self.assertFalse(self.plan['atomic_update'])
        for sector in self.plan['sector_order']:
            i=int(sector['offset'],16)
            self.assertEqual(sector['page_offsets'],[hex(i+p) for p in range(0,4096,256)])
            self.assertEqual(sector['changed_bytes']+sector['unchanged_neighbor_bytes_rewritten'],4096)

    def test_update_and_restore_exact(self):
        result=planner.simulate(self.original,self.candidate,self.plan)
        self.assertTrue(result['byte_array_update_and_restore_match'])
        self.assertFalse(result['physical_recovery_proven'])
        self.assertEqual(result['model_update_operations'],17*len(self.allowed))

    def test_every_operation_boundary_has_reconstructible_original_sectors(self):
        # Each checkpoint models a stop after a complete erase/page command.
        # Independent sector-copy reconstruction is the recovery oracle, not
        # evidence that the device could re-enter UBOOT or complete a restore.
        state=bytearray(self.original)
        touched=set()
        for cut,operation in enumerate(self.operations,1):
            planner.model_apply(state,operation)
            touched.add(operation.offset&~4095)
            restored=bytearray(state)
            for offset in touched:restored[offset:offset+4096]=self.original[offset:offset+4096]
            self.assertEqual(restored,self.original,'operation boundary '+str(cut))
        self.assertEqual(state,self.candidate)

    def test_partial_sector_erase_or_page_write_recover_in_model(self):
        for offset in (self.allowed[0],self.allowed[-1]):
            state=bytearray(self.original)
            state[offset:offset+513]=bytes([255])*513
            state[offset+513:offset+640]=bytes(127)
            for operation in planner.model_operations(state,self.original,self.allowed):
                planner.model_apply(state,operation)
            self.assertEqual(state,self.original)

    def test_restore_refuses_damage_outside_planned_sectors(self):
        for offset in (0,0x30000,0xff000):
            state=bytearray(self.candidate);state[offset]^=1
            with self.assertRaisesRegex(ValueError,'outside planned sectors'):
                list(planner.model_operations(state,self.original,self.allowed))

    def test_invalid_private_candidate_never_planned(self):
        candidate=bytearray(self.candidate);candidate[0xff000]^=1
        with self.assertRaisesRegex(ValueError,'preserved bytes'):
            planner.build_plan(self.original,candidate,self.app)

    def test_program_without_erase_cannot_restore_zero_bits(self):
        state=bytearray(planner.SIZE)
        with self.assertRaisesRegex(ValueError,'without erase'):
            planner.model_apply(state,planner.ModelOperation('program',0x4000,bytes([255])*256))

    def test_unaligned_or_out_of_range_operations_rejected(self):
        state=bytearray(self.original)
        for operation in (planner.ModelOperation('erase',0x4001),
                          planner.ModelOperation('erase',planner.SIZE),
                          planner.ModelOperation('program',0x4001,bytes(256)),
                          planner.ModelOperation('program',0x4000,bytes(255))):
            with self.assertRaises(ValueError):planner.model_apply(state,operation)


if __name__=='__main__':unittest.main()
