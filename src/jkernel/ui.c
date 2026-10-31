#include "ui.h"

#define UI_COLOR_BACKGROUND  0x000a1020U
#define UI_COLOR_HEADER      0x000e1728U
#define UI_COLOR_SIDEBAR     0x000c1423U
#define UI_COLOR_PANEL       0x00111c2eU
#define UI_COLOR_PANEL_ALT   0x000e192aU
#define UI_COLOR_BORDER      0x00233349U
#define UI_COLOR_TEXT        0x00e7edf7U
#define UI_COLOR_MUTED       0x008899adU
#define UI_COLOR_ACCENT      0x0049d6b2U
#define UI_COLOR_BLUE        0x007cb7ffU
#define UI_COLOR_AMBER       0x00f4bd70U
#define UI_COLOR_GREEN       0x006de0a7U
#define UI_COLOR_RED         0x00f27878U
#define UI_HEADER_HEIGHT     36U
#define UI_SIDEBAR_WIDTH     136U
#define UI_MAIN_LEFT         150U
#define UI_MAX_WIDTH         640U
#define UI_MAX_HEIGHT        480U
#define UI_MIN_WIDTH         480U
#define UI_MIN_HEIGHT        400U
#define UI_TEXT_ADVANCE      6U
#define UI_TEXT_HEIGHT       7U

struct ui_canvas {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
};

/* Uppercase 5x7 glyphs keep the renderer small and libc-independent. */
static const uint8_t font[128][UI_TEXT_HEIGHT] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['!'] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },
    ['#'] = { 0x0a, 0x1f, 0x0a, 0x0a, 0x1f, 0x0a, 0x00 },
    ['('] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },
    [')'] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 },
    ['-'] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 },
    ['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c },
    ['/'] = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },
    [':'] = { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 },
    ['>'] = { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 },
    ['['] = { 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e },
    [']'] = { 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e },
    ['_'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f },
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
    ['G'] = { 0x0e, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f },
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

static uint32_t
min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static void
put_pixel(const struct ui_canvas *canvas, uint32_t x, uint32_t y,
          uint32_t color)
{
    if (x < canvas->width && y < canvas->height) {
        canvas->pixels[y * canvas->stride_pixels + x] = color;
    }
}

static void
fill_rect(const struct ui_canvas *canvas, uint32_t x, uint32_t y,
          uint32_t width, uint32_t height, uint32_t color)
{
    uint32_t row;
    if (x >= canvas->width || y >= canvas->height) {
        return;
    }
    width = min_u32(width, canvas->width - x);
    height = min_u32(height, canvas->height - y);
    for (row = 0U; row < height; ++row) {
        uint32_t column;
        uint32_t *pixel = &canvas->pixels[(y + row) * canvas->stride_pixels + x];
        for (column = 0U; column < width; ++column) {
            pixel[column] = color;
        }
    }
}

static void
stroke_rect(const struct ui_canvas *canvas, uint32_t x, uint32_t y,
            uint32_t width, uint32_t height, uint32_t color)
{
    if (width == 0U || height == 0U) {
        return;
    }
    fill_rect(canvas, x, y, width, 1U, color);
    if (height > 1U) {
        fill_rect(canvas, x, y + height - 1U, width, 1U, color);
    }
    fill_rect(canvas, x, y, 1U, height, color);
    if (width > 1U) {
        fill_rect(canvas, x + width - 1U, y, 1U, height, color);
    }
}

static unsigned char
normalize_character(unsigned char character)
{
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
        return (unsigned char)(character - (unsigned char)'a' +
                               (unsigned char)'A');
    }
    if (character >= 128U) {
        return (unsigned char)' ';
    }
    return character;
}

static void
draw_text(const struct ui_canvas *canvas, uint32_t x, uint32_t y,
          uint32_t max_width, const char *text, uint32_t color)
{
    uint32_t advance = 0U;
    if (text == 0) {
        return;
    }
    while (*text != '\0' && max_width - advance >= UI_TEXT_ADVANCE) {
        unsigned char character = normalize_character((unsigned char)*text++);
        uint32_t row;
        for (row = 0U; row < UI_TEXT_HEIGHT; ++row) {
            uint32_t column;
            for (column = 0U; column < 5U; ++column) {
                if ((font[character][row] & (1U << (4U - column))) != 0U) {
                    put_pixel(canvas, x + advance + column, y + row, color);
                }
            }
        }
        advance += UI_TEXT_ADVANCE;
    }
}

