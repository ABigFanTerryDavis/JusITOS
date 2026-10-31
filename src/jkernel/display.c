#include "display.h"

#include <stdint.h>

#include "fw_cfg.h"

#define DISPLAY_WIDTH      640U
#define DISPLAY_HEIGHT     480U
#define DISPLAY_STRIDE     (DISPLAY_WIDTH * 4U)
#define DISPLAY_SCALE      2U
#define RAMFB_FOURCC_XRGB8888 0x34325258U /* DRM_FORMAT_XRGB8888 */

static uint32_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT]
    __attribute__((aligned(4096)));
static uint8_t ramfb_config[28] __attribute__((aligned(16)));
static int display_ready;
/* 5x7 glyphs for the uppercase text drawn on this bring-up screen. */
static const uint8_t font[128][7] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['!'] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },
    ['-'] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 },
    ['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c },
    [':'] = { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 },
    ['/'] = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },
    ['0'] = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    ['1'] = { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
    ['2'] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
    ['3'] = { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
    ['4'] = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
    ['5'] = { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
    ['6'] = { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    ['7'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    ['8'] = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
    ['9'] = { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
    ['A'] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    ['B'] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
    ['C'] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
    ['D'] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
    ['E'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
    ['F'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
    ['G'] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
    ['H'] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    ['I'] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
    ['J'] = { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
    ['K'] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    ['L'] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
    ['M'] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
    ['N'] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    ['O'] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    ['P'] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
    ['Q'] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
    ['R'] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
    ['S'] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
    ['T'] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    ['U'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    ['V'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
    ['W'] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a },
    ['X'] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
    ['Y'] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
    ['Z'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }
};

static void
store_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void
store_be64(uint8_t *destination, uint64_t value)
{
    store_be32(destination, (uint32_t)(value >> 32));
    store_be32(destination + 4, (uint32_t)value);
}

static void
fill_rect(unsigned int x, unsigned int y, unsigned int width,
          unsigned int height, uint32_t color)
{
    unsigned int row;

    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
        return;
    }
    if (width > DISPLAY_WIDTH - x) {
        width = DISPLAY_WIDTH - x;
    }
    if (height > DISPLAY_HEIGHT - y) {
        height = DISPLAY_HEIGHT - y;
    }
    for (row = 0U; row < height; ++row) {
        unsigned int column;
        uint32_t *pixel = &framebuffer[(y + row) * DISPLAY_WIDTH + x];

        for (column = 0U; column < width; ++column) {
            pixel[column] = color;
        }
    }
}

static void
draw_text(unsigned int x, unsigned int y, const char *text, uint32_t color)
{
    unsigned int origin_x = x;

    while (*text != '\0') {
        unsigned char character = (unsigned char)*text++;
        unsigned int row;

        if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
            character = (unsigned char)(character - (unsigned char)'a' +
                                        (unsigned char)'A');
        }
        if (character >= 128U) {
            character = (unsigned char)' ';
        }
        if (x + 6U * DISPLAY_SCALE > DISPLAY_WIDTH) {
            break;
        }
        for (row = 0U; row < 7U; ++row) {
            unsigned int column;

            for (column = 0U; column < 5U; ++column) {
                if ((font[character][row] & (1U << (4U - column))) != 0U) {
                    fill_rect(x + column * DISPLAY_SCALE,
                              y + row * DISPLAY_SCALE,
                              DISPLAY_SCALE, DISPLAY_SCALE, color);
                }
            }
        }
        x += 6U * DISPLAY_SCALE;
    }
    (void)origin_x;
}

static void
draw_test_screen(void)
{
    static const uint32_t bars[8] = {
        0x00000000U, 0x00ffffffU, 0x00ff0000U, 0x0000ff00U,
        0x000000ffU, 0x00ffff00U, 0x0000ffffU, 0x00ff00ffU
    };
    unsigned int index;

    fill_rect(0U, 0U, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x00111a28U);
    for (index = 0U; index < 8U; ++index) {
        fill_rect(index * 80U, 0U, 80U, 120U, bars[index]);
    }
    fill_rect(0U, 120U, DISPLAY_WIDTH, 4U, 0x0048c9b0U);
    draw_text(32U, 158U, "JKERNEL DISPLAY TEST", 0x00f3f6ffU);
    draw_text(32U, 204U, "FW_CFG DMA / RAMFB", 0x008be9fdU);
    draw_text(32U, 250U, "SOFTWARE DRAWN 640X480", 0x00f3f6ffU);
    fill_rect(32U, 298U, 576U, 2U, 0x0032435aU);
    draw_text(32U, 326U, "SERIAL CONSOLE: ZESH>", 0x00f6c177U);
    draw_text(32U, 372U, "HELP  VERSION", 0x00cbd5e1U);
}

int
display_init(void)
{
    uint64_t framebuffer_address = (uint64_t)(uintptr_t)framebuffer;

    draw_test_screen();

    /* QEMU ramfb's 28-byte configuration structure is big-endian. */
    store_be64(ramfb_config, framebuffer_address);
    store_be32(ramfb_config + 8U, RAMFB_FOURCC_XRGB8888);
    store_be32(ramfb_config + 12U, 0U);
    store_be32(ramfb_config + 16U, DISPLAY_WIDTH);
    store_be32(ramfb_config + 20U, DISPLAY_HEIGHT);
    store_be32(ramfb_config + 24U, DISPLAY_STRIDE);

    display_ready = fw_cfg_write_file("etc/ramfb", ramfb_config,
                                      sizeof(ramfb_config)) == 0;
    return display_ready != 0 ? 0 : -1;
}

int
display_get_surface(struct display_surface *surface)
{
    if (surface == 0 || display_ready == 0) {
        return -1;
    }
    surface->pixels = framebuffer;
    surface->width = DISPLAY_WIDTH;
    surface->height = DISPLAY_HEIGHT;
    surface->stride_pixels = DISPLAY_WIDTH;
    return 0;
}
