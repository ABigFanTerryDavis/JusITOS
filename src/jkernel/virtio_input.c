#include "virtio_input.h"

#include <stdint.h>

#include "fdt.h"

#define VIRTIO_MMIO_NODE_LIMIT       32U
#define VIRTIO_MMIO_REQUIRED_SIZE    0x200U
#define VIRTIO_MMIO_MAGIC            0x74726976U
#define VIRTIO_MMIO_VERSION_2        2U
#define VIRTIO_INPUT_DEVICE_ID       18U

#define MMIO_MAGIC_VALUE             0x000U
#define MMIO_VERSION                 0x004U
#define MMIO_DEVICE_ID               0x008U
#define MMIO_DEVICE_FEATURES         0x010U
#define MMIO_DEVICE_FEATURES_SEL     0x014U
#define MMIO_DRIVER_FEATURES         0x020U
#define MMIO_DRIVER_FEATURES_SEL     0x024U
#define MMIO_QUEUE_SEL               0x030U
#define MMIO_QUEUE_NUM_MAX           0x034U
#define MMIO_QUEUE_NUM               0x038U
#define MMIO_QUEUE_READY             0x044U
#define MMIO_QUEUE_NOTIFY            0x050U
#define MMIO_DEVICE_STATUS            0x070U
#define MMIO_QUEUE_DESC_LOW           0x080U
#define MMIO_QUEUE_DRIVER_LOW         0x090U
#define MMIO_QUEUE_DEVICE_LOW         0x0a0U
#define MMIO_CONFIG_GENERATION        0x0fcU
#define MMIO_CONFIG                   0x100U

#define VIRTIO_F_VERSION_1            32U
#define VIRTIO_STATUS_ACKNOWLEDGE     1U
#define VIRTIO_STATUS_DRIVER          2U
#define VIRTIO_STATUS_DRIVER_OK       4U
#define VIRTIO_STATUS_FEATURES_OK     8U
#define VIRTIO_STATUS_FAILED          128U

#define VIRTQ_MAX_SIZE                8U
#define VIRTQ_DESC_F_WRITE            2U
#define VIRTQ_AVAIL_F_NO_INTERRUPT    1U
#define VIRTIO_INPUT_EVENT_SIZE       8U

#define VIRTIO_INPUT_CFG_ABS_INFO     0x12U
#define VIRTIO_INPUT_CFG_EV_BITS      0x11U
#define VIRTIO_INPUT_EV_SYN           0x00U
#define VIRTIO_INPUT_EV_KEY           0x01U
#define VIRTIO_INPUT_EV_ABS           0x03U
#define VIRTIO_INPUT_SYN_REPORT       0x00U
#define VIRTIO_INPUT_ABS_X            0x00U
#define VIRTIO_INPUT_ABS_Y            0x01U
#define VIRTIO_INPUT_BTN_LEFT         0x110U
#define VIRTIO_INPUT_BTN_RIGHT        0x111U
#define VIRTIO_INPUT_BTN_MIDDLE       0x112U

#define VIRTIO_INPUT_ABS_SIZE         8U
#define VIRTIO_INPUT_RESET_TIMEOUT     1000000U
#define VIRTIO_INPUT_CONFIG_RETRIES    4U

struct virtq_desc {
    volatile uint64_t address;
    volatile uint32_t length;
    volatile uint16_t flags;
    volatile uint16_t next;
};

struct virtq_avail {
    volatile uint16_t flags;
    volatile uint16_t index;
    volatile uint16_t ring[VIRTQ_MAX_SIZE];
};

struct virtq_used_element {
    volatile uint32_t id;
    volatile uint32_t length;
};

struct virtq_used {
    volatile uint16_t flags;
    volatile uint16_t index;
    struct virtq_used_element ring[VIRTQ_MAX_SIZE];
};

static volatile struct virtq_desc event_descriptors[VIRTQ_MAX_SIZE]
    __attribute__((aligned(16)));
static volatile struct virtq_avail event_available
    __attribute__((aligned(16)));
static volatile struct virtq_used event_used
    __attribute__((aligned(16)));
static volatile uint8_t event_buffers[VIRTQ_MAX_SIZE][VIRTIO_INPUT_EVENT_SIZE]
    __attribute__((aligned(16)));

/* The status queue is initialized as required by VirtIO input, but remains
 * empty because this pointer-only slice does not send device status events. */