static void
draw_sidebar_item(const struct ui_canvas *canvas, uint32_t y,
                 const char *label, int active, uint32_t icon_kind)
{
    uint32_t color = active != 0 ? UI_COLOR_TEXT : UI_COLOR_MUTED;
    if (active != 0) {
        fill_rect(canvas, 8U, y - 6U, UI_SIDEBAR_WIDTH - 16U, 26U,
                  UI_COLOR_PANEL);
        fill_rect(canvas, 8U, y - 6U, 2U, 26U, UI_COLOR_ACCENT);
    }
    if (icon_kind == 0U) {
        stroke_rect(canvas, 19U, y, 10U, 9U, active != 0 ? UI_COLOR_ACCENT
                                                       : UI_COLOR_MUTED);
        fill_rect(canvas, 21U, y + 2U, 2U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                         : UI_COLOR_MUTED);
    } else if (icon_kind == 1U) {
        fill_rect(canvas, 19U, y + 1U, 10U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                           : UI_COLOR_MUTED);
        fill_rect(canvas, 19U, y + 5U, 7U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                          : UI_COLOR_MUTED);
        fill_rect(canvas, 19U, y + 9U, 9U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                          : UI_COLOR_MUTED);
    } else if (icon_kind == 2U) {
        stroke_rect(canvas, 19U, y, 10U, 10U, active != 0 ? UI_COLOR_ACCENT
                                                         : UI_COLOR_MUTED);
        fill_rect(canvas, 22U, y + 3U, 4U, 4U, active != 0 ? UI_COLOR_ACCENT
                                                         : UI_COLOR_MUTED);
    } else {
        fill_rect(canvas, 19U, y + 1U, 10U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                           : UI_COLOR_MUTED);
        fill_rect(canvas, 19U, y + 5U, 10U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                           : UI_COLOR_MUTED);
        fill_rect(canvas, 19U, y + 9U, 10U, 2U, active != 0 ? UI_COLOR_ACCENT
                                                           : UI_COLOR_MUTED);
    }
    draw_text(canvas, 37U, y + 2U, 89U, label, color);
}

static void
draw_cursor(const struct ui_canvas *canvas,
            const struct virtio_pointer_state *pointer)
{
    static const uint16_t cursor_rows[13] = {
        0x1000U, 0x1800U, 0x1c00U, 0x1e00U, 0x1f00U, 0x1f80U,
        0x1fc0U, 0x1fe0U, 0x1ff0U, 0x1f80U, 0x1dc0U, 0x18e0U,
        0x1060U
    };
    uint32_t x;
    uint32_t y;
    uint32_t row;
    uint32_t base_color;
    if (pointer == 0) {
        return;
    }
    x = (uint32_t)(((uint64_t)pointer->x * (canvas->width - 1U)) / 65535U);
    y = (uint32_t)(((uint64_t)pointer->y * (canvas->height - 1U)) / 65535U);
    base_color = (pointer->buttons & VIRTIO_POINTER_BUTTON_LEFT) != 0U
                     ? UI_COLOR_AMBER : UI_COLOR_TEXT;
    for (row = 0U; row < 13U; ++row) {
        uint32_t column;
        for (column = 0U; column < 13U; ++column) {
            uint32_t pixel_bit = 1U << (12U - column);
            if ((cursor_rows[row] & pixel_bit) != 0U) {
                int edge = column == 0U || column == 12U ||
                           row == 0U || row == 12U;

                if (!edge) {
                    edge = (cursor_rows[row] & (1U << (13U - column))) == 0U ||
                           (cursor_rows[row] & (1U << (11U - column))) == 0U ||
                           (cursor_rows[row - 1U] & pixel_bit) == 0U ||
                           (cursor_rows[row + 1U] & pixel_bit) == 0U;
                }
                put_pixel(canvas, x + column, y + row,
                          edge ? 0x0002070dU : base_color);
            }
        }
    }
}

