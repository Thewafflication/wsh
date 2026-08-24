/**
 * @file portable_core.c
 * @brief Portable ownership, Unicode, value, context, and runtime foundation.
 */

#include "wsh/core.h"

#include <stdlib.h>
#include <string.h>

/** One immutable UTF-8 string allocation. */
struct wsh_string {
    /** Allocator that owns this structure and its bytes. */
    wsh_allocator allocator;
    /** Zero-terminated storage; the terminator is outside length. */
    char *bytes;
    /** Number of immutable content bytes. */
    size_t length;
};

/** Mutable bytes that have not yet been published. */
struct wsh_string_builder {
    /** Allocator for the builder and published result. */
    wsh_allocator allocator;
    /** Copied resource ceilings. */
    wsh_limits limits;
    /** Mutable unpublished zero-terminated storage. */
    char *bytes;
    /** Number of content bytes. */
    size_t length;
    /** Number of allocated bytes. */
    size_t capacity;
};

/** One immutable, necessarily flat list. */
struct wsh_value {
    /** Allocator that owns the list array. */
    wsh_allocator allocator;
    /** Owned immutable string elements. */
    wsh_string **items;
    /** Number of elements in items. */
    size_t count;
};

/** Mutable flat-list construction state. */
struct wsh_value_builder {
    /** Allocator for the builder and published result. */
    wsh_allocator allocator;
    /** Copied resource ceilings. */
    wsh_limits limits;
    /** Owned unpublished string elements. */
    wsh_string **items;
    /** Number of initialized elements. */
    size_t count;
    /** Number of allocated element slots. */
    size_t capacity;
};

/** One immutable status sequence. */
struct wsh_status_list {
    /** Allocator that owns the status array. */
    wsh_allocator allocator;
    /** Owned immutable status elements. */
    uint32_t *items;
    /** Number of status elements. */
    size_t count;
};

/** Mutable status construction state. */
struct wsh_status_builder {
    /** Allocator for the builder and published result. */
    wsh_allocator allocator;
    /** Copied resource ceilings. */
    wsh_limits limits;
    /** Owned unpublished status elements. */
    uint32_t *items;
    /** Number of initialized elements. */
    size_t count;
    /** Number of allocated element slots. */
    size_t capacity;
};

/** Map entry for one normalized Unicode-scalar boundary. */
typedef struct wsh_source_map_entry {
    /** Public position at this scalar boundary. */
    wsh_source_position position;
} wsh_source_map_entry;

/** Decoded source with a map at every scalar boundary. */
struct wsh_source {
    /** Allocator that owns every source allocation. */
    wsh_allocator allocator;
    /** Leading source BOM classification. */
    wsh_bom_kind bom;
    /** Normalized immutable UTF-8 text. */
    char *text;
    /** Number of normalized content bytes. */
    size_t length;
    /** Number of Unicode scalars in text. */
    size_t scalar_count;
    /** Number of logical lines. */
    size_t line_count;
    /** One position for every scalar boundary including end. */
    wsh_source_map_entry *map;
};

/** One context-owned variable. */
typedef struct wsh_variable_entry {
    /** Owned exact case-sensitive name. */
    wsh_string *name;
    /** Owned immutable flat value. */
    wsh_value *value;
    /** Nonzero when exported at the environment boundary. */
    int exported;
} wsh_variable_entry;

/** One context-owned diagnostic. */
typedef struct wsh_diagnostic_entry {
    /** Controlled diagnostic severity. */
    wsh_diagnostic_severity severity;
    /** Stable portable-core code. */
    wsh_diagnostic_code code;
    /** Owned validated message. */
    wsh_string *message;
    /** Optional owned validated source name. */
    wsh_string *source_name;
    /** Nonzero when span is meaningful. */
    int has_span;
    /** Copied source span. */
    wsh_source_span span;
} wsh_diagnostic_entry;

/** All mutable shell-core state owned by one host context. */
struct wsh_context {
    /** Allocator for every context-owned object. */
    wsh_allocator allocator;
    /** Copied resource ceilings. */
    wsh_limits limits;
    /** Copied abstract runtime callbacks. */
    wsh_runtime runtime;
    /** Owned variable array. */
    wsh_variable_entry *variables;
    /** Number of initialized variables. */
    size_t variable_count;
    /** Number of allocated variable slots. */
    size_t variable_capacity;
    /** Owned diagnostic array. */
    wsh_diagnostic_entry *diagnostics;
    /** Number of initialized diagnostics. */
    size_t diagnostic_count;
    /** Number of allocated diagnostic slots. */
    size_t diagnostic_capacity;
    /** Number of runtime calls attempted. */
    size_t runtime_calls;
};

/** One ordered deterministic fake-runtime response. */
typedef struct wsh_fake_expectation {
    /** Expected operation category. */
    wsh_runtime_operation operation;
    /** Owned expected subject. */
    wsh_string *subject;
    /** Optional owned exact expected arguments. */
    wsh_value *arguments;
    /** Nonzero when arguments must compare exactly. */
    int compare_arguments;
    /** Owned scripted output. */
    wsh_value *output;
    /** Owned scripted statuses. */
    wsh_status_list *status;
    /** Scripted callback result. */
    wsh_result result;
} wsh_fake_expectation;

/** Scripted side-effect-free runtime implementation. */
struct wsh_fake_runtime {
    /** Allocator for the fake and its expectation graph. */
    wsh_allocator allocator;
    /** Copied resource ceilings. */
    wsh_limits limits;
    /** Ordered owned expectation array. */
    wsh_fake_expectation *expectations;
    /** Number of initialized expectations. */
    size_t expectation_count;
    /** Number of allocated expectation slots. */
    size_t expectation_capacity;
    /** Index of the next expected request. */
    size_t next_expectation;
    /** Number of requests observed, including mismatches. */
    size_t call_count;
};

/** Allocate with the C runtime for the default allocator. */
static void *default_allocate(void *user_data, size_t size)
{
    (void)user_data;
    return malloc(size);
}

/** Release with the C runtime for the default allocator. */
static void default_deallocate(void *user_data, void *pointer)
{
    (void)user_data;
    free(pointer);
}

/** Copy a valid allocator or select the default allocator. */
static wsh_result normalize_allocator(
    const wsh_allocator *input,
    wsh_allocator *output)
{
    if (output == NULL) {
        return WSH_ERR_INVALID;
    }
    if (input == NULL) {
        *output = wsh_allocator_default();
        return WSH_OK;
    }
    if (input->allocate == NULL || input->deallocate == NULL) {
        return WSH_ERR_INVALID;
    }
    *output = *input;
    return WSH_OK;
}

/** Copy supplied limits or select default limits. */
static wsh_result normalize_limits(
    const wsh_limits *input,
    wsh_limits *output)
{
    if (output == NULL) {
        return WSH_ERR_INVALID;
    }
    *output = input == NULL ? wsh_limits_default() : *input;
    if (output->max_source_bytes == 0U ||
        output->max_string_bytes == 0U ||
        output->max_list_items == 0U ||
        output->max_variables == 0U ||
        output->max_diagnostics == 0U ||
        output->max_runtime_expectations == 0U ||
        output->max_runtime_calls == 0U ||
        output->max_source_bytes == (size_t)-1 ||
        output->max_string_bytes == (size_t)-1 ||
        output->max_list_items == (size_t)-1 ||
        output->max_variables == (size_t)-1 ||
        output->max_diagnostics == (size_t)-1 ||
        output->max_runtime_expectations == (size_t)-1 ||
        output->max_runtime_calls == (size_t)-1) {
        return WSH_ERR_INVALID;
    }
    return WSH_OK;
}

/** Allocate at least one byte so zero-sized owners stay deterministic. */
static void *allocate_block(
    const wsh_allocator *allocator,
    size_t size)
{
    if (allocator == NULL || allocator->allocate == NULL) {
        return NULL;
    }
    return allocator->allocate(
        allocator->user_data,
        size == 0U ? 1U : size);
}

/** Return whether adding two sizes is representable. */
static int checked_add(size_t left, size_t right, size_t *output)
{
    if (output == NULL || right > (size_t)-1 - left) {
        return 0;
    }
    *output = left + right;
    return 1;
}

/** Return whether multiplying two sizes is representable. */
static int checked_multiply(size_t left, size_t right, size_t *output)
{
    if (output == NULL) {
        return 0;
    }
    if (left != 0U && right > (size_t)-1 / left) {
        return 0;
    }
    *output = left * right;
    return 1;
}

/**
 * Grow an array by allocate-copy-commit rather than relying on realloc.
 * Existing bytes and capacity remain unchanged on failure.
 */
