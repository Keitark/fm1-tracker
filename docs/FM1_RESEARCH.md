# M-VAVE FM-1 hardware and firmware research

Captured: 2026-08-12  
Status: research baseline complete; custom firmware not yet safe to flash

## Executive finding

The FM-1 is built around a **JieLi AC7911B8** system-on-chip. It is a dual-core proprietary 32-bit DSP/RISC part with hardware floating point, nominally up to 320 MHz, 578 KiB family-level on-chip SRAM, an integrated 1 MiB serial flash in the `B8` package, Bluetooth and Wi-Fi radios, USB 1.1 and USB 2.0 OTG, and extensive audio peripherals. The product uses an external **Cirrus Logic CS4344 stereo DAC** and an **SL5316 boosted speaker amplifier**.

Replacement firmware is plausible because JieLi publishes an AC79-family SDK and toolchain, and the stock image matches the SDK's `wl82`/`AC791N` SFC/XIP memory model. It is not yet safe because the FM-1 board support package, exact GPIO/peripheral map, display controller, and full OTA protocol are not public. The stock synthesizer source is also unavailable.

The shortest credible route is therefore:

1. Preserve and test a stock recovery path.
2. Capture the updater traffic and USB descriptors without writing a custom image.
3. Trace the board nets and test pads.
4. Build a minimal board-support firmware that only enumerates USB and drives a harmless indicator.
5. Add display/input/audio in isolated milestones.
6. Port or implement a legally compatible six-operator FM engine only after the hardware layer is stable.

## Confidence convention

- **Verified**: supported by manufacturer files, the chip datasheet, official SDK source, binary inspection, or clear board photographs.
- **Strong inference**: multiple observations agree, but the relevant board net has not been electrically traced.
- **Unverified**: community claim or ambiguous component marking that still needs direct measurement.

## Product-level facts

The manual describes:

- 6-operator FM synthesis, 32 algorithms, and 128 presets.
- 27 silicone keys, one 300-degree volume control, and seven rotary encoders.
- 1.54-inch TFT display.
- 16-pattern, 16-step sequencer and arpeggiator.
- Six effects.
- USB-C, BLE MIDI, 3.5 mm TRS MIDI input, and 3.5 mm audio output.
- 161.5 x 96.5 x 28.6 mm, 251 g.
- DTP704060 3.7 V 2000 mAh battery, advertised 12-hour life.
- 2.403-2.478 GHz radio operation and 9 dBm product-level maximum RF power.

The linked review reports good DX7/Dexed patch compatibility, more practical polyphony than a Volca FM2 in the reviewer's tests, weak internal speaker sound, usable line output, basic effects, and occasional incompatibility with complex DX7 patches. Those are valuable test cases, not hardware specifications.

## Board identification

Teardown photographs identify the following parts:

| Marking | Function | Confidence | Notes |
|---|---|---:|---|
| JieLi AC7911B8 | Main SoC, flash, Bluetooth/Wi-Fi, USB, control and DSP | Verified | QFN48, 6 x 6 mm; `8` package suffix indicates 8 Mbit/1 MiB flash. |
| Cirrus Logic CS4344 | External stereo audio DAC | Verified | 24-bit, up to 192 kHz, 105 dB dynamic range, -90 dB THD+N; 10-pin package. |
| SL5316 | Boosted class-F speaker amplifier | Verified marking; function verified by vendor | Vendor specifies 4.8 W at 10% THD+N or 4.2 W at 1% into 4 ohms plus 15 uH at VBAT 4.2 V, with 6.5 V boost and protection/AGC. |
| 2 x 74HC595D | Serial-to-parallel output shift registers | Verified | Likely LED/UI expansion; exact signal ownership needs continuity tracing. |
| ACC3610S | Eight-pin power device | Unverified | Treat as an unidentified power-management candidate until its pins are traced. |

The main-board silkscreen reads `DX7 MB V07 260620`, which is likely a board revision and date code. Photographs show unpopulated pads, but they do **not** prove that a usable UART/JTAG/ISP header exists. Continuity testing is required before assigning any pad.

## AC7911B8 specification

### CPU and memory

- Dual-core proprietary 32-bit RISC/DSP CPU with floating-point unit.
- Datasheet/SDK advertised maximum: 320 MHz.
- 24 KiB six-way data cache and 32 KiB eight-way instruction cache.
- MMU, 128 vectored interrupts, four interrupt priority levels.
- AC79-family SDK reports 578 KiB total on-chip SRAM.
- The official SFC linker exposes `ram0` from `0x01c00000`, with a non-MMU `RAM0_SIZE` of 523,596 bytes after boot metadata reservations. Enabling its 8 KiB TLB reduces that region to 515,404 bytes. The linker can reclaim up to 28 KiB of data-cache ways as RAM in its default SFC model. These numbers describe SDK allocation, not free RAM in the FM-1 application.
- The `B8` package contains 8 Mbit = 1 MiB integrated flash.
- QFN48, 6 x 6 mm, 26 GPIO; -40 to +85 C operating range.