static void
draw_terminal(const struct ui_canvas *canvas, uint32_t x, uint32_t y,
              uint32_t width, uint32_t height,
              const struct ui_console_state *console)
{
    static const char *default_lines[] = {
        "Jkernel boot sequence online.",
        "PL011 serial monitor ready.",
        "QEMU RAMFB framebuffer active.",
        "ZESH prompt remains on serial.",
        "Waiting for console integration..."
    };
    uint32_t content_y = y + 43U;
    uint32_t log_limit = (height > 96U) ? (height - 96U) / 17U : 0U;
    uint32_t line_count;
    uint32_t first_line;
    uint32_t index;
    const char *prompt = "ZESH> _";
    fill_rect(canvas, x, y, width, height, UI_COLOR_PANEL);
    stroke_rect(canvas, x, y, width, height, UI_COLOR_BORDER);
    fill_rect(canvas, x, y, width, 29U, UI_COLOR_PANEL_ALT);
    fill_rect(canvas, x, y + 28U, width, 1U, UI_COLOR_BORDER);
    fill_rect(canvas, x + 13U, y + 11U, 7U, 7U, UI_COLOR_GREEN);
    draw_text(canvas, x + 28U, y + 11U, width - 42U,
              "ZESH SERIAL SESSION", UI_COLOR_TEXT);
    draw_text(canvas, x + width - 86U, y + 11U, 75U, "LIVE", UI_COLOR_GREEN);
    fill_rect(canvas, x + 14U, y + 37U, 3U, height - 84U, UI_COLOR_BORDER);
    if (console != 0 && console->prompt != 0) {
        prompt = console->prompt;
    }
    if (console != 0) {
        line_count = min_u32(console->line_count, UI_MAX_LOG_LINES);
    } else {
        line_count = (uint32_t)(sizeof(default_lines) / sizeof(default_lines[0]));
    }
    if (log_limit > 8U) {
        log_limit = 8U;
    }
    if (line_count > log_limit) {
        first_line = line_count - log_limit;
        line_count = log_limit;
    } else {
        first_line = 0U;
    }
    for (index = 0U; index < line_count; ++index) {
        const char *line;
        if (console != 0) {
            line = console->lines[first_line + index];
        } else {
            line = default_lines[first_line + index];
        }
        if (line != 0) {
            draw_text(canvas, x + 25U, content_y + index * 17U,
                      width - 40U, line,
                      index == 0U ? UI_COLOR_BLUE : UI_COLOR_MUTED);
        }
    }
    fill_rect(canvas, x + 12U, y + height - 38U, width - 24U, 25U,
              UI_COLOR_PANEL_ALT);
    fill_rect(canvas, x + 12U, y + height - 38U, 2U, 25U, UI_COLOR_ACCENT);
    draw_text(canvas, x + 23U, y + height - 29U, width - 45U, prompt,
              UI_COLOR_TEXT);
}

static void
draw_status_card(const struct ui_canvas *canvas, uint32_t x, uint32_t y,
                 uint32_t width, const char *label, const char *value,
                 uint32_t accent)
{
    fill_rect(canvas, x, y, width, 66U, UI_COLOR_PANEL);
    stroke_rect(canvas, x, y, width, 66U, UI_COLOR_BORDER);
    fill_rect(canvas, x + 12U, y + 12U, 3U, 9U, accent);
    draw_text(canvas, x + 22U, y + 13U, width - 32U, label, UI_COLOR_MUTED);
    draw_text(canvas, x + 12U, y + 39U, width - 24U, value, UI_COLOR_TEXT);
}

