#include "zesh.h"

#define ZESH_LINE_CAPACITY 128U

struct zesh_state {
    char line[ZESH_LINE_CAPACITY];
    unsigned int length;
    int line_overflow;
    int prompt_printed;
    int skip_lf;
};

/* The API exposes no shell-state object; this module supports one shell instance. */
static struct zesh_state state;

static void
write_text(zesh_write_fn write_byte, void *ctx, const char *text)
{
    while (*text != '\0') {
        write_byte(ctx, *text++);
    }
}

static int
line_equals(const char *line, unsigned int length, const char *word)
{
    unsigned int i = 0U;

    while (i < length && word[i] != '\0' && line[i] == word[i]) {
        ++i;
    }
    return i == length && word[i] == '\0';
}

static void
run_command(zesh_write_fn write_byte, void *ctx)
{
    unsigned int first = 0U;
    unsigned int last = state.length;

    while (first < last && state.line[first] == ' ') {
        ++first;
    }
    while (last > first && state.line[last - 1U] == ' ') {
        --last;
    }

    if (line_equals(&state.line[first], last - first, "help")) {
        write_text(write_byte, ctx, "Commands: help, version\n");
    } else if (line_equals(&state.line[first], last - first, "version")) {
        write_text(write_byte, ctx, "zesh 0.1\n");
    } else if (first != last) {
        write_text(write_byte, ctx, "Unknown command\n");
    }
}

static void
finish_line(zesh_write_fn write_byte, void *ctx)
{
    write_byte(ctx, '\n');
    if (state.line_overflow) {
        write_text(write_byte, ctx, "Error: command line too long; discarded.\n");
    } else {
        run_command(write_byte, ctx);
    }
    state.length = 0U;
    state.line[0] = '\0';
    state.line_overflow = 0;
    write_text(write_byte, ctx, "zesh> ");
}

void
zesh_run(zesh_read_fn read_byte, zesh_write_fn write_byte, void *ctx)
{
    if (read_byte == 0 || write_byte == 0) {
        return;
    }

    if (!state.prompt_printed) {
        write_text(write_byte, ctx, "zesh> ");
        state.prompt_printed = 1;
    }

    for (;;) {
        int value = read_byte(ctx);
        unsigned char ch;

        if (value < 0) {
            return;
        }
        if (value > 255) {
            continue;
        }
        ch = (unsigned char)value;

        if (state.skip_lf) {
            state.skip_lf = 0;
            if (ch == '\n') {
                continue;
            }
        }

        if (ch == '\r' || ch == '\n') {
            finish_line(write_byte, ctx);
            state.skip_lf = ch == '\r';
        } else if (ch == '\b' || ch == 0x7fU) {
            if (state.length > 0U) {
                --state.length;
                state.line[state.length] = '\0';
                write_text(write_byte, ctx, "\b \b");
            }
        } else if (ch >= 0x20U && ch <= 0x7eU) {
            if (state.length + 1U < ZESH_LINE_CAPACITY) {
                state.line[state.length++] = (char)ch;
                state.line[state.length] = '\0';
                write_byte(ctx, (char)ch);
            } else {
                state.line_overflow = 1;
                write_byte(ctx, '\a');
            }
        }
    }
}
