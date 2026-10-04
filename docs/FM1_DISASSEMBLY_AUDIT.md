# FM-1_010 offline disassembly and structural audit

Date: 2026-09-26. Target: the actual FM-1 readback, not the community V13 image.

## Verdict

**Yes: this firmware is structurally recoverable and its application can be
disassembled.** It is not an opaque compressed blob after the normal container
decoding. There are checksum-validated file directories, coherent pi32v2 startup
instructions, a recoverable RAM initialization map, direct string references,
and structured parameter/menu pointer tables.

This is an initial structural audit, not a complete decompilation, functional
security audit, recovered source tree, or authorization to flash anything.
No USB/device access was performed for this work.

## Input and integrity

- Raw backup SHA-256:
  `a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61`.
- Extracted application: 583,228 bytes, embedded marker `FM-1_010`.
- Application SHA-256:
  `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
- Flash-header, parsed directory-header and application-data CRCs pass.
- Synthetic ELF payload was checked byte-for-byte against the extracted app.
  Vendor `llvm-objcopy -O binary -j .sweep` independently reproduces its hash.
  This checks the ELF wrapper, **not** instruction reassembly correctness.
- All ten local readback/comparison/audit tests pass. Original backup hash is
  unchanged. Keys/configuration blobs were not exported by this audit.

## Recoverable file structure

| Flash location | Object | Size / meaning |
| --- | --- | --- |
| `0x00000` | Flash header and top directory | Parsed successfully |
| `0x000a0` | `uboot.boot` | 14,384 bytes |
| `0x038d0` | `isd_config.ini` | 699 bytes; content not exported |
| `0x04000` | `app_dir_head` / application directory | Includes application and reserved-area descriptors |
| `0x04120` | `app.bin` | 583,228 bytes |
| `0x9275c` | `cfg_tool.bin` | 383 bytes |
| `0xff000` | `key_mac` reserved region | 4,096 bytes; content not exported |

The application directory also names `VM`, `PRCT`, `BTIF`, and `USR`. Their
stored offsets are respectively `0x94000`, `0`, `0xe9000`, and `0xea000`.
Their reserved-entry address semantics must not be confused with regular
file offsets: the generic upstream iterator adds the application base to
every entry. That produces questionable overlapping reserved ranges. Both
stored and parser-computed offsets are retained in `audit.json`; **do not use
the latter as a flash-write map**. Resources following the first application
area have not been exhaustively catalogued in this pass.

## Corrected executable address mapping

`app.bin` byte zero maps to **`0x02000120`**, not `0x02000000`.
The application-area header maps to `0x02000000` and occupies `0x120` bytes
before app.bin. Thus, for the application payload:

```text
CPU address = app.bin offset + 0x02000120
            = physical flash offset - 0x4000 + 0x02000000
