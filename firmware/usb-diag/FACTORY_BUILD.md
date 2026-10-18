# FM1 FW Factory diagnostic build

The factory profile is asset-free: it embeds no ROM, music, WAD or sample bank.
It keeps CDC recovery, LCD DMA, input scanning and the analog DAC under the fixed
runtime. Optional UAC1 exposes stereo PCM16 at 48 kHz; the analog DAC runs at
44.1 kHz. USB capture precedes PC playback mixing and analog master gain.

Build with the pinned SDK and PI32 toolchain from the repository root:

```powershell
python firmware/usb-diag/build.py --controller 0 --factory-diag --out firmware/usb-diag/build/fm1-forge-diag-v1
```

`--forge-no-uac` genuinely omits the audio USB class while retaining key tones.
`--forge-user-module PATH` compiles a self-authored ABI1 module, whose source,
headers and optional project configuration enter the freshness closure.
`firmware/forge/build_project.py` forwards the validated project configuration
to this actual target builder. See `../forge/README.md` for the callback contract.

The default is `--forge-clock sdk-default --forge-lcd stock-dma`, which preserves
the boot clock. The experimental `sdk-320` preset uses the SDK application clock
setter once before task/USB/DAC startup and verifies the returned clock and bus
bounds. It requires `stock-dma`: the SDK's 320 MHz table uses a 53 MHz LSB clock,
while `spi15-rgb444` and `spi30-rgb444` require the driver's guarded 60 MHz bus.
Nondefault presets still need physical timing acceptance.

A successful manifest identifies `FM1_FORGE_FACTORY_DIAG_V1`, `factory_schema: 1`,
`factory_diag: true`, selected USB/clock/LCD/module options, exact source and SDK
archive hashes, and a complete static audit. Static checks pin the boot/power
sequence, CPU0 task table, internal RAM/heap budget, USB descriptors/setup policy,
packet IRQ/DMA ownership, render/mix/gain ordering and confirmed UBOOT quiescence.
Reused USB sources and their upstream hashes are recorded in
`../usb-audio/SOURCE_ORIGIN.json`. The current project-owned USB/audio grant is
Apache-2.0; see `../usb-audio/RELICENSE.md`, `LICENSE` and `NOTICE` there.
Historical GPL import evidence remains in provenance. SDK/PeakRacing and any
separately linked MDX engines retain their own terms.

Validation commands after a target build:

```powershell
cmake -S firmware/usb-diag -B firmware/usb-diag/build/host-forge
cmake --build firmware/usb-diag/build/host-forge --config Release
ctest --test-dir firmware/usb-diag/build/host-forge -C Release --output-on-failure
python firmware/usb-diag/tests/test_factory_image.py -v
python -m unittest discover -s tools/jieli-wl82 -p test_factory_profile.py
```

Packaging reuses the existing exact stock SFC application replacement. Private
catalog export additionally requires a verified 1 MiB baseline, matching
readback receipt, fresh build closure and preservation of boot/config/reserved
bytes. It introduces no AES key or official FWSC signing claim. Private full
images and catalog bundles stay in ignored local directories.

Installation uses an existing audited CDC firmware to enter UBOOT, or an already
enumerated UBOOT device, through the reviewed local session writer. `FORGE STOP`
shuts down scanner/LCD/DAC ownership before the normal arm/confirm handshake.
Raw CDC `COMMIT` remains blocked. `serial_status` reports counters/faults without
stopping diagnostics; `observe` requires advancing counters and zero retained
input/ADC errors. These observations do not prove the screen, every control,
encoder direction, volume sweep, audible output or real USB audio transfer.
Those remain physical acceptance checks against the exact saved candidate.
