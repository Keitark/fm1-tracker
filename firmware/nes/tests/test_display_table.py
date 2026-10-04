from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]

class TableTest(unittest.TestCase):
    def test_stock_table_matches(self):
        def table(path):
            text=path.read_text()
            body=re.search(r'panel_init\[21\]\[18\]\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
            return re.sub(r'\s+','',body)
        self.assertEqual(table(ROOT/'src/fm1_board.c'),table(ROOT/'boot/display_test.c'))

if __name__=='__main__':unittest.main()