### Radio and wired interfaces

- Bluetooth 5.0 with BR/EDR and BLE, up to +15 dBm SoC transmit power and -93 dBm stated sensitivity.
- 802.11b/g/n Wi-Fi, 1x1, 20/40 MHz.
- Full-speed USB 1.1 and high-speed USB 2.0 OTG.
- Three UART, three SPI, two SD interfaces, one I2C/IIC controller.
- I2S/audio link and S/PDIF.
- The chip has Wi-Fi capability, but the FM-1 product documentation exposes BLE MIDI only. Do not assume the PCB RF path or firmware supports Wi-Fi use.

### Audio and analog

- One 16-bit internal DAC/headphone-amplifier channel.
- Four 16-bit audio ADC channels and four microphone preamplifiers.
- Ten-channel, 10-bit general ADC.
- Hardware FFT/IFFT and matrix operations are described by the family SDK, along with audio codecs/effects in prebuilt libraries. Availability and licensing of individual binary libraries must be checked before redistribution.

### Pins worth tracing first

| SoC pin | Datasheet functions | Why it matters |
|---:|---|---|
| 40 / 41 | HUSB D- / D+ | Likely high-speed USB path. |
| 47 / 48 | FUSB D- / D+; alternate UART1 RX/TX, ISP DI/CLK, SPI2, IIC | Possible recovery/update and multiplexed debug route. |
| 42 (PB1) | GPIO, ISP_DO, UART0_TX, ADC3, long-press reset | Stock image reset mapping names PB01; high-priority continuity target. |

## Stock firmware evidence

### Current-version mismatch

M-VAVE's download landing page still labels the major package as V14. That Windows ZIP embeds V14. However, the manufacturer's direct `FM-1.fwsc` link currently returns V15, and the live release-note text begins with **V15 dated 2026-07-30**.

The two original firmware containers were preserved separately:

| Image | Bytes | SHA-256 | Evidence |
|---|---:|---|---|
| V14 embedded in Windows updater | 704,052 | `a1adca99b1f9823ff2873292be74877a7a14ca0846fca3d25a23aba2750162d3` | App string `FM-1_014` at app offset `0x4f978`. |
| V15 direct manufacturer download | 699,956 | `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` | App string `FM-1_015` at app offset `0x4ea64`. |

### Container and flash map

The `.fwsc` file is a wrapper over a JieLi new-firmware/UFW-style image. The outer encoding uses groups of 48 bytes; the existing `jl-misctools` unpacker removes the wrapper and parses the inner image. Both versions identify `AC791N`, product `AC791N_STORY`, and SFC/XIP entry point `0x02000120`, matching the official `wl82` linker.

Logical flash allocations from the image headers:

| Region | V14 | V15 | Notes |
|---|---:|---:|---|
| Bootloader payload | 14,384 B | 14,384 B | Same payload in both images. |
| Application payload | 584,956 B | 581,564 B | V14 SHA-256 `54a32371...f0443`; V15 `306e4706...3203`. |
| Application allocation (`PRCT`) | `0x94000` | `0x93000` | App begins after the first `0x4000` bytes. |
| VM/user state | `0x98000-0xecfff` (`0x55000`) | `0x97000-0xecfff` (`0x56000`) | V15 gives 4 KiB more to VM by shrinking the app allocation. |
| BTIF | `0xed000-0xedfff` | Same | 4 KiB. |
| USR | `0xee000-0xfefff` | Same | 72 KiB. |
| Reserved identity/config sector | `0xff000-0xfffff` | Same | Do not overwrite or publish its contents. |

The stock image configuration contains:

- 24 MHz oscillator.
- `SYS_CLK` field decoding to 360,000,000 Hz.
- HSB divider 1, LSB divider 2.
- UART baud field 1,000,000.
- Reset mapping `PB01_08_0`.
- `UPDATE_JUMP = 0`.
- `ENABLE_SDRAM = 0`; there is no external SDRAM configured.

The 360 MHz field is above the datasheet/SDK's 320 MHz advertised maximum. It may be a vendor overclock or a field-semantics difference; it must not be copied into a replacement build without clock-output or runtime verification.

### V15 release changes

The official V15 notes report:

