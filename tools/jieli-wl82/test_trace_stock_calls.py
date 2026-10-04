import unittest
from trace_stock_calls import parse


class TraceTests(unittest.TestCase):
    def test_instruction_lengths_and_sign(self):
        rows=(
            (' 2000120:    04 81             \tgoto 0x2',0x2000124),
            (' 2000130:    80 ff b0 00 00 00 \tcall 0xb0',0x20001e6),
            (' 202308e:    bf ea 96 05       \tcall -0x1f4d4',0x2003bbe),
        )
        for row,expected in rows:self.assertEqual(parse(row)[2],expected)

    def test_indirect_is_not_guessed(self):
        self.assertIsNone(parse(' 20001b6:    d6 00             \tgoto r6')[2])
        self.assertIsNone(parse('Disassembly of section .sweep:'))


if __name__=='__main__':unittest.main()
