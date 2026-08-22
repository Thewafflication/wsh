/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file evaluator.c
 * @brief Bounded portable WSH language evaluator.
 */

#include "wsh/evaluator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Default maximum semantic steps per public call. */
#define WSH_EVALUATOR_DEFAULT_STEPS 100000U
/** Default maximum evaluator nesting depth. */
#define WSH_EVALUATOR_DEFAULT_DEPTH 128U

/** Forward declaration for an evaluator-owned AST copy. */
typedef struct eval_node eval_node;

/** Evaluator-owned copy of one AST node. */
struct eval_node {
    /** Copied immutable node kind. */
    wsh_ast_kind kind;
    /** Copied source span. */
    wsh_source_span span;
    /** Optional owned primary text. */
    wsh_string *text;
    /** Optional owned auxiliary text. */
    wsh_string *auxiliary;
    /** Ordered owned child pointers. */
    eval_node **children;
    /** Number of child pointers. */
    size_t child_count;
};

/** One evaluator-owned expanded word plus glob eligibility. */
typedef struct eval_word {
    /** Owned strict UTF-8 word. */
    wsh_string *text;
    /** Nonzero when wildcard expansion is allowed. */
    int pattern;
} eval_word;

/** Mutable unpublished sequence used during expansion. */
typedef struct eval_words {
    /** Allocator used by this sequence. */
    wsh_allocator allocator;
    /** Copied string and item ceilings. */
    wsh_limits limits;
    /** Owned expanded-word array. */
    eval_word *items;
    /** Number of initialized words. */
    size_t count;
    /** Number of allocated word slots. */
    size_t capacity;
} eval_words;

/** Persistent function definition. */
typedef struct eval_function {
    /** Owned exact function name. */
    wsh_string *name;
    /** Owned persistent function body. */
    eval_node *body;
} eval_function;

/** Prior exact binding retained for one local scope. */
typedef struct eval_local {
    /** Owned exact variable name. */
    wsh_string *name;
    /** Owned prior value when the binding existed. */
    wsh_value *value;
    /** Nonzero when the prior binding existed. */
    int existed;
    /** Prior export attribute. */
    int exported;
} eval_local;

/** Dynamic local-binding frame. */
typedef struct eval_scope {
    /** Enclosing dynamic scope. */
    struct eval_scope *previous;
    /** Owned prior-binding records. */
    eval_local *locals;
    /** Number of initialized records. */
    size_t count;
    /** Number of allocated record slots. */
    size_t capacity;
} eval_scope;

/** Nonlocal control signal contained within the evaluator. */
typedef enum eval_signal {
    EVAL_SIGNAL_NONE = 0,
    EVAL_SIGNAL_BREAK,
    EVAL_SIGNAL_CONTINUE,
    EVAL_SIGNAL_RETURN
} eval_signal;

/** Persistent evaluator state. */
struct wsh_evaluator {
    /** Allocator for evaluator-owned objects. */
    wsh_allocator allocator;
    /** Copied core ceilings. */
    wsh_limits limits;
    /** Borrowed isolated context. */
    wsh_context *context;
    /** Owned logical diagnostic source name. */
    wsh_string *source_name;
    /** Step ceiling per public evaluation. */
    size_t max_steps;
    /** Recursive semantic depth ceiling. */
    size_t max_depth;
    /** Steps charged in the current call. */
    size_t steps;
    /** Current recursive semantic depth. */
    size_t depth;
    /** Active loop nesting. */
    size_t loop_depth;
    /** Active function nesting. */
    size_t function_depth;
    /** Active sourced-file nesting. */
    size_t source_depth;
    /** Pending contained control transfer. */
    eval_signal signal;
    /** Optional owned return status. */
    wsh_status_list *signal_status;
    /** Current dynamic local scope. */
    eval_scope *scope;
    /** Owned persistent function table. */
    eval_function *functions;
    /** Number of initialized functions. */
    size_t function_count;
    /** Number of allocated function slots. */
    size_t function_capacity;
    /** Optional borrowed active substitution capture. */
    wsh_string_builder *capture;
};

/** Return whether multiplying two sizes is representable. */
static int eval_multiply(size_t left, size_t right, size_t *out_value)
{
    if (out_value == NULL || (left != 0U && right > (size_t)-1 / left)) {
        return 0;
    }
    *out_value = left * right;
    return 1;
}

/** Grow an evaluator-owned array without exposing partial state. */
static wsh_result eval_grow(
    wsh_evaluator *evaluator,
    void **array,
    size_t element_size,
    size_t count,
    size_t *capacity,
    size_t required,
    size_t maximum)
{
    size_t next;
    size_t old_bytes;
    size_t new_bytes;
    void *replacement;

    if (evaluator == NULL || array == NULL || capacity == NULL ||
        element_size == 0U || required > maximum) {
        return required > maximum ? WSH_ERR_RESOURCE : WSH_ERR_INVALID;
    }
    if (required <= *capacity) {
        return WSH_OK;
    }
    next = *capacity == 0U ? 4U : *capacity;
    if (next > maximum) {
        next = maximum;
    }
    while (next < required) {
        next = next > maximum / 2U ? maximum : next * 2U;
    }
    if (!eval_multiply(count, element_size, &old_bytes) ||
        !eval_multiply(next, element_size, &new_bytes)) {
        return WSH_ERR_RESOURCE;
    }
    replacement = evaluator->allocator.allocate(
        evaluator->allocator.user_data,
        new_bytes == 0U ? 1U : new_bytes);
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (*array != NULL && old_bytes != 0U) {
        memcpy(replacement, *array, old_bytes);
    }
    evaluator->allocator.deallocate(evaluator->allocator.user_data, *array);
    *array = replacement;
    *capacity = next;
    return WSH_OK;
}

/** Allocate and zero one evaluator-owned block. */
static void *eval_allocate(wsh_evaluator *evaluator, size_t size)
{
    void *result;

    result = evaluator->allocator.allocate(
        evaluator->allocator.user_data,
        size == 0U ? 1U : size);
    if (result != NULL) {
        memset(result, 0, size);
    }
    return result;
}

/** Destroy an evaluator-owned AST copy. */
static void eval_node_destroy(wsh_evaluator *evaluator, eval_node *node)
{
    size_t index;

    if (evaluator == NULL || node == NULL) {
        return;
    }
    for (index = 0U; node->children != NULL &&
         index < node->child_count; ++index) {
        eval_node_destroy(evaluator, node->children[index]);
    }
    wsh_string_destroy(node->text);
    wsh_string_destroy(node->auxiliary);
    evaluator->allocator.deallocate(
        evaluator->allocator.user_data,
        node->children);
    evaluator->allocator.deallocate(evaluator->allocator.user_data, node);
}

/** Deep-copy an M3 AST into evaluator ownership. */
static wsh_result eval_node_copy_ast(
    wsh_evaluator *evaluator,
    const wsh_ast_node *source,
    eval_node **out_node)
{
    eval_node *node;
    size_t bytes;
    size_t index;
    wsh_result result;
    wsh_string_view text;

    if (evaluator == NULL || source == NULL || out_node == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_node = NULL;
    node = (eval_node *)eval_allocate(evaluator, sizeof(*node));
    if (node == NULL) {
        return WSH_ERR_RESOURCE;
    }
    node->kind = wsh_ast_node_kind(source);
    result = wsh_ast_node_span(source, &node->span);
    if (result != WSH_OK) {
        eval_node_destroy(evaluator, node);
        return result;
    }
    text = wsh_ast_node_text(source);
    if (text.length != 0U) {
        result = wsh_string_create(
            &evaluator->allocator,
            &evaluator->limits,
            text,
            &node->text);
        if (result != WSH_OK) {
            eval_node_destroy(evaluator, node);
            return result;
        }
    }
    text = wsh_ast_node_auxiliary(source);
    if (text.length != 0U) {
        result = wsh_string_create(
            &evaluator->allocator,
            &evaluator->limits,
            text,
            &node->auxiliary);
        if (result != WSH_OK) {
            eval_node_destroy(evaluator, node);
            return result;
        }
    }
    node->child_count = wsh_ast_node_child_count(source);
    if (node->child_count != 0U) {
        if (!eval_multiply(
                node->child_count,
                sizeof(*node->children),
                &bytes)) {
            eval_node_destroy(evaluator, node);
            return WSH_ERR_RESOURCE;
        }
        node->children = (eval_node **)eval_allocate(evaluator, bytes);
        if (node->children == NULL) {
            eval_node_destroy(evaluator, node);
            return WSH_ERR_RESOURCE;
        }
    }
    for (index = 0U; index < node->child_count; ++index) {
        result = eval_node_copy_ast(
            evaluator,
            wsh_ast_node_child_at(source, index),
            &node->children[index]);
        if (result != WSH_OK) {
            eval_node_destroy(evaluator, node);
            return result;
        }
    }
    *out_node = node;
    return WSH_OK;
}

/** Deep-copy one evaluator-owned AST. */
static wsh_result eval_node_clone(
    wsh_evaluator *evaluator,
    const eval_node *source,
    eval_node **out_node)
{
    eval_node *node;
    size_t bytes;
    size_t index;
    wsh_result result;

    if (evaluator == NULL || source == NULL || out_node == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_node = NULL;
    node = (eval_node *)eval_allocate(evaluator, sizeof(*node));
    if (node == NULL) {
        return WSH_ERR_RESOURCE;
    }
    node->kind = source->kind;
    node->span = source->span;
    if (source->text != NULL) {
        result = wsh_string_create(
            &evaluator->allocator,
            &evaluator->limits,
            wsh_string_bytes(source->text),
            &node->text);
        if (result != WSH_OK) {
            eval_node_destroy(evaluator, node);
            return result;
        }
    }
    if (source->auxiliary != NULL) {
        result = wsh_string_create(
            &evaluator->allocator,
            &evaluator->limits,
            wsh_string_bytes(source->auxiliary),
            &node->auxiliary);
        if (result != WSH_OK) {
            eval_node_destroy(evaluator, node);
            return result;
        }
    }
    node->child_count = source->child_count;
    if (node->child_count != 0U) {
        if (!eval_multiply(
                node->child_count,
                sizeof(*node->children),
                &bytes)) {
            eval_node_destroy(evaluator, node);
            return WSH_ERR_RESOURCE;
        }
        node->children = (eval_node **)eval_allocate(evaluator, bytes);
        if (node->children == NULL) {
            eval_node_destroy(evaluator, node);
            return WSH_ERR_RESOURCE;
        }
    }
    for (index = 0U; index < node->child_count; ++index) {
        result = eval_node_clone(
            evaluator,
            source->children[index],
            &node->children[index]);
        if (result != WSH_OK) {
            eval_node_destroy(evaluator, node);
            return result;
        }
    }
    *out_node = node;
    return WSH_OK;
}

/** Retain one semantic diagnostic without masking its primary result. */
static void eval_diagnostic(
    wsh_evaluator *evaluator,
    wsh_diagnostic_code code,
    const char *message,
    const eval_node *node)
{
    const wsh_source_span *span;

    if (evaluator == NULL) {
        return;
    }
    span = node == NULL ? NULL : &node->span;
    (void)wsh_context_add_diagnostic(
        evaluator->context,
        WSH_DIAGNOSTIC_ERROR,
        code,
        wsh_string_view_from_cstr(message),
        wsh_string_bytes(evaluator->source_name),
        span);
}

/** Charge one deterministic semantic step and recursion level. */
static wsh_result eval_enter(wsh_evaluator *evaluator, const eval_node *node)
{
    if (evaluator->steps >= evaluator->max_steps ||
        evaluator->depth >= evaluator->max_depth) {
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_LIMIT,
            "evaluator step or depth limit reached",
            node);
        return WSH_ERR_RESOURCE;
    }
    evaluator->steps += 1U;
    evaluator->depth += 1U;
    return WSH_OK;
}

/** Leave one charged recursive semantic operation. */
static void eval_leave(wsh_evaluator *evaluator)
{
    if (evaluator->depth != 0U) {
        evaluator->depth -= 1U;
    }
}

/** Build an owned single-element status list. */
static wsh_result eval_status_one(
    wsh_evaluator *evaluator,
    uint32_t code,
    wsh_status_list **out_status)
{
    wsh_status_builder *builder;
    wsh_result result;

    *out_status = NULL;
    result = wsh_status_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    if (result == WSH_OK) {
        result = wsh_status_builder_append(builder, code);
    }
    if (result == WSH_OK) {
        result = wsh_status_builder_finish(builder, out_status);
    }
    wsh_status_builder_destroy(builder);
    return result;
}

/** Deep-copy a nonempty status list. */
static wsh_result eval_status_clone(
    wsh_evaluator *evaluator,
    const wsh_status_list *source,
    wsh_status_list **out_status)
{
    wsh_status_builder *builder;
    size_t index;
    uint32_t value;
    wsh_result result;

    if (source == NULL || wsh_status_list_count(source) == 0U) {
        return WSH_ERR_INVALID;
    }
    *out_status = NULL;
    result = wsh_status_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    for (index = 0U; result == WSH_OK &&
         index < wsh_status_list_count(source); ++index) {
        result = wsh_status_list_at(source, index, &value);
        if (result == WSH_OK) {
            result = wsh_status_builder_append(builder, value);
        }
    }
    if (result == WSH_OK) {
        result = wsh_status_builder_finish(builder, out_status);
    }
    wsh_status_builder_destroy(builder);
    return result;
}

/** Mirror an execution status into the ordinary `$status` value. */
static wsh_result eval_publish_status(
    wsh_evaluator *evaluator,
    const wsh_status_list *status)
{
    wsh_value_builder *builder;
    wsh_value *value;
    char decimal[16];
    size_t index;
    uint32_t code;
    int length;
    wsh_string_view text;
    wsh_result result;

    if (status == NULL || wsh_status_list_count(status) == 0U) {
        return WSH_ERR_INVALID;
    }
    result = wsh_value_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    for (index = 0U; result == WSH_OK &&
         index < wsh_status_list_count(status); ++index) {
        result = wsh_status_list_at(status, index, &code);
        if (result == WSH_OK) {
            length = snprintf(decimal, sizeof(decimal), "%lu",
                (unsigned long)code);
            if (length < 0 || (size_t)length >= sizeof(decimal)) {
                result = WSH_ERR_INTERNAL;
            } else {
                text.data = decimal;
                text.length = (size_t)length;
                result = wsh_value_builder_append(builder, text);
            }
        }
    }
    value = NULL;
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, &value);
    }
    wsh_value_builder_destroy(builder);
    if (result == WSH_OK) {
        result = wsh_context_set_variable(
            evaluator->context,
            wsh_string_view_from_cstr("status"),
            value);
    }
    wsh_value_destroy(value);
    return result;
}

