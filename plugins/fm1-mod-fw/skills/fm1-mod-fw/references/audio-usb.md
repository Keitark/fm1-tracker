# Runtime audio, ADC and USB findings

## Verified shared board paths

Evidence: `fm1/firmware/nes/src/fm1_volume.c`, `fm1_audio_queue.c`, `fm1_wl82_keyscan.c`, boot `display_test.c`, and `docs/FM1_LCD_HANDOFF_FIX_20260929.md`.

- Physical volume input is **PB6, ADC channel 4** (`AD_CH_PB06`), derived from stock firmware and the SDK map. The working NES deployment recorded raw ADC122..971 and gain0..121 over about183 seconds. Doom used the same volume source. Do not guess another pin because the knob is stuck.
- Key scanner owns PB7; its bit0x80 GPIO changes preserve volume's PB6 bit0x40.
- IIS route is PORTC PC0..6, stereo24bit at44100 Hz; LCD owns PC7..10 with PA2 backlight.
- Stock LCD uses a 64-byte aligned3840-byte DMA strip. Wait for DMA completion and issue the required coherence barrier before reusing it. The shared strip avoids a duplicate allocation.

## Discriminate volume failures

Read `AUDIO` raw, sample count, valid/errors, target and applied gain while the user deliberately sweeps the knob. Sampling count advancing is not evidence that the voltage changes.

| Observation | Next investigation |
| --- | --- |
| Raw constant during confirmed sweep | ADC channel/configuration, analog gates and PB6 registers, physical input |
| Samples stop | Timer/ADC ownership and scanner recovery |
| Target changes, gain stays fixed | CPU control publication and output callback |
| Gain changes, loudness does not | Actual DMA samples and installed artifact |

The2026-10-06 Doom build `8a36a90` recorded raw260..261 throughout a confirmed sweep, advancing samples, valid1/errors0 and gain=target32. No wrong pin or missing gain operation was demonstrated. Live read-only ADC/GPIO diagnostics were being added; **cause still unresolved at this checkpoint**. Keep this distinct from the earlier working NES result.

Doom's current split is CPU0 timer ADC polling, CPU1 ALINK renderer, aligned volatile Q7 control with `csync`. Reverting IRQ affinity without evidence can restore CPU0 starvation. Check emitted code and the real IRQ output path, not only an envelope unit test.

The real NES driver reads control `0x13100` and result `0x13104`. Its launch writes `0xf44e` then sets kick/enable, with ADC IRQ bit5 disabled; completion is bit7. PB6 analog input requires DIR bit6 set and DIE, PU, PD and DIEH bit6 cleared. The working deployed NES path is `firmware/usb-diag/peripherals.c`: LCD init, scanner start, volume start, then IIS open/rate/IRQ. Doom uses the same volume-before-IIS order. The pinned44100Hz IIS branch clears CLK_CON2 bits8/9 and programs ALINK0_CON3; it does not overwrite the volume WLA_CON0 or PLL_CON1 gates. This ordering hypothesis was checked and did not explain the fault.

## Audio timing and fidelity

64 stereo frames at44100Hz give an IRQ rendering budget of about1.451ms. Historical current telemetry showed a4500us maximum and roughly350..400 callbacks/sec against about689 expected. These counters indicate a timing fault; they are not a dedicated DAC underrun counter. The timer used for that maximum is quantized in500us steps and excludes time waiting for the mixer lock.

OPL music means Doom DMX register timing plus the original GENMIDI operator data rendered through9-voice OPL2. It is separate from original PCM effects. An analog synth mode intentionally changes the timbre. E4 toggles that mode; confirm `AUDIO synth_mode=0` before diagnosing OPL fidelity. A host PCM comparison cannot establish physical IIS playback timing or audible output quality.

