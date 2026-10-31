#ifndef JKERNEL_VIRTIO_INPUT_H
#define JKERNEL_VIRTIO_INPUT_H

#include <stdint.h>

/* Normalized pointer coordinates: 0 maps to the advertised ABS minimum and
 * UINT16_MAX maps to its maximum, independent of the display resolution. */
#define VIRTIO_POINTER_BUTTON_LEFT   (1U << 0)
#define VIRTIO_POINTER_BUTTON_RIGHT  (1U << 1)
#define VIRTIO_POINTER_BUTTON_MIDDLE (1U << 2)

/* `buttons` is the OR of the button masks above that are currently pressed. */
struct virtio_pointer_state {
    uint16_t x;
    uint16_t y;
    uint32_t buttons;
};

/* Discover and initialize the VirtIO input device from the supplied FDT. */
int virtio_input_init(const void *dtb);

/* Poll the event queue without blocking; call regularly from the kernel loop. */
void virtio_input_poll(void);

/* Copy the most recent SYN_REPORT state. Returns -1 until init succeeds. */
int virtio_input_get_pointer(struct virtio_pointer_state *state);

#endif
