#ifndef JKERNEL_FW_CFG_H
#define JKERNEL_FW_CFG_H

#include <stddef.h>
#include <stdint.h>

/* Initialize the QEMU fw_cfg MMIO interface discovered from the FDT. */
int fw_cfg_init(uintptr_t mmio_base, uint64_t mmio_size);

/*
 * Write a complete fw_cfg file item from identity-mapped guest RAM using DMA.
 * The data pointer must be a physical address while the MMU is off, as in the
 * current direct-boot jkernel entry path.
 */
int fw_cfg_write_file(const char *name, const void *data, size_t length);

#endif