static wsh_result grow_array(
    const wsh_allocator *allocator,
    void **array,
    size_t element_size,
    size_t used_count,
    size_t *capacity,
    size_t required,
    size_t maximum)
{
    size_t new_capacity;
    size_t old_bytes;
    size_t new_bytes;
    void *replacement;

    if (allocator == NULL || array == NULL || capacity == NULL ||
        element_size == 0U || required > maximum) {
        return required > maximum ? WSH_ERR_RESOURCE : WSH_ERR_INVALID;
    }
    if (required <= *capacity) {
        return WSH_OK;
    }
    new_capacity = *capacity == 0U ? 4U : *capacity;
    if (new_capacity > maximum) {
        new_capacity = maximum;
    }
    while (new_capacity < required) {
        if (new_capacity > maximum / 2U) {
            new_capacity = maximum;
        } else {
            new_capacity *= 2U;
        }
    }
    if (!checked_multiply(new_capacity, element_size, &new_bytes) ||
        !checked_multiply(used_count, element_size, &old_bytes)) {
        return WSH_ERR_RESOURCE;
    }
    replacement = allocate_block(allocator, new_bytes);
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (*array != NULL && old_bytes != 0U) {
        memcpy(replacement, *array, old_bytes);
    }
    allocator->deallocate(allocator->user_data, *array);
    *array = replacement;
    *capacity = new_capacity;
    return WSH_OK;
}

/** Return whether a scalar is forbidden by the WSH text contract. */
static int scalar_is_forbidden(uint32_t scalar)
{
    if (scalar == 0U || scalar > 0x10FFFFU) {
        return 1;
    }
    if (scalar >= 0xD800U && scalar <= 0xDFFFU) {
        return 1;
    }
    if (scalar >= 0xFDD0U && scalar <= 0xFDEFU) {
        return 1;
    }
    return (scalar & 0xFFFFU) == 0xFFFEU ||
        (scalar & 0xFFFFU) == 0xFFFFU;
}

/** Return the UTF-8 width for one valid scalar. */
static size_t utf8_width(uint32_t scalar)
{
    if (scalar <= 0x7FU) {
        return 1U;
    }
    if (scalar <= 0x7FFU) {
        return 2U;
    }
    if (scalar <= 0xFFFFU) {
        return 3U;
    }
    return 4U;
}

/** Encode one valid scalar into a caller-provided four-byte region. */
static size_t encode_utf8(uint32_t scalar, char *output)
{
    if (scalar <= 0x7FU) {
        output[0] = (char)scalar;
        return 1U;
    }
    if (scalar <= 0x7FFU) {
        output[0] = (char)(0xC0U | (scalar >> 6U));
        output[1] = (char)(0x80U | (scalar & 0x3FU));
        return 2U;
    }
    if (scalar <= 0xFFFFU) {
        output[0] = (char)(0xE0U | (scalar >> 12U));
        output[1] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
        output[2] = (char)(0x80U | (scalar & 0x3FU));
        return 3U;
    }
    output[0] = (char)(0xF0U | (scalar >> 18U));
    output[1] = (char)(0x80U | ((scalar >> 12U) & 0x3FU));
    output[2] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
    output[3] = (char)(0x80U | (scalar & 0x3FU));
    return 4U;
}

/** Decode one strict UTF-8 scalar and advance offset on success. */
static wsh_result decode_utf8_scalar(
    const unsigned char *bytes,
    size_t length,
    size_t *offset,
    uint32_t *out_scalar)
{
    size_t index;
    unsigned char first;
    uint32_t scalar;
    size_t width;
    size_t cursor;

    if (bytes == NULL || offset == NULL || out_scalar == NULL ||
        *offset >= length) {
        return WSH_ERR_INVALID;
    }
    index = *offset;
    first = bytes[index];
    if (first <= 0x7FU) {
        scalar = first;
        width = 1U;
    } else if (first >= 0xC2U && first <= 0xDFU) {
        scalar = (uint32_t)(first & 0x1FU);
        width = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        scalar = (uint32_t)(first & 0x0FU);
        width = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        scalar = (uint32_t)(first & 0x07U);
        width = 4U;
    } else {
        return WSH_ERR_ENCODING;
    }
    if (width > length - index) {
        return WSH_ERR_ENCODING;
    }
    for (cursor = 1U; cursor < width; ++cursor) {
        unsigned char next = bytes[index + cursor];
        if ((next & 0xC0U) != 0x80U) {
            return WSH_ERR_ENCODING;
        }
        scalar = (scalar << 6U) | (uint32_t)(next & 0x3FU);
    }
    if ((width == 2U && scalar < 0x80U) ||
        (width == 3U && scalar < 0x800U) ||
        (width == 4U && scalar < 0x10000U) ||
        scalar_is_forbidden(scalar)) {
        return WSH_ERR_ENCODING;
    }
    *offset = index + width;
    *out_scalar = scalar;
    return WSH_OK;
}

/** Read one endian-selected UTF-16 unit from raw bytes. */
static uint16_t read_utf16_unit(
    const unsigned char *bytes,
    size_t offset,
    int big_endian)
{
    if (big_endian) {
        return (uint16_t)(((uint16_t)bytes[offset] << 8U) |
            (uint16_t)bytes[offset + 1U]);
    }
    return (uint16_t)((uint16_t)bytes[offset] |
        ((uint16_t)bytes[offset + 1U] << 8U));
}

/** Decode one strict UTF-16 scalar from raw endian-selected bytes. */
static wsh_result decode_utf16_bytes_scalar(
    const unsigned char *bytes,
    size_t length,
    size_t *offset,
    int big_endian,
    uint32_t *out_scalar)
{
    uint16_t first;
    uint16_t second;
    uint32_t scalar;

    if (bytes == NULL || offset == NULL || out_scalar == NULL ||
        *offset > length || length - *offset < 2U) {
        return WSH_ERR_ENCODING;
    }
    first = read_utf16_unit(bytes, *offset, big_endian);
    *offset += 2U;
    if (first >= 0xD800U && first <= 0xDBFFU) {
        if (length - *offset < 2U) {
            return WSH_ERR_ENCODING;
        }
        second = read_utf16_unit(bytes, *offset, big_endian);
        if (second < 0xDC00U || second > 0xDFFFU) {
            return WSH_ERR_ENCODING;
        }
        *offset += 2U;
        scalar = 0x10000U +
            (((uint32_t)first - 0xD800U) << 10U) +
            ((uint32_t)second - 0xDC00U);
    } else {
        scalar = first;
    }
    if (scalar_is_forbidden(scalar)) {
        return WSH_ERR_ENCODING;
    }
    *out_scalar = scalar;
    return WSH_OK;
}

/** Decode one raw source scalar according to its detected encoding. */
static wsh_result decode_source_scalar(
    const unsigned char *bytes,
    size_t length,
    wsh_bom_kind bom,
    size_t *offset,
    uint32_t *out_scalar)
{
    if (bom == WSH_BOM_UTF16_LE || bom == WSH_BOM_UTF16_BE) {
        return decode_utf16_bytes_scalar(
            bytes,
            length,
            offset,
            bom == WSH_BOM_UTF16_BE,
            out_scalar);
    }
    return decode_utf8_scalar(
        bytes,
        length,
        offset,
        out_scalar);
}

/** Return the first content byte after a recognized BOM. */
static size_t source_content_offset(wsh_bom_kind bom)
{
    if (bom == WSH_BOM_UTF8) {
        return 3U;
    }
    if (bom == WSH_BOM_UTF16_LE || bom == WSH_BOM_UTF16_BE) {
        return 2U;
    }
    return 0U;
}

/** Decode one scalar and collapse CRLF into one normalized LF. */
static wsh_result decode_normalized_source_scalar(
    const unsigned char *bytes,
    size_t length,
    wsh_bom_kind bom,
    size_t *offset,
    size_t *out_original_offset,
    uint32_t *out_scalar)
{
    size_t original;
    size_t lookahead;
    uint32_t scalar;
    uint32_t next;
    wsh_result result;

    if (offset == NULL || out_original_offset == NULL ||
        out_scalar == NULL) {
        return WSH_ERR_INVALID;
    }
    original = *offset;
    result = decode_source_scalar(bytes, length, bom, offset, &scalar);
    if (result != WSH_OK) {
        return result;
    }
    if (scalar == 0x0DU) {
        scalar = 0x0AU;
        lookahead = *offset;
        if (lookahead < length) {
            result = decode_source_scalar(
                bytes,
                length,
                bom,
                &lookahead,
                &next);
            if (result != WSH_OK) {
                return result;
            }
            if (next == 0x0AU) {
                *offset = lookahead;
            }
        }
    }
    *out_original_offset = original;
    *out_scalar = scalar;
    return WSH_OK;
}

/** Reserve a byte region for a mutable string builder. */
static wsh_result reserve_string_bytes(
    wsh_string_builder *builder,
    size_t required)
{
    return grow_array(
        &builder->allocator,
        (void **)&builder->bytes,
        sizeof(*builder->bytes),
        builder->length,
        &builder->capacity,
        required,
        builder->limits.max_string_bytes + 1U);
}