```

Evidence: the image's entry-point field, the SDK's SFC linker ROM origin,
coherent startup at app offset zero, and actual pointer-table/string matches.
For example, the pointer at CPU `0x0204fb0c` is `0x020555f1`, which resolves
exactly to the `Algorithm` string at app offset `0x554d1`. Loading app.bin at
`0x02000000` makes this and other pointers wrong by `0x120`.

## Startup and RAM map

| Region | Address / source | Bytes | Evidence |
| --- | --- | ---: | --- |
| XIP code and constants | `0x02000120` to `0x020848e0` exclusive | 542,656 | Startup copy source and SDK concatenation layout |
| Initialized RAM, including RAM-executed code | App offset `0x847c0` copied to `0x01c00000` | 40,540 | Explicit startup copy loop |
| Zero-initialized RAM | `0x01c09e5c` to `0x01c211dc` exclusive | 95,104 | Explicit startup clearing loop |
| App trailing bytes | After RAM initialization image | 32 | Remaining bytes; purpose not determined |

The startup sets SP=`0x01c14b94` and SSP=`0x01c15b94`. A separate disassembly
maps the RAM initialization bytes at their runtime address, so RAM functions
are not mistakenly analyzed at their flash storage addresses.

Simplified startup behavior recovered from instructions (not original C):

```c
// Boot setup/calls omitted; this shows only the verified memory operations.
zero_words(0x01c09e5c, 0x17380);
copy_words(/* dst */ 0x01c00000, /* src */ 0x020848e0, 0x9e5c);
// Additional initialization calls and a jump to 0x02001c24 follow.
```

## Concrete application structure

The pointer scan found nine candidate runs of at least four printable-string
pointers. These are exact pointer-to-string relationships, not guessed labels:

| CPU table address | Entries | Contents |
| --- | ---: | --- |
| `0x0204faec` | 40 | FM parameters: envelope rates/levels, Algorithm, Feedback, Osc Sync, LFO, scaling, velocity, coarse/fine frequency, Detune |
| `0x0204fa64` | 15 | Effect controls: Type, Decay, Mix, Rate, Gain, Tone, Level, Freq, Depth |
| `0x0204f3c0` | 12 | Filter/Reverb/Delay/Distortion/Chorus/Phaser followed by operator-edit page labels |
| `0x0204f408` | 6 | Triangle, Saw Down, Saw Up, Square, Sine, S&Hold |
| `0x0204f4a4` | 7 | Up, Down, Inclusive, Exclusive, Random, Order, Repeat |

Runs can span adjacent logical arrays; counts above are scanner run lengths,
not necessarily original C array declarations. UI labels alone do not prove
the location or exact implementation of the DSP algorithms.

The application also contains `usr_app_task`, `usr_audio_task`, a DX7 bank-save
prompt, and sequencer/arpeggiator labels. Useful immediate string references:

| String | Instruction address examples | Interpretation |
| --- | --- | --- |
| `midi_route` | `0x02000afa`, `0x02005d2c`, `0x020061de` | Good entry points for tracing MIDI task communication |
| `audio_server` | `0x02026906` | Audio-service lookup/use candidate |
| `LOADER.BIN` | `0x02027960`, `0x02027a38` | Loader/update file handling candidate |
| `usb_update_mode` | RAM runtime `0x01c02660` | Update-mode code reference; flash-storage listing shows `0x02086f40` |
| `mnt/sdfile/app/btif` | `0x02032b14`, `0x02032da2` | Persistent BT configuration access candidate |

These are **reference sites**, not established function entry points. Names
describe referenced strings; they are not original function symbols.

## Disassembly tooling and artifacts

Used the locally installed JieLi LLVM 4.0.1 `llvm-objdump.exe` with
`-arch-name=pi32v2 -mcpu=r3`. The AC79 SDK ELF confirms ELF32 little-endian
machine ID `0xf1`. A minimal analysis-only ELF carries the unchanged app bytes
at their corrected address. The firmware itself did not provide an ELF symbol
table or debug information to this workflow.

Artifacts are private under:
`F:\dev\fm1\private-backups\readback-20260926-024637\analysis\`

- `app-FM-1_010.bin`: decoded application only.
- `app-analysis-only.elf`: synthetic wrapper, **not flashable firmware**.
- `app-linear-sweep.asm`: full vendor disassembly, about 10 MB.
- `app-annotated.asm`: selected string references annotated.
- `ram-copy-analysis-only.elf`, `ram-copy-linear-sweep.asm`: relocated RAM view.
- `strings.json`, `selected-string-xrefs.json`, `string-pointer-tables.json`.
- `audit.json`: hashes, parsed layout, tool command, and memory-map evidence.
- `elf-roundtrip.bin`: independent object-copy validation result.

Reproduce the audit:

```powershell
C:\Python311\python.exe tools/jieli-wl82/audit_fm1_firmware.py
C:\Python311\python.exe -m unittest discover -s tools/jieli-wl82 -p 'test_*.py' -v
```

The vendor listing prints PC-relative call/branch operands. They must not be
treated directly as absolute target addresses. A linear sweep also attempts
to decode strings, tables and literal pools as instructions; unknown or
plausible-looking instructions there do not establish executable code.

## Modification feasibility and next step

- **Data/UI exploration: promising.** There are concrete parameter and menu
  structures to inspect, with exact locations and consistent references.
- **Behavior changes: plausible but not yet mapped.** Trace the MIDI-routing
  references into handlers and recover their control flow and data structures.
- **DSP replacement or rebuilt firmware: not established.** This pass does not
  recover all functions, DSP equations, ABI, linker boundaries, scheduler
  behavior, timing requirements, update packaging, or restore safety.

The next useful bounded analysis is to recover the MIDI/SysEx dispatcher and
the parameter-table consumers. That connects external controls to the synth
state without presuming that editing a table is enough to change behavior.

## Supporting source files

- Local SDK `references/source/fw-AC79_AIoT_SDK/cpu/wl82/sdk_ld_sfc.c`:
  ROM origin `0x02000120`, `.text`/`.rodata` and RAM layout.
- SDK `cpu/wl82/tools/download.c`: app.bin is assembled from text, data,
  dynamic-data, RAM-data and cache-RAM sections.
- [JieLi firmware format notes](https://kagaimiq.github.io/jielie/datafmt/newfw.html):
  boot/application file structure.
- [Community analysis workflow](https://github.com/AL-255/FM-1-RE/blob/main/scripts/run_ghidra.sh):
  independent Ghidra workflow considered as background; not executed here.
  Its raw import base was not adopted; local evidence above controls mapping.