/** Initialize an unpublished expanded-word sequence. */
static void eval_words_init(wsh_evaluator *evaluator, eval_words *words)
{
    memset(words, 0, sizeof(*words));
    words->allocator = evaluator->allocator;
    words->limits = evaluator->limits;
}

/** Destroy an expanded-word sequence. */
static void eval_words_destroy(eval_words *words)
{
    size_t index;

    if (words == NULL) {
        return;
    }
    for (index = 0U; index < words->count; ++index) {
        wsh_string_destroy(words->items[index].text);
    }
    words->allocator.deallocate(words->allocator.user_data, words->items);
    memset(words, 0, sizeof(*words));
}

/** Append one copied expanded word. */
static wsh_result eval_words_append(
    wsh_evaluator *evaluator,
    eval_words *words,
    wsh_string_view text,
    int pattern)
{
    wsh_string *copy;
    wsh_result result;

    if (words->count >= words->limits.max_list_items) {
        return WSH_ERR_RESOURCE;
    }
    result = wsh_string_create(
        &words->allocator,
        &words->limits,
        text,
        &copy);
    if (result != WSH_OK) {
        return result;
    }
    result = eval_grow(
        evaluator,
        (void **)&words->items,
        sizeof(*words->items),
        words->count,
        &words->capacity,
        words->count + 1U,
        words->limits.max_list_items);
    if (result != WSH_OK) {
        wsh_string_destroy(copy);
        return result;
    }
    words->items[words->count].text = copy;
    words->items[words->count].pattern = pattern ? 1 : 0;
    words->count += 1U;
    return WSH_OK;
}

/** Append an immutable value to expanded words. */
static wsh_result eval_words_append_value(
    wsh_evaluator *evaluator,
    eval_words *words,
    const wsh_value *value,
    int pattern)
{
    size_t index;
    wsh_string_view text;
    wsh_result result;

    for (index = 0U; index < wsh_value_count(value); ++index) {
        result = wsh_value_at(value, index, &text);
        if (result != WSH_OK) {
            return result;
        }
        result = eval_words_append(evaluator, words, text, pattern);
        if (result != WSH_OK) {
            return result;
        }
    }
    return WSH_OK;
}

/** Convert expanded words to a public immutable flat value. */
static wsh_result eval_words_value(
    wsh_evaluator *evaluator,
    const eval_words *words,
    wsh_value **out_value)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_result result;

    *out_value = NULL;
    result = wsh_value_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    for (index = 0U; result == WSH_OK && index < words->count; ++index) {
        result = wsh_value_builder_append(
            builder,
            wsh_string_bytes(words->items[index].text));
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_value);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Return one exact context variable or an empty-list value. */
static wsh_result eval_get_variable(
    wsh_evaluator *evaluator,
    wsh_string_view name,
    wsh_value **out_value)
{
    const wsh_value *borrowed;
    wsh_value_builder *builder;
    wsh_result result;

    *out_value = NULL;
    result = wsh_context_get_variable(evaluator->context, name, &borrowed);
    if (result == WSH_OK) {
        return wsh_value_clone(
            &evaluator->allocator,
            &evaluator->limits,
            borrowed,
            out_value);
    }
    result = wsh_value_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_value);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Return the primary text of an evaluator-owned node. */
static wsh_string_view eval_node_text(const eval_node *node)
{
    return wsh_string_bytes(node == NULL ? NULL : node->text);
}

/** Decode one already validated UTF-8 scalar. */
static size_t eval_scalar_width(const char *bytes, size_t length, size_t offset)
{
    unsigned char first;

    if (bytes == NULL || offset >= length) {
        return 0U;
    }
    first = (unsigned char)bytes[offset];
    if (first < 0x80U) {
        return 1U;
    }
    if (first < 0xE0U) {
        return 2U;
    }
    if (first < 0xF0U) {
        return 3U;
    }
    return 4U;
}

/** Decode one scalar to a numeric value from valid UTF-8. */
static uint32_t eval_scalar_value(
    const char *bytes,
    size_t length,
    size_t offset,
    size_t *out_width)
{
    unsigned char first;
    size_t width;
    size_t index;
    uint32_t scalar;

    width = eval_scalar_width(bytes, length, offset);
    if (width == 0U || width > length - offset) {
        *out_width = 0U;
        return 0U;
    }
    first = (unsigned char)bytes[offset];
    if (width == 1U) {
        scalar = first;
    } else if (width == 2U) {
        scalar = first & 0x1FU;
    } else if (width == 3U) {
        scalar = first & 0x0FU;
    } else {
        scalar = first & 0x07U;
    }
    for (index = 1U; index < width; ++index) {
        scalar = (scalar << 6U) |
            ((unsigned char)bytes[offset + index] & 0x3FU);
    }
    *out_width = width;
    return scalar;
}

/** Fold ASCII case for the Windows-ordinal portable fallback. */
static uint32_t eval_fold_scalar(uint32_t scalar, int insensitive)
{
    if (insensitive && scalar >= (uint32_t)'A' && scalar <= (uint32_t)'Z') {
        return scalar + (uint32_t)('a' - 'A');
    }
    return scalar;
}

/** Return whether a byte is a path-component separator. */
static int eval_is_separator(uint32_t scalar)
{
    return scalar == (uint32_t)'/' || scalar == (uint32_t)'\\';
}

/** Match one bracket class and return bytes consumed after `[`. */
static int eval_match_class(
    const char *pattern,
    size_t pattern_length,
    size_t offset,
    uint32_t scalar,
    int insensitive,
    size_t *out_after)
{
    int complement;
    int matched;
    size_t width;
    size_t next_width;
    uint32_t first;
    uint32_t last;

    complement = offset < pattern_length && pattern[offset] == '~';
    if (complement) {
        offset += 1U;
    }
    matched = 0;
    while (offset < pattern_length && pattern[offset] != ']') {
        first = eval_scalar_value(
            pattern, pattern_length, offset, &width);
        if (width == 0U) {
            return 0;
        }
        offset += width;
        last = first;
        if (offset < pattern_length && pattern[offset] == '-' &&
            offset + 1U < pattern_length && pattern[offset + 1U] != ']') {
            offset += 1U;
            last = eval_scalar_value(
                pattern, pattern_length, offset, &next_width);
            if (next_width == 0U) {
                return 0;
            }
            offset += next_width;
        }
        first = eval_fold_scalar(first, insensitive);
        last = eval_fold_scalar(last, insensitive);
        scalar = eval_fold_scalar(scalar, insensitive);
        if (first <= scalar && scalar <= last) {
            matched = 1;
        }
    }
    if (offset >= pattern_length || pattern[offset] != ']') {
        return 0;
    }
    *out_after = offset + 1U;
    return complement ? !matched : matched;
}

/** Recursive bounded wildcard matcher over Unicode scalars. */
static int eval_pattern_match_at(
    wsh_string_view pattern,
    size_t pattern_offset,
    wsh_string_view text,
    size_t text_offset,
    int insensitive,
    size_t *budget)
{
    size_t pattern_width;
    size_t text_width;
    size_t after;
    uint32_t pattern_scalar;
    uint32_t text_scalar;

    if (*budget == 0U) {
        return 0;
    }
    *budget -= 1U;
    if (pattern_offset == pattern.length) {
        return text_offset == text.length;
    }
    if (text_offset < text.length && text.data[text_offset] == '.' &&
        (text_offset == 0U || text.data[text_offset - 1U] == '/' ||
         text.data[text_offset - 1U] == '\\') &&
        pattern.data[pattern_offset] != '.') {
        return 0;
    }
    if (pattern.data[pattern_offset] == '*') {
        pattern_offset += 1U;
        while (pattern_offset < pattern.length &&
            pattern.data[pattern_offset] == '*') {
            pattern_offset += 1U;
        }
        if (eval_pattern_match_at(
                pattern, pattern_offset, text, text_offset,
                insensitive, budget)) {
            return 1;
        }
        while (text_offset < text.length) {
            text_scalar = eval_scalar_value(
                text.data, text.length, text_offset, &text_width);
            if (text_width == 0U || eval_is_separator(text_scalar)) {
                break;
            }
            text_offset += text_width;
            if (eval_pattern_match_at(
                    pattern, pattern_offset, text, text_offset,
                    insensitive, budget)) {
                return 1;
            }
        }
        return 0;
    }
    if (text_offset == text.length) {
        return 0;
    }
    text_scalar = eval_scalar_value(
        text.data, text.length, text_offset, &text_width);
    if (text_width == 0U) {
        return 0;
    }
    if (pattern.data[pattern_offset] == '?') {
        if (eval_is_separator(text_scalar)) {
            return 0;
        }
        return eval_pattern_match_at(
            pattern, pattern_offset + 1U,
            text, text_offset + text_width,
            insensitive, budget);
    }
    if (pattern.data[pattern_offset] == '[') {
        if (eval_is_separator(text_scalar) ||
            !eval_match_class(
                pattern.data,
                pattern.length,
                pattern_offset + 1U,
                text_scalar,
                insensitive,
                &after)) {
            return 0;
        }
        return eval_pattern_match_at(
            pattern, after,
            text, text_offset + text_width,
            insensitive, budget);
    }
    pattern_scalar = eval_scalar_value(
        pattern.data, pattern.length, pattern_offset, &pattern_width);
    if (pattern_width == 0U ||
        eval_fold_scalar(pattern_scalar, insensitive) !=
            eval_fold_scalar(text_scalar, insensitive)) {
        return 0;
    }
    return eval_pattern_match_at(
        pattern, pattern_offset + pattern_width,
        text, text_offset + text_width,
        insensitive, budget);
}