/** Create an immutable empty or copied value through its builder. */
static wsh_result clone_value_internal(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const wsh_value *value,
    wsh_value **out_value)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_result result;

    if (out_value == NULL || value == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_value = NULL;
    result = wsh_value_builder_create(allocator, limits, &builder);
    if (result != WSH_OK) {
        return result;
    }
    for (index = 0U; index < value->count; ++index) {
        result = wsh_value_builder_append(
            builder,
            wsh_string_bytes(value->items[index]));
        if (result != WSH_OK) {
            wsh_value_builder_destroy(builder);
            return result;
        }
    }
    result = wsh_value_builder_finish(builder, out_value);
    wsh_value_builder_destroy(builder);
    return result;
}

/** Deep-copy an immutable status list. */
static wsh_result clone_status_internal(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const wsh_status_list *status,
    wsh_status_list **out_status)
{
    wsh_status_builder *builder;
    size_t index;
    wsh_result result;

    if (status == NULL || out_status == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_status = NULL;
    result = wsh_status_builder_create(allocator, limits, &builder);
    if (result != WSH_OK) {
        return result;
    }
    for (index = 0U; index < status->count; ++index) {
        result = wsh_status_builder_append(builder, status->items[index]);
        if (result != WSH_OK) {
            wsh_status_builder_destroy(builder);
            return result;
        }
    }
    result = wsh_status_builder_finish(builder, out_status);
    wsh_status_builder_destroy(builder);
    return result;
}

/** Find an exact variable name, returning count when absent. */
static size_t find_variable(
    const wsh_context *context,
    wsh_string_view name)
{
    size_t index;

    for (index = 0U; index < context->variable_count; ++index) {
        if (wsh_string_view_equal(
                wsh_string_bytes(context->variables[index].name),
                name)) {
            return index;
        }
    }
    return context->variable_count;
}

/** ASCII-only deterministic fallback for exported-name comparison. */
static int ascii_names_equal(
    wsh_string_view left,
    wsh_string_view right)
{
    size_t index;

    if (left.length != right.length) {
        return 0;
    }
    for (index = 0U; index < left.length; ++index) {
        unsigned char left_byte = (unsigned char)left.data[index];
        unsigned char right_byte = (unsigned char)right.data[index];
        if (left_byte >= (unsigned char)'A' &&
            left_byte <= (unsigned char)'Z') {
            left_byte = (unsigned char)(left_byte + ('a' - 'A'));
        }
        if (right_byte >= (unsigned char)'A' &&
            right_byte <= (unsigned char)'Z') {
            right_byte = (unsigned char)(right_byte + ('a' - 'A'));
        }
        if (left_byte != right_byte) {
            return 0;
        }
    }
    return 1;
}

/** Compare names through the runtime or the portable ASCII fallback. */
static int context_names_equal(
    const wsh_context *context,
    wsh_string_view left,
    wsh_string_view right)
{
    if (context->runtime.names_equal != NULL) {
        return context->runtime.names_equal(
            context->runtime.user_data,
            left,
            right);
    }
    return ascii_names_equal(left, right);
}

/** Check whether exporting a variable would collide with another export. */
static int export_collides(
    const wsh_context *context,
    size_t own_index,
    wsh_string_view name)
{
    size_t index;

    for (index = 0U; index < context->variable_count; ++index) {
        if (index != own_index && context->variables[index].exported &&
            context_names_equal(
                context,
                wsh_string_bytes(context->variables[index].name),
                name)) {
            return 1;
        }
    }
    return 0;
}

/** Set or import a variable using one allocate-then-commit path. */
static wsh_result set_variable_internal(
    wsh_context *context,
    wsh_string_view name,
    const wsh_value *value,
    int imported)
{
    size_t index;
    wsh_string *new_name;
    wsh_value *new_value;
    wsh_result result;

    if (context == NULL || value == NULL || name.length == 0U) {
        return WSH_ERR_INVALID;
    }
    result = wsh_utf8_validate(name, NULL);
    if (result != WSH_OK) {
        return result;
    }
    if (name.length > context->limits.max_string_bytes) {
        return WSH_ERR_RESOURCE;
    }
    index = find_variable(context, name);
    if (imported && export_collides(context, index, name)) {
        return WSH_ERR_MISMATCH;
    }
    result = clone_value_internal(
        &context->allocator,
        &context->limits,
        value,
        &new_value);
    if (result != WSH_OK) {
        return result;
    }
    if (index < context->variable_count) {
        wsh_value_destroy(context->variables[index].value);
        context->variables[index].value = new_value;
        if (imported) {
            context->variables[index].exported = 1;
        }
        return WSH_OK;
    }
    if (context->variable_count >= context->limits.max_variables) {
        wsh_value_destroy(new_value);
        return WSH_ERR_RESOURCE;
    }
    result = wsh_string_create(
        &context->allocator,
        &context->limits,
        name,
        &new_name);
    if (result != WSH_OK) {
        wsh_value_destroy(new_value);
        return result;
    }
    result = grow_array(
        &context->allocator,
        (void **)&context->variables,
        sizeof(*context->variables),
        context->variable_count,
        &context->variable_capacity,
        context->variable_count + 1U,
        context->limits.max_variables);
    if (result != WSH_OK) {
        wsh_string_destroy(new_name);
        wsh_value_destroy(new_value);
        return result;
    }
    context->variables[context->variable_count].name = new_name;
    context->variables[context->variable_count].value = new_value;
    context->variables[context->variable_count].exported = imported ? 1 : 0;
    context->variable_count += 1U;
    return WSH_OK;
}

/** Destroy fields owned by one diagnostic entry. */
static void destroy_diagnostic_entry(wsh_diagnostic_entry *entry)
{
    if (entry == NULL) {
        return;
    }
    wsh_string_destroy(entry->message);
    wsh_string_destroy(entry->source_name);
    memset(entry, 0, sizeof(*entry));
}

/** Append every element in one value to a mutable output builder. */
static wsh_result append_value_to_builder(
    wsh_value_builder *builder,
    const wsh_value *value)
{
    size_t index;
    wsh_result result;

    for (index = 0U; index < value->count; ++index) {
        result = wsh_value_builder_append(
            builder,
            wsh_string_bytes(value->items[index]));
        if (result != WSH_OK) {
            return result;
        }
    }
    return WSH_OK;
}

/** Append every status in one immutable list to a builder. */
static wsh_result append_status_to_builder(
    wsh_status_builder *builder,
    const wsh_status_list *status)
{
    size_t index;
    wsh_result result;

    for (index = 0U; index < status->count; ++index) {
        result = wsh_status_builder_append(builder, status->items[index]);
        if (result != WSH_OK) {
            return result;
        }
    }
    return WSH_OK;
}

/** Compare two flat values byte-for-byte in order. */
static int values_equal(const wsh_value *left, const wsh_value *right)
{
    size_t index;

    if (left == NULL || right == NULL || left->count != right->count) {
        return 0;
    }
    for (index = 0U; index < left->count; ++index) {
        if (!wsh_string_view_equal(
                wsh_string_bytes(left->items[index]),
                wsh_string_bytes(right->items[index]))) {
            return 0;
        }
    }
    return 1;
}

/** Runtime callback used by the deterministic fake. */
static wsh_result fake_runtime_invoke(
    void *user_data,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    wsh_fake_runtime *fake = (wsh_fake_runtime *)user_data;
    wsh_fake_expectation *expectation;
    wsh_result result;

    if (fake == NULL || request == NULL || output == NULL || status == NULL) {
        return WSH_ERR_INVALID;
    }
    fake->call_count += 1U;
    if (fake->next_expectation >= fake->expectation_count) {
        return WSH_ERR_MISMATCH;
    }
    expectation = &fake->expectations[fake->next_expectation];
    if (expectation->operation != request->operation ||
        !wsh_string_view_equal(
            wsh_string_bytes(expectation->subject),
            request->subject) ||
        (expectation->compare_arguments &&
         !values_equal(expectation->arguments, request->arguments))) {
        return WSH_ERR_MISMATCH;
    }
    if (expectation->result != WSH_OK) {
        fake->next_expectation += 1U;
        return expectation->result;
    }
    result = append_value_to_builder(output, expectation->output);
    if (result != WSH_OK) {
        return result;
    }
    result = append_status_to_builder(status, expectation->status);
    if (result == WSH_OK) {
        fake->next_expectation += 1U;
    }
    return result;
}

/** Name comparator used by the deterministic fake. */
static int fake_runtime_names_equal(
    void *user_data,
    wsh_string_view left,
    wsh_string_view right)
{
    (void)user_data;
    return ascii_names_equal(left, right);
}

/** @brief Implements wsh_allocator_default. */
wsh_allocator wsh_allocator_default(void)
{
    wsh_allocator allocator;

    allocator.user_data = NULL;
    allocator.allocate = default_allocate;
    allocator.deallocate = default_deallocate;
    return allocator;
}

/** @brief Implements wsh_allocator_release. */
void wsh_allocator_release(
    const wsh_allocator *allocator,
    void *pointer)
{
    wsh_allocator normalized;

    if (pointer == NULL ||
        normalize_allocator(allocator, &normalized) != WSH_OK) {
        return;
    }
    normalized.deallocate(normalized.user_data, pointer);
}

/** @brief Implements wsh_limits_default. */
wsh_limits wsh_limits_default(void)
{
    wsh_limits limits;

    limits.max_source_bytes = 16U * 1024U * 1024U;
    limits.max_string_bytes = 1024U * 1024U;
    limits.max_list_items = 65536U;
    limits.max_variables = 4096U;
    limits.max_diagnostics = 1024U;
    limits.max_runtime_expectations = 4096U;
    limits.max_runtime_calls = 4096U;
    return limits;
}

/** @brief Implements wsh_string_view_from_cstr. */
wsh_string_view wsh_string_view_from_cstr(const char *text)
{
    wsh_string_view view;

    view.data = text;
    view.length = text == NULL ? 0U : strlen(text);
    return view;
}

/** @brief Implements wsh_string_view_equal. */
int wsh_string_view_equal(
    wsh_string_view left,
    wsh_string_view right)
{
    if (left.length != right.length) {
        return 0;
    }
    if (left.length == 0U) {
        return 1;
    }
    if (left.data == NULL || right.data == NULL) {
        return 0;
    }
    return memcmp(left.data, right.data, left.length) == 0;
}

/** @brief Implements wsh_utf8_validate. */
wsh_result wsh_utf8_validate(
    wsh_string_view text,
    size_t *out_scalar_count)
{
    size_t offset = 0U;
    size_t count = 0U;
    uint32_t scalar;
    wsh_result result;

    if (text.data == NULL && text.length != 0U) {
        return WSH_ERR_INVALID;
    }
    while (offset < text.length) {
        result = decode_utf8_scalar(
            (const unsigned char *)text.data,
            text.length,
            &offset,
            &scalar);
        if (result != WSH_OK) {
            return result;
        }
        count += 1U;
    }
    if (out_scalar_count != NULL) {
        *out_scalar_count = count;
    }
    return WSH_OK;
}

/** @brief Implements wsh_utf8_to_utf16. */
wsh_result wsh_utf8_to_utf16(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_string_view input,
    uint16_t **out_units,
    size_t *out_length)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    size_t offset;
    size_t unit_count;
    size_t allocation_count;
    size_t allocation_bytes;
    uint16_t *units;
    size_t output_index;
    uint32_t scalar;
    wsh_result result;

    if (out_units == NULL || out_length == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_units = NULL;
    *out_length = 0U;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    if (input.length > actual_limits.max_string_bytes ||
        (input.data == NULL && input.length != 0U)) {
        return input.length > actual_limits.max_string_bytes ?
            WSH_ERR_RESOURCE : WSH_ERR_INVALID;
    }
    offset = 0U;
    unit_count = 0U;
    while (offset < input.length) {
        result = decode_utf8_scalar(
            (const unsigned char *)input.data,
            input.length,
            &offset,
            &scalar);
        if (result != WSH_OK) {
            return result;
        }
        if (!checked_add(
                unit_count,
                scalar >= 0x10000U ? 2U : 1U,
                &unit_count)) {
            return WSH_ERR_RESOURCE;
        }
    }
    if (!checked_add(unit_count, 1U, &allocation_count) ||
        !checked_multiply(
            allocation_count,
            sizeof(*units),
            &allocation_bytes)) {
        return WSH_ERR_RESOURCE;
    }
    units = (uint16_t *)allocate_block(
        &actual_allocator,
        allocation_bytes);
    if (units == NULL) {
        return WSH_ERR_RESOURCE;
    }
    offset = 0U;
    output_index = 0U;
    while (offset < input.length) {
        result = decode_utf8_scalar(
            (const unsigned char *)input.data,
            input.length,
            &offset,
            &scalar);
        if (result != WSH_OK) {
            actual_allocator.deallocate(actual_allocator.user_data, units);
            return result;
        }
        if (scalar < 0x10000U) {
            units[output_index++] = (uint16_t)scalar;
        } else {
            uint32_t pair = scalar - 0x10000U;
            units[output_index++] =
                (uint16_t)(0xD800U | (pair >> 10U));
            units[output_index++] =
                (uint16_t)(0xDC00U | (pair & 0x3FFU));
        }
    }
    units[output_index] = 0U;
    *out_units = units;
    *out_length = output_index;
    return WSH_OK;
}

/** Decode one strict scalar from native-endian UTF-16 units. */
static wsh_result decode_utf16_units_scalar(
    const uint16_t *units,
    size_t length,
    size_t *offset,
    uint32_t *out_scalar)
{
    uint16_t first;
    uint16_t second;
    uint32_t scalar;

    if (units == NULL || offset == NULL || out_scalar == NULL ||
        *offset >= length) {
        return WSH_ERR_INVALID;
    }
    first = units[(*offset)++];
    if (first >= 0xD800U && first <= 0xDBFFU) {
        if (*offset >= length) {
            return WSH_ERR_ENCODING;
        }
        second = units[(*offset)++];
        if (second < 0xDC00U || second > 0xDFFFU) {
            return WSH_ERR_ENCODING;
        }
        scalar = 0x10000U +
            (((uint32_t)first - 0xD800U) << 10U) +
            ((uint32_t)second - 0xDC00U);
    } else {
        scalar = first;
    }
    if (scalar_is_forbidden(scalar)) {
        return WSH_ERR_ENCODING;
    }
    *out_scalar = scalar;
    return WSH_OK;
}

/** @brief Implements wsh_utf16_to_utf8. */
wsh_result wsh_utf16_to_utf8(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const uint16_t *units,
    size_t length,
    char **out_bytes,
    size_t *out_length)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    size_t offset;
    size_t byte_count;
    size_t allocation_size;
    size_t output_index;
    char *bytes;
    uint32_t scalar;
    wsh_result result;

    if (out_bytes == NULL || out_length == NULL ||
        (units == NULL && length != 0U)) {
        return WSH_ERR_INVALID;
    }
    *out_bytes = NULL;
    *out_length = 0U;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    if (length > actual_limits.max_string_bytes) {
        return WSH_ERR_RESOURCE;
    }
    offset = 0U;
    byte_count = 0U;
    while (offset < length) {
        result = decode_utf16_units_scalar(
            units,
            length,
            &offset,
            &scalar);
        if (result != WSH_OK) {
            return result;
        }
        if (!checked_add(byte_count, utf8_width(scalar), &byte_count) ||
            byte_count > actual_limits.max_string_bytes) {
            return WSH_ERR_RESOURCE;
        }
    }
    if (!checked_add(byte_count, 1U, &allocation_size)) {
        return WSH_ERR_RESOURCE;
    }
    bytes = (char *)allocate_block(&actual_allocator, allocation_size);
    if (bytes == NULL) {
        return WSH_ERR_RESOURCE;
    }
    offset = 0U;
    output_index = 0U;
    while (offset < length) {
        result = decode_utf16_units_scalar(
            units,
            length,
            &offset,
            &scalar);
        if (result != WSH_OK) {
            actual_allocator.deallocate(actual_allocator.user_data, bytes);
            return result;
        }
        output_index += encode_utf8(scalar, bytes + output_index);
    }
    bytes[output_index] = '\0';
    *out_bytes = bytes;
    *out_length = output_index;
    return WSH_OK;
}

