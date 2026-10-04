ENTRY(fm1_status_dispatch)
original_response = 0x01c04b32;
original_fc_resume = 0x01c0431c;
original_fc_fallback = 0x01c04318;
SECTIONS {
    .route 0x01c04272 : { KEEP(*(.route)) }
    .patch 0x01c04318 : { KEEP(*(.patch)) }
    .text 0x01c048e4 : { KEEP(*(.text.status_dispatch)) *(.text .text.*) *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.route) == 4, "Route replacement must be four bytes")
ASSERT(SIZEOF(.patch) == 4, "Hook replacement must be four bytes")
ASSERT(SIZEOF(.text) <= 382, "Extension exceeds retired FC41..48 handler region")
ASSERT(SIZEOF(.data) == 0, "No new mutable globals")
ASSERT(SIZEOF(.bss) == 0, "No new BSS")