The2026-10-06 audit found unnecessary synth voices, drum stem extraction and blending still computed in settled OPL mode. A new source candidate boots in OPL mode and skips that unused work; exact noise LFSR chunks preserve state across rhythm changes. Host validation passed37784 ordered register writes,4233600 identical PCM samples and65536 rhythm-toggle samples. Measure the newly built target before claiming the deadline is met. AM-table behavior was suspect in one portable branch, but all15 active E1M1 patches had AM disabled, so it did not explain this song.

## USB and stack

The WL82 SDK task stack field is in32-bit words. LTO can inline separate handlers into one larger task frame. Doom once emitted a3192-byte reply/task frame plus formatting, exceeding a4096-byte stack. Out-of-line protocol handlers and an emitted call-chain gate fixed that cause. Inspect the actual ELF and stack call chain after adding diagnostics; source buffer sizes alone are insufficient. ISR stack and task stacks are separate budgets.

Scalar telemetry must avoid taking the long CPU1 mixer lock. USB task/CPU0 service can otherwise wait behind audio and appear hung. USB queues and writes must remain bounded; never add a busy-poll packet writer to an IRQ. The frame capture protocol intentionally pauses engine ticks during a bounded snapshot; do not misdiagnose that pause as the old input freeze.

## USB Audio Class

CDC presence does not imply audio interfaces exist. The deployed Doom8a36a90 was CDC-only. The sibling `fm1-mdx` commit93c6820 has custom UAC1 audio interfaces2..4, EP1 audio and CDC OUT2/notifyIN2/dataIN3. Its full duplex FIFO/state costs about9KiB; Doom had only4244 bytes of live free heap and about1024 bytes above the reviewed startup gate, so blindly copying it cannot fit.

`fm1-mdx/USB_AUDIO.md` records an older packet writer stalling after75.62 seconds. Its bounded replacement was an unflashed candidate at this checkpoint. Reuse source patterns with current tests and actual target validation, not a claim that every MDX revision is qualified.

For capture-only native44100Hz stereoPCM16, EP1IN is free alongside Doom CDC. A small bounded producer FIFO, persistent pending packet and aligned endpoint DMA can fit with a measured static/linker budget. Preserve packet data on endpoint busy, bound IRQ work, handle alternate-setting/reset epochs and report drops/underruns. Keep the4096-byte reviewed runtime heap reserve; do not weaken the gate to force a build through.

## Allocator and capture bring-up checkpoint (2026-10-06)

The linked SDK allocator is `system.a(malloc.c.o)` dlmalloc, not the SDK source
`apps/common/system/mem_heap.c`. Its chunks round to32B: IIS's1024B DMA request
needs1056B. Heap growth uses4096B increments, and the pinned `sbrk` rejects
an end at or beyond HEAP_END. `get_malloc_remain_heap_size` combines free and
uncommitted space; it is neither a largest-block measurement nor proof that
another allocation can succeed. Keep this distinction in startup gates.

The first Doom capture image was readback-verified (`c510e024...`) and exposed
CDC plus a Windows recording endpoint after UBOOT reset. IIS then reported
ready0/error-1/irqs0 and stopped its ADC/capture producer during cleanup.
After the user's reset Doom remained visible but no USB interface appeared.
This establishes neither a USB recording nor the exact allocation failure.

Running Doom proves `task_create(doom_usb)` succeeded: app_main returns before
creating Doom when that call fails. USB initialization can separately fail
on CDC allocations (-3), configuration allocation (-1), or EP0 DMA (-2), then
sleep indefinitely. Capture initialization itself is static; its descriptor
order matches MDX and its endpoint DMA does not use the CDC pool.

The replacement restores1024B headroom using USB stack2560B and a512B CDC
reply ring. Every reply is bounded511B; production-task tests cover full
replies, busy retries, burst rejection and generation cancellation. Emitted
diagnostic chain1472B plus1024B SDK margin fits the stack. Linker heap45132B
and reviewed reserve5120B are source/build evidence; cold boot, live stack,
IIS timing, the physical knob and actual capture still require measurement.
Boot gain follows the ADC. `DOOM MUTE 1/0` is an optional speaker override;
capture taps the mixed audio before that gain/override.
