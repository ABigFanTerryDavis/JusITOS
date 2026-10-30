#ifndef JKERNEL_ZESH_H
#define JKERNEL_ZESH_H

/*
 * Character I/O callbacks for the zesh command line.
 * read_byte returns -1 when no byte is ready, otherwise a byte in 0..255.
 */
typedef int (*zesh_read_fn)(void *ctx);
typedef void (*zesh_write_fn)(void *ctx, char ch);

/*
 * Print the prompt when needed and process all currently available input.
 * The editor retains its line between calls, so callers may poll repeatedly.
 */
void zesh_run(zesh_read_fn read_byte, zesh_write_fn write_byte, void *ctx);

#endif
