/*
 * ==========================================================================
 *  JusITOS 0.0.1 — Console Init / Splash
 * ==========================================================================
 *  This is PID 1 on JusITOS. The kernel hands off to /sbin/init ->
 *  /bin/jusit-init (this binary).
 *
 *  Responsibilities in v0.0.1:
 *    1. Open the framebuffer (/dev/fb0).
 *    2. Map it into user memory via mmap().
 *    3. Paint the JusITOS splash screen (solid background + ASCII-art logo
 *       drawn pixel by pixel from a built-in bitmap).
 *    4. Open the keyboard (/dev/kbd0 or stdin from console).
 *    5. Loop forever reading keypresses; print the scan name at the bottom
 *       of the screen to prove input works.
 *    6. Poweroff on Ctrl+D (EOF) — only way to exit cleanly.
 *
 *  Build (on a FreeBSD arm64 host, or cross-built):
 *      cc -O2 -static -o jusit-init jusit_init.c
 *
 *  Notes:
 *    - We deliberately avoid libcurses, libdrm, libinput. Just syscall-level
 *      code so the binary stays ~30 KB and has no library dependencies.
 *    - All screen geometry is queried at runtime via FBIO_GETFBINFO ioctl,
 *      so the same binary works on QEMU virt (640x480) and any other panel.
 * ==========================================================================
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/fbio.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define VERSION_STRING  "0.0.1"
#define BUILD_DATE      __DATE__ " " __TIME__

/* Splash screen colors (RGB-24). */
#define COLOR_BG        0x0E0E14   /* deep slate  */
#define COLOR_FG        0x4DE2C5   /* mint accent */
#define COLOR_DIM       0x6B6B78   /* dim text    */
#define COLOR_PRESS     0xFFB347   /* keypress bar */

/* 8x16 monospace bitmap for our 5x7 ASCII glyphs.
 * We only ship the glyphs we actually print, to keep the binary small.
 * Each glyph is 5 cols x 7 rows; one byte per row, low 5 bits used. */
static const unsigned char font5x7[][7] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J'] = {0x02,0x04,0x04,0x04,0x04,0x14,0x0C},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['S'] = {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    [':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    ['-'] = {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
    ['>'] = {0x01,0x02,0x04,0x08,0x04,0x02,0x01},
};

static int      fb_fd     = -1;
static uint8_t *fb_mem    = MAP_FAILED;
static size_t   fb_size   = 0;
static int      fb_width  = 0;
static int      fb_height = 0;
static int      fb_stride = 0;     /* bytes per row */
static int      fb_bpp    = 0;     /* bytes per pixel */

static void
die(const char *what)
{
    perror(what);
    _exit(1);
}

static void
fb_open(void)
{
    struct fbtype fbinfo;
    struct fb_var_screeninfo var;
    int prot = PROT_READ | PROT_WRITE;

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        /* Some kernels expose it as /dev/fb/0 — try that. */
        fb_fd = open("/dev/fb/0", O_RDWR);
    }
    if (fb_fd < 0) die("open /dev/fb0");

    /* Try the modern FBIOGET_VSCREENINFO first (Linux-compatible); fall back
     * to the older fbtype ioctl. Both are best-effort. */
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) == 0) {
        fb_width  = var.xres;
        fb_height = var.yres;
        fb_bpp    = var.bits_per_pixel / 8;
        fb_stride = fb_width * fb_bpp;
        fb_size   = (size_t)fb_stride * fb_height;
    } else if (ioctl(fb_fd, FBIOGTYPE, &fbinfo) == 0) {
        fb_width  = fbinfo.fb_width;
        fb_height = fbinfo.fb_height;
        fb_bpp    = fbinfo.fb_depth / 8;
        fb_stride = fb_width * fb_bpp;
        fb_size   = (size_t)fb_stride * fb_height;
    } else {
        die("ioctl FBIOGET_*");
    }

    if (fb_bpp < 2) die("unsupported bpp");

    fb_mem = mmap(NULL, fb_size, prot, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) die("mmap /dev/fb0");
}

static inline void
fb_putpixel(int x, int y, uint32_t rgb)
{
    if (x < 0 || y < 0 || x >= fb_width || y >= fb_height) return;
    uint8_t *p = fb_mem + y * fb_stride + x * fb_bpp;
    switch (fb_bpp) {
        case 2: /* RGB565 */
            p[0] = (rgb >> 16 & 0xF8) | (rgb >> 13 & 0x07);
            p[1] = (rgb >> 5  & 0xE0) | (rgb >> 3  & 0x1F);
            break;
        case 3: /* RGB888 packed */
            p[0] = (rgb >> 16) & 0xFF;
            p[1] = (rgb >> 8)  & 0xFF;
            p[2] =  rgb        & 0xFF;
            break;
        case 4: /* XRGB8888 */
            p[0] = (rgb >> 16) & 0xFF;
            p[1] = (rgb >> 8)  & 0xFF;
            p[2] =  rgb        & 0xFF;
            p[3] = 0xFF;
            break;
        default: break;
    }
}

