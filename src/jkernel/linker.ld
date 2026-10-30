OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = 0x40080000;

    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } :text

    .data : ALIGN(16)
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __stack_bottom = .;
        . += 16K;
        __stack_top = .;
        __bss_end = .;
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame.*)
    }
}
