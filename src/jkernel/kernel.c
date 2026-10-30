/* Freestanding kernel entry point for QEMU's AArch64 virt machine. */
#include <stddef.h>

#include "pl011.h"
#include "zesh.h"

void
kernel_main(void)
{
    pl011_init();
    pl011_write_string("jkernel: milestone 0.2\n");
    pl011_write_string("AArch64 QEMU virt PL011 console ready.\n");

    for (;;) {
        zesh_run(pl011_read_byte, pl011_write_char, NULL);
    }
}
