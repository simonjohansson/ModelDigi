/* The cost driver at the start of the emulator's first RAM page; the stack sits at its top. */
SECTIONS
{
    . = 0x40000400;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note.*) }
}