/** Match a path pattern, including the explicit-leading-dot rule. */
static int eval_pattern_matches_mode(
    wsh_string_view pattern,
    wsh_string_view text,
    int insensitive)
{
    size_t budget;
    if (wsh_utf8_validate(pattern, NULL) != WSH_OK ||
        wsh_utf8_validate(text, NULL) != WSH_OK) {
        return 0;
    }
    budget = 100000U;
    return eval_pattern_match_at(
        pattern, 0U, text, 0U, insensitive, &budget);
}

/** @brief Implements wsh_pattern_matches. */
int wsh_pattern_matches(wsh_string_view pattern, wsh_string_view text)
{
    return eval_pattern_matches_mode(pattern, text, 0);
}

/** Return whether an unquoted word contains wildcard syntax. */
static int eval_has_pattern(wsh_string_view text)
{
    size_t index;

    for (index = 0U; index < text.length; ++index) {
        if (text.data[index] == '*' || text.data[index] == '?' ||
            text.data[index] == '[') {
            return 1;
        }
    }
    return 0;
}

/** Compare strings by ASCII-folded bytes and then exact bytes. */
static int eval_compare_strings(
    const void *left_pointer,
    const void *right_pointer)
{
    const eval_word *left = (const eval_word *)left_pointer;
    const eval_word *right = (const eval_word *)right_pointer;
    wsh_string_view left_text = wsh_string_bytes(left->text);
    wsh_string_view right_text = wsh_string_bytes(right->text);
    size_t count;
    size_t index;
    unsigned char left_byte;
    unsigned char right_byte;
    int exact;

    count = left_text.length < right_text.length ?
        left_text.length : right_text.length;
    exact = 0;
    for (index = 0U; index < count; ++index) {
        left_byte = (unsigned char)left_text.data[index];
        right_byte = (unsigned char)right_text.data[index];
        if (exact == 0 && left_byte != right_byte) {
            exact = left_byte < right_byte ? -1 : 1;
        }
        if (left_byte >= 'A' && left_byte <= 'Z') {
            left_byte = (unsigned char)(left_byte + ('a' - 'A'));
        }
        if (right_byte >= 'A' && right_byte <= 'Z') {
            right_byte = (unsigned char)(right_byte + ('a' - 'A'));
        }
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left_text.length != right_text.length) {
        return left_text.length < right_text.length ? -1 : 1;
    }
    return exact;
}

/** Expand eligible wildcard words through the abstract runtime. */
static wsh_result eval_glob_words(
    wsh_evaluator *evaluator,
    const eval_node *node,
    eval_words *words)
{
    eval_words replacement;
    eval_words matches;
    size_t index;
    size_t candidate;
    wsh_runtime_request request;
    wsh_value *output;
    wsh_status_list *status;
    wsh_string_view pattern;
    wsh_string_view text;
    wsh_result result;

    eval_words_init(evaluator, &replacement);
    for (index = 0U; index < words->count; ++index) {
        pattern = wsh_string_bytes(words->items[index].text);
        if (!words->items[index].pattern || !eval_has_pattern(pattern)) {
            result = eval_words_append(
                evaluator, &replacement, pattern,
                words->items[index].pattern);
            if (result != WSH_OK) {
                eval_words_destroy(&replacement);
                return result;
            }
            continue;
        }
        memset(&request, 0, sizeof(request));
        request.operation = WSH_RUNTIME_MATCH_PATHS;
        request.subject = pattern;
        output = NULL;
        status = NULL;
        result = wsh_context_runtime_invoke(
            evaluator->context, &request, &output, &status);
        if (result != WSH_OK || !wsh_status_list_is_success(status)) {
            wsh_value_destroy(output);
            wsh_status_list_destroy(status);
            eval_words_destroy(&replacement);
            eval_diagnostic(
                evaluator,
                WSH_DIAGNOSTIC_EXPANSION,
                "pathname expansion failed",
                node);
            return result == WSH_OK ? WSH_ERR_MISMATCH : result;
        }
        eval_words_init(evaluator, &matches);
        for (candidate = 0U; candidate < wsh_value_count(output); ++candidate) {
            result = wsh_value_at(output, candidate, &text);
            if (result == WSH_OK &&
                eval_pattern_matches_mode(pattern, text, 1)) {
                result = eval_words_append(evaluator, &matches, text, 0);
            }
            if (result != WSH_OK) {
                break;
            }
        }
        wsh_value_destroy(output);
        wsh_status_list_destroy(status);
        if (result != WSH_OK) {
            eval_words_destroy(&matches);
            eval_words_destroy(&replacement);
            return result;
        }
        if (matches.count == 0U) {
            result = eval_words_append(evaluator, &replacement, pattern, 0);
        } else {
            qsort(matches.items, matches.count, sizeof(*matches.items),
                eval_compare_strings);
            for (candidate = 0U; result == WSH_OK &&
                 candidate < matches.count; ++candidate) {
                result = eval_words_append(
                    evaluator,
                    &replacement,
                    wsh_string_bytes(matches.items[candidate].text),
                    0);
            }
        }
        eval_words_destroy(&matches);
        if (result != WSH_OK) {
            eval_words_destroy(&replacement);
            return result;
        }
    }
    eval_words_destroy(words);
    *words = replacement;
    return WSH_OK;
}

static wsh_result eval_execute(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status);

/** Test whether one output scalar occurs in the current `$ifs`. */
static int eval_scalar_in_ifs(
    wsh_evaluator *evaluator,
    const char *bytes,
    size_t width)
{
    const wsh_value *ifs;
    wsh_string_view item;
    size_t item_index;
    size_t offset;
    size_t item_width;

    if (wsh_context_get_variable(
            evaluator->context,
            wsh_string_view_from_cstr("ifs"),
            &ifs) != WSH_OK) {
        return width == 1U &&
            (bytes[0] == ' ' || bytes[0] == '\t' ||
             bytes[0] == '\r' || bytes[0] == '\n');
    }
    for (item_index = 0U; item_index < wsh_value_count(ifs); ++item_index) {
        if (wsh_value_at(ifs, item_index, &item) != WSH_OK) {
            continue;
        }
        offset = 0U;
        while (offset < item.length) {
            item_width = eval_scalar_width(item.data, item.length, offset);
            if (item_width == width &&
                memcmp(item.data + offset, bytes, width) == 0) {
                return 1;
            }
            offset += item_width;
        }
    }
    return 0;
}

/** Split captured UTF-8 using the Unicode scalar set in `$ifs`. */
static wsh_result eval_split_capture(
    wsh_evaluator *evaluator,
    wsh_string_view captured,
    eval_words *out_words)
{
    size_t offset;
    size_t width;
    size_t start;
    wsh_string_view part;
    wsh_result result;

    offset = 0U;
    start = 0U;
    while (offset < captured.length) {
        width = eval_scalar_width(captured.data, captured.length, offset);
        if (width == 0U) {
            return WSH_ERR_ENCODING;
        }
        if (eval_scalar_in_ifs(evaluator, captured.data + offset, width)) {
            if (offset > start) {
                part.data = captured.data + start;
                part.length = offset - start;
                result = eval_words_append(evaluator, out_words, part, 1);
                if (result != WSH_OK) {
                    return result;
                }
            }
            offset += width;
            start = offset;
        } else {
            offset += width;
        }
    }
    if (offset > start) {
        part.data = captured.data + start;
        part.length = offset - start;
        return eval_words_append(evaluator, out_words, part, 1);
    }
    return WSH_OK;
}

/** Evaluate and split one command-substitution node. */
static wsh_result eval_command_substitution(
    wsh_evaluator *evaluator,
    const eval_node *node,
    eval_words *out_words)
{
    wsh_string_builder *outer_capture;
    wsh_string_builder *capture;
    wsh_string *captured;
    wsh_status_list *status;
    wsh_result result;

    if (node->child_count != 1U) {
        return WSH_ERR_INTERNAL;
    }
    outer_capture = evaluator->capture;
    result = wsh_string_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &capture);
    if (result != WSH_OK) {
        return result;
    }
    evaluator->capture = capture;
    status = NULL;
    result = eval_execute(evaluator, node->children[0], &status);
    evaluator->capture = outer_capture;
    captured = NULL;
    if (result == WSH_OK && wsh_status_list_is_success(status)) {
        result = wsh_string_builder_finish(capture, &captured);
    } else if (result == WSH_OK) {
        result = WSH_ERR_MISMATCH;
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_EXPANSION,
            "failed command substitution suppressed containing command",
            node);
    }
    wsh_string_builder_destroy(capture);
    wsh_status_list_destroy(status);
    if (result == WSH_OK) {
        result = eval_split_capture(
            evaluator,
            wsh_string_bytes(captured),
            out_words);
    }
    wsh_string_destroy(captured);
    return result;
}

/** Parse a strict positive decimal size. */
static int eval_parse_index(wsh_string_view text, size_t *out_value)
{
    size_t value;
    size_t index;
    unsigned digit;

    if (text.length == 0U) {
        return 0;
    }
    value = 0U;
    for (index = 0U; index < text.length; ++index) {
        if (text.data[index] < '0' || text.data[index] > '9') {
            return 0;
        }
        digit = (unsigned)(text.data[index] - '0');
        if (value > ((size_t)-1 - digit) / 10U) {
            return 0;
        }
        value = value * 10U + digit;
    }
    if (value == 0U) {
        return 0;
    }
    *out_value = value;
    return 1;
}

/** Apply one-origin subscript nodes to a variable value. */
static wsh_result eval_variable_subscripts(
    wsh_evaluator *evaluator,
    const eval_node *node,
    const wsh_value *value,
    eval_words *out_words)
{
    size_t child;
    wsh_string_view item;
    size_t dash;
    size_t first;
    size_t last;
    size_t index;
    wsh_string_view selected;
    wsh_result result;

    if (node->child_count == 0U) {
        return eval_words_append_value(evaluator, out_words, value, 1);
    }
    for (child = 0U; child < node->child_count; ++child) {
        item = eval_node_text(node->children[child]);
        dash = 0U;
        while (dash < item.length && item.data[dash] != '-') {
            dash += 1U;
        }
        if (dash == item.length) {
            if (!eval_parse_index(item, &first)) {
                goto invalid;
            }
            last = first;
        } else {
            wsh_string_view left;
            wsh_string_view right;

            left.data = item.data;
            left.length = dash;
            right.data = item.data + dash + 1U;
            right.length = item.length - dash - 1U;
            if (!eval_parse_index(left, &first)) {
                goto invalid;
            }
            if (right.length == 0U) {
                last = wsh_value_count(value);
            } else if (!eval_parse_index(right, &last)) {
                goto invalid;
            }
            if (last < first) {
                goto invalid;
            }
        }
        for (index = first; index <= last && index != 0U; ++index) {
            if (index <= wsh_value_count(value)) {
                result = wsh_value_at(value, index - 1U, &selected);
                if (result != WSH_OK) {
                    return result;
                }
                result = eval_words_append(
                    evaluator, out_words, selected, 1);
                if (result != WSH_OK) {
                    return result;
                }
            }
        }
    }
    return WSH_OK;

invalid:
    eval_diagnostic(
        evaluator,
        WSH_DIAGNOSTIC_EXPANSION,
        "invalid one-origin variable subscript",
        node);
    return WSH_ERR_INVALID;
}