static volatile struct virtq_desc status_descriptors[VIRTQ_MAX_SIZE]
    __attribute__((aligned(16)));
static volatile struct virtq_avail status_available
    __attribute__((aligned(16)));
static volatile struct virtq_used status_used
    __attribute__((aligned(16)));

static uintptr_t input_mmio_base;
static uint16_t event_queue_size;
static uint16_t event_available_index;
static uint16_t event_used_index;
static int input_ready;
static int32_t abs_x_minimum;
static int32_t abs_x_maximum;
static int32_t abs_y_minimum;
static int32_t abs_y_maximum;
static int32_t pointer_raw_x;
static int32_t pointer_raw_y;
static uint32_t pointer_buttons;
static struct virtio_pointer_state pointer_state;

static uint32_t
mmio_read32(uintptr_t offset)
{
    return *(volatile uint32_t *)(input_mmio_base + offset);
}

static void
mmio_write32(uintptr_t offset, uint32_t value)
{
    *(volatile uint32_t *)(input_mmio_base + offset) = value;
}

static uint32_t
load_le32_volatile(const volatile uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint16_t
load_le16_volatile(const volatile uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static int32_t
signed_from_u32(uint32_t value)
{
    if (value <= (uint32_t)INT32_MAX) {
        return (int32_t)value;
    }
    return -1 - (int32_t)(UINT32_MAX - value);
}

static int32_t
load_i32_le_volatile(const volatile uint8_t *bytes)
{
    return signed_from_u32(load_le32_volatile(bytes));
}

static int
virtio_fail(void)
{
    uint32_t status = mmio_read32(MMIO_DEVICE_STATUS);

    mmio_write32(MMIO_DEVICE_STATUS, status | VIRTIO_STATUS_FAILED);
    input_ready = 0;
    return -1;
}

static int
read_absolute_capabilities(void)
{
    volatile uint8_t *config =
        (volatile uint8_t *)(input_mmio_base + MMIO_CONFIG);
    uint32_t attempt;

    for (attempt = 0U; attempt < VIRTIO_INPUT_CONFIG_RETRIES; ++attempt) {
        uint32_t generation_before =
            mmio_read32(MMIO_CONFIG_GENERATION);
        uint8_t size;
        uint8_t absolute_codes;
        uint32_t generation_after;

        config[0] = VIRTIO_INPUT_CFG_EV_BITS;
        config[1] = VIRTIO_INPUT_EV_ABS;
        __asm__ volatile("dmb osh" ::: "memory");
        size = config[2];
        __asm__ volatile("dmb oshld" ::: "memory");
        absolute_codes = config[8];
        __asm__ volatile("dmb oshld" ::: "memory");
        generation_after = mmio_read32(MMIO_CONFIG_GENERATION);

        if (generation_before == generation_after && size >= 1U &&
            (absolute_codes & 0x03U) == 0x03U) {
            return 0;
        }
    }
    return -1;
}

static int
read_abs_range(uint8_t axis, int32_t *minimum, int32_t *maximum)
{
    volatile uint8_t *config =
        (volatile uint8_t *)(input_mmio_base + MMIO_CONFIG);
    uint32_t attempt;

    for (attempt = 0U; attempt < VIRTIO_INPUT_CONFIG_RETRIES; ++attempt) {
        uint32_t generation_before =
            mmio_read32(MMIO_CONFIG_GENERATION);
        uint8_t size;
        int32_t found_minimum;
        int32_t found_maximum;
        uint32_t generation_after;

        config[0] = VIRTIO_INPUT_CFG_ABS_INFO;
        config[1] = axis;
        __asm__ volatile("dmb osh" ::: "memory");
        size = config[2];
        __asm__ volatile("dmb oshld" ::: "memory");
        found_minimum = signed_from_u32(mmio_read32(MMIO_CONFIG + 8U));
        found_maximum = signed_from_u32(mmio_read32(MMIO_CONFIG + 12U));
        __asm__ volatile("dmb oshld" ::: "memory");
        generation_after = mmio_read32(MMIO_CONFIG_GENERATION);

        if (generation_before == generation_after &&
            size >= VIRTIO_INPUT_ABS_SIZE &&
            found_maximum > found_minimum) {
            *minimum = found_minimum;
            *maximum = found_maximum;
            return 0;
        }
    }
    return -1;
}

static uint16_t
normalize_axis(int32_t value, int32_t minimum, int32_t maximum)
{
    uint64_t span = (uint64_t)((int64_t)maximum - (int64_t)minimum);
    uint64_t delta;

    if (value <= minimum) {
        return 0U;
    }
    if (value >= maximum) {
        return UINT16_MAX;
    }
    delta = (uint64_t)((int64_t)value - (int64_t)minimum);
    return (uint16_t)((delta * (uint64_t)UINT16_MAX) / span);
}

static uint16_t
largest_queue_size(uint32_t maximum)
{
    uint16_t size = 1U;

    while ((uint32_t)size < maximum && size < VIRTQ_MAX_SIZE) {
        size = (uint16_t)(size << 1);
    }
    if ((uint32_t)size > maximum) {
        size = (uint16_t)(size >> 1);
    }
    return size;
}

static int
configure_queue(uint16_t queue_index, uint16_t queue_size,
                volatile struct virtq_desc *descriptors,
                volatile struct virtq_avail *available,
                volatile struct virtq_used *used)
{
    uint64_t address;

    mmio_write32(MMIO_QUEUE_SEL, queue_index);
    __asm__ volatile("dmb osh" ::: "memory");
    if (mmio_read32(MMIO_QUEUE_NUM_MAX) < (uint32_t)queue_size) {
        return -1;
    }
    mmio_write32(MMIO_QUEUE_NUM, queue_size);

    address = (uint64_t)(uintptr_t)descriptors;
    mmio_write32(MMIO_QUEUE_DESC_LOW, (uint32_t)address);
    mmio_write32(MMIO_QUEUE_DESC_LOW + 4U, (uint32_t)(address >> 32));
    address = (uint64_t)(uintptr_t)available;
    mmio_write32(MMIO_QUEUE_DRIVER_LOW, (uint32_t)address);
    mmio_write32(MMIO_QUEUE_DRIVER_LOW + 4U, (uint32_t)(address >> 32));
    address = (uint64_t)(uintptr_t)used;
    mmio_write32(MMIO_QUEUE_DEVICE_LOW, (uint32_t)address);
    mmio_write32(MMIO_QUEUE_DEVICE_LOW + 4U, (uint32_t)(address >> 32));

    __asm__ volatile("dmb oshst" ::: "memory");
    mmio_write32(MMIO_QUEUE_READY, 1U);
    __asm__ volatile("dmb osh" ::: "memory");
    if (mmio_read32(MMIO_QUEUE_READY) != 1U) {
        return -1;
    }
    return 0;
}

static int
initialize_virtio_device(void)
{
    uint32_t spins;
    uint32_t offered_features;
    uint32_t status;
    uint32_t queue_zero_maximum;
    uint32_t queue_one_maximum;
    uint16_t index;

    mmio_write32(MMIO_DEVICE_STATUS, 0U);
    __asm__ volatile("dmb osh" ::: "memory");
    for (spins = 0U; spins < VIRTIO_INPUT_RESET_TIMEOUT; ++spins) {
        if (mmio_read32(MMIO_DEVICE_STATUS) == 0U) {
            break;
        }
    }
    if (spins == VIRTIO_INPUT_RESET_TIMEOUT) {
        return virtio_fail();
    }

    mmio_write32(MMIO_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    __asm__ volatile("dmb oshst" ::: "memory");
    mmio_write32(MMIO_DEVICE_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write32(MMIO_DEVICE_FEATURES_SEL, 1U);
    __asm__ volatile("dmb osh" ::: "memory");
    offered_features = mmio_read32(MMIO_DEVICE_FEATURES);
    if ((offered_features &
         (1U << (VIRTIO_F_VERSION_1 - 32U))) == 0U) {
        return virtio_fail();
    }

    /* Negotiate only VIRTIO_F_VERSION_1; all optional features stay off. */
    mmio_write32(MMIO_DRIVER_FEATURES_SEL, 0U);
    mmio_write32(MMIO_DRIVER_FEATURES, 0U);
    mmio_write32(MMIO_DRIVER_FEATURES_SEL, 1U);
    __asm__ volatile("dmb oshst" ::: "memory");
    mmio_write32(MMIO_DRIVER_FEATURES,
                 1U << (VIRTIO_F_VERSION_1 - 32U));
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
             VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(MMIO_DEVICE_STATUS, status);
    __asm__ volatile("dmb osh" ::: "memory");
    if ((mmio_read32(MMIO_DEVICE_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0U) {
        return virtio_fail();
    }

    if (read_absolute_capabilities() != 0 ||
        read_abs_range(VIRTIO_INPUT_ABS_X,
                       &abs_x_minimum, &abs_x_maximum) != 0 ||
        read_abs_range(VIRTIO_INPUT_ABS_Y,
                       &abs_y_minimum, &abs_y_maximum) != 0) {
        return virtio_fail();
    }

    mmio_write32(MMIO_QUEUE_SEL, 0U);
    __asm__ volatile("dmb osh" ::: "memory");
    queue_zero_maximum = mmio_read32(MMIO_QUEUE_NUM_MAX);
    mmio_write32(MMIO_QUEUE_SEL, 1U);
    __asm__ volatile("dmb osh" ::: "memory");
    queue_one_maximum = mmio_read32(MMIO_QUEUE_NUM_MAX);
    if (queue_zero_maximum == 0U || queue_one_maximum == 0U) {
        return virtio_fail();
    }
    event_queue_size = largest_queue_size(queue_zero_maximum);
    if (event_queue_size == 0U) {
        return virtio_fail();
    }

    event_available.flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
    event_available.index = event_queue_size;
    event_used.flags = 0U;
    event_used.index = 0U;
    status_available.flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
    status_available.index = 0U;
    status_used.flags = 0U;
    status_used.index = 0U;
    event_available_index = event_queue_size;
    event_used_index = 0U;

    for (index = 0U; index < VIRTQ_MAX_SIZE; ++index) {
        event_descriptors[index].address = 0U;
        event_descriptors[index].length = 0U;
        event_descriptors[index].flags = 0U;
        event_descriptors[index].next = 0U;
        status_descriptors[index].address = 0U;
        status_descriptors[index].length = 0U;
        status_descriptors[index].flags = 0U;
        status_descriptors[index].next = 0U;
    }
    for (index = 0U; index < event_queue_size; ++index) {
        uint8_t byte;
        uint64_t buffer_address =
            (uint64_t)(uintptr_t)&event_buffers[index][0];

        event_descriptors[index].address = buffer_address;
        event_descriptors[index].length = VIRTIO_INPUT_EVENT_SIZE;
        event_descriptors[index].flags = VIRTQ_DESC_F_WRITE;
        event_descriptors[index].next = 0U;
        event_available.ring[index] = index;
        for (byte = 0U; byte < VIRTIO_INPUT_EVENT_SIZE; ++byte) {
            event_buffers[index][byte] = 0U;
        }
    }

    if (configure_queue(0U, event_queue_size, event_descriptors,
                        &event_available, &event_used) != 0 ||
        configure_queue(1U, 1U, status_descriptors,
                        &status_available, &status_used) != 0) {
        return virtio_fail();
    }

    pointer_raw_x = (int32_t)((int64_t)abs_x_minimum +
                     (((int64_t)abs_x_maximum - abs_x_minimum) / 2));
    pointer_raw_y = (int32_t)((int64_t)abs_y_minimum +
                     (((int64_t)abs_y_maximum - abs_y_minimum) / 2));
    pointer_buttons = 0U;
    pointer_state.x = normalize_axis(pointer_raw_x,
                                     abs_x_minimum, abs_x_maximum);
    pointer_state.y = normalize_axis(pointer_raw_y,
                                     abs_y_minimum, abs_y_maximum);
    pointer_state.buttons = 0U;

    __asm__ volatile("dmb oshst" ::: "memory");
    mmio_write32(MMIO_DEVICE_STATUS, status | VIRTIO_STATUS_DRIVER_OK);
    __asm__ volatile("dmb osh" ::: "memory");
    if ((mmio_read32(MMIO_DEVICE_STATUS) & VIRTIO_STATUS_DRIVER_OK) == 0U) {
        return virtio_fail();
    }
    input_ready = 1;
    mmio_write32(MMIO_QUEUE_NOTIFY, 0U);
    return 0;
}

int
virtio_input_init(const void *dtb)
{
    uint32_t occurrence;

    input_ready = 0;
    input_mmio_base = 0U;
    if (dtb == 0) {
        return -1;
    }

    /* QEMU virt publishes 32 compatible transports; only one has device ID 18. */
    for (occurrence = 0U; occurrence < VIRTIO_MMIO_NODE_LIMIT; ++occurrence) {
        uintptr_t candidate_base;
        uint64_t candidate_size;
        uint32_t version;

        if (fdt_find_mmio_compatible_at(dtb, "virtio,mmio", occurrence,
                                        &candidate_base,
                                        &candidate_size) != 0) {
            break;
        }
        if (candidate_base == 0U ||
            candidate_size < VIRTIO_MMIO_REQUIRED_SIZE ||
            candidate_base > UINTPTR_MAX - VIRTIO_MMIO_REQUIRED_SIZE ||
            (candidate_base & 3U) != 0U) {
            continue;
        }

        input_mmio_base = candidate_base;
        if (mmio_read32(MMIO_MAGIC_VALUE) != VIRTIO_MMIO_MAGIC) {
            continue;
        }
        if (mmio_read32(MMIO_DEVICE_ID) != VIRTIO_INPUT_DEVICE_ID) {
            continue;
        }
        version = mmio_read32(MMIO_VERSION);
        if (version != VIRTIO_MMIO_VERSION_2) {
            return -1;
        }
        if (initialize_virtio_device() == 0) {
            return 0;
        }
        return -1;
    }

    input_mmio_base = 0U;
    return -1;
}

static void
handle_input_event(const volatile uint8_t *event)
{
    uint16_t type = load_le16_volatile(event);
    uint16_t code = load_le16_volatile(event + 2U);
    int32_t value = load_i32_le_volatile(event + 4U);

    if (type == VIRTIO_INPUT_EV_ABS) {
        if (code == VIRTIO_INPUT_ABS_X) {
            pointer_raw_x = value;
        } else if (code == VIRTIO_INPUT_ABS_Y) {
            pointer_raw_y = value;
        }
    } else if (type == VIRTIO_INPUT_EV_KEY) {
        uint32_t button = 0U;

        if (code == VIRTIO_INPUT_BTN_LEFT) {
            button = VIRTIO_POINTER_BUTTON_LEFT;
        } else if (code == VIRTIO_INPUT_BTN_RIGHT) {
            button = VIRTIO_POINTER_BUTTON_RIGHT;
        } else if (code == VIRTIO_INPUT_BTN_MIDDLE) {
            button = VIRTIO_POINTER_BUTTON_MIDDLE;
        }
        if (button != 0U) {
            if (value != 0) {
                pointer_buttons |= button;
            } else {
                pointer_buttons &= ~button;
            }
        }
    } else if (type == VIRTIO_INPUT_EV_SYN &&
               code == VIRTIO_INPUT_SYN_REPORT) {
        pointer_state.x = normalize_axis(pointer_raw_x,
                                         abs_x_minimum, abs_x_maximum);
        pointer_state.y = normalize_axis(pointer_raw_y,
                                         abs_y_minimum, abs_y_maximum);
        pointer_state.buttons = pointer_buttons;
    }
}

void
virtio_input_poll(void)
{
    int notify_device = 0;

    if (input_ready == 0) {
        return;
    }

    for (;;) {
        uint16_t device_index = event_used.index;
        uint16_t slot;
        uint32_t buffer_id;
        uint32_t used_length;

        __asm__ volatile("dmb oshld" ::: "memory");
        if (event_used_index == device_index) {
            break;
        }
        slot = (uint16_t)(event_used_index & (event_queue_size - 1U));
        buffer_id = event_used.ring[slot].id;
        used_length = event_used.ring[slot].length;
        ++event_used_index;

        if (buffer_id >= event_queue_size) {
            (void)virtio_fail();
            return;
        }
        if (used_length >= VIRTIO_INPUT_EVENT_SIZE) {
            handle_input_event(event_buffers[buffer_id]);
        }

        event_available.ring[event_available_index &
                             (event_queue_size - 1U)] =
            (uint16_t)buffer_id;
        __asm__ volatile("dmb oshst" ::: "memory");
        ++event_available_index;
        event_available.index = event_available_index;
        __asm__ volatile("dmb oshst" ::: "memory");
        notify_device = 1;
    }

    if (notify_device != 0) {
        mmio_write32(MMIO_QUEUE_NOTIFY, 0U);
    }
}

int
virtio_input_get_pointer(struct virtio_pointer_state *state)
{
    if (state == 0 || input_ready == 0) {
        return -1;
    }
    *state = pointer_state;
    return 0;
}
