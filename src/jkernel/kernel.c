/* Freestanding kernel entry point for QEMU's AArch64 virt machine. */
#include <stddef.h>
#include <stdint.h>

#include "display.h"
#include "fdt.h"
#include "fw_cfg.h"
#include "pl011.h"
#include "ui.h"
#include "virtio_input.h"
#include "zesh.h"

/* QEMU 8.2.2 direct AArch64 ELF boot places the generated DTB here when free.
 * start.S repurposes x0 before C, so this path deliberately does not use x0. */
#define QEMU_DTB_ADDRESS ((const void *)(uintptr_t)0x40000000UL)

void
kernel_main(void)
{
    uintptr_t fw_cfg_base;
    uint64_t fw_cfg_size;
    struct display_surface surface;
    struct virtio_pointer_state pointer;
    struct virtio_pointer_state last_pointer;
    int display_ready = 0;
    int input_ready = 0;
    int ui_drawn = 0;
    int last_pointer_valid = 0;

    pl011_init();
    pl011_write_string("jkernel: milestone 0.3\n");
    pl011_write_string("AArch64 QEMU virt PL011 console ready.\n");

    if (fdt_find_mmio_compatible(QEMU_DTB_ADDRESS, "qemu,fw-cfg-mmio",
                                 &fw_cfg_base, &fw_cfg_size) != 0) {
        pl011_write_string("jkernel: fw_cfg FDT discovery failed; serial only\n");
    } else if (fw_cfg_init(fw_cfg_base, fw_cfg_size) != 0) {
        pl011_write_string("jkernel: fw_cfg DMA unavailable; serial only\n");
    } else if (display_init() != 0) {
        pl011_write_string("jkernel: ramfb setup failed; serial only\n");
    } else if (display_get_surface(&surface) != 0) {
        pl011_write_string("jkernel: ramfb surface unavailable; serial only\n");
    } else {
        display_ready = 1;
        pl011_write_string("jkernel: 640x480 ramfb UI surface ready\n");
    }

    if (virtio_input_init(QEMU_DTB_ADDRESS) != 0) {
        pl011_write_string("jkernel: virtio tablet unavailable; pointer polling off\n");
    } else {
        input_ready = 1;
        pl011_write_string("jkernel: virtio tablet pointer polling ready\n");
    }

    for (;;) {
        int pointer_valid = 0;
        int redraw_ui;

        if (input_ready != 0) {
            virtio_input_poll();
            if (virtio_input_get_pointer(&pointer) == 0) {
                pointer_valid = 1;
            }
        }

        redraw_ui = !ui_drawn || pointer_valid != last_pointer_valid;
        if (pointer_valid != 0 && last_pointer_valid != 0 &&
            (pointer.x != last_pointer.x || pointer.y != last_pointer.y ||
             pointer.buttons != last_pointer.buttons)) {
            redraw_ui = 1;
        }
        if (display_ready != 0 && redraw_ui != 0) {
            ui_render(surface.pixels, surface.width, surface.height,
                      surface.stride_pixels, 0,
                      pointer_valid != 0 ? &pointer : 0);
            ui_drawn = 1;
            last_pointer_valid = pointer_valid;
            if (pointer_valid != 0) {
                last_pointer = pointer;
            }
        }

        zesh_run(pl011_read_byte, pl011_write_char, NULL);
    }
}