/** @brief Implements wsh_source_detect_bom. */
wsh_bom_kind wsh_source_detect_bom(
    const unsigned char *bytes,
    size_t length)
{
    if (bytes == NULL) {
        return WSH_BOM_NONE;
    }
    if (length >= 3U && bytes[0] == 0xEFU &&
        bytes[1] == 0xBBU && bytes[2] == 0xBFU) {
        return WSH_BOM_UTF8;
    }
    if (length >= 2U && bytes[0] == 0xFFU && bytes[1] == 0xFEU) {
        return WSH_BOM_UTF16_LE;
    }
    if (length >= 2U && bytes[0] == 0xFEU && bytes[1] == 0xFFU) {
        return WSH_BOM_UTF16_BE;
    }
    return WSH_BOM_NONE;
}

/** @brief Implements wsh_source_create. */
wsh_result wsh_source_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const unsigned char *bytes,
    size_t length,
    wsh_source **out_source)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    wsh_bom_kind bom;
    size_t content_offset;
    size_t offset;
    size_t original_offset;
    size_t scalar_count;
    size_t text_length;
    size_t line_count;
    size_t allocation_size;
    uint32_t scalar;
    wsh_result result;
    wsh_source *source;
    size_t map_count;
    size_t map_bytes;
    size_t text_offset;
    size_t scalar_index;
    size_t line;
    size_t scalar_column;
    size_t display_column;

    if (out_source == NULL || (bytes == NULL && length != 0U)) {
        return WSH_ERR_INVALID;
    }
    *out_source = NULL;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    if (length > actual_limits.max_source_bytes) {
        return WSH_ERR_RESOURCE;
    }
    bom = wsh_source_detect_bom(bytes, length);
    content_offset = source_content_offset(bom);
    if ((bom == WSH_BOM_UTF16_LE || bom == WSH_BOM_UTF16_BE) &&
        ((length - content_offset) & 1U) != 0U) {
        return WSH_ERR_ENCODING;
    }
    offset = content_offset;
    scalar_count = 0U;
    text_length = 0U;
    line_count = 1U;
    while (offset < length) {
        result = decode_normalized_source_scalar(
            bytes,
            length,
            bom,
            &offset,
            &original_offset,
            &scalar);
        if (result != WSH_OK) {
            return result;
        }
        if (!checked_add(scalar_count, 1U, &scalar_count) ||
            !checked_add(text_length, utf8_width(scalar), &text_length) ||
            text_length > actual_limits.max_source_bytes) {
            return WSH_ERR_RESOURCE;
        }
        if (scalar == 0x0AU) {
            line_count += 1U;
        }
    }
    if (!checked_add(text_length, 1U, &allocation_size) ||
        !checked_add(scalar_count, 1U, &map_count) ||
        !checked_multiply(
            map_count,
            sizeof(*source->map),
            &map_bytes)) {
        return WSH_ERR_RESOURCE;
    }
    source = (wsh_source *)allocate_block(
        &actual_allocator,
        sizeof(*source));
    if (source == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(source, 0, sizeof(*source));
    source->allocator = actual_allocator;
    source->text = (char *)allocate_block(
        &actual_allocator,
        allocation_size);
    if (source->text == NULL) {
        wsh_source_destroy(source);
        return WSH_ERR_RESOURCE;
    }
    source->map = (wsh_source_map_entry *)allocate_block(
        &actual_allocator,
        map_bytes);
    if (source->map == NULL) {
        wsh_source_destroy(source);
        return WSH_ERR_RESOURCE;
    }
    offset = content_offset;
    text_offset = 0U;
    scalar_index = 0U;
    line = 1U;
    scalar_column = 1U;
    display_column = 1U;
    while (offset < length) {
        result = decode_normalized_source_scalar(
            bytes,
            length,
            bom,
            &offset,
            &original_offset,
            &scalar);
        if (result != WSH_OK) {
            wsh_source_destroy(source);
            return result;
        }
        source->map[scalar_index].position.original_byte_offset =
            original_offset;
        source->map[scalar_index].position.utf8_byte_offset = text_offset;
        source->map[scalar_index].position.scalar_offset = scalar_index;
        source->map[scalar_index].position.line = line;
        source->map[scalar_index].position.scalar_column = scalar_column;
        source->map[scalar_index].position.display_column = display_column;
        text_offset += encode_utf8(scalar, source->text + text_offset);
        scalar_index += 1U;
        if (scalar == 0x0AU) {
            line += 1U;
            scalar_column = 1U;
            display_column = 1U;
        } else {
            scalar_column += 1U;
            if (scalar == 0x09U) {
                display_column =
                    (((display_column - 1U) / 8U) + 1U) * 8U + 1U;
            } else {
                display_column += 1U;
            }
        }
    }
    source->text[text_offset] = '\0';
    source->map[scalar_index].position.original_byte_offset = length;
    source->map[scalar_index].position.utf8_byte_offset = text_offset;
    source->map[scalar_index].position.scalar_offset = scalar_index;
    source->map[scalar_index].position.line = line;
    source->map[scalar_index].position.scalar_column = scalar_column;
    source->map[scalar_index].position.display_column = display_column;
    source->bom = bom;
    source->length = text_offset;
    source->scalar_count = scalar_index;
    source->line_count = line_count;
    *out_source = source;
    return WSH_OK;
}

