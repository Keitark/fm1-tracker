"""Offline regression checks for the stock index; never accesses a device."""
import struct
import unittest
from map_stock_execution import parse_listing, successors, closure, callback_table, table_branch, u32


def listing(instructions, base=0x1000):
    """Parser fixtures only: arbitrary bytes, explicit vendor-text semantics."""
    lines=[];blob=bytearray();addresses=[]
    for item in instructions:
        if isinstance(item,str):lines.append(item);continue
        size,text=item;address=base+len(blob);addresses.append(address)
        code=bytes([len(addresses)])*size;blob.extend(code)
        lines.append(f"{address:x}: {' '.join(f'{v:02x}' for v in code)}  \t{text}")
    rows,coverage=parse_listing('\n'.join(lines),bytes(blob),base)
    return rows,coverage,addresses


class StockMapTests(unittest.TestCase):
    def test_relative_targets_use_instruction_end(self):
        rows,_,a=listing([(2,'goto 0x2'),(6,'call -0x8'),(2,'rts')])
        self.assertEqual(rows[a[0]]['direct_target'],0x1004)
        self.assertEqual(rows[a[1]]['direct_target'],0x1000)

    def test_byte_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError,'bytes differ'):
            parse_listing('1000: 80 00  rts',b'\0\0',0x1000)

    def test_overlap_rejected(self):
        with self.assertRaisesRegex(ValueError,'Overlapping'):
            parse_listing('1000: 80 00  rts\n1000: 80 00  rts',b'\x80\0',0x1000)

    def test_gap_reported(self):
        _,c=parse_listing('1002: 80 00  rts',b'\0\0\x80\0',0x1000)
        self.assertEqual(c['unlisted_ranges'],[[0x1000,0x1002]])

    def test_unknown_not_silently_removed(self):
        rows,c,a=listing([(2,'<unknown instruction>'),(2,'nop')])
        seen,graph=closure(rows,[a[0]])
        self.assertEqual(c['listed_bytes'],4)
        self.assertEqual(seen,{a[0]})
        self.assertEqual(graph['reached_unknown_instructions'],[a[0]])

    def test_predicated_block_has_false_edge(self):
        rows,_,a=listing([(4,'if (r0 != 0) {'),(2,'r1 = 1'),'}',(2,'rts')])
        self.assertEqual(set(successors(rows[a[0]])),{a[1],a[2]})

    def test_else_does_not_pop_outer_repeat(self):
        rows,c,a=listing([(2,'rep 0x8 r2 {'),(4,'if (r0 != 0) {'),
                          (2,'r1 = 1'),'} else {',(2,'r1 = 2'),'}','}',(2,'rts')])
        self.assertEqual(set(successors(rows[a[1]])),{a[2],a[3]})
        self.assertEqual(successors(rows[a[2]]),[a[4]])
        self.assertEqual(set(successors(rows[a[3]])),{a[1],a[4]})
        self.assertEqual(c['unclosed_printed_blocks'],0)
        self.assertEqual(c['malformed_repeat_blocks'],[])

    def test_delayed_repeat_brace(self):
        rows,c,a=listing([(2,'rep 0x2 r1 {'),(2,'rep 0x2 r2'),'} {',(2,'nop'),'}',(2,'rts')])
        self.assertEqual(c['unclosed_printed_blocks'],0)
        self.assertIn(a[2],successors(rows[a[2]]))

    def test_wrong_repeat_extent_does_not_invent_backedge(self):
        rows,c,a=listing([(2,'rep 0x20 r1 {'),(2,'nop'),'}',(2,'rts')])
        self.assertEqual(c['malformed_repeat_blocks'],[a[0]])
        self.assertEqual(successors(rows[a[1]]),[a[2]])

    def test_unclosed_block_visible(self):
        rows,_,a=listing([(4,'if (r0 != 0) {'),(2,'nop')])
        _,g=closure(rows,[a[0]])
        self.assertEqual(g['reached_unclosed_blocks'],[a[0]])

    def test_indirect_jump_stops_but_call_can_return(self):
        for insn,fallthrough in [('goto r1',False),('tbh [r2]',False),('tbb [r0]',False),('call r0',True)]:
            rows,_,a=listing([(2,insn),(2,'rts')])
            seen,g=closure(rows,[a[0]])
            self.assertEqual(a[1] in seen,fallthrough)
            self.assertEqual(g['unresolved_indirect_sites'],[a[0]])

    def test_returns_stop(self):
        for insn in ['rts','rti','{pc, r4} = [sp++]','pc = [sp++]']:
            rows,_,a=listing([(2,insn),(2,'nop')])
            self.assertEqual(successors(rows[a[0]]),[])

    def test_milestone_limits_slice(self):
        rows,_,a=listing([(2,'nop'),(2,'nop'),(2,'rts')])
        seen,g=closure(rows,[a[0]],stop_at=[a[1]])
        self.assertEqual(seen,{a[0],a[1]})
        self.assertEqual(g['reached_stop_addresses'],[a[1]])

    def test_explicit_handoff_separate_phase(self):
        first,_,a=listing([(2,'goto r0'),(2,'rts')])
        second,_,_=listing([(2,'rts'),(2,'nop')])
        seen,g=closure(first,[a[0]],{a[0]:[a[1]]})
        self.assertEqual(seen,set(a));self.assertEqual(g['unresolved_indirect_sites'],[])
        self.assertEqual(closure(second,[a[0]])[0],{a[0]})

    def test_pointer_bounds(self):
        for address in (0x200011c,0x2000122):
            with self.assertRaises(ValueError):u32(b'\0'*4,address)

    def test_registry_requires_reviewed_caller_and_valid_target(self):
        rows,_,a=listing([(2,'call r1'),(2,'rts')])
        app=struct.pack('<II',0x2000124,a[1])
        registry=callback_table(app,rows,0x2000120,0x2000124,4,0,a[0],'r1',0)
        self.assertEqual(registry['entries'][0]['target'],a[1])
        with self.assertRaises(ValueError):
            callback_table(app,rows,0x2000120,0x2000124,4,0,a[0],'r2',0)
        with self.assertRaises(ValueError):
            callback_table(b'\0'*8,rows,0x2000120,0x2000124,4,0,a[0],'r1',0)

    def test_table_byte_offsets_are_halfwords_from_table_start(self):
        blob=b'\x00\x01\x01\x02\x80\x00\x80\x00'
        rows,_=parse_listing('1000: 00 01  tbb [r0]\n1004: 80 00  rts\n1006: 80 00  rts',blob,0x1000)
        result=table_branch(blob,0x1000,rows,0x1000,2,1,'r0')
        self.assertEqual(result['targets'],[0x1004,0x1006])
        seen,g=closure(rows,[0x1000],{0x1000:result['targets']})
        self.assertEqual(seen,{0x1000,0x1004,0x1006})
        self.assertEqual(g['unresolved_indirect_sites'],[])

    def test_table_halfword_little_endian_offsets(self):
        blob=b'\x01\x01\x02\x00\x03\x00\x80\x00\x80\x00'
        rows,_=parse_listing('1000: 01 01  tbh [r1]\n1006: 80 00  rts\n1008: 80 00  rts',blob,0x1000)
        result=table_branch(blob,0x1000,rows,0x1000,2,2,'r1')
        self.assertEqual(result['targets'],[0x1006,0x1008])
        with self.assertRaises(ValueError):table_branch(blob,0x1000,rows,0x1000,20,2,'r1')
        with self.assertRaises(ValueError):table_branch(blob,0x1000,rows,0x1000,2,1,'r1')
        with self.assertRaises(ValueError):table_branch(blob,0x1000,rows,0x1000,2,2,'r2')

    def test_table_rejects_data_target_and_nonboundary(self):
        rows={0x1000:{'instruction':'tbb [r0]','size':2},
              0x1002:{'instruction':'nop','size':2}}
        with self.assertRaises(ValueError):table_branch(b'\x00\x01\x00\x00',0x1000,rows,0x1000,1,1,'r0')
        with self.assertRaises(ValueError):table_branch(b'\x00\x01\x01\x00',0x1000,rows,0x1000,1,1,'r0')
        with self.assertRaises(ValueError):table_branch(b'',0x1000,rows,0x1000,0,1,'r0')
        with self.assertRaises(ValueError):table_branch(b'',0x1000,rows,0x1000,1,4,'r0')


if __name__=='__main__':unittest.main()
