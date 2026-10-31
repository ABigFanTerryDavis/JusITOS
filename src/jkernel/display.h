#ifndef JKERNEL_DISPLAY_H
#define JKERNEL_DISPLAY_H

#include <stdint.h>

/* Draw and publish the first software framebuffer test screen via ramfb. */
int display_init(void);

struct display_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
};

/* Return the live framebuffer surface after display_init() succeeds. */
int display_get_surface(struct display_surface *surface);

#endif