/** Expand one expression node without filesystem matching. */
static wsh_result eval_expand_expression(
    wsh_evaluator *evaluator,
    const eval_node *node,
    eval_words *out_words)
{
    wsh_result result;
    wsh_value *value;
    wsh_string_view name;
    wsh_string_view text;
    eval_words left;
    eval_words right;
    size_t index;
    size_t left_index;
    size_t right_index;
    wsh_string_builder *builder;
    wsh_string *joined;
    char decimal[32];
    int length;

    if (node == NULL) {
        return WSH_ERR_INVALID;
    }
    switch (node->kind) {
    case WSH_AST_WORD:
        return eval_words_append(evaluator, out_words, eval_node_text(node), 1);
    case WSH_AST_QUOTED_WORD:
        return eval_words_append(evaluator, out_words, eval_node_text(node), 0);
    case WSH_AST_LIST:
        for (index = 0U; index < node->child_count; ++index) {
            result = eval_expand_expression(
                evaluator, node->children[index], out_words);
            if (result != WSH_OK) {
                return result;
            }
        }
        return WSH_OK;
    case WSH_AST_VARIABLE:
        name = eval_node_text(node);
        if (name.length == 1U && name.data[0] >= '1' &&
            name.data[0] <= '9') {
            result = eval_get_variable(
                evaluator, wsh_string_view_from_cstr("*"), &value);
            if (result == WSH_OK &&
                (size_t)(name.data[0] - '0') <= wsh_value_count(value)) {
                result = wsh_value_at(
                    value, (size_t)(name.data[0] - '1'), &text);
                if (result == WSH_OK) {
                    result = eval_words_append(evaluator, out_words, text, 1);
                }
            }
            wsh_value_destroy(value);
            return result;
        }
        result = eval_get_variable(evaluator, name, &value);
        if (result == WSH_OK) {
            result = eval_variable_subscripts(
                evaluator, node, value, out_words);
        }
        wsh_value_destroy(value);
        return result;
    case WSH_AST_COUNT:
        result = eval_get_variable(evaluator, eval_node_text(node), &value);
        if (result != WSH_OK) {
            return result;
        }
        length = snprintf(decimal, sizeof(decimal), "%lu",
            (unsigned long)wsh_value_count(value));
        wsh_value_destroy(value);
        if (length < 0 || (size_t)length >= sizeof(decimal)) {
            return WSH_ERR_INTERNAL;
        }
        text.data = decimal;
        text.length = (size_t)length;
        return eval_words_append(evaluator, out_words, text, 0);
    case WSH_AST_FLATTEN:
        result = eval_get_variable(evaluator, eval_node_text(node), &value);
        if (result != WSH_OK) {
            return result;
        }
        result = wsh_string_builder_create(
            &evaluator->allocator, &evaluator->limits, &builder);
        for (index = 0U; result == WSH_OK &&
             index < wsh_value_count(value); ++index) {
            if (index != 0U) {
                result = wsh_string_builder_append(
                    builder, wsh_string_view_from_cstr(" "));
            }
            if (result == WSH_OK) {
                result = wsh_value_at(value, index, &text);
            }
            if (result == WSH_OK) {
                result = wsh_string_builder_append(builder, text);
            }
        }
        wsh_value_destroy(value);
        joined = NULL;
        if (result == WSH_OK) {
            result = wsh_string_builder_finish(builder, &joined);
        }
        wsh_string_builder_destroy(builder);
        if (result == WSH_OK) {
            result = eval_words_append(
                evaluator, out_words, wsh_string_bytes(joined), 0);
        }
        wsh_string_destroy(joined);
        return result;
    case WSH_AST_CONCAT:
        if (node->child_count != 2U) {
            return WSH_ERR_INTERNAL;
        }
        eval_words_init(evaluator, &left);
        eval_words_init(evaluator, &right);
        result = eval_expand_expression(evaluator, node->children[0], &left);
        if (result == WSH_OK) {
            result = eval_expand_expression(
                evaluator, node->children[1], &right);
        }
        if (result != WSH_OK) {
            eval_words_destroy(&left);
            eval_words_destroy(&right);
            return result;
        }
        if (left.count == 0U && right.count == 0U) {
            eval_words_destroy(&left);
            eval_words_destroy(&right);
            return WSH_OK;
        }
        if (left.count == right.count && left.count != 0U) {
            index = left.count;
        } else if (left.count == 1U && right.count != 0U) {
            index = right.count;
        } else if (right.count == 1U && left.count != 0U) {
            index = left.count;
        } else {
            eval_words_destroy(&left);
            eval_words_destroy(&right);
            eval_diagnostic(
                evaluator,
                WSH_DIAGNOSTIC_EXPANSION,
                "caret operands have incompatible list cardinalities",
                node);
            return WSH_ERR_MISMATCH;
        }
        for (left_index = 0U; left_index < index; ++left_index) {
            right_index = right.count == 1U ? 0U : left_index;
            result = wsh_string_builder_create(
                &evaluator->allocator, &evaluator->limits, &builder);
            if (result == WSH_OK) {
                result = wsh_string_builder_append(
                    builder,
                    wsh_string_bytes(
                        left.items[left.count == 1U ? 0U : left_index].text));
            }
            if (result == WSH_OK) {
                result = wsh_string_builder_append(
                    builder, wsh_string_bytes(right.items[right_index].text));
            }
            joined = NULL;
            if (result == WSH_OK) {
                result = wsh_string_builder_finish(builder, &joined);
            }
            wsh_string_builder_destroy(builder);
            if (result == WSH_OK) {
                result = eval_words_append(
                    evaluator,
                    out_words,
                    wsh_string_bytes(joined),
                    left.items[left.count == 1U ? 0U : left_index].pattern ||
                        right.items[right_index].pattern);
            }
            wsh_string_destroy(joined);
            if (result != WSH_OK) {
                break;
            }
        }
        eval_words_destroy(&left);
        eval_words_destroy(&right);
        return result;
    case WSH_AST_COMMAND_SUBSTITUTION:
        return eval_command_substitution(evaluator, node, out_words);
    case WSH_AST_PROCESS_READ:
    case WSH_AST_PROCESS_WRITE:
    case WSH_AST_PROCESS_DUPLEX:
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_EVALUATION,
            "process substitution belongs to M5 runtime orchestration",
            node);
        return WSH_ERR_INVALID;
    default:
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_EVALUATION,
            "node is not a value-producing expression",
            node);
        return WSH_ERR_INVALID;
    }
}

/** Push one dynamic local-binding frame. */
static wsh_result eval_scope_push(wsh_evaluator *evaluator)
{
    eval_scope *scope;

    scope = (eval_scope *)eval_allocate(evaluator, sizeof(*scope));
    if (scope == NULL) {
        return WSH_ERR_RESOURCE;
    }
    scope->previous = evaluator->scope;
    evaluator->scope = scope;
    return WSH_OK;
}

/** Destroy one local record without changing context state. */
static void eval_local_destroy(eval_local *local)
{
    if (local == NULL) {
        return;
    }
    wsh_string_destroy(local->name);
    wsh_value_destroy(local->value);
    memset(local, 0, sizeof(*local));
}

/** Record a binding once in the current scope before local mutation. */
static wsh_result eval_scope_remember(
    wsh_evaluator *evaluator,
    wsh_string_view name)
{
    eval_scope *scope;
    eval_local local;
    const wsh_value *value;
    size_t index;
    wsh_result result;

    scope = evaluator->scope;
    if (scope == NULL) {
        return WSH_ERR_INTERNAL;
    }
    for (index = 0U; index < scope->count; ++index) {
        if (wsh_string_view_equal(
                wsh_string_bytes(scope->locals[index].name), name)) {
            return WSH_OK;
        }
    }
    memset(&local, 0, sizeof(local));
    result = wsh_string_create(
        &evaluator->allocator,
        &evaluator->limits,
        name,
        &local.name);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_context_get_variable(evaluator->context, name, &value);
    if (result == WSH_OK) {
        local.existed = 1;
        result = wsh_value_clone(
            &evaluator->allocator,
            &evaluator->limits,
            value,
            &local.value);
        if (result == WSH_OK) {
            result = wsh_context_is_exported(
                evaluator->context, name, &local.exported);
        }
    } else {
        result = WSH_OK;
    }
    if (result != WSH_OK) {
        eval_local_destroy(&local);
        return result;
    }
    result = eval_grow(
        evaluator,
        (void **)&scope->locals,
        sizeof(*scope->locals),
        scope->count,
        &scope->capacity,
        scope->count + 1U,
        evaluator->limits.max_variables);
    if (result != WSH_OK) {
        eval_local_destroy(&local);
        return result;
    }
    scope->locals[scope->count++] = local;
    return WSH_OK;
}

/** Restore and destroy the current dynamic scope. */
static wsh_result eval_scope_pop(wsh_evaluator *evaluator)
{
    eval_scope *scope;
    size_t index;
    wsh_string_view name;
    const wsh_value *ignored;
    wsh_result result;
    wsh_result restore_result;

    scope = evaluator->scope;
    if (scope == NULL) {
        return WSH_ERR_INTERNAL;
    }
    evaluator->scope = scope->previous;
    restore_result = WSH_OK;
    index = scope->count;
    while (index != 0U) {
        index -= 1U;
        name = wsh_string_bytes(scope->locals[index].name);
        if (wsh_context_get_variable(
                evaluator->context, name, &ignored) == WSH_OK) {
            (void)wsh_context_unset_variable(evaluator->context, name);
        }
        if (scope->locals[index].existed) {
            result = wsh_context_set_variable(
                evaluator->context,
                name,
                scope->locals[index].value);
            if (result == WSH_OK && scope->locals[index].exported) {
                result = wsh_context_set_exported(
                    evaluator->context, name, 1);
            }
            if (restore_result == WSH_OK && result != WSH_OK) {
                restore_result = result;
            }
        }
        eval_local_destroy(&scope->locals[index]);
    }
    evaluator->allocator.deallocate(
        evaluator->allocator.user_data, scope->locals);
    evaluator->allocator.deallocate(evaluator->allocator.user_data, scope);
    return restore_result;
}

/** Find one persistent function by exact case-sensitive name. */
static size_t eval_find_function(
    const wsh_evaluator *evaluator,
    wsh_string_view name)
{
    size_t index;

    for (index = 0U; index < evaluator->function_count; ++index) {
        if (wsh_string_view_equal(
                wsh_string_bytes(evaluator->functions[index].name), name)) {
            return index;
        }
    }
    return evaluator->function_count;
}

/** Destroy one persistent function record. */
static void eval_function_destroy(
    wsh_evaluator *evaluator,
    eval_function *function)
{
    wsh_string_destroy(function->name);
    eval_node_destroy(evaluator, function->body);
    memset(function, 0, sizeof(*function));
}

/** Define, replace, or remove one persistent function. */
static wsh_result eval_define_function(
    wsh_evaluator *evaluator,
    const eval_node *node)
{
    size_t index;
    wsh_string *name;
    eval_node *body;
    wsh_result result;

    index = eval_find_function(evaluator, eval_node_text(node));
    if (node->child_count == 0U) {
        if (index < evaluator->function_count) {
            eval_function_destroy(evaluator, &evaluator->functions[index]);
            if (index + 1U < evaluator->function_count) {
                memmove(
                    &evaluator->functions[index],
                    &evaluator->functions[index + 1U],
                    (evaluator->function_count - index - 1U) *
                        sizeof(*evaluator->functions));
            }
            evaluator->function_count -= 1U;
        }
        return WSH_OK;
    }
    if (node->child_count != 1U) {
        return WSH_ERR_INTERNAL;
    }
    name = NULL;
    body = NULL;
    result = wsh_string_create(
        &evaluator->allocator,
        &evaluator->limits,
        eval_node_text(node),
        &name);
    if (result == WSH_OK) {
        result = eval_node_clone(evaluator, node->children[0], &body);
    }
    if (result != WSH_OK) {
        wsh_string_destroy(name);
        eval_node_destroy(evaluator, body);
        return result;
    }
    if (index < evaluator->function_count) {
        eval_function_destroy(evaluator, &evaluator->functions[index]);
        evaluator->functions[index].name = name;
        evaluator->functions[index].body = body;
        return WSH_OK;
    }
    result = eval_grow(
        evaluator,
        (void **)&evaluator->functions,
        sizeof(*evaluator->functions),
        evaluator->function_count,
        &evaluator->function_capacity,
        evaluator->function_count + 1U,
        evaluator->limits.max_variables);
    if (result != WSH_OK) {
        wsh_string_destroy(name);
        eval_node_destroy(evaluator, body);
        return result;
    }
    evaluator->functions[evaluator->function_count].name = name;
    evaluator->functions[evaluator->function_count].body = body;
    evaluator->function_count += 1U;
    return WSH_OK;
}

/** One prepared simple-command assignment. */
typedef struct eval_assignment {
    /** Owned exact assignment name. */
    wsh_string *name;
    /** Owned fully expanded value. */
    wsh_value *value;
} eval_assignment;