- Increased base LED brightness.
- Global reset for banks and 0-1 second glide for monophonic use.
- Long-note sequencer recording, voice binding per pattern, custom step length, and chaining 1-16 patterns.
- Init voice, rename, copy/replace, and long-press operator mute editing actions.
- FX and envelope ranges normalized to 0-100.
- MIDI Note On with velocity zero recognized as Note Off and adjusted FX CC response.

V13 added ARP/SEQ BPM sync, program changes 0-127, default Omni/All MIDI channel, and other UI/rate changes. V09 added the global envelope switch and sequencer editing changes.

## Documented MIDI surface

The official English MIDI document says:

- Note channel defaults to All/Omni.
- A separate FX-control channel defaults to MIDI channel 2.
- Note On/Off, Program Change 0-127, 14-bit pitch bend, channel aftertouch, modulation CC1, and sustain CC64 are supported.
- FX-channel CC0-23 cover filter (0-3), reverb (4-7), delay (8-11), distortion (12-15), chorus (16-19), and phaser (20-23). Most effect parameter ranges are 0-100; filter cutoff is 0-107 and Q is 0-10.
- MIDI real-time `F8` clock, `FA` start, `FB` continue, and `FC` stop are recognized. The documented Continue behavior restarts like Start rather than resuming.
- Synth-parameter SysEx is `F0 43 10 pp qq vv F7`, where parameter index is `pp * 128 + qq`, covering parameters 0-155.

This synth-parameter SysEx is **not** the firmware updater protocol.

## Updater behavior and community-code audit

### What the official updater proves

The packaged logs show the FM-1 changing from `FM-1` / `FM-1 Midi` to `ota-FM-1` during an update and then re-enumerating again. Generic updater debug logs show a device-requested transfer model: the device asks for a flash type, address, and length, normally in 512-byte chunks, and later makes a special request at `0xe0000000` for verification/transition handling.

This means a safe replacement host tool must:

- Handle MIDI/USB disappearance and re-enumeration.
- Respond to device-specified addresses and lengths.
- Implement timeouts, retries, verification, and recovery states.
- Preserve the identity/config sector.
- Never assume that streaming the whole file sequentially is sufficient.

### `aroum/fm1-custom-fw`

The community repository is useful for teardown photographs and early protocol observations, but its current flasher must not be used on hardware.

Static inspection found that its supposed `@JMUA` and `JLUFW` firmware signatures are unrelated Qt/UI resource bytes. `JLUFW` is immediately followed by a PNG signature. The actual V14 firmware is stored as a Qt resource at `:/Resources/FM-1.fwsc`, with a 704,052-byte payload. The community extractor instead takes a fixed 150,000-byte slice, so it truncates/misidentifies the firmware. Its flasher also streams sequential SysEx and does not implement the updater's pull requests or re-enumeration state machine.

Treat its handshake captures as leads to reproduce, not as a flashing implementation.

## Toolchain and source feasibility

JieLi's official AC79 SDK maps this family to `wl82`/`AC791N`. The architecture/toolchain is proprietary `pi32v2`, using a JieLi-distributed Clang/LLVM toolchain with `-mcpu=r3`. The Windows build expects tools under `C:\JL\pi32\bin`; the SDK also documents Linux compilation. There is no standard GCC target for this CPU.

What is available:

- Official AC79 SDK, linker scripts, peripheral examples, USB/Bluetooth/audio examples, and binary libraries.
- Existing firmware-container tools in `jl-misctools`.
- Existing reverse-engineering notes for the `pi32v2` instruction set.
- A community Ghidra module exists, but its `pi32v2` support is explicitly at a very early stage. The community UBOOT tool lists `WL82 / AC791N` support as unknown, so neither should be treated as a recovery solution without FM-1-specific proof. JieLi also publishes a customizable bootloader repository that may help explain the family boot flow.

What is missing:

- FM-1 board schematic and board-support package.
- Display controller identity and initialization sequence.
- GPIO map for keys, encoders, shift registers, LEDs, battery sensing, MIDI input, DAC and amplifier control.
- Exact USB descriptors and runtime/OTA command framing from a live unit.
- A proven physical recovery path.
- Source for the stock FM synth, UI, sequencer and effects.

The SoC is capable enough for a six-operator engine, but the 1 MiB flash is tight: the stock application already occupies roughly 582 KiB and relies on the vendor's compact libraries. A replacement should start with a small fixed-point or carefully profiled floating-point core and avoid pulling in the SDK's unrelated media assets/libraries.

## Safe next work package

### Gate 1: live, read-only inventory

- Record USB VID/PID, interface descriptors, endpoint layout, MIDI port names, audio descriptors, and current firmware version.
- Capture normal boot and entry into OTA mode without sending a custom payload.
- Verify whether the device exposes USB audio in addition to MIDI on V15.