/** @brief Implements wsh_source_destroy. */
void wsh_source_destroy(wsh_source *source)
{
    if (source == NULL) {
        return;
    }
    source->allocator.deallocate(
        source->allocator.user_data,
        source->text);
    source->allocator.deallocate(
        source->allocator.user_data,
        source->map);
    source->allocator.deallocate(
        source->allocator.user_data,
        source);
}

/** @brief Implements wsh_source_text. */
wsh_string_view wsh_source_text(const wsh_source *source)
{
    wsh_string_view view;

    view.data = source == NULL ? NULL : source->text;
    view.length = source == NULL ? 0U : source->length;
    return view;
}

/** @brief Implements wsh_source_bom. */
wsh_bom_kind wsh_source_bom(const wsh_source *source)
{
    return source == NULL ? WSH_BOM_NONE : source->bom;
}

/** @brief Implements wsh_source_scalar_count. */
size_t wsh_source_scalar_count(const wsh_source *source)
{
    return source == NULL ? 0U : source->scalar_count;
}

/** @brief Implements wsh_source_line_count. */
size_t wsh_source_line_count(const wsh_source *source)
{
    return source == NULL ? 0U : source->line_count;
}

/** Find a scalar boundary at one normalized UTF-8 offset. */
static size_t find_source_boundary(
    const wsh_source *source,
    size_t utf8_offset)
{
    size_t low = 0U;
    size_t high = source->scalar_count + 1U;

    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        size_t candidate =
            source->map[middle].position.utf8_byte_offset;
        if (candidate < utf8_offset) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low <= source->scalar_count &&
        source->map[low].position.utf8_byte_offset == utf8_offset) {
        return low;
    }
    return source->scalar_count + 1U;
}

/** @brief Implements wsh_source_get_span. */
wsh_result wsh_source_get_span(
    const wsh_source *source,
    size_t utf8_offset,
    size_t utf8_length,
    wsh_source_span *out_span)
{
    size_t end_offset;
    size_t start_index;
    size_t end_index;

    if (source == NULL || out_span == NULL ||
        !checked_add(utf8_offset, utf8_length, &end_offset) ||
        end_offset > source->length) {
        return WSH_ERR_INVALID;
    }
    start_index = find_source_boundary(source, utf8_offset);
    end_index = find_source_boundary(source, end_offset);
    if (start_index > source->scalar_count ||
        end_index > source->scalar_count) {
        return WSH_ERR_INVALID;
    }
    out_span->start = source->map[start_index].position;
    out_span->end = source->map[end_index].position;
    return WSH_OK;
}