static void
fb_fill(uint32_t rgb)
{
    for (int y = 0; y < fb_height; y++) {
        uint8_t *row = fb_mem + y * fb_stride;
        for (int x = 0; x < fb_width; x++) {
            uint8_t *p = row + x * fb_bpp;
            switch (fb_bpp) {
                case 2:
                    p[0] = (rgb >> 16 & 0xF8) | (rgb >> 13 & 0x07);
                    p[1] = (rgb >> 5  & 0xE0) | (rgb >> 3  & 0x1F);
                    break;
                case 3:
                    p[0] = (rgb >> 16) & 0xFF;
                    p[1] = (rgb >> 8)  & 0xFF;
                    p[2] =  rgb        & 0xFF;
                    break;
                case 4:
                    p[0] = (rgb >> 16) & 0xFF;
                    p[1] = (rgb >> 8)  & 0xFF;
                    p[2] =  rgb        & 0xFF;
                    p[3] = 0xFF;
                    break;
                default: break;
            }
        }
    }
}

static void
fb_draw_char(int x0, int y0, char ch, uint32_t color)
{
    const unsigned char *glyph = NULL;
    if ((unsigned char)ch < (sizeof(font5x7)/sizeof(font5x7[0])) && font5x7[(unsigned char)ch][0] != 0xFF)
        glyph = font5x7[(unsigned char)ch];
    if (!glyph) glyph = font5x7['?'];

    for (int r = 0; r < 7; r++) {
        unsigned char row = glyph[r];
        for (int c = 0; c < 5; c++) {
            if (row & (1 << (4 - c)))
                fb_putpixel(x0 + c, y0 + r, color);
        }
    }
}

static void
fb_draw_text(int x0, int y0, const char *s, uint32_t color)
{
    int x = x0;
    for (; *s; s++) {
        fb_draw_char(x, y0, *s, color);
        x += 6;   /* 5 cols + 1 col gap */
    }
}

static void
fb_draw_text_centered(int y, const char *s, uint32_t color)
{
    int w = (int)strlen(s) * 6 - 1;
    fb_draw_text((fb_width - w) / 2, y, s, color);
}

static void
splash_screen(void)
{
    fb_fill(COLOR_BG);

    /* Big block logo: JusITOS */
    fb_draw_text_centered(fb_height * 35 / 100, "J U S I T O S", COLOR_FG);

    /* Version line */
    fb_draw_text_centered(fb_height * 50 / 100,
                          "0.0.1 - initial boot", COLOR_DIM);

    /* Footer hint */
    fb_draw_text_centered(fb_height * 75 / 100,
                          "> press any key to continue", COLOR_DIM);

    /* Status bar at bottom — gets overwritten by keypress echo */
    fb_draw_text(8, fb_height - 16, "READY", COLOR_FG);
}

int
main(void)
{
    fb_open();
    splash_screen();

    /* Read keypresses from stdin (the console).  When one arrives, echo
     * the key character onto the bottom status line. */
    char buf[2] = {0, 0};
    char status[64];
    ssize_t n;

    /* Make stdin non-blocking? No — block; nothing else to do. */
    while (1) {
        n = read(0, buf, 1);
        if (n == 0) {
            /* Ctrl+D — power off cleanly. */
            fb_draw_text_centered(fb_height * 60 / 100,
                                  "POWERING OFF...", COLOR_PRESS);
            sync();
            sleep(1);
            /* Trigger halt via reboot(2) syscall (RB_POWER_OFF). */
            reboot(0x4321fedc); /* RB_POWEROFF */
            _exit(0);
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            die("read stdin");
        }

        /* Wipe the status line first */
        for (int x = 0; x < fb_width - 8; x++)
            for (int y = fb_height - 16; y < fb_height - 8; y++)
                fb_putpixel(x, y, COLOR_BG);

        snprintf(status, sizeof(status), "> KEY: %c   (Ctrl+D to power off)",
                 buf[0] >= 32 ? buf[0] : '?');
        fb_draw_text(8, fb_height - 16, status, COLOR_PRESS);
    }

    return 0;
}
