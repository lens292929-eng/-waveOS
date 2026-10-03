ENTRY(kmain)

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.text*)
    }

    .rodata : ALIGN(8) {
        *(.rodata*)
    }

    .data : ALIGN(8) {
        *(.data*)
    }

    .bss : ALIGN(8) {
        *(COMMON)
        *(.bss*)
    }
}