#ifndef JKERNEL_UI_H
#define JKERNEL_UI_H

#include <stdint.h>
#include "virtio_input.h"

#define UI_MAX_LOG_LINES 8U

/*
 * Optional console content supplied by the kernel's UI integration layer.
 * Unset fields use the built-in bring-up dashboard text. `line_count` is
 * clamped to UI_MAX_LOG_LINES; lines are displayed oldest to newest.
 */
struct ui_console_state {
    const char *lines[UI_MAX_LOG_LINES];
    uint32_t line_count;
    const char *prompt;
    const char *input_status;
};

/*
 * Redraw the 640x480-oriented JARVIS console/dashboard into a packed XRGB8888
 * software framebuffer. Supported width is 480..640, height is 400..480, and
 * `stride_pixels` (pixels between row starts) must be width..640. Pass a
 * non-null VirtIO pointer snapshot to show its cursor; the UI owns cursor
 * rendering.
 */
void ui_render(uint32_t *pixels, uint32_t width, uint32_t height,
               uint32_t stride_pixels,
               const struct ui_console_state *console,
               const struct virtio_pointer_state *pointer);

#endif
