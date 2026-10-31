#include "fdt.h"

#include <stddef.h>

#define FDT_MAGIC              0xd00dfeedU
#define FDT_HEADER_SIZE        40U
#define FDT_VERSION_SUPPORTED  17U
#define FDT_MAX_DEPTH          64U

#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE   2U
#define FDT_PROP       3U
#define FDT_NOP        4U
#define FDT_END        9U

struct fdt_result {
    uintptr_t base;
    uint64_t size;
    uint32_t requested_occurrence;
    uint32_t match_count;
    int found;
};

struct fdt_parser {
    const unsigned char *blob;
    uint32_t cursor;
    uint32_t structure_end;
    uint32_t strings_offset;
    uint32_t strings_size;
    const char *wanted;
    size_t wanted_length;
    struct fdt_result *result;
};

struct fdt_node {
    const unsigned char *compatible;
    const unsigned char *reg;
    uint32_t compatible_length;
    uint32_t reg_length;
    uint32_t address_cells;
    uint32_t size_cells;
    int has_address_cells;
    int has_size_cells;
    int has_compatible;
    int has_reg;
    int child_started;
    int finished;
};

static uint32_t
read_be32(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint64_t
read_be64(const unsigned char *bytes)
{
    return ((uint64_t)read_be32(bytes) << 32) |
           (uint64_t)read_be32(bytes + 4U);
}

static int
range_is_valid(uint32_t offset, uint32_t length, uint32_t total_size)
{
    return offset <= total_size && length <= total_size - offset;
}

static int
ranges_overlap(uint32_t first_start, uint32_t first_end,
               uint32_t second_start, uint32_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

static size_t
c_string_length(const char *text)
{
    size_t length = 0U;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static int
string_equals(const unsigned char *text, uint32_t text_length,
              const char *expected, size_t expected_length)
{
    size_t index;

    if ((size_t)text_length != expected_length) {
        return 0;
    }
    for (index = 0U; index < expected_length; ++index) {
        if (text[index] != (unsigned char)expected[index]) {
            return 0;
        }
    }
    return 1;
}

/* Returns -1 for a malformed list, 0 for no match, and 1 for a match. */
static int
compatible_list_matches(const unsigned char *list, uint32_t list_length,
                        const char *wanted, size_t wanted_length)
{
    uint32_t offset = 0U;
    int matched = 0;

    if (list_length == 0U) {
        return -1;
    }
    while (offset < list_length) {
        uint32_t start = offset;

        while (offset < list_length && list[offset] != '\0') {
            ++offset;
        }
        if (offset == list_length || offset == start) {
            return -1;
        }
        if (string_equals(list + start, offset - start,
                          wanted, wanted_length)) {
            matched = 1;
        }
        ++offset;
    }
    return matched;
}

static int
property_name_equals(const unsigned char *name, uint32_t name_length,
                     const char *expected, size_t expected_length)
{
    return string_equals(name, name_length, expected, expected_length);
}

static int
read_address_cells(const unsigned char *value, uint32_t length,
                   uint32_t *cells)
{
    uint32_t count;

    if (length != 4U) {
        return -1;
    }
    count = read_be32(value);
    if (count != 1U && count != 2U && count != 3U) {
        return -1;
    }
    *cells = count;
    return 0;
}

static int
read_size_cells(const unsigned char *value, uint32_t length,
                uint32_t *cells)
{
    uint32_t count;

    if (length != 4U) {
        return -1;
    }
    count = read_be32(value);
    if (count > 2U) {
        return -1;
    }
    *cells = count;
    return 0;
}

static uint64_t
read_cells(const unsigned char *value, uint32_t count)
{
    if (count == 0U) {
        return 0U;
    }
    if (count == 1U) {
        return (uint64_t)read_be32(value);
    }
    return read_be64(value);
}

static int
finish_node(const struct fdt_node *node,
            uint32_t parent_address_cells, uint32_t parent_size_cells,
            struct fdt_parser *parser)
{
    uint32_t tuple_cells;
    uint32_t tuple_length;
    uint64_t decoded_base;
    int matches;

    if (!node->has_compatible) {
        return 0;
    }
    matches = compatible_list_matches(node->compatible,
                                      node->compatible_length,
                                      parser->wanted,
                                      parser->wanted_length);
    if (matches < 0) {
        return -1;
    }
    if (matches == 0 || !node->has_reg) {
        return 0;
    }

    if (parent_address_cells > 2U) {
        return -1;
    }
    tuple_cells = parent_address_cells + parent_size_cells;
    tuple_length = tuple_cells * 4U;
    if (node->reg_length < tuple_length ||
        node->reg_length % tuple_length != 0U) {
        return -1;
    }
    decoded_base = read_cells(node->reg, parent_address_cells);
    if (decoded_base > (uint64_t)UINTPTR_MAX) {
        return -1;
    }
    if (parser->result->match_count ==
        parser->result->requested_occurrence) {
        parser->result->base = (uintptr_t)decoded_base;
        parser->result->size = read_cells(
            node->reg + parent_address_cells * 4U, parent_size_cells);
        parser->result->found = 1;
    }
    ++parser->result->match_count;
    return 0;
}

/* Called with the cursor immediately after this node's BEGIN_NODE token. */
static int
parse_node(struct fdt_parser *parser, uint32_t depth,
           uint32_t parent_address_cells, uint32_t parent_size_cells)
{
    struct fdt_node node;
    uint32_t name_start;
    uint32_t name_length;
    uint32_t padding;

    if (depth >= FDT_MAX_DEPTH || parser->cursor >= parser->structure_end) {
        return -1;
    }
    node.compatible = NULL;
    node.reg = NULL;
    node.compatible_length = 0U;
    node.reg_length = 0U;
    node.address_cells = 2U;
    node.size_cells = 1U;
    node.has_address_cells = 0;
    node.has_size_cells = 0;
    node.has_compatible = 0;
    node.has_reg = 0;
    node.child_started = 0;
    node.finished = 0;

    name_start = parser->cursor;
    while (parser->cursor < parser->structure_end &&
           parser->blob[parser->cursor] != '\0') {
        ++parser->cursor;
    }
    if (parser->cursor == parser->structure_end) {
        return -1;
    }
    name_length = parser->cursor - name_start;
    ++parser->cursor;
    padding = (4U - (parser->cursor & 3U)) & 3U;
    if (padding > parser->structure_end - parser->cursor) {
        return -1;
    }
    parser->cursor += padding;
    if ((depth == 0U && name_length != 0U) ||
        (depth != 0U && name_length == 0U)) {
        return -1;
    }

    for (;;) {
        uint32_t token;

        if (parser->structure_end - parser->cursor < 4U) {
            return -1;
        }
        token = read_be32(parser->blob + parser->cursor);
        parser->cursor += 4U;

        if (token == FDT_NOP) {
            continue;
        }
        if (token == FDT_PROP) {
            uint32_t property_length;
            uint32_t name_offset;
            uint32_t property_name_length = 0U;
            uint32_t value_offset;
            const unsigned char *property_name;
            const unsigned char *value;

            if (node.child_started ||
                parser->structure_end - parser->cursor < 8U) {
                return -1;
            }
            property_length = read_be32(parser->blob + parser->cursor);
            name_offset = read_be32(parser->blob + parser->cursor + 4U);
            parser->cursor += 8U;
            if (name_offset >= parser->strings_size ||
                !range_is_valid(parser->cursor, property_length,
                                parser->structure_end)) {
                return -1;
            }
            value_offset = parser->cursor;
            value = parser->blob + value_offset;
            padding = (4U - (property_length & 3U)) & 3U;
            if (padding > parser->structure_end - parser->cursor -
                          property_length) {
                return -1;
            }
            parser->cursor += property_length + padding;

            property_name = parser->blob + parser->strings_offset +
                            name_offset;
            while (name_offset + property_name_length <
                       parser->strings_size &&
                   property_name[property_name_length] != '\0') {
                ++property_name_length;
            }
            if (name_offset + property_name_length >= parser->strings_size) {
                return -1;
            }

            if (property_name_equals(property_name, property_name_length,
                                     "#address-cells",
                                     sizeof("#address-cells") - 1U)) {
                if (node.has_address_cells ||
                    read_address_cells(value, property_length,
                                       &node.address_cells) != 0 ||
                    (depth == 0U && node.address_cells > 2U)) {
                    return -1;
                }
                node.has_address_cells = 1;
            } else if (property_name_equals(property_name,
                                            property_name_length,
                                            "#size-cells",
                                            sizeof("#size-cells") - 1U)) {
                if (node.has_size_cells ||
                    read_size_cells(value, property_length,
                                    &node.size_cells) != 0) {
                    return -1;
                }
                node.has_size_cells = 1;
            } else if (property_name_equals(property_name,
                                            property_name_length,
                                            "compatible",
                                            sizeof("compatible") - 1U)) {
                if (node.has_compatible) {
                    return -1;
                }
                node.compatible = value;
                node.compatible_length = property_length;
                node.has_compatible = 1;
            } else if (property_name_equals(property_name,
                                            property_name_length,
                                            "reg", sizeof("reg") - 1U)) {
                if (node.has_reg) {
                    return -1;
                }
                node.reg = value;
                node.reg_length = property_length;
                node.has_reg = 1;
            }
            continue;
        }

        if (token == FDT_BEGIN_NODE) {
            if (!node.child_started) {
                if (finish_node(&node, parent_address_cells,
                                parent_size_cells, parser) != 0) {
                    return -1;
                }
                node.finished = 1;
            }
            node.child_started = 1;
            if (parse_node(parser, depth + 1U,
                           node.address_cells, node.size_cells) != 0) {
                return -1;
            }
            continue;
        }

        if (token == FDT_END_NODE) {
            if (!node.finished &&
                finish_node(&node, parent_address_cells,
                            parent_size_cells, parser) != 0) {
                return -1;
            }
            return 0;
        }
        return -1;
    }
}

int
fdt_find_mmio_compatible_at(const void *dtb, const char *compatible,
                            uint32_t occurrence,
                            uintptr_t *base, uint64_t *size)
{
    const unsigned char *blob = (const unsigned char *)dtb;
    uint32_t total_size;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t reserve_offset;
    uint32_t strings_size;
    uint32_t structure_size;
    uint32_t version;
    uint32_t last_compatible_version;
    uint32_t structure_end;
    uint32_t strings_end;
    uint32_t reserve_end;
    uint32_t reserve_cursor;
    size_t wanted_length;
    struct fdt_result result;
    struct fdt_parser parser;

    result.base = 0U;
    result.size = 0U;
    result.requested_occurrence = occurrence;
    result.match_count = 0U;
    result.found = 0;

    if (blob == NULL || compatible == NULL || base == NULL || size == NULL ||
        compatible[0] == '\0') {
        return -1;
    }
    wanted_length = c_string_length(compatible);

    if (read_be32(blob) != FDT_MAGIC) {
        return -1;
    }
    total_size = read_be32(blob + 4U);
    if (total_size < FDT_HEADER_SIZE) {
        return -1;
    }
    structure_offset = read_be32(blob + 8U);
    strings_offset = read_be32(blob + 12U);
    reserve_offset = read_be32(blob + 16U);
    version = read_be32(blob + 20U);
    last_compatible_version = read_be32(blob + 24U);
    strings_size = read_be32(blob + 32U);
    structure_size = read_be32(blob + 36U);

    if (version < FDT_VERSION_SUPPORTED ||
        last_compatible_version > FDT_VERSION_SUPPORTED ||
        last_compatible_version > version || structure_size < 4U ||
        (structure_offset & 3U) != 0U || (reserve_offset & 7U) != 0U ||
        structure_offset < FDT_HEADER_SIZE ||
        strings_offset < FDT_HEADER_SIZE || reserve_offset < FDT_HEADER_SIZE ||
        (structure_size & 3U) != 0U ||
        !range_is_valid(structure_offset, structure_size, total_size) ||
        !range_is_valid(strings_offset, strings_size, total_size) ||
        reserve_offset > total_size) {
        return -1;
    }
    structure_end = structure_offset + structure_size;
    strings_end = strings_offset + strings_size;
    if (ranges_overlap(structure_offset, structure_end,
                       strings_offset, strings_end)) {
        return -1;
    }

    reserve_cursor = reserve_offset;
    for (;;) {
        uint64_t address;
        uint64_t length;

        if (reserve_cursor > total_size ||
            total_size - reserve_cursor < 16U) {
            return -1;
        }
        address = read_be64(blob + reserve_cursor);
        length = read_be64(blob + reserve_cursor + 8U);
        reserve_cursor += 16U;
        if (address == 0U && length == 0U) {
            break;
        }
    }
    reserve_end = reserve_cursor;
    if (ranges_overlap(reserve_offset, reserve_end,
                       structure_offset, structure_end) ||
        ranges_overlap(reserve_offset, reserve_end,
                       strings_offset, strings_end)) {
        return -1;
    }

    parser.blob = blob;
    parser.cursor = structure_offset;
    parser.structure_end = structure_end;
    parser.strings_offset = strings_offset;
    parser.strings_size = strings_size;
    parser.wanted = compatible;
    parser.wanted_length = wanted_length;
    parser.result = &result;

    if (structure_end - parser.cursor < 4U ||
        read_be32(blob + parser.cursor) != FDT_BEGIN_NODE) {
        return -1;
    }
    parser.cursor += 4U;
    if (parse_node(&parser, 0U, 2U, 1U) != 0 ||
        structure_end - parser.cursor != 4U ||
        read_be32(blob + parser.cursor) != FDT_END || !result.found) {
        return -1;
    }

    *base = result.base;
    *size = result.size;
    return 0;
}

int
fdt_find_mmio_compatible(const void *dtb, const char *compatible,
                         uintptr_t *base, uint64_t *size)
{
    return fdt_find_mmio_compatible_at(dtb, compatible, 0U, base, size);
}