/** @brief Implements wsh_string_create. */
wsh_result wsh_string_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_string_view text,
    wsh_string **out_string)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    wsh_string *string;
    size_t allocation_size;
    wsh_result result;

    if (out_string == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_string = NULL;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    if (text.length > actual_limits.max_string_bytes) {
        return WSH_ERR_RESOURCE;
    }
    result = wsh_utf8_validate(text, NULL);
    if (result != WSH_OK) {
        return result;
    }
    if (!checked_add(text.length, 1U, &allocation_size)) {
        return WSH_ERR_RESOURCE;
    }
    string = (wsh_string *)allocate_block(
        &actual_allocator,
        sizeof(*string));
    if (string == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(string, 0, sizeof(*string));
    string->allocator = actual_allocator;
    string->bytes = (char *)allocate_block(
        &actual_allocator,
        allocation_size);
    if (string->bytes == NULL) {
        wsh_string_destroy(string);
        return WSH_ERR_RESOURCE;
    }
    if (text.length != 0U) {
        memcpy(string->bytes, text.data, text.length);
    }
    string->bytes[text.length] = '\0';
    string->length = text.length;
    *out_string = string;
    return WSH_OK;
}

/** @brief Implements wsh_string_destroy. */
void wsh_string_destroy(wsh_string *string)
{
    if (string == NULL) {
        return;
    }
    string->allocator.deallocate(
        string->allocator.user_data,
        string->bytes);
    string->allocator.deallocate(
        string->allocator.user_data,
        string);
}

/** @brief Implements wsh_string_bytes. */
wsh_string_view wsh_string_bytes(const wsh_string *string)
{
    wsh_string_view view;

    view.data = string == NULL ? NULL : string->bytes;
    view.length = string == NULL ? 0U : string->length;
    return view;
}

/** @brief Implements wsh_string_builder_create. */
wsh_result wsh_string_builder_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_string_builder **out_builder)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    wsh_string_builder *builder;
    wsh_result result;

    if (out_builder == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_builder = NULL;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    builder = (wsh_string_builder *)allocate_block(
        &actual_allocator,
        sizeof(*builder));
    if (builder == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(builder, 0, sizeof(*builder));
    builder->allocator = actual_allocator;
    builder->limits = actual_limits;
    *out_builder = builder;
    return WSH_OK;
}

/** @brief Implements wsh_string_builder_append. */
wsh_result wsh_string_builder_append(
    wsh_string_builder *builder,
    wsh_string_view text)
{
    size_t new_length;
    size_t required;
    wsh_result result;

    if (builder == NULL) {
        return WSH_ERR_INVALID;
    }
    result = wsh_utf8_validate(text, NULL);
    if (result != WSH_OK) {
        return result;
    }
    if (!checked_add(builder->length, text.length, &new_length) ||
        new_length > builder->limits.max_string_bytes ||
        !checked_add(new_length, 1U, &required)) {
        return WSH_ERR_RESOURCE;
    }
    result = reserve_string_bytes(builder, required);
    if (result != WSH_OK) {
        return result;
    }
    if (text.length != 0U) {
        memcpy(builder->bytes + builder->length, text.data, text.length);
    }
    builder->length = new_length;
    builder->bytes[builder->length] = '\0';
    return WSH_OK;
}

/** @brief Implements wsh_string_builder_finish. */
wsh_result wsh_string_builder_finish(
    wsh_string_builder *builder,
    wsh_string **out_string)
{
    wsh_string *string;
    char *empty_bytes;

    if (builder == NULL || out_string == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_string = NULL;
    string = (wsh_string *)allocate_block(
        &builder->allocator,
        sizeof(*string));
    if (string == NULL) {
        return WSH_ERR_RESOURCE;
    }
    empty_bytes = NULL;
    if (builder->bytes == NULL) {
        empty_bytes = (char *)allocate_block(&builder->allocator, 1U);
        if (empty_bytes == NULL) {
            builder->allocator.deallocate(
                builder->allocator.user_data,
                string);
            return WSH_ERR_RESOURCE;
        }
        empty_bytes[0] = '\0';
    }
    string->allocator = builder->allocator;
    string->bytes = builder->bytes == NULL ? empty_bytes : builder->bytes;
    string->length = builder->length;
    builder->bytes = NULL;
    builder->length = 0U;
    builder->capacity = 0U;
    *out_string = string;
    return WSH_OK;
}

/** @brief Implements wsh_string_builder_destroy. */
void wsh_string_builder_destroy(wsh_string_builder *builder)
{
    if (builder == NULL) {
        return;
    }
    builder->allocator.deallocate(
        builder->allocator.user_data,
        builder->bytes);
    builder->allocator.deallocate(
        builder->allocator.user_data,
        builder);
}

/** @brief Implements wsh_value_builder_create. */
wsh_result wsh_value_builder_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_value_builder **out_builder)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    wsh_value_builder *builder;
    wsh_result result;

    if (out_builder == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_builder = NULL;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    builder = (wsh_value_builder *)allocate_block(
        &actual_allocator,
        sizeof(*builder));
    if (builder == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(builder, 0, sizeof(*builder));
    builder->allocator = actual_allocator;
    builder->limits = actual_limits;
    *out_builder = builder;
    return WSH_OK;
}

/** @brief Implements wsh_value_builder_append. */
wsh_result wsh_value_builder_append(
    wsh_value_builder *builder,
    wsh_string_view text)
{
    wsh_string *item;
    wsh_result result;

    if (builder == NULL) {
        return WSH_ERR_INVALID;
    }
    if (builder->count >= builder->limits.max_list_items) {
        return WSH_ERR_RESOURCE;
    }
    result = wsh_string_create(
        &builder->allocator,
        &builder->limits,
        text,
        &item);
    if (result != WSH_OK) {
        return result;
    }
    result = grow_array(
        &builder->allocator,
        (void **)&builder->items,
        sizeof(*builder->items),
        builder->count,
        &builder->capacity,
        builder->count + 1U,
        builder->limits.max_list_items);
    if (result != WSH_OK) {
        wsh_string_destroy(item);
        return result;
    }
    builder->items[builder->count++] = item;
    return WSH_OK;
}

/** @brief Implements wsh_value_builder_finish. */
wsh_result wsh_value_builder_finish(
    wsh_value_builder *builder,
    wsh_value **out_value)
{
    wsh_value *value;

    if (builder == NULL || out_value == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_value = NULL;
    value = (wsh_value *)allocate_block(
        &builder->allocator,
        sizeof(*value));
    if (value == NULL) {
        return WSH_ERR_RESOURCE;
    }
    value->allocator = builder->allocator;
    value->items = builder->items;
    value->count = builder->count;
    builder->items = NULL;
    builder->count = 0U;
    builder->capacity = 0U;
    *out_value = value;
    return WSH_OK;
}

/** @brief Implements wsh_value_builder_destroy. */
void wsh_value_builder_destroy(wsh_value_builder *builder)
{
    size_t index;

    if (builder == NULL) {
        return;
    }
    for (index = 0U; index < builder->count; ++index) {
        wsh_string_destroy(builder->items[index]);
    }
    builder->allocator.deallocate(
        builder->allocator.user_data,
        builder->items);
    builder->allocator.deallocate(
        builder->allocator.user_data,
        builder);
}

/** @brief Implements wsh_value_clone. */
wsh_result wsh_value_clone(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const wsh_value *value,
    wsh_value **out_value)
{
    return clone_value_internal(allocator, limits, value, out_value);
}

/** @brief Implements wsh_value_destroy. */
void wsh_value_destroy(wsh_value *value)
{
    size_t index;

    if (value == NULL) {
        return;
    }
    for (index = 0U; index < value->count; ++index) {
        wsh_string_destroy(value->items[index]);
    }
    value->allocator.deallocate(
        value->allocator.user_data,
        value->items);
    value->allocator.deallocate(
        value->allocator.user_data,
        value);
}

/** @brief Implements wsh_value_count. */
size_t wsh_value_count(const wsh_value *value)
{
    return value == NULL ? 0U : value->count;
}

/** @brief Implements wsh_value_at. */
wsh_result wsh_value_at(
    const wsh_value *value,
    size_t index,
    wsh_string_view *out_text)
{
    if (value == NULL || out_text == NULL || index >= value->count) {
        return WSH_ERR_INVALID;
    }
    *out_text = wsh_string_bytes(value->items[index]);
    return WSH_OK;
}

/** @brief Implements wsh_status_builder_create. */
wsh_result wsh_status_builder_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_status_builder **out_builder)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    wsh_status_builder *builder;
    wsh_result result;

    if (out_builder == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_builder = NULL;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    builder = (wsh_status_builder *)allocate_block(
        &actual_allocator,
        sizeof(*builder));
    if (builder == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(builder, 0, sizeof(*builder));
    builder->allocator = actual_allocator;
    builder->limits = actual_limits;
    *out_builder = builder;
    return WSH_OK;
}

/** @brief Implements wsh_status_builder_append. */
wsh_result wsh_status_builder_append(
    wsh_status_builder *builder,
    uint32_t status)
{
    wsh_result result;

    if (builder == NULL) {
        return WSH_ERR_INVALID;
    }
    if (builder->count >= builder->limits.max_list_items) {
        return WSH_ERR_RESOURCE;
    }
    result = grow_array(
        &builder->allocator,
        (void **)&builder->items,
        sizeof(*builder->items),
        builder->count,
        &builder->capacity,
        builder->count + 1U,
        builder->limits.max_list_items);
    if (result != WSH_OK) {
        return result;
    }
    builder->items[builder->count++] = status;
    return WSH_OK;
}

/** @brief Implements wsh_status_builder_finish. */
wsh_result wsh_status_builder_finish(
    wsh_status_builder *builder,
    wsh_status_list **out_status)
{
    wsh_status_list *status;

    if (builder == NULL || out_status == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_status = NULL;
    status = (wsh_status_list *)allocate_block(
        &builder->allocator,
        sizeof(*status));
    if (status == NULL) {
        return WSH_ERR_RESOURCE;
    }
    status->allocator = builder->allocator;
    status->items = builder->items;
    status->count = builder->count;
    builder->items = NULL;
    builder->count = 0U;
    builder->capacity = 0U;
    *out_status = status;
    return WSH_OK;
}

/** @brief Implements wsh_status_builder_destroy. */
void wsh_status_builder_destroy(wsh_status_builder *builder)
{
    if (builder == NULL) {
        return;
    }
    builder->allocator.deallocate(
        builder->allocator.user_data,
        builder->items);
    builder->allocator.deallocate(
        builder->allocator.user_data,
        builder);
}

/** @brief Implements wsh_status_list_destroy. */
void wsh_status_list_destroy(wsh_status_list *status)
{
    if (status == NULL) {
        return;
    }
    status->allocator.deallocate(
        status->allocator.user_data,
        status->items);
    status->allocator.deallocate(
        status->allocator.user_data,
        status);
}

/** @brief Implements wsh_status_list_count. */
size_t wsh_status_list_count(const wsh_status_list *status)
{
    return status == NULL ? 0U : status->count;
}

/** @brief Implements wsh_status_list_at. */
wsh_result wsh_status_list_at(
    const wsh_status_list *status,
    size_t index,
    uint32_t *out_value)
{
    if (status == NULL || out_value == NULL || index >= status->count) {
        return WSH_ERR_INVALID;
    }
    *out_value = status->items[index];
    return WSH_OK;
}

/** @brief Implements wsh_status_list_is_success. */
int wsh_status_list_is_success(const wsh_status_list *status)
{
    size_t index;

    if (status == NULL || status->count == 0U) {
        return 0;
    }
    for (index = 0U; index < status->count; ++index) {
        if (status->items[index] != 0U) {
            return 0;
        }
    }
    return 1;
}

/** @brief Implements wsh_status_list_last. */
wsh_result wsh_status_list_last(
    const wsh_status_list *status,
    uint32_t *out_value)
{
    if (status == NULL || out_value == NULL || status->count == 0U) {
        return WSH_ERR_INVALID;
    }
    *out_value = status->items[status->count - 1U];
    return WSH_OK;
}

/** @brief Implements wsh_context_options_init. */
void wsh_context_options_init(wsh_context_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    memset(out_options, 0, sizeof(*out_options));
    out_options->allocator = wsh_allocator_default();
    out_options->limits = wsh_limits_default();
}

/** @brief Implements wsh_context_create. */
wsh_result wsh_context_create(
    const wsh_context_options *options,
    wsh_context **out_context)
{
    wsh_context_options defaults;
    wsh_allocator allocator;
    wsh_limits limits;
    wsh_context *context;
    wsh_result result;

    if (out_context == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_context = NULL;
    if (options == NULL) {
        wsh_context_options_init(&defaults);
        options = &defaults;
    }
    result = normalize_allocator(&options->allocator, &allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(&options->limits, &limits);
    if (result != WSH_OK) {
        return result;
    }
    context = (wsh_context *)allocate_block(&allocator, sizeof(*context));
    if (context == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(context, 0, sizeof(*context));
    context->allocator = allocator;
    context->limits = limits;
    context->runtime = options->runtime;
    *out_context = context;
    return WSH_OK;
}

/** @brief Implements wsh_context_destroy. */
void wsh_context_destroy(wsh_context *context)
{
    size_t index;

    if (context == NULL) {
        return;
    }
    for (index = 0U; index < context->variable_count; ++index) {
        wsh_string_destroy(context->variables[index].name);
        wsh_value_destroy(context->variables[index].value);
    }
    for (index = 0U; index < context->diagnostic_count; ++index) {
        destroy_diagnostic_entry(&context->diagnostics[index]);
    }
    context->allocator.deallocate(
        context->allocator.user_data,
        context->variables);
    context->allocator.deallocate(
        context->allocator.user_data,
        context->diagnostics);
    context->allocator.deallocate(
        context->allocator.user_data,
        context);
}

/** @brief Implements wsh_context_set_variable. */
wsh_result wsh_context_set_variable(
    wsh_context *context,
    wsh_string_view name,
    const wsh_value *value)
{
    return set_variable_internal(context, name, value, 0);
}

/** @brief Implements wsh_context_import_variable. */
wsh_result wsh_context_import_variable(
    wsh_context *context,
    wsh_string_view name,
    const wsh_value *value)
{
    return set_variable_internal(context, name, value, 1);
}

/** @brief Implements wsh_context_get_variable. */
wsh_result wsh_context_get_variable(
    const wsh_context *context,
    wsh_string_view name,
    const wsh_value **out_value)
{
    size_t index;

    if (context == NULL || out_value == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_value = NULL;
    index = find_variable(context, name);
    if (index >= context->variable_count) {
        return WSH_ERR_INVALID;
    }
    *out_value = context->variables[index].value;
    return WSH_OK;
}

/** @brief Implements wsh_context_unset_variable. */
wsh_result wsh_context_unset_variable(
    wsh_context *context,
    wsh_string_view name)
{
    size_t index;

    if (context == NULL) {
        return WSH_ERR_INVALID;
    }
    index = find_variable(context, name);
    if (index >= context->variable_count) {
        return WSH_ERR_INVALID;
    }
    wsh_string_destroy(context->variables[index].name);
    wsh_value_destroy(context->variables[index].value);
    if (index + 1U < context->variable_count) {
        memmove(
            &context->variables[index],
            &context->variables[index + 1U],
            (context->variable_count - index - 1U) *
                sizeof(*context->variables));
    }
    context->variable_count -= 1U;
    return WSH_OK;
}

/** @brief Implements wsh_context_set_exported. */
wsh_result wsh_context_set_exported(
    wsh_context *context,
    wsh_string_view name,
    int exported)
{
    size_t index;

    if (context == NULL) {
        return WSH_ERR_INVALID;
    }
    index = find_variable(context, name);
    if (index >= context->variable_count) {
        return WSH_ERR_INVALID;
    }
    if (exported && export_collides(context, index, name)) {
        return WSH_ERR_MISMATCH;
    }
    context->variables[index].exported = exported ? 1 : 0;
    return WSH_OK;
}

/** @brief Implements wsh_context_is_exported. */
wsh_result wsh_context_is_exported(
    const wsh_context *context,
    wsh_string_view name,
    int *out_exported)
{
    size_t index;

    if (context == NULL || out_exported == NULL) {
        return WSH_ERR_INVALID;
    }
    index = find_variable(context, name);
    if (index >= context->variable_count) {
        return WSH_ERR_INVALID;
    }
    *out_exported = context->variables[index].exported;
    return WSH_OK;
}

/** @brief Implements wsh_context_variable_count. */
size_t wsh_context_variable_count(const wsh_context *context)
{
    return context == NULL ? 0U : context->variable_count;
}

/** @brief Implements wsh_context_variable_at. */
wsh_result wsh_context_variable_at(
    const wsh_context *context,
    size_t index,
    wsh_string_view *out_name,
    const wsh_value **out_value,
    int *out_exported)
{
    if (context == NULL || out_name == NULL || out_value == NULL ||
        out_exported == NULL || index >= context->variable_count) {
        return WSH_ERR_INVALID;
    }
    *out_name = wsh_string_bytes(context->variables[index].name);
    *out_value = context->variables[index].value;
    *out_exported = context->variables[index].exported;
    return WSH_OK;
}

/** @brief Implements wsh_context_get_options. */
wsh_result wsh_context_get_options(
    const wsh_context *context,
    wsh_context_options *out_options)
{
    if (context == NULL || out_options == NULL) {
        return WSH_ERR_INVALID;
    }
    out_options->allocator = context->allocator;
    out_options->limits = context->limits;
    out_options->runtime = context->runtime;
    return WSH_OK;
}

/** @brief Implements wsh_context_add_diagnostic. */
wsh_result wsh_context_add_diagnostic(
    wsh_context *context,
    wsh_diagnostic_severity severity,
    wsh_diagnostic_code code,
    wsh_string_view message,
    wsh_string_view source_name,
    const wsh_source_span *span)
{
    wsh_string *message_copy;
    wsh_string *source_copy;
    wsh_result result;
    wsh_diagnostic_entry *entry;

    if (context == NULL || severity < WSH_DIAGNOSTIC_NOTE ||
        severity > WSH_DIAGNOSTIC_ERROR) {
        return WSH_ERR_INVALID;
    }
    if (context->diagnostic_count >= context->limits.max_diagnostics) {
        return WSH_ERR_RESOURCE;
    }
    result = wsh_string_create(
        &context->allocator,
        &context->limits,
        message,
        &message_copy);
    if (result != WSH_OK) {
        return result;
    }
    source_copy = NULL;
    if (source_name.length != 0U) {
        result = wsh_string_create(
            &context->allocator,
            &context->limits,
            source_name,
            &source_copy);
        if (result != WSH_OK) {
            wsh_string_destroy(message_copy);
            return result;
        }
    }
    result = grow_array(
        &context->allocator,
        (void **)&context->diagnostics,
        sizeof(*context->diagnostics),
        context->diagnostic_count,
        &context->diagnostic_capacity,
        context->diagnostic_count + 1U,
        context->limits.max_diagnostics);
    if (result != WSH_OK) {
        wsh_string_destroy(message_copy);
        wsh_string_destroy(source_copy);
        return result;
    }
    entry = &context->diagnostics[context->diagnostic_count++];
    memset(entry, 0, sizeof(*entry));
    entry->severity = severity;
    entry->code = code;
    entry->message = message_copy;
    entry->source_name = source_copy;
    entry->has_span = span == NULL ? 0 : 1;
    if (span != NULL) {
        entry->span = *span;
    }
    return WSH_OK;
}

/** @brief Implements wsh_context_diagnostic_count. */
size_t wsh_context_diagnostic_count(const wsh_context *context)
{
    return context == NULL ? 0U : context->diagnostic_count;
}

/** @brief Implements wsh_context_diagnostic_at. */
wsh_result wsh_context_diagnostic_at(
    const wsh_context *context,
    size_t index,
    wsh_diagnostic_view *out_view)
{
    const wsh_diagnostic_entry *entry;

    if (context == NULL || out_view == NULL ||
        index >= context->diagnostic_count) {
        return WSH_ERR_INVALID;
    }
    entry = &context->diagnostics[index];
    out_view->severity = entry->severity;
    out_view->code = entry->code;
    out_view->message = wsh_string_bytes(entry->message);
    out_view->source_name = wsh_string_bytes(entry->source_name);
    out_view->has_span = entry->has_span;
    out_view->span = entry->span;
    return WSH_OK;
}

/** @brief Implements wsh_context_runtime_invoke. */
wsh_result wsh_context_runtime_invoke(
    wsh_context *context,
    const wsh_runtime_request *request,
    wsh_value **out_value,
    wsh_status_list **out_status)
{
    wsh_value_builder *value_builder;
    wsh_status_builder *status_builder;
    wsh_value *value = NULL;
    wsh_status_list *status = NULL;
    wsh_result result;

    if (context == NULL || request == NULL || out_value == NULL ||
        out_status == NULL || context->runtime.invoke == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_value = NULL;
    *out_status = NULL;
    if (context->runtime_calls >= context->limits.max_runtime_calls) {
        return WSH_ERR_RESOURCE;
    }
    if (request->operation < WSH_RUNTIME_READ_SOURCE ||
        request->operation > WSH_RUNTIME_LIBRARY) {
        return WSH_ERR_INVALID;
    }
    result = wsh_utf8_validate(request->subject, NULL);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_value_builder_create(
        &context->allocator,
        &context->limits,
        &value_builder);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_status_builder_create(
        &context->allocator,
        &context->limits,
        &status_builder);
    if (result != WSH_OK) {
        wsh_value_builder_destroy(value_builder);
        return result;
    }
    context->runtime_calls += 1U;
    result = context->runtime.invoke(
        context->runtime.user_data,
        request,
        value_builder,
        status_builder);
    if (result != WSH_OK) {
        wsh_value_builder_destroy(value_builder);
        wsh_status_builder_destroy(status_builder);
        return result;
    }
    result = wsh_value_builder_finish(value_builder, &value);
    if (result == WSH_OK) {
        result = wsh_status_builder_finish(status_builder, &status);
        if (result != WSH_OK) {
            wsh_value_destroy(value);
        }
    }
    wsh_value_builder_destroy(value_builder);
    wsh_status_builder_destroy(status_builder);
    if (result != WSH_OK) {
        return result;
    }
    *out_value = value;
    *out_status = status;
    return WSH_OK;
}

/** @brief Implements wsh_fake_runtime_create. */
wsh_result wsh_fake_runtime_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_fake_runtime **out_fake)
{
    wsh_allocator actual_allocator;
    wsh_limits actual_limits;
    wsh_fake_runtime *fake;
    wsh_result result;

    if (out_fake == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_fake = NULL;
    result = normalize_allocator(allocator, &actual_allocator);
    if (result != WSH_OK) {
        return result;
    }
    result = normalize_limits(limits, &actual_limits);
    if (result != WSH_OK) {
        return result;
    }
    fake = (wsh_fake_runtime *)allocate_block(
        &actual_allocator,
        sizeof(*fake));
    if (fake == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(fake, 0, sizeof(*fake));
    fake->allocator = actual_allocator;
    fake->limits = actual_limits;
    *out_fake = fake;
    return WSH_OK;
}

/** Destroy one fake expectation's owned graph. */
static void destroy_fake_expectation(wsh_fake_expectation *expectation)
{
    if (expectation == NULL) {
        return;
    }
    wsh_string_destroy(expectation->subject);
    wsh_value_destroy(expectation->arguments);
    wsh_value_destroy(expectation->output);
    wsh_status_list_destroy(expectation->status);
    memset(expectation, 0, sizeof(*expectation));
}

/** @brief Implements wsh_fake_runtime_destroy. */
void wsh_fake_runtime_destroy(wsh_fake_runtime *fake)
{
    size_t index;

    if (fake == NULL) {
        return;
    }
    for (index = 0U; index < fake->expectation_count; ++index) {
        destroy_fake_expectation(&fake->expectations[index]);
    }
    fake->allocator.deallocate(
        fake->allocator.user_data,
        fake->expectations);
    fake->allocator.deallocate(fake->allocator.user_data, fake);
}

/** @brief Implements wsh_fake_runtime_interface. */
wsh_runtime wsh_fake_runtime_interface(wsh_fake_runtime *fake)
{
    wsh_runtime runtime;

    memset(&runtime, 0, sizeof(runtime));
    if (fake != NULL) {
        runtime.user_data = fake;
        runtime.invoke = fake_runtime_invoke;
        runtime.names_equal = fake_runtime_names_equal;
    }
    return runtime;
}

/** Create an owned empty immutable value. */
static wsh_result create_empty_value(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_value **out_value)
{
    wsh_value_builder *builder;
    wsh_result result;

    result = wsh_value_builder_create(allocator, limits, &builder);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_value_builder_finish(builder, out_value);
    wsh_value_builder_destroy(builder);
    return result;
}

/** Create an owned empty immutable status list. */
static wsh_result create_empty_status(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_status_list **out_status)
{
    wsh_status_builder *builder;
    wsh_result result;

    result = wsh_status_builder_create(allocator, limits, &builder);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_status_builder_finish(builder, out_status);
    wsh_status_builder_destroy(builder);
    return result;
}

/** @brief Implements wsh_fake_runtime_expect. */
wsh_result wsh_fake_runtime_expect(
    wsh_fake_runtime *fake,
    wsh_runtime_operation operation,
    wsh_string_view subject,
    const wsh_value *output,
    const wsh_status_list *status,
    wsh_result result_code)
{
    return wsh_fake_runtime_expect_arguments(
        fake,
        operation,
        subject,
        NULL,
        output,
        status,
        result_code);
}

/** @brief Implements wsh_fake_runtime_expect_arguments. */
wsh_result wsh_fake_runtime_expect_arguments(
    wsh_fake_runtime *fake,
    wsh_runtime_operation operation,
    wsh_string_view subject,
    const wsh_value *arguments,
    const wsh_value *output,
    const wsh_status_list *status,
    wsh_result result_code)
{
    wsh_fake_expectation expectation;
    wsh_result result;

    if (fake == NULL || operation < WSH_RUNTIME_READ_SOURCE ||
        operation > WSH_RUNTIME_LIBRARY) {
        return WSH_ERR_INVALID;
    }
    if (fake->expectation_count >= fake->limits.max_runtime_expectations) {
        return WSH_ERR_RESOURCE;
    }
    memset(&expectation, 0, sizeof(expectation));
    expectation.operation = operation;
    expectation.result = result_code;
    result = wsh_string_create(
        &fake->allocator,
        &fake->limits,
        subject,
        &expectation.subject);
    if (result != WSH_OK) {
        return result;
    }
    if (arguments != NULL) {
        result = clone_value_internal(
            &fake->allocator,
            &fake->limits,
            arguments,
            &expectation.arguments);
        if (result != WSH_OK) {
            destroy_fake_expectation(&expectation);
            return result;
        }
        expectation.compare_arguments = 1;
    }
    if (output == NULL) {
        result = create_empty_value(
            &fake->allocator,
            &fake->limits,
            &expectation.output);
    } else {
        result = clone_value_internal(
            &fake->allocator,
            &fake->limits,
            output,
            &expectation.output);
    }
    if (result != WSH_OK) {
        destroy_fake_expectation(&expectation);
        return result;
    }
    if (status == NULL) {
        result = create_empty_status(
            &fake->allocator,
            &fake->limits,
            &expectation.status);
    } else {
        result = clone_status_internal(
            &fake->allocator,
            &fake->limits,
            status,
            &expectation.status);
    }
    if (result != WSH_OK) {
        destroy_fake_expectation(&expectation);
        return result;
    }
    result = grow_array(
        &fake->allocator,
        (void **)&fake->expectations,
        sizeof(*fake->expectations),
        fake->expectation_count,
        &fake->expectation_capacity,
        fake->expectation_count + 1U,
        fake->limits.max_runtime_expectations);
    if (result != WSH_OK) {
        destroy_fake_expectation(&expectation);
        return result;
    }
    fake->expectations[fake->expectation_count++] = expectation;
    return WSH_OK;
}

/** @brief Implements wsh_fake_runtime_complete. */
wsh_result wsh_fake_runtime_complete(const wsh_fake_runtime *fake)
{
    if (fake == NULL) {
        return WSH_ERR_INVALID;
    }
    return fake->next_expectation == fake->expectation_count ?
        WSH_OK : WSH_ERR_MISMATCH;
}

/** @brief Implements wsh_fake_runtime_call_count. */
size_t wsh_fake_runtime_call_count(const wsh_fake_runtime *fake)
{
    return fake == NULL ? 0U : fake->call_count;
}
