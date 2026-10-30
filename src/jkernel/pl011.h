#ifndef JKERNEL_PL011_H
#define JKERNEL_PL011_H

void pl011_init(void);
/* Returns -1 when RX is empty, otherwise a byte in the range 0..255. */
int pl011_read_byte(void *context);
void pl011_write_char(void *context, char ch);
void pl011_write_string(const char *text);

#endif