/** Destroy one prepared assignment. */
static void eval_assignment_destroy(eval_assignment *assignment)
{
    if (assignment == NULL) {
        return;
    }
    wsh_string_destroy(assignment->name);
    wsh_value_destroy(assignment->value);
    memset(assignment, 0, sizeof(*assignment));
}

/** Expand an assignment fully without committing context state. */
static wsh_result eval_prepare_assignment(
    wsh_evaluator *evaluator,
    const eval_node *node,
    eval_assignment *out_assignment)
{
    eval_words words;
    wsh_result result;

    if (node->kind != WSH_AST_ASSIGNMENT || node->child_count != 1U) {
        return WSH_ERR_INTERNAL;
    }
    memset(out_assignment, 0, sizeof(*out_assignment));
    result = wsh_string_create(
        &evaluator->allocator,
        &evaluator->limits,
        eval_node_text(node),
        &out_assignment->name);
    if (result != WSH_OK) {
        return result;
    }
    eval_words_init(evaluator, &words);
    result = eval_expand_expression(
        evaluator, node->children[0], &words);
    if (result == WSH_OK) {
        result = eval_words_value(evaluator, &words, &out_assignment->value);
    }
    eval_words_destroy(&words);
    if (result != WSH_OK) {
        eval_assignment_destroy(out_assignment);
    }
    return result;
}

/** Create one immutable flat value from a suffix of expanded words. */
static wsh_result eval_words_suffix_value(
    wsh_evaluator *evaluator,
    const eval_words *words,
    size_t first,
    wsh_value **out_value)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_result result;

    *out_value = NULL;
    result = wsh_value_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    for (index = first; result == WSH_OK && index < words->count; ++index) {
        result = wsh_value_builder_append(
            builder, wsh_string_bytes(words->items[index].text));
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_value);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Parse and evaluate strict UTF-8 in the existing evaluator context. */
static wsh_result eval_text(
    wsh_evaluator *evaluator,
    wsh_string_view text,
    wsh_status_list **out_status)
{
    wsh_source *source;
    wsh_parse_tree *tree;
    wsh_parser_options parser_options;
    eval_node *root;
    wsh_result result;

    *out_status = NULL;
    source = NULL;
    tree = NULL;
    root = NULL;
    result = wsh_source_create(
        &evaluator->allocator,
        &evaluator->limits,
        (const unsigned char *)text.data,
        text.length,
        &source);
    if (result != WSH_OK) {
        return result;
    }
    wsh_parser_options_init(&parser_options);
    parser_options.allocator = evaluator->allocator;
    result = wsh_parse(&parser_options, source, &tree);
    if (result == WSH_OK &&
        wsh_parse_tree_status(tree) != WSH_SYNTAX_COMPLETE) {
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_EVALUATION,
            "eval or source text is not a complete valid command",
            NULL);
        result = WSH_ERR_INVALID;
    }
    if (result == WSH_OK) {
        result = eval_node_copy_ast(
            evaluator, wsh_parse_tree_root(tree), &root);
    }
    if (result == WSH_OK) {
        result = eval_execute(evaluator, root, out_status);
    }
    eval_node_destroy(evaluator, root);
    wsh_parse_tree_destroy(tree);
    wsh_source_destroy(source);
    return result;
}

/** Append value bytes to the active command-substitution capture. */
static wsh_result eval_capture_value(
    wsh_evaluator *evaluator,
    const wsh_value *value)
{
    size_t index;
    wsh_string_view text;
    wsh_result result;

    if (evaluator->capture == NULL) {
        return WSH_OK;
    }
    for (index = 0U; index < wsh_value_count(value); ++index) {
        result = wsh_value_at(value, index, &text);
        if (result == WSH_OK) {
            result = wsh_string_builder_append(evaluator->capture, text);
        }
        if (result != WSH_OK) {
            return result;
        }
    }
    return WSH_OK;
}

/** Invoke one abstract runtime request and require a nonempty status. */
static wsh_result eval_runtime(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_runtime_operation operation,
    wsh_string_view subject,
    const wsh_value *arguments,
    int capture_output,
    wsh_status_list **out_status)
{
    wsh_runtime_request request;
    wsh_value *output;
    wsh_result result;

    memset(&request, 0, sizeof(request));
    request.operation = operation;
    request.subject = subject;
    request.arguments = arguments;
    output = NULL;
    *out_status = NULL;
    result = wsh_context_runtime_invoke(
        evaluator->context, &request, &output, out_status);
    if (result != WSH_OK) {
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_RUNTIME,
            "abstract runtime request failed",
            node);
    }
    if (result == WSH_OK && wsh_status_list_count(*out_status) == 0U) {
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_RUNTIME,
            "abstract runtime returned an empty status",
            node);
        result = WSH_ERR_MISMATCH;
    }
    if (result == WSH_OK && capture_output) {
        result = eval_capture_value(evaluator, output);
    }
    wsh_value_destroy(output);
    if (result != WSH_OK) {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
    }
    return result;
}

/** Discard the current rollback frame after a successful commit. */
static void eval_scope_commit(wsh_evaluator *evaluator)
{
    eval_scope *scope;
    size_t index;

    scope = evaluator->scope;
    if (scope == NULL) {
        return;
    }
    evaluator->scope = scope->previous;
    for (index = 0U; index < scope->count; ++index) {
        eval_local_destroy(&scope->locals[index]);
    }
    evaluator->allocator.deallocate(
        evaluator->allocator.user_data, scope->locals);
    evaluator->allocator.deallocate(evaluator->allocator.user_data, scope);
}

/** Compare a borrowed word with one literal command name. */
static int eval_word_is(const eval_words *words, size_t index, const char *text)
{
    if (words == NULL || index >= words->count) {
        return 0;
    }
    return wsh_string_view_equal(
        wsh_string_bytes(words->items[index].text),
        wsh_string_view_from_cstr(text));
}

/** Parse one unsigned Windows status in canonical decimal form. */
static int eval_parse_status(wsh_string_view text, uint32_t *out_status)
{
    uint32_t value;
    size_t index;
    unsigned digit;

    if (text.length == 0U) {
        return 0;
    }
    value = 0U;
    for (index = 0U; index < text.length; ++index) {
        if (text.data[index] < '0' || text.data[index] > '9') {
            return 0;
        }
        digit = (unsigned)(text.data[index] - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return 0;
        }
        value = value * 10U + digit;
    }
    *out_status = value;
    return 1;
}

/** Create and assign an empty-list value. */
static wsh_result eval_set_empty_variable(
    wsh_evaluator *evaluator,
    wsh_string_view name)
{
    wsh_value_builder *builder;
    wsh_value *value;
    wsh_result result;

    value = NULL;
    result = wsh_value_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, &value);
    }
    wsh_value_builder_destroy(builder);
    if (result == WSH_OK) {
        result = wsh_context_set_variable(evaluator->context, name, value);
    }
    wsh_value_destroy(value);
    return result;
}

/** Execute `echo` through one mediated write request. */
static wsh_result eval_builtin_echo(
    wsh_evaluator *evaluator,
    const eval_node *node,
    const eval_words *words,
    wsh_status_list **out_status)
{
    size_t first;
    size_t index;
    int newline;
    wsh_string_builder *builder;
    wsh_string *rendered;
    wsh_value *arguments;
    wsh_result result;

    first = 1U;
    newline = 1;
    if (eval_word_is(words, first, "-n")) {
        first += 1U;
        newline = 0;
    }
    result = wsh_string_builder_create(
        &evaluator->allocator,
        &evaluator->limits,
        &builder);
    for (index = first; result == WSH_OK && index < words->count; ++index) {
        if (index != first) {
            result = wsh_string_builder_append(
                builder, wsh_string_view_from_cstr(" "));
        }
        if (result == WSH_OK) {
            result = wsh_string_builder_append(
                builder, wsh_string_bytes(words->items[index].text));
        }
    }
    if (result == WSH_OK && newline) {
        result = wsh_string_builder_append(
            builder, wsh_string_view_from_cstr("\n"));
    }
    rendered = NULL;
    if (result == WSH_OK) {
        result = wsh_string_builder_finish(builder, &rendered);
    }
    wsh_string_builder_destroy(builder);
    arguments = NULL;
    if (result == WSH_OK) {
        eval_words rendered_words;

        eval_words_init(evaluator, &rendered_words);
        result = eval_words_append(
            evaluator,
            &rendered_words,
            wsh_string_bytes(rendered),
            0);
        if (result == WSH_OK) {
            result = eval_words_value(
                evaluator, &rendered_words, &arguments);
        }
        eval_words_destroy(&rendered_words);
    }
    if (result == WSH_OK && evaluator->capture != NULL) {
        result = wsh_string_builder_append(
            evaluator->capture, wsh_string_bytes(rendered));
    }
    if (result == WSH_OK) {
        result = eval_runtime(
            evaluator,
            node,
            WSH_RUNTIME_WRITE,
            wsh_string_view_from_cstr("stdout"),
            arguments,
            0,
            out_status);
    }
    wsh_value_destroy(arguments);
    wsh_string_destroy(rendered);
    return result;
}

/** Evaluate a source file returned by the abstract runtime. */
static wsh_result eval_builtin_source(
    wsh_evaluator *evaluator,
    const eval_node *node,
    const eval_words *words,
    wsh_status_list **out_status)
{
    wsh_runtime_request request;
    wsh_value *arguments;
    wsh_value *output;
    wsh_status_list *read_status;
    wsh_string_view source_text;
    wsh_result result;
    wsh_result restore_result;
    int pushed_scope;

    if (words->count < 2U) {
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_EVALUATION,
            "source requires one source name", node);
        return WSH_ERR_INVALID;
    }
    arguments = NULL;
    result = eval_words_suffix_value(evaluator, words, 2U, &arguments);
    memset(&request, 0, sizeof(request));
    request.operation = WSH_RUNTIME_READ_SOURCE;
    request.subject = wsh_string_bytes(words->items[1].text);
    request.arguments = arguments;
    output = NULL;
    read_status = NULL;
    pushed_scope = 0;
    if (result == WSH_OK) {
        result = wsh_context_runtime_invoke(
            evaluator->context, &request, &output, &read_status);
    }
    if (result == WSH_OK && wsh_status_list_count(read_status) == 0U) {
        result = WSH_ERR_MISMATCH;
    }
    if (result == WSH_OK && !wsh_status_list_is_success(read_status)) {
        *out_status = read_status;
        read_status = NULL;
        wsh_value_destroy(arguments);
        wsh_value_destroy(output);
        return WSH_OK;
    }
    if (result == WSH_OK && wsh_value_count(output) != 1U) {
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_RUNTIME,
            "source runtime must return exactly one UTF-8 text value", node);
        result = WSH_ERR_MISMATCH;
    }
    if (result == WSH_OK) {
        result = wsh_value_at(output, 0U, &source_text);
    }
    if (result == WSH_OK) {
        result = eval_scope_push(evaluator);
        if (result == WSH_OK) {
            pushed_scope = 1;
        }
    }
    if (result == WSH_OK) {
        result = eval_scope_remember(
            evaluator, wsh_string_view_from_cstr("*"));
    }
    if (result == WSH_OK) {
        result = wsh_context_set_variable(
            evaluator->context,
            wsh_string_view_from_cstr("*"),
            arguments);
    }
    if (result == WSH_OK) {
        evaluator->source_depth += 1U;
        result = eval_text(evaluator, source_text, out_status);
        evaluator->source_depth -= 1U;
        if (evaluator->signal == EVAL_SIGNAL_RETURN) {
            evaluator->signal = EVAL_SIGNAL_NONE;
            if (evaluator->signal_status != NULL) {
                wsh_status_list_destroy(*out_status);
                *out_status = evaluator->signal_status;
                evaluator->signal_status = NULL;
            }
        }
    }
    if (pushed_scope) {
        restore_result = eval_scope_pop(evaluator);
        if (result == WSH_OK) {
            result = restore_result;
        }
    }
    wsh_value_destroy(arguments);
    wsh_value_destroy(output);
    wsh_status_list_destroy(read_status);
    if (result != WSH_OK) {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
    }
    return result;
}

