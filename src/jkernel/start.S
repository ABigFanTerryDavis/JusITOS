.section .text.boot, "ax"
.align  4
.global _start
.type   _start, %function

_start:
    /* QEMU's direct kernel entry has no C runtime; use our private stack. */
    msr     daifset, #0xf
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0

    /* Clear zero-initialized data and the stack before entering C. */
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:
    cmp     x0, x1
    b.hs    2f
    stp     xzr, xzr, [x0], #16
    b       1b
2:
    bl      kernel_main

3:
    wfe
    b       3b

.size _start, . - _start
