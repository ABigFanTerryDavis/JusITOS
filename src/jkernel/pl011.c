#include "pl011.h"
#include <stddef.h>

#define PL011_BASE        0x09000000UL
#define PL011_DR          (*(volatile unsigned int *)(PL011_BASE + 0x000U))
#define PL011_FR          (*(volatile unsigned int *)(PL011_BASE + 0x018U))
#define PL011_IBRD        (*(volatile unsigned int *)(PL011_BASE + 0x024U))
#define PL011_FBRD        (*(volatile unsigned int *)(PL011_BASE + 0x028U))
#define PL011_LCR_H       (*(volatile unsigned int *)(PL011_BASE + 0x02cU))
#define PL011_CR          (*(volatile unsigned int *)(PL011_BASE + 0x030U))
#define PL011_IMSC        (*(volatile unsigned int *)(PL011_BASE + 0x038U))
#define PL011_ICR         (*(volatile unsigned int *)(PL011_BASE + 0x044U))

#define PL011_FR_RXFE     (1U << 4)
#define PL011_FR_TXFF     (1U << 5)
#define PL011_CR_UARTEN   (1U << 0)
#define PL011_CR_TXE      (1U << 8)
#define PL011_CR_RXE      (1U << 9)
#define PL011_LCR_H_FEN   (1U << 4)
#define PL011_LCR_H_WLEN8 (3U << 5)

void
pl011_init(void)
{
    /* QEMU virt's 24 MHz PL011 clock and these divisors select 115200 baud. */
    PL011_CR = 0U;
    PL011_IMSC = 0U;
    PL011_ICR = 0x7ffU;
    PL011_IBRD = 13U;
    PL011_FBRD = 1U;
    PL011_LCR_H = PL011_LCR_H_WLEN8 | PL011_LCR_H_FEN;
    PL011_CR = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

int
pl011_read_byte(void *context)
{
    (void)context;
    if ((PL011_FR & PL011_FR_RXFE) != 0U) {
        return -1;
    }
    return (int)(PL011_DR & 0xffU);
}

void
pl011_write_char(void *context, char ch)
{
    (void)context;
    if (ch == '\n') {
        while ((PL011_FR & PL011_FR_TXFF) != 0U) {
        }
        PL011_DR = (unsigned int)'\r';
    }
    while ((PL011_FR & PL011_FR_TXFF) != 0U) {
    }
    PL011_DR = (unsigned int)(unsigned char)ch;
}

void
pl011_write_string(const char *text)
{
    while (*text != '\0') {
        pl011_write_char(NULL, *text++);
    }
}