/** Evaluate text supplied to the `eval` builtin. */
static wsh_result eval_builtin_eval(
    wsh_evaluator *evaluator,
    const eval_words *words,
    wsh_status_list **out_status)
{
    wsh_string_builder *builder;
    wsh_string *text;
    size_t index;
    wsh_result result;

    result = wsh_string_builder_create(
        &evaluator->allocator, &evaluator->limits, &builder);
    for (index = 1U; result == WSH_OK && index < words->count; ++index) {
        if (index != 1U) {
            result = wsh_string_builder_append(
                builder, wsh_string_view_from_cstr(" "));
        }
        if (result == WSH_OK) {
            result = wsh_string_builder_append(
                builder, wsh_string_bytes(words->items[index].text));
        }
    }
    text = NULL;
    if (result == WSH_OK) {
        result = wsh_string_builder_finish(builder, &text);
    }
    wsh_string_builder_destroy(builder);
    if (result == WSH_OK) {
        result = eval_text(evaluator, wsh_string_bytes(text), out_status);
    }
    wsh_string_destroy(text);
    return result;
}

/** Execute the `local` builtin using prepared assignment syntax. */
static wsh_result eval_builtin_local(
    wsh_evaluator *evaluator,
    const eval_node *node,
    const eval_words *words,
    eval_assignment *assignments,
    size_t assignment_count,
    wsh_status_list **out_status)
{
    size_t index;
    wsh_string_view name;
    const wsh_value *ignored;
    wsh_result result;

    if (evaluator->scope == NULL) {
        return WSH_ERR_INTERNAL;
    }
    result = WSH_OK;
    for (index = 0U; result == WSH_OK && index < assignment_count; ++index) {
        name = wsh_string_bytes(assignments[index].name);
        result = eval_scope_remember(evaluator, name);
        if (result == WSH_OK) {
            result = wsh_context_set_variable(
                evaluator->context, name, assignments[index].value);
        }
    }
    for (index = 1U; result == WSH_OK && index < words->count; ++index) {
        name = wsh_string_bytes(words->items[index].text);
        result = eval_scope_remember(evaluator, name);
        if (result == WSH_OK &&
            wsh_context_get_variable(
                evaluator->context, name, &ignored) != WSH_OK) {
            result = eval_set_empty_variable(evaluator, name);
        }
    }
    if (result != WSH_OK) {
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_EVALUATION,
            "local binding could not be established", node);
        return result;
    }
    return eval_status_one(evaluator, 0U, out_status);
}

/** Execute a built-in command when recognized. */
static wsh_result eval_builtin(
    wsh_evaluator *evaluator,
    const eval_node *node,
    const eval_words *words,
    eval_assignment *assignments,
    size_t assignment_count,
    int *out_handled,
    wsh_status_list **out_status)
{
    size_t index;
    wsh_string_view name;
    const wsh_value *ignored;
    wsh_result result;
    uint32_t code;
    int matched;
    wsh_value *value;
    wsh_value_builder *builder;
    wsh_string_view item;
    size_t shift;

    *out_handled = 1;
    if (eval_word_is(words, 0U, "echo")) {
        return eval_builtin_echo(evaluator, node, words, out_status);
    }
    if (eval_word_is(words, 0U, "local")) {
        return eval_builtin_local(
            evaluator, node, words, assignments,
            assignment_count, out_status);
    }
    if (eval_word_is(words, 0U, "export") ||
        eval_word_is(words, 0U, "unexport")) {
        matched = eval_word_is(words, 0U, "export");
        for (index = 1U; index < words->count; ++index) {
            name = wsh_string_bytes(words->items[index].text);
            if (wsh_context_get_variable(
                    evaluator->context, name, &ignored) != WSH_OK) {
                result = eval_set_empty_variable(evaluator, name);
                if (result != WSH_OK) {
                    return result;
                }
            }
            result = wsh_context_set_exported(
                evaluator->context, name, matched);
            if (result != WSH_OK) {
                eval_diagnostic(
                    evaluator,
                    WSH_DIAGNOSTIC_EXPORT_COLLISION,
                    "exported variable name collides under Windows identity",
                    node);
                return result;
            }
        }
        return eval_status_one(evaluator, 0U, out_status);
    }
    if (eval_word_is(words, 0U, "unset")) {
        for (index = 1U; index < words->count; ++index) {
            name = wsh_string_bytes(words->items[index].text);
            if (wsh_context_get_variable(
                    evaluator->context, name, &ignored) == WSH_OK) {
                (void)wsh_context_unset_variable(evaluator->context, name);
            }
        }
        return eval_status_one(evaluator, 0U, out_status);
    }
    if (eval_word_is(words, 0U, "~")) {
        if (words->count < 3U) {
            return eval_status_one(evaluator, 1U, out_status);
        }
        matched = 0;
        for (index = 2U; index < words->count && !matched; ++index) {
            matched = wsh_pattern_matches(
                wsh_string_bytes(words->items[index].text),
                wsh_string_bytes(words->items[1].text));
        }
        return eval_status_one(evaluator, matched ? 0U : 1U, out_status);
    }
    if (eval_word_is(words, 0U, "break") ||
        eval_word_is(words, 0U, "continue")) {
        if (words->count != 1U || evaluator->loop_depth == 0U) {
            eval_diagnostic(
                evaluator, WSH_DIAGNOSTIC_CONTROL,
                "loop transfer used outside an active loop", node);
            return WSH_ERR_INVALID;
        }
        evaluator->signal = eval_word_is(words, 0U, "break") ?
            EVAL_SIGNAL_BREAK : EVAL_SIGNAL_CONTINUE;
        return eval_status_one(evaluator, 0U, out_status);
    }
    if (eval_word_is(words, 0U, "return")) {
        if (evaluator->function_depth == 0U && evaluator->source_depth == 0U) {
            eval_diagnostic(
                evaluator, WSH_DIAGNOSTIC_CONTROL,
                "return used outside a function or sourced file", node);
            return WSH_ERR_INVALID;
        }
        if (words->count > 2U ||
            (words->count == 2U &&
             !eval_parse_status(
                wsh_string_bytes(words->items[1].text), &code))) {
            eval_diagnostic(
                evaluator, WSH_DIAGNOSTIC_CONTROL,
                "return accepts at most one unsigned decimal status", node);
            return WSH_ERR_INVALID;
        }
        evaluator->signal = EVAL_SIGNAL_RETURN;
        wsh_status_list_destroy(evaluator->signal_status);
        evaluator->signal_status = NULL;
        if (words->count == 2U) {
            result = eval_status_one(
                evaluator, code, &evaluator->signal_status);
        } else {
            result = eval_get_variable(
                evaluator, wsh_string_view_from_cstr("status"), &value);
            if (result == WSH_OK && wsh_value_count(value) != 0U &&
                wsh_value_at(value, 0U, &item) == WSH_OK &&
                eval_parse_status(item, &code)) {
                result = eval_status_one(
                    evaluator, code, &evaluator->signal_status);
            } else if (result == WSH_OK) {
                result = eval_status_one(
                    evaluator, 0U, &evaluator->signal_status);
            }
            wsh_value_destroy(value);
        }
        if (result != WSH_OK) {
            evaluator->signal = EVAL_SIGNAL_NONE;
            return result;
        }
        return eval_status_clone(
            evaluator, evaluator->signal_status, out_status);
    }
    if (eval_word_is(words, 0U, "shift")) {
        shift = 1U;
        if (words->count > 2U ||
            (words->count == 2U &&
             !eval_parse_index(
                wsh_string_bytes(words->items[1].text), &shift))) {
            return WSH_ERR_INVALID;
        }
        result = eval_get_variable(
            evaluator, wsh_string_view_from_cstr("*"), &value);
        if (result != WSH_OK) {
            return result;
        }
        result = wsh_value_builder_create(
            &evaluator->allocator, &evaluator->limits, &builder);
        for (index = shift; result == WSH_OK &&
             index < wsh_value_count(value); ++index) {
            result = wsh_value_at(value, index, &item);
            if (result == WSH_OK) {
                result = wsh_value_builder_append(builder, item);
            }
        }
        wsh_value_destroy(value);
        value = NULL;
        if (result == WSH_OK) {
            result = wsh_value_builder_finish(builder, &value);
        }
        wsh_value_builder_destroy(builder);
        if (result == WSH_OK) {
            result = wsh_context_set_variable(
                evaluator->context,
                wsh_string_view_from_cstr("*"),
                value);
        }
        wsh_value_destroy(value);
        if (result != WSH_OK) {
            return result;
        }
        return eval_status_one(evaluator, 0U, out_status);
    }
    if (eval_word_is(words, 0U, "source") ||
        eval_word_is(words, 0U, ".")) {
        return eval_builtin_source(evaluator, node, words, out_status);
    }
    if (eval_word_is(words, 0U, "eval")) {
        return eval_builtin_eval(evaluator, words, out_status);
    }
    *out_handled = 0;
    return WSH_OK;
}

/** Validate scalar ordinary-program exports before a launch request. */
static wsh_result eval_validate_exports(
    wsh_evaluator *evaluator,
    const eval_node *node)
{
    size_t index;
    wsh_string_view name;
    const wsh_value *value;
    int exported;
    wsh_result result;

    for (index = 0U; index <
         wsh_context_variable_count(evaluator->context); ++index) {
        result = wsh_context_variable_at(
            evaluator->context, index, &name, &value, &exported);
        if (result != WSH_OK) {
            return result;
        }
        if (exported && wsh_value_count(value) != 1U &&
            !wsh_string_view_equal(
                name, wsh_string_view_from_cstr("path"))) {
            eval_diagnostic(
                evaluator, WSH_DIAGNOSTIC_EVALUATION,
                "ordinary external launch requires scalar exported values",
                node);
            return WSH_ERR_MISMATCH;
        }
    }
    return WSH_OK;
}

/** Call one persistent function with a saved local `$*`. */
static wsh_result eval_call_function(
    wsh_evaluator *evaluator,
    const eval_node *call_node,
    size_t function_index,
    const eval_words *words,
    wsh_status_list **out_status)
{
    wsh_value *arguments;
    wsh_result result;
    wsh_result restore_result;
    int pushed_scope;

    arguments = NULL;
    pushed_scope = 0;
    result = eval_words_suffix_value(evaluator, words, 1U, &arguments);
    if (result == WSH_OK) {
        result = eval_scope_push(evaluator);
        if (result == WSH_OK) {
            pushed_scope = 1;
        }
    }
    if (result == WSH_OK) {
        result = eval_scope_remember(
            evaluator, wsh_string_view_from_cstr("*"));
    }
    if (result == WSH_OK) {
        result = wsh_context_set_variable(
            evaluator->context,
            wsh_string_view_from_cstr("*"),
            arguments);
    }
    if (result == WSH_OK) {
        evaluator->function_depth += 1U;
        result = eval_execute(
            evaluator,
            evaluator->functions[function_index].body,
            out_status);
        evaluator->function_depth -= 1U;
        if (evaluator->signal == EVAL_SIGNAL_RETURN) {
            evaluator->signal = EVAL_SIGNAL_NONE;
            if (evaluator->signal_status != NULL) {
                wsh_status_list_destroy(*out_status);
                *out_status = evaluator->signal_status;
                evaluator->signal_status = NULL;
            }
        }
    }
    if (pushed_scope) {
        restore_result = eval_scope_pop(evaluator);
        if (result == WSH_OK) {
            result = restore_result;
        }
    }
    wsh_value_destroy(arguments);
    if (result != WSH_OK) {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_EVALUATION,
            "function evaluation failed", call_node);
    }
    return result;
}

