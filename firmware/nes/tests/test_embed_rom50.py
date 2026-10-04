import importlib.util
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('embed_rom50', HERE/'embed_rom50.py')
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)


class EmbedTests(unittest.TestCase):
    def test_smb1_exact_asset_and_wrong_selection(self):
        source=embed.rom_path('smb1')
        if not source.exists():self.skipTest('Private user ROM absent')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'rom.c'
            embed.generate(source,output,'smb1')
            saved=output.read_text()
            embedded=saved.split('cartridge[] = {\n',1)[1].split('};',1)[0]
            self.assertEqual(bytes(int(x.strip(),16) for x in embedded.split(',') if x.strip()),
                             source.read_bytes())
            with self.assertRaises(ValueError):embed.generate(source,output,'rom50')
            self.assertEqual(output.read_text(),saved)

    def test_refuses_unreviewed_rom_without_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/'wrong.nes', Path(directory)/'rom.c'
            output.write_text('preserve')
            for data in (b'', embed.HEADER + bytes(49168-16)):
                source.write_bytes(data)
                with self.assertRaises(ValueError): embed.generate(source, output)
                self.assertEqual(output.read_text(), 'preserve')

    def test_optional_exact_embedded_bytes(self):
        source = HERE/'roms/50-coin-shine.nes'
        if not source.exists(): self.skipTest('User ROM not redistributed')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'rom.c'
            embed.generate(source, output)
            first = output.read_text()
            embedded = first.split('cartridge[] = {\n', 1)[1].split('};', 1)[0]
            self.assertEqual(bytes(int(x.strip(), 16) for x in embedded.split(',') if x.strip()),
                             source.read_bytes())
            embed.generate(source, output)
            self.assertEqual(output.read_text(), first)


if __name__ == '__main__': unittest.main()
