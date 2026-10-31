#include "fw_cfg.h"

#include <stdint.h>

#define FW_CFG_DATA_OFFSET       0x00U
#define FW_CFG_SELECTOR_OFFSET   0x08U
#define FW_CFG_DMA_OFFSET        0x10U
#define FW_CFG_MIN_MMIO_SIZE     0x18U
#define FW_CFG_SIGNATURE_KEY     0x0000U
#define FW_CFG_ID_KEY            0x0001U
#define FW_CFG_FILE_DIR_KEY      0x0019U
#define FW_CFG_FILE_COUNT_LIMIT  4096U
#define FW_CFG_FILE_ENTRY_SIZE   64U
#define FW_CFG_DMA_CTL_ERROR     0x01U
#define FW_CFG_DMA_CTL_SELECT    0x08U
#define FW_CFG_DMA_CTL_WRITE     0x10U
#define FW_CFG_DMA_TIMEOUT       10000000U

struct fw_cfg_dma_request {
    volatile uint8_t control[4];
    volatile uint8_t length[4];
    volatile uint8_t address[8];
} __attribute__((aligned(16)));

static uintptr_t fw_cfg_base;
static int fw_cfg_ready;
static struct fw_cfg_dma_request dma_request;

static uint16_t
byte_swap16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t
byte_swap32(uint32_t value)
{
    return ((value & 0x000000ffU) << 24) |
           ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) |
           ((value & 0xff000000U) >> 24);
}

static void
store_be32(volatile uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void
store_be64(volatile uint8_t *destination, uint64_t value)
{
    store_be32(destination, (uint32_t)(value >> 32));
    store_be32(destination + 4, (uint32_t)value);
}

static uint32_t
load_be32(const volatile uint8_t *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static uint32_t
load_be32_bytes(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static uint16_t
load_be16_bytes(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | (uint16_t)source[1]);
}

static void
fw_cfg_select(uint16_t selector)
{
    volatile uint16_t *register_address =
        (volatile uint16_t *)(fw_cfg_base + FW_CFG_SELECTOR_OFFSET);

    /* The selector register is a big-endian 16-bit MMIO register. */
    *register_address = byte_swap16(selector);
}

static uint8_t
fw_cfg_read_byte(void)
{
    return *(volatile uint8_t *)(fw_cfg_base + FW_CFG_DATA_OFFSET);
}

static void
fw_cfg_read_bytes(uint8_t *destination, uint32_t length)
{
    uint32_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = fw_cfg_read_byte();
    }
}

static int
fw_cfg_dma_write(uint16_t selector, const void *data, uint32_t length)
{
    volatile uint32_t *dma_high;
    volatile uint32_t *dma_low;
    uint64_t request_address;
    uint32_t control;
    uint32_t spins;

    if (data == 0 || length == 0U || fw_cfg_ready == 0) {
        return -1;
    }

    control = ((uint32_t)selector << 16) |
              FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE;
    store_be32(dma_request.control, control);
    store_be32(dma_request.length, length);
    store_be64(dma_request.address, (uint64_t)(uintptr_t)data);

    request_address = (uint64_t)(uintptr_t)&dma_request;
    dma_high = (volatile uint32_t *)(fw_cfg_base + FW_CFG_DMA_OFFSET);
    dma_low = (volatile uint32_t *)(fw_cfg_base + FW_CFG_DMA_OFFSET + 4U);

    /* Publish the big-endian descriptor before notifying the DMA device. */
    __asm__ volatile("dmb oshst" ::: "memory");
    *dma_high = byte_swap32((uint32_t)(request_address >> 32));
    __asm__ volatile("dmb oshst" ::: "memory");
    /* Writing the low half triggers the operation, per the fw_cfg spec. */
    *dma_low = byte_swap32((uint32_t)request_address);

    for (spins = 0U; spins < FW_CFG_DMA_TIMEOUT; ++spins) {
        uint32_t result;

        __asm__ volatile("dmb oshld" ::: "memory");
        result = load_be32(dma_request.control);
        if ((result & ~FW_CFG_DMA_CTL_ERROR) == 0U) {
            return (result & FW_CFG_DMA_CTL_ERROR) == 0U ? 0 : -1;
        }
    }
    return -1;
}

static int
fw_cfg_name_matches(const uint8_t entry[FW_CFG_FILE_ENTRY_SIZE],
                    const char *name)
{
    uint32_t index;

    for (index = 0U; index < 56U; ++index) {
        uint8_t entry_character = entry[8U + index];
        unsigned char name_character = (unsigned char)name[index];

        if (entry_character != (uint8_t)name_character) {
            return 0;
        }
        if (entry_character == 0U) {
            return 1;
        }
    }
    return name[56] == '\0';
}

static int
fw_cfg_find_file(const char *name, uint16_t *selector_out)
{
    uint8_t count_bytes[4];
    uint8_t entry[FW_CFG_FILE_ENTRY_SIZE];
    uint32_t count;
    uint32_t index;

    if (name == 0 || selector_out == 0) {
        return -1;
    }

    fw_cfg_select(FW_CFG_FILE_DIR_KEY);
    fw_cfg_read_bytes(count_bytes, 4U);
    count = load_be32_bytes(count_bytes);
    if (count > FW_CFG_FILE_COUNT_LIMIT) {
        return -1;
    }

    for (index = 0U; index < count; ++index) {
        fw_cfg_read_bytes(entry, FW_CFG_FILE_ENTRY_SIZE);
        if (fw_cfg_name_matches(entry, name)) {
            uint16_t selector = load_be16_bytes(entry + 4U);

            if (selector < 0x0020U) {
                return -1;
            }
            *selector_out = selector;
            return 0;
        }
    }
    return -1;
}

int
fw_cfg_init(uintptr_t mmio_base, uint64_t mmio_size)
{
    uint8_t signature[4];
    uint8_t revision_bytes[4];
    uint32_t revision;

    fw_cfg_ready = 0;
    if (mmio_base == 0U || mmio_size < FW_CFG_MIN_MMIO_SIZE ||
        mmio_base > UINTPTR_MAX - FW_CFG_MIN_MMIO_SIZE) {
        return -1;
    }

    fw_cfg_base = mmio_base;
    fw_cfg_select(FW_CFG_SIGNATURE_KEY);
    fw_cfg_read_bytes(signature, 4U);
    if (signature[0] != (uint8_t)'Q' || signature[1] != (uint8_t)'E' ||
        signature[2] != (uint8_t)'M' || signature[3] != (uint8_t)'U') {
        fw_cfg_base = 0U;
        return -1;
    }

    fw_cfg_select(FW_CFG_ID_KEY);
    fw_cfg_read_bytes(revision_bytes, 4U);
    /* FW_CFG_ID is a little-endian 32-bit feature bitmap. */
    revision = (uint32_t)revision_bytes[0] |
               ((uint32_t)revision_bytes[1] << 8) |
               ((uint32_t)revision_bytes[2] << 16) |
               ((uint32_t)revision_bytes[3] << 24);
    if ((revision & (1U << 1)) == 0U) {
        fw_cfg_base = 0U;
        return -1;
    }

    fw_cfg_ready = 1;
    return 0;
}

int
fw_cfg_write_file(const char *name, const void *data, size_t length)
{
    uint16_t selector;

    if (fw_cfg_ready == 0 || name == 0 || data == 0 || length == 0U ||
        length > 0xffffffffU) {
        return -1;
    }
    if (fw_cfg_find_file(name, &selector) != 0) {
        return -1;
    }
    return fw_cfg_dma_write(selector, data, (uint32_t)length);
}