/** Evaluate one fully prepared simple command. */
static wsh_result eval_simple_command(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    eval_assignment *assignments;
    size_t assignment_count;
    size_t assignment_index;
    eval_words words;
    size_t index;
    size_t prepared;
    wsh_result result;
    wsh_result restore_result;
    int temporary_scope;
    int handled;
    size_t function_index;
    wsh_value *arguments;
    wsh_string_view subject;

    *out_status = NULL;
    assignment_count = 0U;
    for (index = 0U; index < node->child_count; ++index) {
        if (node->children[index]->kind == WSH_AST_REDIRECTION) {
            eval_diagnostic(
                evaluator,
                WSH_DIAGNOSTIC_EVALUATION,
                "redirection belongs to M5 runtime orchestration",
                node->children[index]);
            return WSH_ERR_INVALID;
        }
        if (node->children[index]->kind == WSH_AST_ASSIGNMENT) {
            assignment_count += 1U;
        }
    }
    assignments = NULL;
    if (assignment_count != 0U) {
        assignments = (eval_assignment *)eval_allocate(
            evaluator, assignment_count * sizeof(*assignments));
        if (assignments == NULL) {
            return WSH_ERR_RESOURCE;
        }
    }
    prepared = 0U;
    eval_words_init(evaluator, &words);
    result = WSH_OK;
    for (index = 0U; result == WSH_OK && index < node->child_count; ++index) {
        if (node->children[index]->kind == WSH_AST_ASSIGNMENT) {
            result = eval_prepare_assignment(
                evaluator,
                node->children[index],
                &assignments[prepared]);
            if (result == WSH_OK) {
                prepared += 1U;
            }
        } else {
            result = eval_expand_expression(
                evaluator, node->children[index], &words);
        }
    }
    if (result == WSH_OK && words.count != 0U &&
        !eval_word_is(&words, 0U, "~")) {
        result = eval_glob_words(evaluator, node, &words);
    }
    if (result != WSH_OK) {
        goto cleanup;
    }

    if (words.count == 0U) {
        result = eval_scope_push(evaluator);
        temporary_scope = result == WSH_OK;
        for (assignment_index = 0U; result == WSH_OK &&
             assignment_index < assignment_count; ++assignment_index) {
            result = eval_scope_remember(
                evaluator,
                wsh_string_bytes(assignments[assignment_index].name));
            if (result == WSH_OK) {
                result = wsh_context_set_variable(
                    evaluator->context,
                    wsh_string_bytes(assignments[assignment_index].name),
                    assignments[assignment_index].value);
            }
        }
        if (result == WSH_OK) {
            eval_scope_commit(evaluator);
            temporary_scope = 0;
            result = eval_status_one(evaluator, 0U, out_status);
        }
        if (temporary_scope) {
            (void)eval_scope_pop(evaluator);
        }
        goto cleanup;
    }

    if (eval_word_is(&words, 0U, "local")) {
        result = eval_builtin(
            evaluator, node, &words, assignments,
            assignment_count, &handled, out_status);
        goto cleanup;
    }

    temporary_scope = 0;
    if (assignment_count != 0U) {
        result = eval_scope_push(evaluator);
        if (result == WSH_OK) {
            temporary_scope = 1;
        }
        for (assignment_index = 0U; result == WSH_OK &&
             assignment_index < assignment_count; ++assignment_index) {
            result = eval_scope_remember(
                evaluator,
                wsh_string_bytes(assignments[assignment_index].name));
            if (result == WSH_OK) {
                result = wsh_context_set_variable(
                    evaluator->context,
                    wsh_string_bytes(assignments[assignment_index].name),
                    assignments[assignment_index].value);
            }
        }
    }
    if (result == WSH_OK) {
        subject = wsh_string_bytes(words.items[0].text);
        function_index = eval_find_function(evaluator, subject);
        if (function_index < evaluator->function_count) {
            result = eval_call_function(
                evaluator, node, function_index, &words, out_status);
        } else {
            result = eval_builtin(
                evaluator, node, &words, assignments,
                assignment_count, &handled, out_status);
            if (result == WSH_OK && !handled) {
                result = eval_validate_exports(evaluator, node);
                arguments = NULL;
                if (result == WSH_OK) {
                    result = eval_words_suffix_value(
                        evaluator, &words, 1U, &arguments);
                }
                if (result == WSH_OK) {
                    result = eval_runtime(
                        evaluator,
                        node,
                        WSH_RUNTIME_LAUNCH,
                        subject,
                        arguments,
                        1,
                        out_status);
                }
                wsh_value_destroy(arguments);
            }
        }
    }
    if (temporary_scope) {
        restore_result = eval_scope_pop(evaluator);
        if (result == WSH_OK) {
            result = restore_result;
        }
    }

cleanup:
    for (assignment_index = 0U; assignment_index < prepared;
         ++assignment_index) {
        eval_assignment_destroy(&assignments[assignment_index]);
    }
    evaluator->allocator.deallocate(
        evaluator->allocator.user_data, assignments);
    eval_words_destroy(&words);
    if (result == WSH_OK && *out_status != NULL) {
        (void)eval_publish_status(evaluator, *out_status);
    } else if (result != WSH_OK) {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
    }
    return result;
}

/** Clone variables and persistent functions for a semantic subshell. */
static wsh_result eval_subshell(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    wsh_context_options context_options;
    wsh_context *context;
    wsh_evaluator_options options;
    wsh_evaluator *child;
    size_t index;
    wsh_string_view name;
    const wsh_value *value;
    int exported;
    eval_function function;
    wsh_result result;

    if (node->child_count != 1U) {
        return WSH_ERR_INTERNAL;
    }
    context = NULL;
    child = NULL;
    result = wsh_context_get_options(evaluator->context, &context_options);
    if (result == WSH_OK) {
        result = wsh_context_create(&context_options, &context);
    }
    for (index = 0U; result == WSH_OK &&
         index < wsh_context_variable_count(evaluator->context); ++index) {
        result = wsh_context_variable_at(
            evaluator->context, index, &name, &value, &exported);
        if (result == WSH_OK) {
            result = wsh_context_set_variable(context, name, value);
        }
        if (result == WSH_OK && exported) {
            result = wsh_context_set_exported(context, name, 1);
        }
    }
    wsh_evaluator_options_init(&options);
    options.allocator = evaluator->allocator;
    options.limits = evaluator->limits;
    options.max_steps = evaluator->max_steps - evaluator->steps;
    if (options.max_steps == 0U) {
        options.max_steps = 1U;
    }
    options.max_depth = evaluator->max_depth;
    options.source_name = wsh_string_bytes(evaluator->source_name);
    if (result == WSH_OK) {
        result = wsh_evaluator_create(context, &options, &child);
    }
    for (index = 0U; result == WSH_OK &&
         index < evaluator->function_count; ++index) {
        memset(&function, 0, sizeof(function));
        result = wsh_string_create(
            &child->allocator,
            &child->limits,
            wsh_string_bytes(evaluator->functions[index].name),
            &function.name);
        if (result == WSH_OK) {
            result = eval_node_clone(
                child, evaluator->functions[index].body, &function.body);
        }
        if (result == WSH_OK) {
            result = eval_grow(
                child,
                (void **)&child->functions,
                sizeof(*child->functions),
                child->function_count,
                &child->function_capacity,
                child->function_count + 1U,
                child->limits.max_variables);
        }
        if (result == WSH_OK) {
            child->functions[child->function_count++] = function;
        } else {
            eval_function_destroy(child, &function);
        }
    }
    if (result == WSH_OK) {
        result = eval_scope_push(child);
    }
    if (result == WSH_OK) {
        result = eval_execute(child, node->children[0], out_status);
    }
    if (child != NULL && child->scope != NULL) {
        (void)eval_scope_pop(child);
    }
    if (child != NULL) {
        evaluator->steps += child->steps;
    }
    wsh_evaluator_destroy(child);
    wsh_context_destroy(context);
    if (result == WSH_OK) {
        (void)eval_publish_status(evaluator, *out_status);
    } else {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_EVALUATION,
            "semantic subshell evaluation failed", node);
    }
    return result;
}

/** Evaluate a braced block in a dynamic local-binding scope. */
static wsh_result eval_block(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    wsh_result result;
    wsh_result restore_result;
    int pushed_scope;

    if (node->child_count > 1U) {
        return WSH_ERR_INTERNAL;
    }
    pushed_scope = 0;
    result = eval_scope_push(evaluator);
    if (result == WSH_OK) {
        pushed_scope = 1;
    }
    if (result == WSH_OK && node->child_count == 0U) {
        result = eval_status_one(evaluator, 0U, out_status);
    } else if (result == WSH_OK) {
        result = eval_execute(evaluator, node->children[0], out_status);
    }
    if (pushed_scope) {
        restore_result = eval_scope_pop(evaluator);
        if (result == WSH_OK) {
            result = restore_result;
        }
    }
    return result;
}

/** Evaluate one if command. */
static wsh_result eval_if_command(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    wsh_status_list *condition;
    int success;
    wsh_result result;

    if (node->child_count < 2U || node->child_count > 3U) {
        return WSH_ERR_INTERNAL;
    }
    condition = NULL;
    result = eval_execute(evaluator, node->children[0], &condition);
    success = result == WSH_OK && wsh_status_list_is_success(condition);
    if (result == WSH_OK && success) {
        result = eval_execute(evaluator, node->children[1], out_status);
    } else if (result == WSH_OK && node->child_count == 3U) {
        result = eval_execute(evaluator, node->children[2], out_status);
    } else if (result == WSH_OK) {
        result = eval_status_clone(evaluator, condition, out_status);
    }
    wsh_status_list_destroy(condition);
    return result;
}

/** Evaluate one while command with bounded steps and loop transfer. */
static wsh_result eval_while_command(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    wsh_status_list *condition;
    wsh_status_list *body_status;
    wsh_result result;
    int success;

    if (node->child_count != 2U) {
        return WSH_ERR_INTERNAL;
    }
    result = eval_status_one(evaluator, 0U, out_status);
    evaluator->loop_depth += 1U;
    while (result == WSH_OK) {
        condition = NULL;
        result = eval_execute(evaluator, node->children[0], &condition);
        success = result == WSH_OK && wsh_status_list_is_success(condition);
        if (result != WSH_OK || !success) {
            if (result == WSH_OK) {
                wsh_status_list_destroy(*out_status);
                *out_status = condition;
                condition = NULL;
            }
            wsh_status_list_destroy(condition);
            break;
        }
        wsh_status_list_destroy(condition);
        body_status = NULL;
        result = eval_execute(evaluator, node->children[1], &body_status);
        if (result == WSH_OK) {
            wsh_status_list_destroy(*out_status);
            *out_status = body_status;
            body_status = NULL;
        }
        wsh_status_list_destroy(body_status);
        if (evaluator->signal == EVAL_SIGNAL_BREAK) {
            evaluator->signal = EVAL_SIGNAL_NONE;
            break;
        }
        if (evaluator->signal == EVAL_SIGNAL_CONTINUE) {
            evaluator->signal = EVAL_SIGNAL_NONE;
        } else if (evaluator->signal != EVAL_SIGNAL_NONE) {
            break;
        }
    }
    evaluator->loop_depth -= 1U;
    return result;
}

/** Evaluate one for command. */
static wsh_result eval_for_command(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    eval_words values;
    wsh_value *arguments;
    const eval_node *body;
    size_t index;
    wsh_value_builder *builder;
    wsh_value *single;
    wsh_result result;
    wsh_status_list *body_status;

    if (node->child_count == 0U) {
        return WSH_ERR_INTERNAL;
    }
    body = node->children[node->child_count - 1U];
    eval_words_init(evaluator, &values);
    result = WSH_OK;
    if (node->child_count == 1U) {
        arguments = NULL;
        result = eval_get_variable(
            evaluator, wsh_string_view_from_cstr("*"), &arguments);
        if (result == WSH_OK) {
            result = eval_words_append_value(
                evaluator, &values, arguments, 1);
        }
        wsh_value_destroy(arguments);
    } else {
        for (index = 0U; result == WSH_OK &&
             index + 1U < node->child_count; ++index) {
            result = eval_expand_expression(
                evaluator, node->children[index], &values);
        }
        if (result == WSH_OK) {
            result = eval_glob_words(evaluator, node, &values);
        }
    }
    if (result == WSH_OK) {
        result = eval_status_one(evaluator, 0U, out_status);
    }
    evaluator->loop_depth += 1U;
    for (index = 0U; result == WSH_OK && index < values.count; ++index) {
        single = NULL;
        result = wsh_value_builder_create(
            &evaluator->allocator, &evaluator->limits, &builder);
        if (result == WSH_OK) {
            result = wsh_value_builder_append(
                builder, wsh_string_bytes(values.items[index].text));
        }
        if (result == WSH_OK) {
            result = wsh_value_builder_finish(builder, &single);
        }
        wsh_value_builder_destroy(builder);
        if (result == WSH_OK) {
            result = wsh_context_set_variable(
                evaluator->context, eval_node_text(node), single);
        }
        wsh_value_destroy(single);
        body_status = NULL;
        if (result == WSH_OK) {
            result = eval_execute(evaluator, body, &body_status);
        }
        if (result == WSH_OK) {
            wsh_status_list_destroy(*out_status);
            *out_status = body_status;
            body_status = NULL;
        }
        wsh_status_list_destroy(body_status);
        if (evaluator->signal == EVAL_SIGNAL_BREAK) {
            evaluator->signal = EVAL_SIGNAL_NONE;
            break;
        }
        if (evaluator->signal == EVAL_SIGNAL_CONTINUE) {
            evaluator->signal = EVAL_SIGNAL_NONE;
        } else if (evaluator->signal != EVAL_SIGNAL_NONE) {
            break;
        }
    }
    evaluator->loop_depth -= 1U;
    eval_words_destroy(&values);
    return result;
}

