/* Three in-place V14 patches, used only WITH the accepted polled-pixel loop.
 * Pending was cleared by the preceding unchanged command transaction.
 * Polling remains unbounded, as in stock. No calls, stack changes or IRQ changes.
 */
.section .params_table,"ax",@progbits
.global fm1_polled_table_params
fm1_polled_table_params:
    /* 020232F2..02023310: r0=row base, r10=count offset, r6=SPI1, r7=PC OUT.
     * Unchanged mask guarantees nonzero length. r0/r1/r2 are scratch here. */
    r1 = b[r0+r10] (u)
    r0 = r0 + 0x16a2
.Ltablebyte:
    r2 = b[r0++=1] (u)
    [r6+8] = r2
.Ltablewait:
    r2 = [r6]
    if ((r2 & 0x8000) == 0) goto .Ltablewait
    [r6] |= 0x4000
    if (--r1 != 0) goto .Ltablebyte
    [r7] |= 0x80

.section .params_columns,"ax",@progbits
.global fm1_polled_columns
fm1_polled_columns:
    /* 02021808..02021822: r0=PC OUT, r1=SPI1. r4 is already saved by stock.
     * r2 no longer needs to point to the unused DMA pixel buffer. */
    r3 = sp + 80
    r4 = 4
.Lcolumnbyte:
    r2 = b[r3++=1] (u)
    [r1+8] = r2
.Lcolumnwait:
    r2 = [r1]
    if ((r2 & 0x8000) == 0) goto .Lcolumnwait
    [r1] |= 0x4000
    if (--r4 != 0) goto .Lcolumnbyte
    [r0] |= 0x80

.section .params_rows,"ax",@progbits
.global fm1_polled_rows
fm1_polled_rows:
    /* 02021848..02021862: same contract, other unchanged coordinate buffer. */
    r3 = sp + 84
    r4 = 4
.Lrowbyte:
    r2 = b[r3++=1] (u)
    [r1+8] = r2
.Lrowwait:
    r2 = [r1]
    if ((r2 & 0x8000) == 0) goto .Lrowwait
    [r1] |= 0x4000
    if (--r4 != 0) goto .Lrowbyte
    [r0] |= 0x80
