/*
 * ==========================================================================
 *  JusITOS 0.0.1 — Console Init / Splash
 * ==========================================================================
 *  This is PID 1 on JusITOS. The kernel hands off to /sbin/init ->
 *  /bin/jusit-init (this binary).
 *
 *  Responsibilities in v0.0.1:
 *    1. Open /dev/console on standard input/output/error and enable raw input.
 *    2. Use a supported 32-bit UEFI framebuffer for the splash when available.
 *    3. Fall back to serial/console text if the framebuffer is unavailable.
 *    4. Echo keypresses and request poweroff on Ctrl+D (byte 0x04).
 *
 *  Build (on a FreeBSD arm64 host, or cross-built):
 *      cc -O2 -static -o jusit-init jusit_init.c
 *
 *  Notes:
 *    - We deliberately avoid libcurses, libdrm, libinput. Just syscall-level
 *      code so the binary stays ~30 KB and has no library dependencies.
 *    - Geometry and size come from FBIOGTYPE; stride and RGB offsets come
 *      from FreeBSD's FBIO_GETLINEWIDTH and FBIO_GETRGBOFFS ioctls.
 * ==========================================================================
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/fbio.h>
#include <sys/reboot.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#define VERSION_STRING  "0.0.1"
#define BUILD_DATE      __DATE__ " " __TIME__

/* Splash screen colors (RGB-24). */
#define COLOR_BG        0x0E0E14   /* deep slate  */
#define COLOR_FG        0x4DE2C5   /* mint accent */
#define COLOR_DIM       0x6B6B78   /* dim text    */
#define COLOR_PRESS     0xFFB347   /* keypress bar */

/* 5x7 bitmap glyphs; each row uses the low 5 bits. */
static const unsigned char font5x7[][7] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    ['('] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    [')'] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['3'] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5'] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    ['6'] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
    ['?'] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    [':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    ['-'] = {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
    ['>'] = {0x01,0x02,0x04,0x08,0x04,0x02,0x01},
    ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J'] = {0x02,0x04,0x04,0x04,0x04,0x14,0x0C},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W'] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static int
font5x7_has(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == ' ' || ch == '!' || ch == '(' || ch == ')' ||
           ch == '.' || ch == ':' || ch == '-' || ch == '>' || ch == '?';
}

static int      fb_fd     = -1;
static uint8_t *fb_mem    = MAP_FAILED;
static size_t   fb_size   = 0;
static int      fb_width  = 0;
static int      fb_height = 0;
static size_t   fb_stride = 0;     /* bytes per row */
static int      fb_red_shift = 0;
static int      fb_green_shift = 0;
static int      fb_blue_shift = 0;
static uint32_t fb_reserved_mask = 0;

static void
die(const char *what)
{
    int saved_errno = errno;
    dprintf(STDERR_FILENO, "[JusITOS] fatal: %s: %s\n", what,
        strerror(saved_errno));

    /* Keep PID 1 alive, but leave a serial/console path for shutdown. */
    for (;;) {
        char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) {
            sleep(1);
            continue;
        }
        if (n < 0) {
            sleep(1);
            continue;
        }
        if ((unsigned char)ch == 0x04) {
            sync();
            if (reboot(RB_POWEROFF) < 0)
                dprintf(STDERR_FILENO, "[JusITOS] poweroff failed: %s\n",
                    strerror(errno));
        } else {
            (void)write(STDOUT_FILENO, &ch, 1);
        }
    }
}

static void
console_open(void)
{
    int fd = open("/dev/console", O_RDWR);
    if (fd < 0) die("open /dev/console");

    for (int target = STDIN_FILENO; target <= STDERR_FILENO; target++) {
        if (fd != target && dup2(fd, target) < 0)
            die("dup2 /dev/console");
    }
    if (fd > STDERR_FILENO) close(fd);

    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) < 0) die("tcgetattr /dev/console");
    cfmakeraw(&term);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0)
        die("tcsetattr /dev/console");
}