### Gate 2: recovery proof

- Photograph both PCB sides at high resolution.
- Continuity-map pins 40/41, 47/48 and PB1 to USB/test pads.
- Identify voltage levels before attaching any UART/logic analyzer.
- Determine whether stock V15 can always be restored using the official updater.
- Do not proceed to custom images until a failed application can be recovered independently of that application.

### Gate 3: updater protocol capture

- Capture one official V14-to-V15 or V15-to-V15 transfer with USBPcap/Wireshark or a MIDI proxy.
- Derive framing, checksums, request/response directions, re-enumeration states and verification behavior.
- Replay only against a disposable or recovery-proven unit.

### Gate 4: board-support firmware

- Build an SDK `demo_hello`-class image with the stock flash layout and conservative 320 MHz clock.
- First milestone: USB enumeration plus UART logging, no audio and no erase outside the app region.
- Then bring up display, keys/encoders, CS4344 I2S audio, speaker control, TRS MIDI, BLE MIDI, battery sensing and persistent storage one subsystem at a time.

### Gate 5: synth firmware

- Evaluate a six-operator engine such as Dexed/MSFA only after checking its license and reducing it to the required embedded feature set.
- Build DX7 SysEx compatibility tests from known voices, including the reviewer's failing long-envelope/algorithm cases.
- Add polyphony/load measurement, output-level calibration, MIDI timing, sequencer clock and note-off regression tests.

## Preserved local evidence

| File | SHA-256 |
|---|---|
| `references/downloads/AC7911B8-datasheet-v1.1.pdf` | `99f5163d8847ceba06ef1d088238d077826289bc301e0d7dbd4d2200eb524a49` |
| `references/downloads/FM-1-MIDI-EN.docx` | `f7cb237e3c83b2ee416c07c3b55019a8eb6dcab118cb7b9df39070d894e8dd8a` |
| `references/downloads/FM-1-release-notes.txt` | `c7b816137539801490cee42561c8180bd02ecdd68de074a1ee486d870817bc1b` |
| `references/downloads/FM1-official-complete.zip` | `eedf0c915583af30592f52e163bcc812506a4dfc8a749f5543d05e4666ef1128` |
| `references/originals/FM-1-v14-original.fwsc` | `a1adca99b1f9823ff2873292be74877a7a14ca0846fca3d25a23aba2750162d3` |
| `references/originals/FM-1-v15-original.fwsc` | `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` |
| `references/originals/FM-1-v14-factory-4bank.bin` | `590cb9a7c3447af3fb6932e182ef61e70e618ee308dc7a519d291a687e869b58` |

Source snapshots:

| Repository | Commit |
|---|---|
| `aroum/fm1-custom-fw` | `451709c79dc21a526aa9588ec6ba12582604e1e4` |
| `kagaimiq/jl-misctools` | `0a5b12db0ef38f3042acffbe2452730a37fd2405` |
| `kagaimiq/jielie` | `1657d25e6e51df6b2c18cd55cfc576c4a6370c63` |
| `Jieli-Tech/fw-AC79_AIoT_SDK`, branch `release/AC79NN_SDK_V1.2.0` | `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` |

## Web sources

- [M-VAVE download center](https://www.m-vave.com/download)
- [Direct current FM-1 firmware](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc)
- [Official FM-1 release notes](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1.txt)
- [Official FM-1 MIDI document](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1%20MIDI%20EN.docx)
- [FM-1 manual mirror](https://device.report/manual/20413651)
- [User-linked FM-1 review](https://www.reddit.com/r/synthesizers/comments/1v5sxzq/mvave_fm1_review/)
- [FM-1 teardown with photos](https://www.reddit.com/r/synth/comments/1vgotkd/mvave_fm1_teardown_with_photos/)
- [AC7911B8 datasheet mirror](https://www.szjixing.cn/r01/F99B7/p03/file/20240115/AC7911B8_351.pdf)
- [JieLi AC79 official SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK)
- [Cirrus Logic CS4344 datasheet](https://statics.cirrus.com/pubs/proDatasheet/CS4344-45-48_F2.pdf)
- [ChipSourceTek SL5316 product page](https://en.chipsourcetek.com/Audio-Chip/4856.html)
- [Community FM-1 repository](https://github.com/aroum/fm1-custom-fw)
- [JieLi firmware utilities](https://github.com/kagaimiq/jl-misctools)
- [Early-stage Ghidra support for JieLi CPUs](https://github.com/kagaimiq/ghidra-jieli)
- [Community JieLi UBOOT tool](https://github.com/kagaimiq/jl-uboot-tool)
- [JieLi official customizable bootloader repository](https://github.com/Jieli-Tech/fw-Bootloader)
