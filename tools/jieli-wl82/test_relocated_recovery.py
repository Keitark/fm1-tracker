"""Offline recovery guards and broker tests. No real device or flash access."""
import base64
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import flash_session_worker as worker


def request(base, image, op='recover_flash'):
    return dict(op=op, image=base64.b64encode(image).decode(), sha256=worker.sha(image),
                baseline_sha256=worker.sha(base))


class RecoveryReadTests(unittest.TestCase):
    def setUp(self):
        self.base = bytes(worker.SIZE)
        image = bytearray(self.base)
        image[0x4000] = 1; image[worker.APP_START] = 2
        image[0x5000] = 3; image[0x6000] = 4; image[worker.APP_END-1] = 5
        self.image = bytes(image)
        self.sectors = [0x5000,0x6000,0x92000,0x4000]
        self.receipt = dict(status='failed',baseline_sha256=worker.sha(self.base),
            candidate_sha256=worker.sha(self.image),verified_sectors=[],current_sector='0x5000')

    def verify(self, current, receipt=None, sectors=None, image=None, base=None):
        return worker.verify_recovery_read(self.base if base is None else base,
            self.image if image is None else image,self.sectors if sectors is None else sectors,
            self.receipt if receipt is None else receipt,bytes(current))

    def current_after(self, prefix):
        current = bytearray(self.base)
        for address in self.sectors[:prefix]:
            current[address:address+4096] = self.image[address:address+4096]
        return current

    def test_partial_current_application_sector_is_accepted(self):
        current = self.current_after(0)
        current[0x5000:0x5100] = self.image[0x5000:0x5100]
        current[0x5100:0x6000] = b'\xff'*0xf00
        result = self.verify(current)
        self.assertEqual(result['current_sha256'],worker.sha(current))
        self.assertEqual(result['verified_prefix_sectors'],0)

    def test_verified_prefix_must_match_candidate_before_uncertain_sector(self):
        current = self.current_after(1); current[0x6000:0x6100] = b'\xff'*256
        receipt = dict(self.receipt,verified_sectors=['0x5000'],current_sector='0x6000')
        self.assertEqual(self.verify(current,receipt)['verified_prefix_sectors'],1)

    def test_uncertain_sector_can_still_be_baseline_or_already_be_candidate(self):
        for current in (self.current_after(0),self.current_after(1)):
            with self.subTest(current_sha256=worker.sha(current)):
                self.assertEqual(self.verify(current)['verified_prefix_sectors'],0)

    def test_uncertain_directory_allows_only_directory_records_and_application_bytes(self):
        current = self.current_after(3)
        current[0x4000:0x4040] = b'\xff'*0x40
        current[worker.APP_START:0x5000] = b'\xff'*(0x5000-worker.APP_START)
        receipt = dict(self.receipt,verified_sectors=[hex(a) for a in self.sectors[:3]],
                       current_sector='0x4000')
        self.assertEqual(self.verify(current,receipt)['verified_prefix_sectors'],3)

    def test_uncertain_last_application_sector_preserves_reserved_tail(self):
        current = self.current_after(2)
        current[0x92000:worker.APP_END] = b'\xff'*(worker.APP_END-0x92000)
        receipt = dict(self.receipt,verified_sectors=['0x5000','0x6000'],current_sector='0x92000')
        self.assertEqual(self.verify(current,receipt)['verified_prefix_sectors'],2)

    def test_all_verified_sectors_require_the_complete_candidate(self):
        receipt = dict(self.receipt,verified_sectors=[hex(a) for a in self.sectors],
                       current_sector='0x4000')
        self.assertEqual(self.verify(self.image,receipt)['verified_prefix_sectors'],len(self.sectors))
        current = bytearray(self.image); current[worker.APP_START+1] = 6
        with self.assertRaises(ValueError): self.verify(current,receipt)

    def test_all_verified_sectors_require_the_last_sector_as_current(self):
        receipt = dict(self.receipt,verified_sectors=[hex(a) for a in self.sectors],
                       current_sector='0x92000')
        with self.assertRaises(ValueError): self.verify(self.image,receipt)

    def test_current_read_must_be_exactly_one_megabyte(self):
        for size in (0,worker.SIZE-1,worker.SIZE+1):
            with self.subTest(size=size):
                with self.assertRaises(ValueError): self.verify(bytes(size))

    def test_receipt_must_record_a_completed_failure(self):
        for status in (None,'pending','sector_write_outcome_pending','written_and_readback_verified'):
            with self.subTest(status=status):
                with self.assertRaises(ValueError): self.verify(self.base,dict(self.receipt,status=status))

    def test_receipt_baseline_hash_must_match_exact_baseline(self):
        with self.assertRaises(ValueError):
            self.verify(self.base,dict(self.receipt,baseline_sha256='0'*64))

    def test_receipt_candidate_hash_must_match_exact_candidate(self):
        with self.assertRaises(ValueError):
            self.verify(self.base,dict(self.receipt,candidate_sha256='0'*64))

    def test_verified_sectors_must_be_an_ordered_canonical_hex_prefix(self):
        for prefix in (['0x6000'],['0x5000','0x92000'],['0x5000','0x5000'],[0x5000],
                       ['0X5000'],['0x05000'],'0x5000',None):
            with self.subTest(prefix=prefix):
                with self.assertRaises(ValueError):
                    self.verify(self.base,dict(self.receipt,verified_sectors=prefix))

    def test_verified_prefix_cannot_exceed_the_write_scope(self):
        receipt = dict(self.receipt,verified_sectors=[hex(a) for a in self.sectors]+['0x4000'])
        with self.assertRaises(ValueError): self.verify(self.image,receipt)

    def test_current_sector_must_be_the_next_unverified_sector(self):
        for address in ('0x5000','0x92000','0x7000',0x6000,None):
            with self.subTest(address=address):
                receipt = dict(self.receipt,verified_sectors=['0x5000'],current_sector=address)
                with self.assertRaises(ValueError): self.verify(self.current_after(1),receipt)

    def test_verified_sector_cannot_differ_from_the_candidate(self):
        current = self.current_after(1); current[0x5001] = 9
        receipt = dict(self.receipt,verified_sectors=['0x5000'],current_sector='0x6000')
        with self.assertRaises(ValueError): self.verify(current,receipt)

    def test_future_write_sector_must_still_match_baseline(self):
        current = self.current_after(0); current[0x6000] = self.image[0x6000]
        with self.assertRaises(ValueError): self.verify(current)

    def test_other_application_sector_must_still_match_baseline(self):
        current = self.current_after(0); current[0x7000] = 9
        with self.assertRaises(ValueError): self.verify(current)

    def test_boot_config_and_reserved_bytes_must_match_baseline(self):
        for address in (0,0x3fff,0x4040,worker.APP_START-1,worker.APP_END,worker.SIZE-1):
            with self.subTest(address=hex(address)):
                current = self.current_after(0); current[address] = 9
                with self.assertRaises(ValueError): self.verify(current)

    def test_uncertain_directory_cannot_change_shared_config_bytes(self):
        receipt = dict(self.receipt,verified_sectors=[hex(a) for a in self.sectors[:3]],
                       current_sector='0x4000')
        current = self.current_after(3); current[0x4040] = 9
        with self.assertRaises(ValueError): self.verify(current,receipt)

    def test_uncertain_last_application_sector_cannot_change_reserved_tail(self):
        receipt = dict(self.receipt,verified_sectors=['0x5000','0x6000'],current_sector='0x92000')
        current = self.current_after(2); current[worker.APP_END] = 9
        with self.assertRaises(ValueError): self.verify(current,receipt)

    def test_sector_scope_must_exactly_cover_candidate_changes_and_end_with_directory(self):
        for sectors in (self.sectors[:-1],self.sectors+[0x7000],sorted(self.sectors),
                        [0x6000,0x5000,0x92000,0x4000],self.sectors+[0x4000]):
            with self.subTest(sectors=sectors):
                with self.assertRaises(ValueError): self.verify(self.base,sectors=sectors)

    def test_candidate_cannot_change_protected_bytes(self):
        image = bytearray(self.image); image[0x4040] = 9
        receipt = dict(self.receipt,candidate_sha256=worker.sha(image))
        with self.assertRaises(ValueError): self.verify(self.base,receipt,image=image)

    def test_baseline_and_candidate_must_have_full_image_length(self):
        for base,image in ((self.base[:-1],self.image),(self.base,self.image[:-1])):
            with self.subTest(baseline_size=len(base),candidate_size=len(image)):
                receipt = dict(self.receipt,baseline_sha256=worker.sha(base),candidate_sha256=worker.sha(image))
                with self.assertRaises(ValueError): self.verify(self.base,receipt,base=base,image=image)


class RelocatedRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = bytes(worker.SIZE)
        image = bytearray(self.base)
        image[0x4000] = 1; image[worker.APP_START] = 2
        image[0x5000] = 3; image[0x6000] = 4; image[worker.APP_END-1] = 5
        self.image = bytes(image)
        self.sectors = [0x5000,0x6000,0x92000,0x4000]
        (self.root/'baseline.bin').write_bytes(self.base)
        (self.root/'state.json').write_text(json.dumps(dict(baseline_file='baseline.bin',
            baseline_sha256=worker.sha(self.base),blocked=False,reset_pending=False)))
        self.target = {'DeviceID':'fake-drive-3','PNPDeviceID':'original-usb-target'}
        self.memory = bytearray(self.base)
        self.writes = []; self.reads = []; self.modes = []; self.events = []; self.io_states = []
        self.fail = True; self.fail_address = 0x6000
        self.short_current = False; self.corrupt_final = False; self.preserve_shared = False
        self.change_after_current = False; self.change_after_recovery_write = False
        self.current_read_completed = False

    def cli(self, device, commands, log, mode='write'):
        self.modes.append(mode)
        self.events.append((device,list(commands),log))
        self.io_states.append(worker.Session(self.root).state)
        log.write_text('\n'.join(commands)+'\n')
        if mode in ('load','reset'): return
        for command in commands:
            bits = command.split(' ',3)
            addr = int(bits[1])
            if bits[0] == 'write':
                self.writes.append(addr)
                source = Path(' '.join(bits[2:])).read_bytes()
                if self.fail and addr == self.fail_address:
                    if self.preserve_shared:
                        for start,end in ((0x4000,0x4040),(worker.APP_START,worker.APP_END)):
                            start,end = max(start,addr),min(end,addr+4096)
                            if start < end: self.memory[start:end] = b'\xff'*(end-start)
                    else:
                        self.memory[addr:addr+4096] = b'\xff'*4096
                    partial = 0x40 if addr == 0x4000 else 256
                    self.memory[addr:addr+partial] = source[:partial]
                    raise OSError('simulated partial write / USB disconnect')
                self.memory[addr:addr+4096] = source
                if self.change_after_recovery_write and self.current_read_completed:
                    self.target = dict(self.target,DeviceID='fake-drive-changed')
                    self.change_after_recovery_write = False
            else:
                size = int(bits[2]); self.reads.append((addr,size))
                path = Path(bits[3]); data = bytes(self.memory[addr:addr+size])
                if path.name == 'current.bin':
                    if self.short_current: data = data[:-1]
                    self.current_read_completed = True
                    if self.change_after_current:
                        self.target = dict(self.target,PNPDeviceID='changed-during-current-read')
                if path.name == 'final-1.bin' and self.corrupt_final:
                    data = b'\x09'+data[1:]
                path.write_bytes(data)

    def run_request(self, op, image=None, base=None):
        with patch.object(worker,'disk',side_effect=lambda:dict(self.target)), \
             patch.object(worker,'execute_cli',side_effect=self.cli), \
             contextlib.redirect_stdout(io.StringIO()):
            return worker.Session(self.root).handle(request(self.base if base is None else base,
                self.image if image is None else image,op))

    def failed_flash(self):
        with self.assertRaisesRegex(OSError,'partial write'): self.run_request('flash')
        state = worker.Session(self.root).state
        self.assertTrue(state['blocked']); self.assertFalse(state['reset_pending'])
        self.assertEqual(state['baseline_sha256'],worker.sha(self.base))
        failed = self.root/state['flash_attempt']['run']
        receipt = json.loads((failed/'deployment.json').read_text())
        self.assertEqual(receipt['status'],'failed')
        self.failed_state = state
        self.failed_evidence = {p.name:p.read_bytes() for p in failed.iterdir()}
        self.target = {'DeviceID':'fake-drive-8','PNPDeviceID':'relocated-usb-target'}
        self.fail = False
        return failed

    def assert_failed_evidence_preserved(self, failed):
        self.assertEqual({p.name:p.read_bytes() for p in failed.iterdir()},self.failed_evidence)

    def assert_closed(self):
        state = worker.Session(self.root).state
        self.assertTrue(state['blocked']); self.assertFalse(state['reset_pending'])
        self.assertEqual(state['baseline_sha256'],worker.sha(self.base))
        self.assertNotIn('reset',self.modes)
        return state

    def assert_rejected_before_io(self, image=None):
        with patch.object(worker,'disk',side_effect=AssertionError('No hardware')), \
             patch.object(worker,'execute_cli',side_effect=AssertionError('No CLI')):
            with self.assertRaises(ValueError):
                worker.Session(self.root).handle(request(self.base,self.image if image is None else image))

    def test_explicit_recovery_accepts_relocated_target_after_fresh_matching_read(self):
        failed = self.failed_flash(); first_events = len(self.events); first_states = len(self.io_states)
        self.run_request('recover_flash')
        state = worker.Session(self.root).state
        self.assertEqual(self.writes,self.sectors[:2]+self.sectors)
        self.assertEqual(self.reads,[(0x5000,4096),(0,worker.SIZE),
            *[(a,4096) for a in self.sectors],(0,worker.SIZE)])
        recovery_events = self.events[first_events:]
        first_write = next(i for i,(_,commands,_) in enumerate(recovery_events)
                           if any(command.startswith('write ') for command in commands))
        self.assertTrue(any(any('current.bin' in c for c in commands)
                            for _,commands,_ in recovery_events[:first_write]))
        self.assertTrue(all(device == 'fake-drive-8' for device,_,_ in recovery_events))
        self.assertTrue(all(s['blocked'] and not s['reset_pending'] for s in self.io_states[first_states:]))
        self.assertFalse(state['blocked']); self.assertTrue(state['reset_pending'])
        self.assertTrue(state['needs_observation'])
        self.assertEqual(state['baseline_sha256'],worker.sha(self.image))
        self.assertEqual(state['target'],self.target)
        self.assertEqual(state['flash_attempt']['status'],'verified')
        self.assertNotIn('failed_operation',state); self.assertNotIn('error',state)
        run = self.root/state['flash_attempt']['run']
        self.assertEqual((run/'final-1.bin').read_bytes(),self.image)
        self.assertTrue((run/'current.bin').exists())
        self.assertFalse((run/'final-2.bin').exists())
        receipt = json.loads((run/'deployment.json').read_text())
        self.assertEqual(receipt['full_readback_count'],1)
        self.assertEqual(receipt['verified_sectors'],[hex(a) for a in self.sectors])
        self.assert_failed_evidence_preserved(failed)

    def test_same_image_retry_still_rejects_a_changed_pnp_identity(self):
        self.failed_flash(); modes = list(self.modes); reads = list(self.reads)
        with self.assertRaisesRegex(ValueError,'identity changed'): self.run_request('retry_flash')
        self.assertEqual(self.writes,self.sectors[:2])
        self.assertEqual(self.modes,modes); self.assertEqual(self.reads,reads)
        self.assert_closed()

    def test_recovery_requires_a_completed_failed_flash_attempt(self):
        self.failed_flash()
        for status in ('pending','verified'):
            with self.subTest(status=status):
                state = json.loads(json.dumps(self.failed_state)); state['flash_attempt']['status'] = status
                (self.root/'state.json').write_text(json.dumps(state))
                self.assert_rejected_before_io()

    def test_recovery_rejects_a_failure_from_another_operation(self):
        self.failed_flash()
        state = dict(self.failed_state,failed_operation='reset')
        (self.root/'state.json').write_text(json.dumps(state))
        self.assert_rejected_before_io()

    def test_recovery_requires_a_completed_deployment_failure_receipt(self):
        failed = self.failed_flash(); path = failed/'deployment.json'
        original = json.loads(path.read_text())
        path.write_text(json.dumps(dict(original,status='sector_write_outcome_pending')))
        self.assert_rejected_before_io()

    def test_recovery_requires_the_original_deployment_receipt(self):
        failed = self.failed_flash(); (failed/'deployment.json').unlink()
        with patch.object(worker,'disk',side_effect=AssertionError('No hardware')), \
             patch.object(worker,'execute_cli',side_effect=AssertionError('No CLI')):
            with self.assertRaises((ValueError,FileNotFoundError)):
                worker.Session(self.root).handle(request(self.base,self.image))

    def test_recovery_rejects_a_different_candidate_before_device_io(self):
        self.failed_flash(); image = bytearray(self.image); image[0x5001] = 9
        self.assert_rejected_before_io(image)

    def test_recovery_rejects_a_different_recorded_baseline_before_device_io(self):
        self.failed_flash()
        state = json.loads(json.dumps(self.failed_state)); state['flash_attempt']['baseline_sha256'] = '0'*64
        (self.root/'state.json').write_text(json.dumps(state))
        self.assert_rejected_before_io()

    def test_recovery_rejects_a_different_recorded_sector_scope_before_device_io(self):
        self.failed_flash()
        state = json.loads(json.dumps(self.failed_state)); state['flash_attempt']['sectors'] = self.sectors[:-1]
        (self.root/'state.json').write_text(json.dumps(state))
        self.assert_rejected_before_io()

    def test_protected_byte_mismatch_in_fresh_read_prevents_all_writes(self):
        failed = self.failed_flash(); self.memory[0xee000] = 9
        with self.assertRaises(ValueError): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]); self.assertEqual(self.reads.count((0,worker.SIZE)),1)
        state = self.assert_closed()
        self.assertEqual(state['flash_attempt'],self.failed_state['flash_attempt'])
        self.assertEqual(state['target'],self.failed_state['target'])
        self.assertEqual(state['recovery_preflight_attempt']['status'],'failed')
        run = Path(state['last_run'])
        self.assertEqual((run/'current.bin').read_bytes(),bytes(self.memory))
        self.assertTrue((run/'failure.json').exists())
        self.assert_failed_evidence_preserved(failed)

    def test_verified_prefix_mismatch_in_fresh_read_prevents_all_writes(self):
        self.failed_flash(); self.memory[0x5001] = 9
        with self.assertRaises(ValueError): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]); self.assert_closed()

    def test_future_sector_mismatch_in_fresh_read_prevents_all_writes(self):
        self.failed_flash(); self.memory[0x92000] = 9
        with self.assertRaises(ValueError): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]); self.assert_closed()

    def test_other_application_sector_mismatch_in_fresh_read_prevents_all_writes(self):
        self.failed_flash(); self.memory[0x7000] = 9
        with self.assertRaises(ValueError): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]); self.assert_closed()

    def test_short_fresh_read_prevents_all_writes(self):
        self.failed_flash(); self.short_current = True
        with self.assertRaises(ValueError): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]); self.assert_closed()

    def test_target_identity_change_during_fresh_read_prevents_all_writes(self):
        self.failed_flash(); self.change_after_current = True
        with self.assertRaisesRegex(ValueError,'identity changed'): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]); self.assert_closed()

    def test_target_identity_change_during_recovery_writes_keeps_session_closed(self):
        self.failed_flash(); self.change_after_recovery_write = True
        with self.assertRaisesRegex(ValueError,'identity changed'): self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors[:2]+[self.sectors[0]])
        self.assert_closed()

    def test_recovery_write_failure_remains_latched_without_automatic_retry(self):
        failed = self.failed_flash(); self.fail = True
        with self.assertRaisesRegex(OSError,'partial write'): self.run_request('recover_flash')
        state = self.assert_closed()
        self.assertEqual(self.writes,self.sectors[:2]+self.sectors[:2])
        self.assertEqual(state['failed_operation'],'flash')
        self.assertEqual(state['flash_attempt']['status'],'failed')
        self.assert_failed_evidence_preserved(failed)

    def test_recovery_final_full_read_mismatch_keeps_baseline_and_reset_closed(self):
        failed = self.failed_flash(); self.corrupt_final = True
        with self.assertRaisesRegex(ValueError,'Final full read mismatch'): self.run_request('recover_flash')
        self.assert_closed()
        self.assertEqual(self.writes,self.sectors[:2]+self.sectors)
        self.assertEqual(self.reads.count((0,worker.SIZE)),2)
        self.assert_failed_evidence_preserved(failed)

    def test_recovery_accepts_partial_directory_with_shared_config_preserved(self):
        self.fail_address = 0x4000; self.preserve_shared = True
        self.failed_flash()
        self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors+self.sectors)
        self.assertEqual(bytes(self.memory),self.image)
        self.assertFalse(worker.Session(self.root).state['blocked'])

    def test_failed_recovery_request_can_be_migrated_without_clearing_the_latch(self):
        self.failed_flash(); self.fail = True
        with self.assertRaisesRegex(OSError,'partial write'): self.run_request('recover_flash')
        (self.root/'ipc').mkdir()
        (self.root/'ipc/failed-recovery.json').write_text(json.dumps(request(self.base,self.image)))
        with patch.object(worker,'disk',side_effect=AssertionError('Offline only')), \
             patch.object(worker,'execute_cli',side_effect=AssertionError('Offline only')):
            plan = worker.resume_failed_session(self.root)
        self.assertTrue(plan['initial_state']['blocked'])
        self.assertFalse(plan['initial_state']['reset_pending'])
        self.assertEqual(plan['initial_state']['flash_attempt']['sectors'],self.sectors)
        self.assertEqual(plan['initial_state']['target'],self.target)
        self.assertEqual(plan['baseline_sha256'],worker.sha(self.base))
        self.assertIn(str(self.root/'ipc/failed-recovery.json'),[f['source'] for f in plan['files']])

    def test_all_verified_failure_can_recover_only_after_matching_the_full_candidate(self):
        self.fail = False; self.corrupt_final = True
        with self.assertRaisesRegex(ValueError,'Final full read mismatch'): self.run_request('flash')
        state = worker.Session(self.root).state
        receipt = json.loads((self.root/state['flash_attempt']['run']/'deployment.json').read_text())
        self.assertEqual(receipt['verified_sectors'],[hex(a) for a in self.sectors])
        self.assertEqual(receipt['current_sector'],'0x4000')
        self.corrupt_final = False
        self.target = {'DeviceID':'fake-drive-8','PNPDeviceID':'relocated-usb-target'}
        self.run_request('recover_flash')
        self.assertEqual(self.writes,self.sectors+self.sectors)
        self.assertEqual(self.reads.count((0,worker.SIZE)),3)
        self.assertEqual(bytes(self.memory),self.image)
        self.assertFalse(worker.Session(self.root).state['blocked'])


if __name__=='__main__':unittest.main()
