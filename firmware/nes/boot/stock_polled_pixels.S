/* Offline stock-V14 pixel-loop replacement ONLY, not an entry point.
 * At 0x02021880: r0=PC OUT, r1=SPI1 CON; stock has sent RAMWR (2Ch).
 * Replaces 2880 DMA transfers of 40 white bytes with 115200 polled bytes.
 * Leaves r4=[OUT] for the original 29h sequence. No calls or stack changes.
 * Completion polling, like the original stock loop, has no software timeout.
 * No setup, reset, rail, IRQ, watchdog, integrity or startup changes.
 */
.text
.global fm1_stock_polled_pixels
fm1_stock_polled_pixels:
    [r0] |= 0x100
    [r0] &= 0xffffff7f
    r3 = 115200
.Lpixel:
    [r1] |= 0x4000
    [r1+8] = 0xff
.Lpending:
    r4 = [r1]
    if ((r4 & 0x8000) == 0) goto .Lpending
    r3 += -1
    if (r3 != 0) goto .Lpixel
    [r1] |= 0x4000
    [r0] |= 0x80
    r4 = [r0]
    nop
    nop
    nop
