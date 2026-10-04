ENTRY(fm1_status_dispatch)
original_response = 0x01c04b32;
original_fc_resume = 0x01c0431c;
SECTIONS {
    .patch 0x01c04318 : { KEEP(*(.patch)) }
    .text 0x01c60000 : { KEEP(*(.text.status_dispatch)) *(.text .text.*) *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.patch) == 4, "Hook must replace exactly four bytes")
ASSERT(SIZEOF(.text) <= 4096, "Status extension exceeds its proposed RAM window")
ASSERT(SIZEOF(.data) == 0, "No initialized mutable globals allowed")
ASSERT(SIZEOF(.bss) == 0, "No zero-init runtime required")