/** Evaluate one switch command and its first matching case. */
static wsh_result eval_switch_command(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    eval_words subject_words;
    eval_words patterns;
    const eval_node *clause;
    size_t clause_index;
    size_t pattern_index;
    size_t pattern_count;
    int matched;
    wsh_result result;

    if (node->child_count == 0U) {
        return WSH_ERR_INTERNAL;
    }
    eval_words_init(evaluator, &subject_words);
    result = eval_expand_expression(
        evaluator, node->children[0], &subject_words);
    if (result != WSH_OK || subject_words.count != 1U) {
        eval_words_destroy(&subject_words);
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_EVALUATION,
            "switch subject must produce exactly one string", node);
        return result == WSH_OK ? WSH_ERR_MISMATCH : result;
    }
    result = eval_status_one(evaluator, 0U, out_status);
    for (clause_index = 1U; result == WSH_OK &&
         clause_index < node->child_count; ++clause_index) {
        clause = node->children[clause_index];
        if (clause->kind != WSH_AST_CASE) {
            result = WSH_ERR_INTERNAL;
            break;
        }
        pattern_count = clause->child_count;
        if (pattern_count != 0U &&
            clause->children[pattern_count - 1U]->kind ==
                WSH_AST_COMMAND_LIST) {
            pattern_count -= 1U;
        }
        matched = pattern_count == 0U;
        for (pattern_index = 0U; result == WSH_OK && !matched &&
             pattern_index < pattern_count; ++pattern_index) {
            eval_words_init(evaluator, &patterns);
            result = eval_expand_expression(
                evaluator, clause->children[pattern_index], &patterns);
            if (result == WSH_OK) {
                size_t item_index;
                for (item_index = 0U; item_index < patterns.count && !matched;
                     ++item_index) {
                    matched = wsh_pattern_matches(
                        wsh_string_bytes(patterns.items[item_index].text),
                        wsh_string_bytes(subject_words.items[0].text));
                }
            }
            eval_words_destroy(&patterns);
        }
        if (matched) {
            if (clause->child_count > pattern_count) {
                wsh_status_list_destroy(*out_status);
                *out_status = NULL;
                result = eval_execute(
                    evaluator,
                    clause->children[clause->child_count - 1U],
                    out_status);
            }
            break;
        }
    }
    eval_words_destroy(&subject_words);
    return result;
}

/** Evaluate one copied semantic node. */
static wsh_result eval_execute(
    wsh_evaluator *evaluator,
    const eval_node *node,
    wsh_status_list **out_status)
{
    wsh_result result;
    wsh_status_list *left;
    wsh_status_list *right;
    size_t index;
    int evaluate_right;

    if (evaluator == NULL || node == NULL || out_status == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_status = NULL;
    result = eval_enter(evaluator, node);
    if (result != WSH_OK) {
        return result;
    }
    switch (node->kind) {
    case WSH_AST_INPUT:
        result = node->child_count == 1U ?
            eval_execute(evaluator, node->children[0], out_status) :
            eval_status_one(evaluator, 0U, out_status);
        break;
    case WSH_AST_COMMAND_LIST:
        result = eval_status_one(evaluator, 0U, out_status);
        for (index = 0U; result == WSH_OK && index < node->child_count;
             ++index) {
            if (evaluator->signal != EVAL_SIGNAL_NONE) {
                break;
            }
            right = NULL;
            result = eval_execute(evaluator, node->children[index], &right);
            if (result == WSH_OK) {
                wsh_status_list_destroy(*out_status);
                *out_status = right;
                right = NULL;
            }
            wsh_status_list_destroy(right);
        }
        break;
    case WSH_AST_BLOCK:
        result = eval_block(evaluator, node, out_status);
        break;
    case WSH_AST_SIMPLE:
        result = eval_simple_command(evaluator, node, out_status);
        break;
    case WSH_AST_AND:
    case WSH_AST_OR:
        if (node->child_count != 2U) {
            result = WSH_ERR_INTERNAL;
            break;
        }
        left = NULL;
        result = eval_execute(evaluator, node->children[0], &left);
        evaluate_right = result == WSH_OK &&
            ((node->kind == WSH_AST_AND &&
              wsh_status_list_is_success(left)) ||
             (node->kind == WSH_AST_OR &&
              !wsh_status_list_is_success(left)));
        if (result == WSH_OK && evaluate_right) {
            result = eval_execute(evaluator, node->children[1], out_status);
        } else if (result == WSH_OK) {
            *out_status = left;
            left = NULL;
        }
        wsh_status_list_destroy(left);
        break;
    case WSH_AST_NEGATE:
        if (node->child_count != 1U) {
            result = WSH_ERR_INTERNAL;
            break;
        }
        left = NULL;
        result = eval_execute(evaluator, node->children[0], &left);
        if (result == WSH_OK) {
            result = eval_status_one(
                evaluator,
                wsh_status_list_is_success(left) ? 1U : 0U,
                out_status);
        }
        wsh_status_list_destroy(left);
        break;
    case WSH_AST_SUBSHELL:
        result = eval_subshell(evaluator, node, out_status);
        break;
    case WSH_AST_IF:
        result = eval_if_command(evaluator, node, out_status);
        break;
    case WSH_AST_WHILE:
        result = eval_while_command(evaluator, node, out_status);
        break;
    case WSH_AST_FOR:
        result = eval_for_command(evaluator, node, out_status);
        break;
    case WSH_AST_SWITCH:
        result = eval_switch_command(evaluator, node, out_status);
        break;
    case WSH_AST_FUNCTION:
        result = eval_define_function(evaluator, node);
        if (result == WSH_OK) {
            result = eval_status_one(evaluator, 0U, out_status);
        }
        break;
    case WSH_AST_PIPELINE:
    case WSH_AST_BACKGROUND:
    case WSH_AST_REDIRECTION:
    case WSH_AST_PIPE_OPERATOR:
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_EVALUATION,
            "pipeline, background, and redirection effects belong to M5",
            node);
        result = WSH_ERR_INVALID;
        break;
    default:
        eval_diagnostic(
            evaluator,
            WSH_DIAGNOSTIC_EVALUATION,
            "AST node cannot execute as a command",
            node);
        result = WSH_ERR_INVALID;
        break;
    }
    eval_leave(evaluator);
    if (result == WSH_OK && *out_status != NULL) {
        (void)eval_publish_status(evaluator, *out_status);
    } else if (result != WSH_OK) {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
    }
    return result;
}

/** @brief Implements wsh_evaluator_options_init. */
void wsh_evaluator_options_init(wsh_evaluator_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    memset(out_options, 0, sizeof(*out_options));
    out_options->allocator = wsh_allocator_default();
    out_options->limits = wsh_limits_default();
    out_options->max_steps = WSH_EVALUATOR_DEFAULT_STEPS;
    out_options->max_depth = WSH_EVALUATOR_DEFAULT_DEPTH;
    out_options->source_name = wsh_string_view_from_cstr("wsh");
}

/** @brief Implements wsh_evaluator_create. */
wsh_result wsh_evaluator_create(
    wsh_context *context,
    const wsh_evaluator_options *options,
    wsh_evaluator **out_evaluator)
{
    wsh_evaluator_options defaults;
    wsh_context_options context_options;
    wsh_evaluator *evaluator;
    wsh_result result;

    if (context == NULL || out_evaluator == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_evaluator = NULL;
    if (options == NULL) {
        wsh_evaluator_options_init(&defaults);
        result = wsh_context_get_options(context, &context_options);
        if (result != WSH_OK) {
            return result;
        }
        defaults.allocator = context_options.allocator;
        defaults.limits = context_options.limits;
        options = &defaults;
    }
    if (options->allocator.allocate == NULL ||
        options->allocator.deallocate == NULL ||
        options->max_steps == 0U || options->max_depth == 0U ||
        options->max_steps == (size_t)-1 ||
        options->max_depth == (size_t)-1) {
        return WSH_ERR_INVALID;
    }
    evaluator = (wsh_evaluator *)options->allocator.allocate(
        options->allocator.user_data, sizeof(*evaluator));
    if (evaluator == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(evaluator, 0, sizeof(*evaluator));
    evaluator->allocator = options->allocator;
    evaluator->limits = options->limits;
    evaluator->context = context;
    evaluator->max_steps = options->max_steps;
    evaluator->max_depth = options->max_depth;
    result = wsh_string_create(
        &evaluator->allocator,
        &evaluator->limits,
        options->source_name,
        &evaluator->source_name);
    if (result != WSH_OK) {
        evaluator->allocator.deallocate(
            evaluator->allocator.user_data, evaluator);
        return result;
    }
    *out_evaluator = evaluator;
    return WSH_OK;
}

/** @brief Implements wsh_evaluator_destroy. */
void wsh_evaluator_destroy(wsh_evaluator *evaluator)
{
    size_t index;

    if (evaluator == NULL) {
        return;
    }
    while (evaluator->scope != NULL) {
        (void)eval_scope_pop(evaluator);
    }
    for (index = 0U; index < evaluator->function_count; ++index) {
        eval_function_destroy(evaluator, &evaluator->functions[index]);
    }
    evaluator->allocator.deallocate(
        evaluator->allocator.user_data, evaluator->functions);
    wsh_status_list_destroy(evaluator->signal_status);
    wsh_string_destroy(evaluator->source_name);
    evaluator->allocator.deallocate(
        evaluator->allocator.user_data, evaluator);
}

/** @brief Implements wsh_evaluate. */
wsh_result wsh_evaluate(
    wsh_evaluator *evaluator,
    const wsh_parse_tree *tree,
    wsh_status_list **out_status)
{
    eval_node *root;
    wsh_result result;
    wsh_result restore_result;

    if (evaluator == NULL || tree == NULL || out_status == NULL ||
        wsh_parse_tree_status(tree) != WSH_SYNTAX_COMPLETE ||
        wsh_parse_tree_root(tree) == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_status = NULL;
    evaluator->steps = 0U;
    evaluator->depth = 0U;
    evaluator->signal = EVAL_SIGNAL_NONE;
    wsh_status_list_destroy(evaluator->signal_status);
    evaluator->signal_status = NULL;
    root = NULL;
    result = eval_node_copy_ast(
        evaluator, wsh_parse_tree_root(tree), &root);
    if (result == WSH_OK) {
        result = eval_scope_push(evaluator);
    }
    if (result == WSH_OK) {
        result = eval_execute(evaluator, root, out_status);
    }
    if (evaluator->scope != NULL) {
        restore_result = eval_scope_pop(evaluator);
        if (result == WSH_OK) {
            result = restore_result;
        }
    }
    if (result == WSH_OK && evaluator->signal != EVAL_SIGNAL_NONE) {
        eval_diagnostic(
            evaluator, WSH_DIAGNOSTIC_CONTROL,
            "control transfer escaped its legal dynamic context", root);
        result = WSH_ERR_INVALID;
    }
    if (result == WSH_OK) {
        (void)eval_publish_status(evaluator, *out_status);
    } else {
        wsh_status_list_destroy(*out_status);
        *out_status = NULL;
    }
    eval_node_destroy(evaluator, root);
    return result;
}
