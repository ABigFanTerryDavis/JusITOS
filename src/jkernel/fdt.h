#ifndef JKERNEL_FDT_H
#define JKERNEL_FDT_H

#include <stdint.h>

/*
 * Find the zero-based `occurrence` node whose compatible string list contains
 * `compatible` and decode its first reg tuple. The tuple width comes from the
 * node's parent #address-cells/#size-cells properties, using the FDT defaults
 * (2 and 1) when those properties are absent. Address cell counts of 1 or 2
 * and size cell counts of 0, 1, or 2 are supported.
 *
 * Returns 0 on success, or -1 for invalid arguments, a malformed/unsupported
 * blob, an unrepresentable base address, or when the requested matching reg
 * tuple does not exist. Outputs are not modified on error. The caller must
 * provide a readable FDT blob whose available bytes cover the total size
 * declared in its header.
 */
int fdt_find_mmio_compatible_at(const void *dtb, const char *compatible,
                                uint32_t occurrence,
                                uintptr_t *base, uint64_t *size);

/* Convenience wrapper for occurrence zero. */
int fdt_find_mmio_compatible(const void *dtb, const char *compatible,
                             uintptr_t *base, uint64_t *size);

#endif
