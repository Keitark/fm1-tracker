# Third-party source

Emulator core: PeakRacing/nes, https://github.com/PeakRacing/nes

Pinned revision: `68bdfc8de570264c0e84f73766f1a1ed3591066f`.
License: Apache License 2.0. Copyright notices remain in the untouched upstream
source. Full license: [../../references/source/peak-nes/LICENSE](../../references/source/peak-nes/LICENSE).
Retain that license and relevant upstream notices when redistributing source
or binaries. This project uses only `nes.c`, `nes_cpu.c`, `nes_ppu.c`,
`nes_apu.c` and their headers; upstream platform ports/ROM loaders/mapper
registries are not compiled. The FM-1 adapter implements its own bounded NROM/CNROM
setup without changing upstream files.

The existing AC79 SDK contributes target C-library and IIS driver declarations.
The experimental WL82 adapter references the SDK's IIS implementation; it is not
included in the resulting static library and must be linked by a future target
startup build under the SDK's own terms.
It remains a separate local dependency with its own terms, not a component
relicensed by this project. An IIS object and compiler-generated LLVM IR were
extracted into the ignored build directory for local ABI inspection only; do not
redistribute them as project-authored source. No SDK libraries are bundled in
the project source. The LCD table contains hardware configuration bytes recovered
from the user's private FM-1 readback; the original firmware is not included or
relicensed here. The generated diagnostic ROM is original test code from
`tests/make_diagnostic_rom.py`, with no commercial game assets.