void
ui_render(uint32_t *pixels, uint32_t width, uint32_t height,
          uint32_t stride_pixels, const struct ui_console_state *console,
          const struct virtio_pointer_state *pointer)
{
    struct ui_canvas canvas;
    uint32_t main_x = UI_MAIN_LEFT;
    uint32_t main_width;
    uint32_t terminal_y = 91U;
    uint32_t terminal_height;
    uint32_t cards_y;
    uint32_t card_gap = 8U;
    uint32_t card_width;
    uint32_t right_margin = 14U;
    uint32_t content_width;
    uint32_t footer_y;
    const char *input_status = "WAITING";

    if (pixels == 0 || width < UI_MIN_WIDTH || width > UI_MAX_WIDTH ||
        height < UI_MIN_HEIGHT || height > UI_MAX_HEIGHT ||
        stride_pixels < width || stride_pixels > UI_MAX_WIDTH) {
        return;
    }
    canvas.pixels = pixels;
    canvas.width = width;
    canvas.height = height;
    canvas.stride_pixels = stride_pixels;

    fill_rect(&canvas, 0U, 0U, width, height, UI_COLOR_BACKGROUND);
    fill_rect(&canvas, 0U, 0U, width, UI_HEADER_HEIGHT, UI_COLOR_HEADER);
    fill_rect(&canvas, 0U, UI_HEADER_HEIGHT - 1U, width, 1U, UI_COLOR_BORDER);
    fill_rect(&canvas, 12U, 7U, 21U, 21U, UI_COLOR_ACCENT);
    draw_text(&canvas, 20U, 14U, 8U, "J", UI_COLOR_BACKGROUND);
    draw_text(&canvas, 42U, 9U, 250U, "JARVIS / KERNEL CONSOLE",
              UI_COLOR_TEXT);
    fill_rect(&canvas, width - 143U, 10U, 130U, 16U, UI_COLOR_PANEL);
    stroke_rect(&canvas, width - 143U, 10U, 130U, 16U, UI_COLOR_BORDER);
    fill_rect(&canvas, width - 133U, 16U, 4U, 4U, UI_COLOR_GREEN);
    draw_text(&canvas, width - 122U, 15U, 104U, "KERNEL M3 / READY",
              UI_COLOR_MUTED);

    fill_rect(&canvas, 0U, UI_HEADER_HEIGHT, UI_SIDEBAR_WIDTH,
              height - UI_HEADER_HEIGHT, UI_COLOR_SIDEBAR);
    fill_rect(&canvas, UI_SIDEBAR_WIDTH - 1U, UI_HEADER_HEIGHT, 1U,
              height - UI_HEADER_HEIGHT, UI_COLOR_BORDER);
    draw_text(&canvas, 17U, 57U, 102U, "WORKSPACE", UI_COLOR_MUTED);
    draw_sidebar_item(&canvas, 83U, "OVERVIEW", 0, 0U);
    draw_sidebar_item(&canvas, 119U, "CONSOLE", 1, 1U);
    draw_sidebar_item(&canvas, 155U, "INPUT", 0, 2U);
    draw_sidebar_item(&canvas, 191U, "SYSTEM", 0, 3U);
    fill_rect(&canvas, 17U, height - 61U, 101U, 1U, UI_COLOR_BORDER);
    draw_text(&canvas, 17U, height - 47U, 100U, "BUILD", UI_COLOR_MUTED);
    draw_text(&canvas, 17U, height - 31U, 105U, "MILESTONE 0.3", UI_COLOR_TEXT);

    main_width = width - main_x - right_margin;
    draw_text(&canvas, main_x, 53U, main_width, "CONSOLE", UI_COLOR_TEXT);
    draw_text(&canvas, main_x, 68U, main_width,
              "INTERACTIVE KERNEL SESSION", UI_COLOR_MUTED);
    terminal_height = height - 225U;
    draw_terminal(&canvas, main_x, terminal_y, main_width, terminal_height,
                  console);

    cards_y = height - 91U;
    content_width = main_width - card_gap * 2U;
    card_width = content_width / 3U;
    if (pointer != 0) {
        input_status = (pointer->buttons & VIRTIO_POINTER_BUTTON_LEFT) != 0U
                           ? "CLICK" : "POINTER OK";
    } else if (console != 0 && console->input_status != 0) {
        input_status = console->input_status;
    }
    draw_status_card(&canvas, main_x, cards_y, card_width,
                     "DISPLAY", "640 X 480", UI_COLOR_ACCENT);
    draw_status_card(&canvas, main_x + card_width + card_gap, cards_y,
                     card_width, "SERIAL", "PL011 READY", UI_COLOR_BLUE);
    draw_status_card(&canvas, main_x + 2U * (card_width + card_gap), cards_y,
                     main_width - 2U * (card_width + card_gap), "INPUT",
                     input_status,
                     pointer != 0 ? UI_COLOR_GREEN : UI_COLOR_AMBER);

    footer_y = height - 15U;
    draw_text(&canvas, main_x, footer_y, main_width,
              "DIRECT QEMU BOOT  |  ZESH REMAINS ON SERIAL",
              UI_COLOR_MUTED);
    draw_cursor(&canvas, pointer);
}