static int
fb_open(void)
{
    struct fbtype fbinfo;
    struct fb_rgboffs offsets;
    u_int stride;
    int prot = PROT_READ | PROT_WRITE;
    int saved_errno;
    int vt_fd = -1;

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) goto fail;

    if (ioctl(fb_fd, FBIOGTYPE, &fbinfo) < 0)
        goto fail;

    if (fbinfo.fb_type != FBTYPE_EFIFB || fbinfo.fb_width <= 0 ||
        fbinfo.fb_height <= 0 || fbinfo.fb_depth != 32 ||
        fbinfo.fb_size <= 0) {
        errno = EINVAL;
        goto fail;
    }

    vt_fd = open("/dev/ttyv0", O_RDWR);
    if (vt_fd < 0)
        goto fail;
    if (ioctl(vt_fd, FBIO_GETRGBOFFS, &offsets) < 0)
        goto fail;
    close(vt_fd);
    vt_fd = -1;
    if (offsets.red < 0 || offsets.red > 24 || offsets.green < 0 ||
        offsets.green > 24 || offsets.blue < 0 || offsets.blue > 24 ||
        offsets.red != 16 || offsets.green != 8 || offsets.blue != 0) {
        errno = ENOTSUP;
        goto fail;
    }

    uint32_t red_mask = 0xFFu << offsets.red;
    uint32_t green_mask = 0xFFu << offsets.green;
    uint32_t blue_mask = 0xFFu << offsets.blue;
    if ((red_mask & green_mask) != 0 || (red_mask & blue_mask) != 0 ||
        (green_mask & blue_mask) != 0) {
        errno = EINVAL;
        goto fail;
    }

    fb_width = fbinfo.fb_width;
    fb_height = fbinfo.fb_height;
    fb_size = (size_t)fbinfo.fb_size;
    if (ioctl(fb_fd, FBIO_GETLINEWIDTH, &stride) < 0)
        goto fail;
    fb_stride = stride;
    fb_red_shift = offsets.red;
    fb_green_shift = offsets.green;
    fb_blue_shift = offsets.blue;
    fb_reserved_mask = ~(red_mask | green_mask | blue_mask);

    if (fb_stride < (size_t)fb_width * 4 ||
        fb_size < fb_stride * (size_t)fb_height) {
        errno = EINVAL;
        goto fail;
    }

    fb_mem = mmap(NULL, fb_size, prot, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) goto fail;
    return 0;

fail:
    saved_errno = errno;
    if (vt_fd >= 0)
        close(vt_fd);
    if (fb_mem != MAP_FAILED) {
        munmap(fb_mem, fb_size);
        fb_mem = MAP_FAILED;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
    errno = saved_errno;
    dprintf(STDERR_FILENO, "[JusITOS] framebuffer unavailable; using serial console: %s\n",
        strerror(errno));
    return -1;
}

static inline uint32_t
fb_pack_rgb(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xFF;
    uint32_t green = (rgb >> 8) & 0xFF;
    uint32_t blue = rgb & 0xFF;
    return (red << fb_red_shift) | (green << fb_green_shift) |
        (blue << fb_blue_shift) | fb_reserved_mask;
}

static inline void
fb_putpixel(int x, int y, uint32_t rgb)
{
    if (x < 0 || y < 0 || x >= fb_width || y >= fb_height) return;
    uint8_t *p = fb_mem + (size_t)y * fb_stride + (size_t)x * sizeof(uint32_t);
    uint32_t pixel = fb_pack_rgb(rgb);
    memcpy(p, &pixel, sizeof(pixel));
}

static void
fb_fill(uint32_t rgb)
{
    uint32_t pixel = fb_pack_rgb(rgb);
    for (int y = 0; y < fb_height; y++) {
        uint8_t *row = fb_mem + (size_t)y * fb_stride;
        for (int x = 0; x < fb_width; x++) {
            uint8_t *p = row + (size_t)x * sizeof(uint32_t);
            memcpy(p, &pixel, sizeof(pixel));
        }
    }
}

static void
fb_draw_char(int x0, int y0, char ch, uint32_t color)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    const unsigned char *glyph = NULL;
    if ((unsigned char)ch < (sizeof(font5x7)/sizeof(font5x7[0])) &&
        font5x7_has((unsigned char)ch))
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
    console_open();
    dprintf(STDERR_FILENO, "[JusITOS] INIT_REACHED\n");
    int framebuffer_available = (fb_open() == 0);
    if (framebuffer_available) {
        splash_screen();
        dprintf(STDERR_FILENO, "[JusITOS] SPLASH_READY\n");
    } else {
        dprintf(STDOUT_FILENO, "JusITOS 0.0.1: framebuffer splash unavailable.\n"
            "Press keys to echo; Ctrl+D requests poweroff.\n");
    }

    /* Read keypresses from stdin (the console).  When one arrives, echo
     * the key character onto the bottom status line. */
    char buf[2] = {0, 0};
    char status[64];
    ssize_t n;

    /* Make stdin non-blocking? No — block; nothing else to do. */
    while (1) {
        n = read(0, buf, 1);
        if (n == 0) {
            /* A closed input stream is not a Ctrl+D keypress. */
            sleep(1);
            continue;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            die("read stdin");
        }

        if ((unsigned char)buf[0] == 0x04) {
            dprintf(STDERR_FILENO, "[JusITOS] POWEROFF_REQUESTED\n");
            if (framebuffer_available)
                fb_draw_text_centered(fb_height * 60 / 100,
                                      "POWERING OFF...", COLOR_PRESS);
            else
                dprintf(STDOUT_FILENO, "JusITOS: powering off...\n");
            sync();
            sleep(1);
            if (reboot(RB_POWEROFF) < 0) {
                perror("reboot(RB_POWEROFF)");
                if (framebuffer_available)
                    fb_draw_text_centered(fb_height * 60 / 100,
                                          "POWEROFF FAILED", COLOR_PRESS);
            }
            continue;
        }

        if (!framebuffer_available) {
            char echo = (buf[0] >= 32 && buf[0] < 127) ? buf[0] : '?';
            dprintf(STDOUT_FILENO, "JusITOS key: %c\n", echo);
            continue;
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
