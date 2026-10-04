# Stock volume input: recovered from FM-1_010

Offline investigation only. No flash, reset, live ADC read or pin configuration
was performed. Current NES firmware has fixed gain128 and no volume ADC reader.
Earlier notes calling the knob an independent analog attenuator were unverified
and are superseded by this trace: stock uses ADC input and digital audio gain.

## Identifiers

| Item | Recovered value |
| --- | --- |
| ADC channel ID |4, SDK `AD_CH_PB06` |
| GPIO |PB6, SDK `IO_PORTB_06` =22 /0x16 |
| Raw reading |10-bit, nominal0..1023 |
| Volume value |raw >>3, nominal0..127 |
| Queued event |0x04000000 OR volume; type byte0x04 |
| Stock getter |0x02024436, matching SDK `adc_get_value` |
| Poll callback |0x02024552, scheduled with interval2ms |
| Latest raw sample |0x01c0e6b0 (=0x01c0e650+0x60) |
| Last accepted raw sample |0x01c08fd2 |
| Digital target gain byte |0x01c1d219 |
| Ramped current gain byte |0x01c1d218 |

ADC channel3 /PB1 is read alongside it for battery/power reporting; it is not
the volume input. The seven rotary encoder IDs are also a separate interface.
RAM addresses above belong to this stock image only, not our linked NES image.

## Evidence chain

Source image `private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin`,
SHA256 `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
Used existing hash-gated `tools/jieli-wl82/trace_stock_calls.py` and the saved
application/RAM-copy disassemblies. SDK header
`references/source/fw-AC79_AIoT_SDK/include_lib/driver/cpu/wl82/asm/adc_api.h`
maps channel4 toPB6; `asm/gpio.h` mapsPB6 toGPIO0x16.

1. At0x0200450e..0x0200451c stock configures GPIO0x16 through its input helper
   and registers ADC channel4 via0x0200419a. Channel3 is separately registered.
2. At0x02004520..0x0200452a it registers callback0x02024552 with interval2.
3. Callback0x02024554 loads r0=4, calls0x02024436 and saves the raw halfword
   at0x01c0e650+0x60. Getter searches the same channel/value queue layout as
   SDK adc_get_value, including its VBAT/LDOREF special cases.
4. At0x020245f4..0x0202461c it rejects differences within +/-6 raw counts,
   requires10 consecutive out-of-deadband polls, and compares raw>>3 against
   the last accepted raw>>3 before publishing. This is not ten identical
   candidate readings. Accepted raw is stored at0x01c08fd2.
5. At0x02024610 raw is shifted right3;0x0202464a ORs0x04000000 and queues it.
6. Consumer0x020239ea extracts the high byte. Jump table at0x020239f4 is
   `07 3e 3e 3e 06 3e 05 04`; index4 dispatches to0x02023a00, which jumps
   to0x02023228. That handler updates target byte0x01c1d219 at0x0202324c.
   It also converts the value by approximately100/127 for the UI.
7. RAM audio routine0x01c04b46..0x01c04b5e moves current gain toward target
   one step per callback, after startup mute. At0x01c04bfa..0x01c04bfe it
   multiplies audio by that gain and shifts right7 (gain/128).

This establishes the software assignment and digital-control path. Actual
knob endpoint readings, direction and noise still need a live test. The
current USB protocol has no ADC/VOLUME command, so no current knob position
can be reported without adding a scoped ADC diagnostic. Reuse the SDK's
asynchronous channel sampling; do not put a conversion wait inside the NES
frame loop or audio ISR. Preserve the working USB update path and pacing.
