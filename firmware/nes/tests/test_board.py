"""Exercise the real board pipeline using fake transports and an original ROM."""
from pathlib import Path
import subprocess
import sys
import tempfile
from make_diagnostic_rom import make_rom

with tempfile.TemporaryDirectory(prefix='fm1-board-test-') as folder:
    rom=Path(folder)/'diagnostic.nes'
    rom.write_bytes(make_rom())
    subprocess.run([sys.argv[1],str(rom)],check=True,timeout=30)
