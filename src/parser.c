/**
 * @file parser.c
 * @brief Portable WSH lexer, parser, and immutable AST implementation.
 */

#include "wsh/parser.h"

#include <stdint.h>
#include <string.h>

/** Internal owned token. */
typedef struct wsh_token_data {
    wsh_token_kind kind; /**< Token category. */
    char *text; /**< Owned primary text. */
    size_t text_length; /**< Primary text bytes. */
    char *auxiliary; /**< Owned here-document body. */
    size_t auxiliary_length; /**< Auxiliary text bytes. */
    size_t auxiliary_start; /**< Auxiliary source start. */
    size_t auxiliary_end; /**< Auxiliary source end. */
    wsh_source_span span; /**< Resolved source span. */
    size_t start; /**< Normalized UTF-8 start. */
    size_t end; /**< Normalized UTF-8 end. */
} wsh_token_data;

/** Internal owned syntax diagnostic. */
typedef struct wsh_syntax_diagnostic_data {
    wsh_syntax_diagnostic_code code; /**< Stable code. */
    char *message; /**< Owned message. */
    size_t message_length; /**< Message bytes. */
    wsh_source_span span; /**< Diagnostic source span. */
} wsh_syntax_diagnostic_data;

/** Immutable token-stream owner. */
struct wsh_token_stream {
    wsh_allocator allocator; /**< Object allocator. */
    wsh_parser_options options; /**< Applied limits. */
    wsh_syntax_status status; /**< Final lexical status. */
    wsh_token_data *tokens; /**< Owned token array. */
    size_t token_count; /**< Published token count. */
    size_t token_capacity; /**< Allocated token slots. */
    wsh_syntax_diagnostic_data *diagnostics; /**< Owned diagnostics. */
    size_t diagnostic_count; /**< Retained diagnostic count. */
    size_t diagnostic_capacity; /**< Allocated diagnostic slots. */
};

/** Immutable AST node. */
struct wsh_ast_node {
    wsh_ast_kind kind; /**< Node category. */
    wsh_source_span span; /**< Node source span. */
    char *text; /**< Owned primary text. */
    size_t text_length; /**< Primary text bytes. */
    char *auxiliary; /**< Owned auxiliary text. */
    size_t auxiliary_length; /**< Auxiliary text bytes. */
    struct wsh_ast_node **children; /**< Ordered borrowed children. */
    size_t child_count; /**< Child count. */
    size_t child_capacity; /**< Allocated child slots. */
};

/** Immutable parse-tree owner and node arena. */
struct wsh_parse_tree {
    wsh_allocator allocator; /**< Object allocator. */
    wsh_parser_options options; /**< Applied limits. */
    wsh_syntax_status status; /**< Final parse status. */
    wsh_ast_node *root; /**< Borrowed complete root. */
    wsh_ast_node **nodes; /**< Owned node arena. */
    size_t node_count; /**< Arena node count. */
    size_t node_capacity; /**< Allocated arena slots. */
    wsh_syntax_diagnostic_data *diagnostics; /**< Owned diagnostics. */
    size_t diagnostic_count; /**< Retained diagnostic count. */
    size_t diagnostic_capacity; /**< Allocated diagnostic slots. */
};

/** Mutable lexer state for one call. */
typedef struct wsh_lexer {
    const wsh_source *source; /**< Borrowed decoded source. */
    wsh_string_view text; /**< Borrowed normalized UTF-8. */
    wsh_token_stream *stream; /**< Result under construction. */
    size_t position; /**< Current normalized byte offset. */
    size_t *pending_here; /**< Marker token indices. */
    size_t pending_count; /**< Pending here-document count. */
    size_t pending_capacity; /**< Allocated marker slots. */
    int expect_here_marker; /**< A here marker is required. */
    int decoration_expected; /**< An adjacent decoration may follow. */
    size_t decoration_offset; /**< Required adjacent offset. */
} wsh_lexer;

/** Mutable recursive-descent parser state. */
typedef struct wsh_parser {
    const wsh_source *source; /**< Borrowed decoded source. */
    const wsh_token_stream *stream; /**< Borrowed tokens. */
    wsh_parse_tree *tree; /**< Result under construction. */
    size_t position; /**< Current token index. */
    size_t depth; /**< Current recursive depth. */
    wsh_result failure; /**< Resource or internal failure. */
    int stopped; /**< A syntax condition stopped parsing. */
} wsh_parser;

/** Return an empty borrowed string view. */
static wsh_string_view wsh_empty_view(void)
{
    wsh_string_view view;

    view.data = NULL;
    view.length = 0U;
    return view;
}

/** Check whether an addition fits in size_t. */
static int wsh_size_add(size_t left, size_t right, size_t *out_value)
{
    if (out_value == NULL || left > SIZE_MAX - right) {
        return 0;
    }
    *out_value = left + right;
    return 1;
}

/** Check whether a multiplication fits in size_t. */
static int wsh_size_multiply(
    size_t left,
    size_t right,
    size_t *out_value)
{
    if (out_value == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return 0;
    }
    *out_value = left * right;
    return 1;
}

/** Allocate a checked array through an injected allocator. */
static void *wsh_allocate_array(
    const wsh_allocator *allocator,
    size_t count,
    size_t item_size)
{
    size_t bytes;

    if (allocator == NULL || allocator->allocate == NULL ||
        !wsh_size_multiply(count, item_size, &bytes)) {
        return NULL;
    }
    if (bytes == 0U) {
        bytes = 1U;
    }
    return allocator->allocate(allocator->user_data, bytes);
}

/** Release memory through an injected allocator. */
static void wsh_release(
    const wsh_allocator *allocator,
    void *pointer)
{
    if (allocator != NULL && allocator->deallocate != NULL &&
        pointer != NULL) {
        allocator->deallocate(allocator->user_data, pointer);
    }
}

/** Copy bytes and append a convenience NUL terminator. */
static wsh_result wsh_copy_text(
    const wsh_allocator *allocator,
    const char *input,
    size_t length,
    char **out_text)
{
    char *copy;
    size_t allocation_size;

    if (out_text == NULL || (input == NULL && length != 0U) ||
        !wsh_size_add(length, 1U, &allocation_size)) {
        return WSH_ERR_INVALID;
    }
    *out_text = NULL;
    copy = (char *)wsh_allocate_array(allocator, allocation_size, 1U);
    if (copy == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (length != 0U) {
        memcpy(copy, input, length);
    }
    copy[length] = '\0';
    *out_text = copy;
    return WSH_OK;
}

/** Copy caller options or initialize defaults. */
static wsh_result wsh_copy_parser_options(
    const wsh_parser_options *input,
    wsh_parser_options *output)
{
    if (output == NULL) {
        return WSH_ERR_INVALID;
    }
    if (input == NULL) {
        wsh_parser_options_init(output);
    } else {
        *output = *input;
    }
    if (output->allocator.allocate == NULL ||
        output->allocator.deallocate == NULL ||
        output->max_parse_depth > WSH_PARSER_MAX_PARSE_DEPTH) {
        return WSH_ERR_INVALID;
    }
    return WSH_OK;
}

/** Resolve a normalized byte interval to a source span. */
static wsh_result wsh_resolve_span(
    const wsh_source *source,
    size_t start,
    size_t end,
    wsh_source_span *out_span)
{
    if (source == NULL || out_span == NULL || end < start) {
        return WSH_ERR_INVALID;
    }
    return wsh_source_get_span(source, start, end - start, out_span);
}

/** Grow a token array with allocate-copy-commit behavior. */
static wsh_result wsh_grow_tokens(wsh_token_stream *stream)
{
    wsh_token_data *replacement;
    size_t capacity;

    if (stream == NULL || stream->token_count >= stream->options.max_tokens) {
        return WSH_ERR_RESOURCE;
    }
    capacity = stream->token_capacity == 0U ? 16U :
        stream->token_capacity * 2U;
    if (capacity < stream->token_capacity ||
        capacity > stream->options.max_tokens) {
        capacity = stream->options.max_tokens;
    }
    if (capacity <= stream->token_capacity) {
        return WSH_ERR_RESOURCE;
    }
    replacement = (wsh_token_data *)wsh_allocate_array(
        &stream->allocator,
        capacity,
        sizeof(*replacement));
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(replacement, 0, capacity * sizeof(*replacement));
    if (stream->token_count != 0U) {
        memcpy(
            replacement,
            stream->tokens,
            stream->token_count * sizeof(*replacement));
    }
    wsh_release(&stream->allocator, stream->tokens);
    stream->tokens = replacement;
    stream->token_capacity = capacity;
    return WSH_OK;
}

/** Append one owned token to a stream. */
static wsh_result wsh_append_token(
    wsh_lexer *lexer,
    wsh_token_kind kind,
    size_t start,
    size_t end,
    const char *text,
    size_t text_length,
    size_t *out_index)
{
    wsh_token_stream *stream;
    wsh_token_data token;
    wsh_result result;

    if (lexer == NULL || end < start) {
        return WSH_ERR_INVALID;
    }
    stream = lexer->stream;
    if (stream->token_count == stream->token_capacity) {
        result = wsh_grow_tokens(stream);
        if (result != WSH_OK) {
            return result;
        }
    }
    memset(&token, 0, sizeof(token));
    token.kind = kind;
    token.start = start;
    token.end = end;
    result = wsh_resolve_span(lexer->source, start, end, &token.span);
    if (result != WSH_OK) {
        return WSH_ERR_INTERNAL;
    }
    if (text_length != 0U || text != NULL) {
        result = wsh_copy_text(
            &stream->allocator,
            text,
            text_length,
            &token.text);
        if (result != WSH_OK) {
            return result;
        }
        token.text_length = text_length;
    }
    stream->tokens[stream->token_count] = token;
    if (out_index != NULL) {
        *out_index = stream->token_count;
    }
    stream->token_count++;
    return WSH_OK;
}

/** Grow a syntax-diagnostic array. */
static wsh_result wsh_grow_diagnostics(
    const wsh_allocator *allocator,
    size_t limit,
    wsh_syntax_diagnostic_data **items,
    size_t count,
    size_t *capacity)
{
    wsh_syntax_diagnostic_data *replacement;
    size_t new_capacity;

    if (allocator == NULL || items == NULL || capacity == NULL ||
        count >= limit) {
        return WSH_ERR_RESOURCE;
    }
    new_capacity = *capacity == 0U ? 2U : *capacity * 2U;
    if (new_capacity < *capacity || new_capacity > limit) {
        new_capacity = limit;
    }
    if (new_capacity <= *capacity) {
        return WSH_ERR_RESOURCE;
    }
    replacement = (wsh_syntax_diagnostic_data *)wsh_allocate_array(
        allocator,
        new_capacity,
        sizeof(*replacement));
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(replacement, 0, new_capacity * sizeof(*replacement));
    if (count != 0U) {
        memcpy(replacement, *items, count * sizeof(*replacement));
    }
    wsh_release(allocator, *items);
    *items = replacement;
    *capacity = new_capacity;
    return WSH_OK;
}

/** Append one diagnostic to a token stream. */
static wsh_result wsh_stream_add_diagnostic(
    wsh_lexer *lexer,
    wsh_syntax_diagnostic_code code,
    const char *message,
    size_t start,
    size_t end)
{
    wsh_token_stream *stream;
    wsh_syntax_diagnostic_data item;
    wsh_result result;

    if (lexer == NULL || message == NULL) {
        return WSH_ERR_INVALID;
    }
    stream = lexer->stream;
    if (stream->diagnostic_count == stream->diagnostic_capacity) {
        result = wsh_grow_diagnostics(
            &stream->allocator,
            stream->options.max_diagnostics,
            &stream->diagnostics,
            stream->diagnostic_count,
            &stream->diagnostic_capacity);
        if (result != WSH_OK) {
            return result;
        }
    }
    memset(&item, 0, sizeof(item));
    item.code = code;
    item.message_length = strlen(message);
    result = wsh_copy_text(
        &stream->allocator,
        message,
        item.message_length,
        &item.message);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_resolve_span(lexer->source, start, end, &item.span);
    if (result != WSH_OK) {
        wsh_release(&stream->allocator, item.message);
        return WSH_ERR_INTERNAL;
    }
    stream->diagnostics[stream->diagnostic_count++] = item;
    return WSH_OK;
}

/** Return nonzero for an ASCII name-start byte. */
static int wsh_is_name_start(unsigned char value)
{
    return (value >= (unsigned char)'A' && value <= (unsigned char)'Z') ||
        (value >= (unsigned char)'a' && value <= (unsigned char)'z') ||
        value == (unsigned char)'_';
}

/** Return nonzero for an ASCII name-continuation byte. */
static int wsh_is_name_continue(unsigned char value)
{
    return wsh_is_name_start(value) ||
        (value >= (unsigned char)'0' && value <= (unsigned char)'9');
}

/** Return nonzero when text is one complete WSH name. */
static int wsh_is_name(const char *text, size_t length)
{
    size_t position;

    if (text == NULL || length == 0U ||
        !wsh_is_name_start((unsigned char)text[0])) {
        return 0;
    }
    position = 1U;
    for (;;) {
        while (position < length &&
            wsh_is_name_continue((unsigned char)text[position])) {
            position++;
        }
        if (position == length) {
            return 1;
        }
        if (position + 2U >= length || text[position] != ':' ||
            text[position + 1U] != ':' ||
            !wsh_is_name_start((unsigned char)text[position + 2U])) {
            return 0;
        }
        position += 3U;
    }
}

/** Scan a variable name and return the exclusive end offset. */
static size_t wsh_scan_variable_name(
    const char *text,
    size_t length,
    size_t start)
{
    size_t position;

    if (start >= length) {
        return start;
    }
    if (text[start] == '*') {
        return start + 1U;
    }
    if (text[start] >= '0' && text[start] <= '9') {
        position = start + 1U;
        while (position < length && text[position] >= '0' &&
            text[position] <= '9') {
            position++;
        }
        return position;
    }
    if (!wsh_is_name_start((unsigned char)text[start])) {
        return start;
    }
    position = start + 1U;
    for (;;) {
        while (position < length &&
            wsh_is_name_continue((unsigned char)text[position])) {
            position++;
        }
        if (position + 2U < length && text[position] == ':' &&
            text[position + 1U] == ':' &&
            wsh_is_name_start((unsigned char)text[position + 2U])) {
            position += 3U;
            continue;
        }
        break;
    }
    return position;
}

/** Return nonzero for an ASCII lexical separator. */
static int wsh_is_separator(unsigned char value)
{
    return value == (unsigned char)' ' || value == (unsigned char)'\t' ||
        value == (unsigned char)'\f' || value == (unsigned char)'\n';
}

/** Return nonzero for an ASCII WSH metacharacter. */
static int wsh_is_metacharacter(unsigned char value)
{
    const char *characters = "#;&|^$`'{}()<>!";

    return strchr(characters, (int)value) != NULL;
}

/** Grow the pending here-document marker array. */
static wsh_result wsh_grow_pending_here(wsh_lexer *lexer)
{
    size_t *replacement;
    size_t capacity;

    if (lexer == NULL ||
        lexer->pending_count >= lexer->stream->options.max_tokens) {
        return WSH_ERR_RESOURCE;
    }
    capacity = lexer->pending_capacity == 0U ? 4U :
        lexer->pending_capacity * 2U;
    if (capacity < lexer->pending_capacity ||
        capacity > lexer->stream->options.max_tokens) {
        capacity = lexer->stream->options.max_tokens;
    }
    replacement = (size_t *)wsh_allocate_array(
        &lexer->stream->allocator,
        capacity,
        sizeof(*replacement));
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (lexer->pending_count != 0U) {
        memcpy(
            replacement,
            lexer->pending_here,
            lexer->pending_count * sizeof(*replacement));
    }
    wsh_release(&lexer->stream->allocator, lexer->pending_here);
    lexer->pending_here = replacement;
    lexer->pending_capacity = capacity;
    return WSH_OK;
}

/** Record one here-document marker token. */
static wsh_result wsh_add_pending_here(
    wsh_lexer *lexer,
    size_t token_index)
{
    wsh_result result;

    if (lexer->pending_count == lexer->pending_capacity) {
        result = wsh_grow_pending_here(lexer);
        if (result != WSH_OK) {
            return result;
        }
    }
    lexer->pending_here[lexer->pending_count++] = token_index;
    return WSH_OK;
}

/** Set the auxiliary text of a token exactly once. */
static wsh_result wsh_set_token_auxiliary(
    wsh_token_stream *stream,
    size_t token_index,
    const char *text,
    size_t length,
    size_t start,
    size_t end)
{
    wsh_token_data *token;
    wsh_result result;

    if (stream == NULL || token_index >= stream->token_count) {
        return WSH_ERR_INVALID;
    }
    token = &stream->tokens[token_index];
    result = wsh_copy_text(
        &stream->allocator,
        text,
        length,
        &token->auxiliary);
    if (result == WSH_OK) {
        token->auxiliary_length = length;
        token->auxiliary_start = start;
        token->auxiliary_end = end;
    }
    return result;
}

/** Append a normal token and update here-marker state. */
static wsh_result wsh_lexer_emit(
    wsh_lexer *lexer,
    wsh_token_kind kind,
    size_t start,
    size_t end,
    const char *text,
    size_t text_length)
{
    size_t token_index;
    wsh_result result;

    result = wsh_append_token(
        lexer,
        kind,
        start,
        end,
        text,
        text_length,
        &token_index);
    if (result != WSH_OK) {
        return result;
    }
    if (lexer->expect_here_marker &&
        (kind == WSH_TOKEN_WORD || kind == WSH_TOKEN_QUOTED_WORD)) {
        result = wsh_add_pending_here(lexer, token_index);
        if (result != WSH_OK) {
            return result;
        }
        lexer->expect_here_marker = 0;
    } else if (lexer->expect_here_marker &&
        kind != WSH_TOKEN_NEWLINE) {
        lexer->expect_here_marker = 0;
    }
    return WSH_OK;
}

/** Lex one apostrophe-quoted word. */
static wsh_result wsh_lex_quoted_word(wsh_lexer *lexer)
{
    const char *text;
    size_t length;
    size_t start;
    size_t position;
    size_t decoded_length;
    size_t output_position;
    char *decoded;
    wsh_result result;

    text = lexer->text.data;
    length = lexer->text.length;
    start = lexer->position;
    position = start + 1U;
    decoded_length = 0U;
    while (position < length) {
        if (text[position] == '\'') {
            if (position + 1U < length && text[position + 1U] == '\'') {
                decoded_length++;
                position += 2U;
                continue;
            }
            break;
        }
        decoded_length++;
        position++;
    }
    if (position >= length) {
        lexer->stream->status = WSH_SYNTAX_INCOMPLETE;
        return wsh_stream_add_diagnostic(
            lexer,
            WSH_SYNTAX_DIAGNOSTIC_INCOMPLETE,
            "unterminated apostrophe quotation",
            start,
            length);
    }
    decoded = (char *)wsh_allocate_array(
        &lexer->stream->allocator,
        decoded_length + 1U,
        1U);
    if (decoded == NULL) {
        return WSH_ERR_RESOURCE;
    }
    output_position = 0U;
    lexer->position = start + 1U;
    while (lexer->position < position) {
        if (text[lexer->position] == '\'' &&
            lexer->position + 1U < position &&
            text[lexer->position + 1U] == '\'') {
            decoded[output_position++] = '\'';
            lexer->position += 2U;
        } else {
            decoded[output_position++] = text[lexer->position++];
        }
    }
    decoded[decoded_length] = '\0';
    lexer->position = position + 1U;
    result = wsh_lexer_emit(
        lexer,
        WSH_TOKEN_QUOTED_WORD,
        start,
        lexer->position,
        decoded,
        decoded_length);
    wsh_release(&lexer->stream->allocator, decoded);
    return result;
}

/** Lex one variable, count, or flatten token. */
static wsh_result wsh_lex_variable(wsh_lexer *lexer)
{
    const char *text;
    size_t length;
    size_t start;
    size_t name_start;
    size_t end;
    wsh_token_kind kind;

    text = lexer->text.data;
    length = lexer->text.length;
    start = lexer->position;
    kind = WSH_TOKEN_VARIABLE;
    name_start = start + 1U;
    if (name_start < length && text[name_start] == '#') {
        kind = WSH_TOKEN_COUNT;
        name_start++;
    } else if (name_start < length && text[name_start] == '"') {
        kind = WSH_TOKEN_FLATTEN;
        name_start++;
    }
    if (name_start >= length) {
        lexer->stream->status = WSH_SYNTAX_INCOMPLETE;
        return wsh_stream_add_diagnostic(
            lexer,
            WSH_SYNTAX_DIAGNOSTIC_INCOMPLETE,
            "variable expansion requires a name",
            start,
            length);
    }
    end = wsh_scan_variable_name(text, length, name_start);
    if (end == name_start) {
        lexer->stream->status = WSH_SYNTAX_ERROR;
        return wsh_stream_add_diagnostic(
            lexer,
            WSH_SYNTAX_DIAGNOSTIC_LEXICAL,
            "invalid variable name",
            start,
            name_start + 1U);
    }
    lexer->position = end;
    return wsh_lexer_emit(
        lexer,
        kind,
        start,
        end,
        text + name_start,
        end - name_start);
}

/** Lex one ordinary word. */
static wsh_result wsh_lex_word(wsh_lexer *lexer)
{
    size_t start;
    size_t end;

    start = lexer->position;
    end = start;
    while (end < lexer->text.length &&
        !wsh_is_separator((unsigned char)lexer->text.data[end]) &&
        !wsh_is_metacharacter((unsigned char)lexer->text.data[end])) {
        end++;
    }
    if (end == start) {
        return WSH_ERR_INTERNAL;
    }
    lexer->position = end;
    return wsh_lexer_emit(
        lexer,
        WSH_TOKEN_WORD,
        start,
        end,
        lexer->text.data + start,
        end - start);
}

/** Try to split an adjacent descriptor decoration into one word token. */
static wsh_result wsh_lex_decoration(
    wsh_lexer *lexer,
    int *out_consumed)
{
    size_t start;
    size_t end;
    wsh_result result;

    if (lexer == NULL || out_consumed == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_consumed = 0;
    if (!lexer->decoration_expected ||
        lexer->position != lexer->decoration_offset ||
        lexer->position >= lexer->text.length ||
        lexer->text.data[lexer->position] != '[') {
        lexer->decoration_expected = 0;
        return WSH_OK;
    }
    start = lexer->position;
    end = start + 1U;
    while (end < lexer->text.length && lexer->text.data[end] != ']' &&
        lexer->text.data[end] != '\n') {
        end++;
    }
    if (end >= lexer->text.length || lexer->text.data[end] != ']') {
        lexer->decoration_expected = 0;
        return WSH_OK;
    }
    end++;
    result = wsh_append_token(
        lexer,
        WSH_TOKEN_WORD,
        start,
        end,
        lexer->text.data + start,
        end - start,
        NULL);
    if (result != WSH_OK) {
        return result;
    }
    lexer->position = end;
    lexer->decoration_expected = 0;
    *out_consumed = 1;
    return WSH_OK;
}

/** Capture all pending here-document bodies after a command newline. */
static wsh_result wsh_capture_here_documents(wsh_lexer *lexer)
{
    size_t pending_index;

    for (pending_index = 0U;
        pending_index < lexer->pending_count;
        pending_index++) {
        size_t token_index;
        wsh_token_data *marker;
        size_t body_start;
        int found;

        token_index = lexer->pending_here[pending_index];
        marker = &lexer->stream->tokens[token_index];
        body_start = lexer->position;
        found = 0;
        while (lexer->position <= lexer->text.length) {
            size_t line_start;
            size_t line_end;
            size_t line_length;

            line_start = lexer->position;
            line_end = line_start;
            while (line_end < lexer->text.length &&
                lexer->text.data[line_end] != '\n') {
                line_end++;
            }
            line_length = line_end - line_start;
            if (line_length == marker->text_length &&
                (line_length == 0U || memcmp(
                    lexer->text.data + line_start,
                    marker->text,
                    line_length) == 0)) {
                wsh_result result;

                result = wsh_set_token_auxiliary(
                    lexer->stream,
                    token_index,
                    lexer->text.data + body_start,
                    line_start - body_start,
                    body_start,
                    line_start);
                if (result != WSH_OK) {
                    return result;
                }
                lexer->position = line_end < lexer->text.length ?
                    line_end + 1U : line_end;
                found = 1;
                break;
            }
            if (line_end >= lexer->text.length) {
                break;
            }
            lexer->position = line_end + 1U;
        }
        if (!found) {
            lexer->stream->status = WSH_SYNTAX_INCOMPLETE;
            return wsh_stream_add_diagnostic(
                lexer,
                WSH_SYNTAX_DIAGNOSTIC_INCOMPLETE,
                "unterminated here document",
                marker->start,
                lexer->text.length);
        }
    }
    lexer->pending_count = 0U;
    return WSH_OK;
}

/** Emit a fixed punctuation or operator token. */
static wsh_result wsh_lex_fixed(
    wsh_lexer *lexer,
    wsh_token_kind kind,
    size_t width,
    int allow_decoration)
{
    size_t start;
    wsh_result result;

    start = lexer->position;
    lexer->position += width;
    result = wsh_lexer_emit(
        lexer,
        kind,
        start,
        lexer->position,
        lexer->text.data + start,
        width);
    if (result == WSH_OK && allow_decoration) {
        lexer->decoration_expected = 1;
        lexer->decoration_offset = lexer->position;
    }
    return result;
}

/** Lex all normalized source bytes into one stream. */
static wsh_result wsh_run_lexer(wsh_lexer *lexer)
{
    wsh_result result;

    while (lexer->position < lexer->text.length) {
        unsigned char value;
        int decoration_consumed;

        result = wsh_lex_decoration(lexer, &decoration_consumed);
        if (result != WSH_OK) {
            return result;
        }
        if (decoration_consumed) {
            continue;
        }
        value = (unsigned char)lexer->text.data[lexer->position];
        if (value == (unsigned char)' ' || value == (unsigned char)'\t' ||
            value == (unsigned char)'\f') {
            lexer->decoration_expected = 0;
            lexer->position++;
            continue;
        }
        if (value == (unsigned char)'#') {
            lexer->decoration_expected = 0;
            while (lexer->position < lexer->text.length &&
                lexer->text.data[lexer->position] != '\n') {
                lexer->position++;
            }
            continue;
        }
        if (value == (unsigned char)'\n') {
            size_t newline_start;

            newline_start = lexer->position++;
            lexer->decoration_expected = 0;
            if (lexer->expect_here_marker) {
                lexer->stream->status = WSH_SYNTAX_ERROR;
                return wsh_stream_add_diagnostic(
                    lexer,
                    WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
                    "here document requires a literal marker",
                    newline_start,
                    newline_start + 1U);
            }
            if (lexer->pending_count != 0U) {
                result = wsh_capture_here_documents(lexer);
                if (result != WSH_OK ||
                    lexer->stream->status != WSH_SYNTAX_COMPLETE) {
                    return result;
                }
            }
            result = wsh_lexer_emit(
                lexer,
                WSH_TOKEN_NEWLINE,
                newline_start,
                newline_start + 1U,
                "\n",
                1U);
            if (result != WSH_OK) {
                return result;
            }
            continue;
        }
        if (value == (unsigned char)'\'') {
            lexer->decoration_expected = 0;
            result = wsh_lex_quoted_word(lexer);
        } else if (value == (unsigned char)'$') {
            lexer->decoration_expected = 0;
            result = wsh_lex_variable(lexer);
        } else if (value == (unsigned char)'&' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '&') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_AND_IF, 2U, 0);
        } else if (value == (unsigned char)'&') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_AMPERSAND, 1U, 0);
        } else if (value == (unsigned char)'|' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '|') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_OR_IF, 2U, 0);
        } else if (value == (unsigned char)'|') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_PIPE, 1U, 1);
        } else if (value == (unsigned char)'<' &&
            lexer->position + 2U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '>' &&
            lexer->text.data[lexer->position + 2U] == '{') {
            result = wsh_lex_fixed(
                lexer,
                WSH_TOKEN_PROCESS_DUPLEX,
                2U,
                0);
        } else if (value == (unsigned char)'<' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '>') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_DUPLEX, 2U, 0);
        } else if (value == (unsigned char)'<' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '<') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_HERE, 2U, 1);
            if (result == WSH_OK) {
                lexer->expect_here_marker = 1;
            }
        } else if (value == (unsigned char)'<' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '{') {
            result = wsh_lex_fixed(
                lexer,
                WSH_TOKEN_PROCESS_READ,
                1U,
                0);
        } else if (value == (unsigned char)'<') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_LESS, 1U, 1);
        } else if (value == (unsigned char)'>' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '>') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_APPEND, 2U, 1);
        } else if (value == (unsigned char)'>' &&
            lexer->position + 1U < lexer->text.length &&
            lexer->text.data[lexer->position + 1U] == '{') {
            result = wsh_lex_fixed(
                lexer,
                WSH_TOKEN_PROCESS_WRITE,
                1U,
                0);
        } else if (value == (unsigned char)'>') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_GREATER, 1U, 1);
        } else if (value == (unsigned char)';') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_SEMICOLON, 1U, 0);
        } else if (value == (unsigned char)'^') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_CARET, 1U, 0);
        } else if (value == (unsigned char)'`') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_BACKQUOTE, 1U, 0);
        } else if (value == (unsigned char)'!') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_BANG, 1U, 0);
        } else if (value == (unsigned char)'@') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_AT, 1U, 0);
        } else if (value == (unsigned char)'{') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_LEFT_BRACE, 1U, 0);
        } else if (value == (unsigned char)'}') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_RIGHT_BRACE, 1U, 0);
        } else if (value == (unsigned char)'(') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_LEFT_PAREN, 1U, 0);
        } else if (value == (unsigned char)')') {
            result = wsh_lex_fixed(lexer, WSH_TOKEN_RIGHT_PAREN, 1U, 0);
        } else {
            result = wsh_lex_word(lexer);
        }
        if (result != WSH_OK ||
            lexer->stream->status != WSH_SYNTAX_COMPLETE) {
            return result;
        }
    }
    if (lexer->expect_here_marker || lexer->pending_count != 0U) {
        lexer->stream->status = WSH_SYNTAX_INCOMPLETE;
        return wsh_stream_add_diagnostic(
            lexer,
            WSH_SYNTAX_DIAGNOSTIC_INCOMPLETE,
            lexer->expect_here_marker ?
                "here document requires a literal marker" :
                "here document requires a terminating line",
            lexer->text.length,
            lexer->text.length);
    }
    return wsh_lexer_emit(
        lexer,
        WSH_TOKEN_EOF,
        lexer->text.length,
        lexer->text.length,
        NULL,
        0U);
}

/** Grow the parse-tree node arena. */
static wsh_result wsh_grow_nodes(wsh_parse_tree *tree)
{
    wsh_ast_node **replacement;
    size_t capacity;

    if (tree == NULL || tree->node_count >= tree->options.max_ast_nodes) {
        return WSH_ERR_RESOURCE;
    }
    capacity = tree->node_capacity == 0U ? 32U :
        tree->node_capacity * 2U;
    if (capacity < tree->node_capacity ||
        capacity > tree->options.max_ast_nodes) {
        capacity = tree->options.max_ast_nodes;
    }
    if (capacity <= tree->node_capacity) {
        return WSH_ERR_RESOURCE;
    }
    replacement = (wsh_ast_node **)wsh_allocate_array(
        &tree->allocator,
        capacity,
        sizeof(*replacement));
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (tree->node_count != 0U) {
        memcpy(
            replacement,
            tree->nodes,
            tree->node_count * sizeof(*replacement));
    }
    wsh_release(&tree->allocator, tree->nodes);
    tree->nodes = replacement;
    tree->node_capacity = capacity;
    return WSH_OK;
}

/** Allocate and register one AST node. */
static wsh_ast_node *wsh_new_node(
    wsh_parser *parser,
    wsh_ast_kind kind,
    size_t start,
    size_t end,
    const char *text,
    size_t text_length)
{
    wsh_parse_tree *tree;
    wsh_ast_node *node;
    wsh_result result;

    if (parser == NULL || parser->failure != WSH_OK) {
        return NULL;
    }
    tree = parser->tree;
    if (tree->node_count == tree->node_capacity) {
        result = wsh_grow_nodes(tree);
        if (result != WSH_OK) {
            parser->failure = result;
            return NULL;
        }
    }
    node = (wsh_ast_node *)wsh_allocate_array(
        &tree->allocator,
        1U,
        sizeof(*node));
    if (node == NULL) {
        parser->failure = WSH_ERR_RESOURCE;
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    result = wsh_resolve_span(parser->source, start, end, &node->span);
    if (result != WSH_OK) {
        wsh_release(&tree->allocator, node);
        parser->failure = WSH_ERR_INTERNAL;
        return NULL;
    }
    tree->nodes[tree->node_count++] = node;
    if (text_length != 0U || text != NULL) {
        result = wsh_copy_text(
            &tree->allocator,
            text,
            text_length,
            &node->text);
        if (result != WSH_OK) {
            parser->failure = result;
            return NULL;
        }
        node->text_length = text_length;
    }
    return node;
}

/** Set one AST node's auxiliary text. */
static wsh_result wsh_node_set_auxiliary(
    wsh_parser *parser,
    wsh_ast_node *node,
    const char *text,
    size_t length)
{
    wsh_result result;

    if (parser == NULL || node == NULL || node->auxiliary != NULL) {
        return WSH_ERR_INVALID;
    }
    result = wsh_copy_text(
        &parser->tree->allocator,
        text,
        length,
        &node->auxiliary);
    if (result == WSH_OK) {
        node->auxiliary_length = length;
    } else {
        parser->failure = result;
    }
    return result;
}

/** Grow one node's child-pointer array. */
static wsh_result wsh_grow_children(
    wsh_parser *parser,
    wsh_ast_node *node)
{
    wsh_ast_node **replacement;
    size_t capacity;

    if (parser == NULL || node == NULL ||
        node->child_count >= parser->tree->options.max_ast_nodes) {
        return WSH_ERR_RESOURCE;
    }
    capacity = node->child_capacity == 0U ? 4U :
        node->child_capacity * 2U;
    if (capacity < node->child_capacity ||
        capacity > parser->tree->options.max_ast_nodes) {
        capacity = parser->tree->options.max_ast_nodes;
    }
    if (capacity <= node->child_capacity) {
        return WSH_ERR_RESOURCE;
    }
    replacement = (wsh_ast_node **)wsh_allocate_array(
        &parser->tree->allocator,
        capacity,
        sizeof(*replacement));
    if (replacement == NULL) {
        return WSH_ERR_RESOURCE;
    }
    if (node->child_count != 0U) {
        memcpy(
            replacement,
            node->children,
            node->child_count * sizeof(*replacement));
    }
    wsh_release(&parser->tree->allocator, node->children);
    node->children = replacement;
    node->child_capacity = capacity;
    return WSH_OK;
}

/** Append one borrowed child to an arena-owned node. */
static int wsh_add_child(
    wsh_parser *parser,
    wsh_ast_node *parent,
    wsh_ast_node *child)
{
    wsh_result result;

    if (parser == NULL || parent == NULL || child == NULL ||
        parser->failure != WSH_OK) {
        return 0;
    }
    if (parent->child_count == parent->child_capacity) {
        result = wsh_grow_children(parser, parent);
        if (result != WSH_OK) {
            parser->failure = result;
            return 0;
        }
    }
    parent->children[parent->child_count++] = child;
    return 1;
}

/** Update a node's end position after appending syntax. */
static void wsh_node_set_end(
    wsh_parser *parser,
    wsh_ast_node *node,
    size_t end)
{
    wsh_source_span span;

    if (parser == NULL || node == NULL || parser->failure != WSH_OK) {
        return;
    }
    if (wsh_resolve_span(
        parser->source,
        node->span.start.utf8_byte_offset,
        end,
        &span) != WSH_OK) {
        parser->failure = WSH_ERR_INTERNAL;
        return;
    }
    node->span = span;
}

/** Append one diagnostic to a parse tree. */
static wsh_result wsh_tree_add_diagnostic(
    wsh_parse_tree *tree,
    wsh_syntax_diagnostic_code code,
    const char *message,
    const wsh_source_span *span)
{
    wsh_syntax_diagnostic_data item;
    wsh_result result;

    if (tree == NULL || message == NULL || span == NULL) {
        return WSH_ERR_INVALID;
    }
    if (tree->diagnostic_count == tree->diagnostic_capacity) {
        result = wsh_grow_diagnostics(
            &tree->allocator,
            tree->options.max_diagnostics,
            &tree->diagnostics,
            tree->diagnostic_count,
            &tree->diagnostic_capacity);
        if (result != WSH_OK) {
            return result;
        }
    }
    memset(&item, 0, sizeof(item));
    item.code = code;
    item.span = *span;
    item.message_length = strlen(message);
    result = wsh_copy_text(
        &tree->allocator,
        message,
        item.message_length,
        &item.message);
    if (result != WSH_OK) {
        return result;
    }
    tree->diagnostics[tree->diagnostic_count++] = item;
    return WSH_OK;
}

/** Return the current parser token. */
static const wsh_token_data *wsh_current_token(const wsh_parser *parser)
{
    if (parser == NULL || parser->stream == NULL ||
        parser->position >= parser->stream->token_count) {
        return NULL;
    }
    return &parser->stream->tokens[parser->position];
}

/** Return a token at an offset from the parser position. */
static const wsh_token_data *wsh_peek_token(
    const wsh_parser *parser,
    size_t offset)
{
    size_t index;

    if (parser == NULL ||
        !wsh_size_add(parser->position, offset, &index) ||
        index >= parser->stream->token_count) {
        return NULL;
    }
    return &parser->stream->tokens[index];
}

/** Return nonzero when the current token has a specified kind. */
static int wsh_at_kind(
    const wsh_parser *parser,
    wsh_token_kind kind)
{
    const wsh_token_data *token = wsh_current_token(parser);

    return token != NULL && token->kind == kind;
}

/** Consume the current token when it has a specified kind. */
static const wsh_token_data *wsh_take_kind(
    wsh_parser *parser,
    wsh_token_kind kind)
{
    const wsh_token_data *token;

    token = wsh_current_token(parser);
    if (token == NULL || token->kind != kind) {
        return NULL;
    }
    parser->position++;
    return token;
}

/** Compare a word token to one ASCII keyword. */
static int wsh_token_is_word(
    const wsh_token_data *token,
    const char *word)
{
    size_t length;

    if (token == NULL || token->kind != WSH_TOKEN_WORD || word == NULL) {
        return 0;
    }
    length = strlen(word);
    return token->text_length == length &&
        memcmp(token->text, word, length) == 0;
}

/** Return nonzero for a keyword reserved at command start. */
static int wsh_is_reserved_word(const wsh_token_data *token)
{
    static const char *const words[] = {
        "for", "in", "while", "if", "not", "switch", "case", "fn"
    };
    size_t index;

    for (index = 0U; index < sizeof(words) / sizeof(words[0]); index++) {
        if (wsh_token_is_word(token, words[index])) {
            return 1;
        }
    }
    return 0;
}

/** Stop parsing with one syntax diagnostic. */
static void wsh_parser_stop(
    wsh_parser *parser,
    wsh_syntax_status status,
    wsh_syntax_diagnostic_code code,
    const char *message,
    const wsh_token_data *token)
{
    wsh_source_span span;

    if (parser == NULL || parser->stopped || parser->failure != WSH_OK) {
        return;
    }
    if (token != NULL) {
        span = token->span;
    } else if (wsh_resolve_span(
        parser->source,
        wsh_source_text(parser->source).length,
        wsh_source_text(parser->source).length,
        &span) != WSH_OK) {
        parser->failure = WSH_ERR_INTERNAL;
        return;
    }
    parser->tree->status = status;
    parser->failure = wsh_tree_add_diagnostic(
        parser->tree,
        code,
        message,
        &span);
    parser->stopped = parser->failure == WSH_OK;
}

/** Stop for an expected construct, classifying EOF as incomplete. */
static void wsh_expected(
    wsh_parser *parser,
    const char *message)
{
    const wsh_token_data *token = wsh_current_token(parser);
    int at_eof = token == NULL || token->kind == WSH_TOKEN_EOF;

    wsh_parser_stop(
        parser,
        at_eof ? WSH_SYNTAX_INCOMPLETE : WSH_SYNTAX_ERROR,
        at_eof ? WSH_SYNTAX_DIAGNOSTIC_INCOMPLETE :
            WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
        message,
        token);
}

/** Enter one recursive production under the configured depth limit. */
static int wsh_enter_parse(wsh_parser *parser)
{
    if (parser == NULL || parser->stopped || parser->failure != WSH_OK) {
        return 0;
    }
    if (parser->depth >= parser->tree->options.max_parse_depth) {
        parser->failure = WSH_ERR_RESOURCE;
        return 0;
    }
    parser->depth++;
    return 1;
}

/** Leave one recursive production. */
static void wsh_leave_parse(wsh_parser *parser)
{
    if (parser != NULL && parser->depth != 0U) {
        parser->depth--;
    }
}

/** Forward declaration for command-list parsing. */
static wsh_ast_node *wsh_parse_command_list(
    wsh_parser *parser,
    wsh_token_kind terminator,
    int stop_at_case);

/** Forward declaration for one command. */
static wsh_ast_node *wsh_parse_command(wsh_parser *parser);

/** Forward declaration for one argument. */
static wsh_ast_node *wsh_parse_argument(wsh_parser *parser);

/** Return nonzero when a token can begin an argument primary. */
static int wsh_starts_argument(const wsh_token_data *token)
{
    if (token == NULL) {
        return 0;
    }
    switch (token->kind) {
    case WSH_TOKEN_WORD:
    case WSH_TOKEN_QUOTED_WORD:
    case WSH_TOKEN_VARIABLE:
    case WSH_TOKEN_COUNT:
    case WSH_TOKEN_FLATTEN:
    case WSH_TOKEN_LEFT_PAREN:
    case WSH_TOKEN_BACKQUOTE:
    case WSH_TOKEN_PROCESS_READ:
    case WSH_TOKEN_PROCESS_WRITE:
    case WSH_TOKEN_PROCESS_DUPLEX:
        return 1;
    default:
        return 0;
    }
}

/** Validate one grammar-level subscript item. */
static int wsh_is_subscript(const char *text, size_t length)
{
    size_t position;

    if (text == NULL || length == 0U) {
        return 0;
    }
    position = 0U;
    while (position < length && text[position] >= '0' &&
        text[position] <= '9') {
        position++;
    }
    if (position == 0U) {
        return 0;
    }
    if (position == length) {
        return 1;
    }
    if (text[position++] != '-') {
        return 0;
    }
    while (position < length && text[position] >= '0' &&
        text[position] <= '9') {
        position++;
    }
    return position == length;
}

/** Validate a square-bracket descriptor decoration. */
static int wsh_is_decoration(
    const char *text,
    size_t length,
    int pipe_decoration,
    int *out_has_equals)
{
    size_t position;
    size_t digits_start;

    if (out_has_equals != NULL) {
        *out_has_equals = 0;
    }
    if (text == NULL || length < 2U || text[0] != '[' ||
        text[length - 1U] != ']') {
        return 0;
    }
    position = 1U;
    if (pipe_decoration && position == length - 1U) {
        return 1;
    }
    digits_start = position;
    while (position < length - 1U && text[position] >= '0' &&
        text[position] <= '9') {
        position++;
    }
    if (position == digits_start) {
        return 0;
    }
    if (position == length - 1U) {
        return 1;
    }
    if (text[position++] != '=') {
        return 0;
    }
    if (out_has_equals != NULL) {
        *out_has_equals = 1;
    }
    digits_start = position;
    while (position < length - 1U && text[position] >= '0' &&
        text[position] <= '9') {
        position++;
    }
    if (pipe_decoration && position == digits_start) {
        return 0;
    }
    return position == length - 1U;
}

/** Parse one braced command block. */
static wsh_ast_node *wsh_parse_block(wsh_parser *parser)
{
    const wsh_token_data *left;
    const wsh_token_data *right;
    wsh_ast_node *block;
    wsh_ast_node *commands;

    left = wsh_take_kind(parser, WSH_TOKEN_LEFT_BRACE);
    if (left == NULL) {
        wsh_expected(parser, "expected left brace");
        return NULL;
    }
    block = wsh_new_node(
        parser,
        WSH_AST_BLOCK,
        left->start,
        left->end,
        NULL,
        0U);
    if (block == NULL) {
        return NULL;
    }
    commands = wsh_parse_command_list(
        parser,
        WSH_TOKEN_RIGHT_BRACE,
        0);
    if (commands != NULL && !wsh_add_child(parser, block, commands)) {
        return NULL;
    }
    right = wsh_take_kind(parser, WSH_TOKEN_RIGHT_BRACE);
    if (right == NULL) {
        wsh_expected(parser, "expected right brace");
        return NULL;
    }
    wsh_node_set_end(parser, block, right->end);
    return block;
}

/** Parse one variable primary and an optional adjacent subscript. */
static wsh_ast_node *wsh_parse_variable_primary(
    wsh_parser *parser,
    const wsh_token_data *token)
{
    wsh_ast_node *variable;
    const wsh_token_data *left;

    variable = wsh_new_node(
        parser,
        WSH_AST_VARIABLE,
        token->start,
        token->end,
        token->text,
        token->text_length);
    if (variable == NULL) {
        return NULL;
    }
    left = wsh_current_token(parser);
    if (left == NULL || left->kind != WSH_TOKEN_LEFT_PAREN ||
        left->start != token->end) {
        return variable;
    }
    parser->position++;
    if (wsh_at_kind(parser, WSH_TOKEN_RIGHT_PAREN)) {
        wsh_parser_stop(
            parser,
            WSH_SYNTAX_ERROR,
            WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
            "variable subscript cannot be empty",
            wsh_current_token(parser));
        return NULL;
    }
    while (!parser->stopped && parser->failure == WSH_OK &&
        !wsh_at_kind(parser, WSH_TOKEN_RIGHT_PAREN)) {
        const wsh_token_data *item;
        wsh_ast_node *subscript;

        item = wsh_take_kind(parser, WSH_TOKEN_WORD);
        if (item == NULL ||
            !wsh_is_subscript(item->text, item->text_length)) {
            wsh_expected(parser, "expected decimal subscript item");
            return NULL;
        }
        subscript = wsh_new_node(
            parser,
            WSH_AST_SUBSCRIPT,
            item->start,
            item->end,
            item->text,
            item->text_length);
        if (subscript == NULL ||
            !wsh_add_child(parser, variable, subscript)) {
            return NULL;
        }
    }
    {
        const wsh_token_data *right;

        right = wsh_take_kind(parser, WSH_TOKEN_RIGHT_PAREN);
        if (right == NULL) {
            wsh_expected(parser, "expected right parenthesis in subscript");
            return NULL;
        }
        wsh_node_set_end(parser, variable, right->end);
    }
    return variable;
}

/** Parse one argument primary without caret concatenation. */
static wsh_ast_node *wsh_parse_primary(wsh_parser *parser)
{
    const wsh_token_data *token;
    wsh_ast_node *node;

    if (!wsh_enter_parse(parser)) {
        return NULL;
    }
    token = wsh_current_token(parser);
    if (token == NULL) {
        wsh_expected(parser, "expected argument");
        wsh_leave_parse(parser);
        return NULL;
    }
    switch (token->kind) {
    case WSH_TOKEN_WORD:
        parser->position++;
        node = wsh_new_node(
            parser,
            WSH_AST_WORD,
            token->start,
            token->end,
            token->text,
            token->text_length);
        break;
    case WSH_TOKEN_QUOTED_WORD:
        parser->position++;
        node = wsh_new_node(
            parser,
            WSH_AST_QUOTED_WORD,
            token->start,
            token->end,
            token->text,
            token->text_length);
        break;
    case WSH_TOKEN_VARIABLE:
        parser->position++;
        node = wsh_parse_variable_primary(parser, token);
        break;
    case WSH_TOKEN_COUNT:
        parser->position++;
        node = wsh_new_node(
            parser,
            WSH_AST_COUNT,
            token->start,
            token->end,
            token->text,
            token->text_length);
        break;
    case WSH_TOKEN_FLATTEN:
        parser->position++;
        node = wsh_new_node(
            parser,
            WSH_AST_FLATTEN,
            token->start,
            token->end,
            token->text,
            token->text_length);
        break;
    case WSH_TOKEN_LEFT_PAREN:
        {
            const wsh_token_data *left;
            const wsh_token_data *right;

            left = token;
            parser->position++;
            node = wsh_new_node(
                parser,
                WSH_AST_LIST,
                left->start,
                left->end,
                NULL,
                0U);
            while (node != NULL && !parser->stopped &&
                parser->failure == WSH_OK &&
                !wsh_at_kind(parser, WSH_TOKEN_RIGHT_PAREN)) {
                wsh_ast_node *item;

                if (wsh_at_kind(parser, WSH_TOKEN_EOF)) {
                    wsh_expected(parser, "expected right parenthesis in list");
                    node = NULL;
                    break;
                }
                item = wsh_parse_argument(parser);
                if (item == NULL || !wsh_add_child(parser, node, item)) {
                    node = NULL;
                    break;
                }
            }
            right = wsh_take_kind(parser, WSH_TOKEN_RIGHT_PAREN);
            if (node != NULL && right == NULL) {
                wsh_expected(parser, "expected right parenthesis in list");
                node = NULL;
            } else if (node != NULL) {
                wsh_node_set_end(parser, node, right->end);
            }
        }
        break;
    case WSH_TOKEN_BACKQUOTE:
    case WSH_TOKEN_PROCESS_READ:
    case WSH_TOKEN_PROCESS_WRITE:
    case WSH_TOKEN_PROCESS_DUPLEX:
        {
            wsh_ast_kind kind;
            wsh_ast_node *block;

            parser->position++;
            if (token->kind == WSH_TOKEN_BACKQUOTE) {
                kind = WSH_AST_COMMAND_SUBSTITUTION;
            } else if (token->kind == WSH_TOKEN_PROCESS_READ) {
                kind = WSH_AST_PROCESS_READ;
            } else if (token->kind == WSH_TOKEN_PROCESS_WRITE) {
                kind = WSH_AST_PROCESS_WRITE;
            } else {
                kind = WSH_AST_PROCESS_DUPLEX;
            }
            node = wsh_new_node(
                parser,
                kind,
                token->start,
                token->end,
                NULL,
                0U);
            block = wsh_parse_block(parser);
            if (node == NULL || block == NULL ||
                !wsh_add_child(parser, node, block)) {
                node = NULL;
            } else {
                wsh_node_set_end(
                    parser,
                    node,
                    block->span.end.utf8_byte_offset);
            }
        }
        break;
    default:
        wsh_expected(parser, "expected argument");
        node = NULL;
        break;
    }
    wsh_leave_parse(parser);
    return node;
}

/** Continue an argument from an existing primary through carets. */
static wsh_ast_node *wsh_parse_argument_tail(
    wsh_parser *parser,
    wsh_ast_node *left)
{
    while (left != NULL && !parser->stopped && parser->failure == WSH_OK) {
        const wsh_token_data *token;
        int explicit_caret;
        int free_caret;
        wsh_ast_node *right;
        wsh_ast_node *concat;
        size_t start;

        token = wsh_current_token(parser);
        explicit_caret = token != NULL &&
            token->kind == WSH_TOKEN_CARET;
        free_caret = token != NULL && wsh_starts_argument(token) &&
            left->span.end.utf8_byte_offset == token->start;
        if (!explicit_caret && !free_caret) {
            break;
        }
        start = left->span.start.utf8_byte_offset;
        if (explicit_caret) {
            parser->position++;
            if (!wsh_starts_argument(wsh_current_token(parser))) {
                wsh_expected(parser, "caret requires a right argument");
                return NULL;
            }
        }
        right = wsh_parse_primary(parser);
        if (right == NULL) {
            return NULL;
        }
        concat = wsh_new_node(
            parser,
            WSH_AST_CONCAT,
            start,
            right->span.end.utf8_byte_offset,
            explicit_caret ? "explicit" : "free",
            explicit_caret ? 8U : 4U);
        if (concat == NULL || !wsh_add_child(parser, concat, left) ||
            !wsh_add_child(parser, concat, right)) {
            return NULL;
        }
        left = concat;
    }
    return left;
}

/** Parse one complete argument including explicit or free carets. */
static wsh_ast_node *wsh_parse_argument(wsh_parser *parser)
{
    wsh_ast_node *primary;

    primary = wsh_parse_primary(parser);
    return wsh_parse_argument_tail(parser, primary);
}

/** Return nonzero when a token starts a redirection item. */
static int wsh_starts_redirection(const wsh_token_data *token)
{
    if (token == NULL) {
        return 0;
    }
    return token->kind == WSH_TOKEN_LESS ||
        token->kind == WSH_TOKEN_GREATER ||
        token->kind == WSH_TOKEN_APPEND ||
        token->kind == WSH_TOKEN_HERE;
}

/** Return nonzero when a token ends a simple command. */
static int wsh_ends_simple(const wsh_token_data *token)
{
    if (token == NULL) {
        return 1;
    }
    switch (token->kind) {
    case WSH_TOKEN_EOF:
    case WSH_TOKEN_NEWLINE:
    case WSH_TOKEN_SEMICOLON:
    case WSH_TOKEN_AMPERSAND:
    case WSH_TOKEN_AND_IF:
    case WSH_TOKEN_OR_IF:
    case WSH_TOKEN_PIPE:
    case WSH_TOKEN_RIGHT_BRACE:
    case WSH_TOKEN_RIGHT_PAREN:
        return 1;
    default:
        return 0;
    }
}

/** Find a valid assignment prefix in one word token. */
static size_t wsh_assignment_equals(const wsh_token_data *token)
{
    size_t index;

    if (token == NULL || token->kind != WSH_TOKEN_WORD) {
        return SIZE_MAX;
    }
    for (index = 1U; index < token->text_length; index++) {
        if (token->text[index] == '=') {
            return wsh_is_name(token->text, index) ? index : SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

/** Parse one assignment recognized from an unquoted word prefix. */
static wsh_ast_node *wsh_parse_assignment(
    wsh_parser *parser,
    size_t equals_index)
{
    const wsh_token_data *token;
    wsh_ast_node *assignment;
    wsh_ast_node *value;
    size_t value_length;

    token = wsh_current_token(parser);
    if (token == NULL || equals_index == SIZE_MAX) {
        return NULL;
    }
    parser->position++;
    assignment = wsh_new_node(
        parser,
        WSH_AST_ASSIGNMENT,
        token->start,
        token->end,
        token->text,
        equals_index);
    if (assignment == NULL) {
        return NULL;
    }
    value_length = token->text_length - equals_index - 1U;
    if (value_length != 0U) {
        value = wsh_new_node(
            parser,
            WSH_AST_WORD,
            token->start + equals_index + 1U,
            token->end,
            token->text + equals_index + 1U,
            value_length);
        value = wsh_parse_argument_tail(parser, value);
    } else if (wsh_starts_argument(wsh_current_token(parser)) &&
        wsh_current_token(parser)->start == token->end) {
        value = wsh_parse_argument(parser);
    } else {
        wsh_expected(parser, "assignment requires an adjacent value");
        return NULL;
    }
    if (value == NULL || !wsh_add_child(parser, assignment, value)) {
        return NULL;
    }
    wsh_node_set_end(
        parser,
        assignment,
        value->span.end.utf8_byte_offset);
    return assignment;
}

/** Parse one ordered redirection item. */
static wsh_ast_node *wsh_parse_redirection(wsh_parser *parser)
{
    const wsh_token_data *operator_token;
    const wsh_token_data *decoration;
    wsh_ast_node *redirection;
    int has_equals;

    operator_token = wsh_current_token(parser);
    if (!wsh_starts_redirection(operator_token)) {
        return NULL;
    }
    parser->position++;
    redirection = wsh_new_node(
        parser,
        WSH_AST_REDIRECTION,
        operator_token->start,
        operator_token->end,
        operator_token->text,
        operator_token->text_length);
    if (redirection == NULL) {
        return NULL;
    }
    decoration = wsh_current_token(parser);
    has_equals = 0;
    if (decoration != NULL && decoration->kind == WSH_TOKEN_WORD &&
        decoration->start == operator_token->end &&
        decoration->text_length >= 2U && decoration->text[0] == '[') {
        if (!wsh_is_decoration(
            decoration->text,
            decoration->text_length,
            0,
            &has_equals)) {
            wsh_parser_stop(
                parser,
                WSH_SYNTAX_ERROR,
                WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
                "invalid redirection descriptor decoration",
                decoration);
            return NULL;
        }
        parser->position++;
        if (wsh_node_set_auxiliary(
            parser,
            redirection,
            decoration->text,
            decoration->text_length) != WSH_OK) {
            return NULL;
        }
        wsh_node_set_end(parser, redirection, decoration->end);
    }
    if (operator_token->kind == WSH_TOKEN_HERE) {
        const wsh_token_data *marker;
        wsh_ast_node *marker_node;
        wsh_ast_node *body;
        wsh_ast_kind marker_kind;

        marker = wsh_current_token(parser);
        if (marker == NULL ||
            (marker->kind != WSH_TOKEN_WORD &&
                marker->kind != WSH_TOKEN_QUOTED_WORD)) {
            wsh_expected(parser, "here document requires a literal marker");
            return NULL;
        }
        parser->position++;
        marker_kind = marker->kind == WSH_TOKEN_WORD ?
            WSH_AST_WORD : WSH_AST_QUOTED_WORD;
        marker_node = wsh_new_node(
            parser,
            marker_kind,
            marker->start,
            marker->end,
            marker->text,
            marker->text_length);
        body = wsh_new_node(
            parser,
            WSH_AST_HERE_BODY,
            marker->auxiliary_start,
            marker->auxiliary_end,
            marker->auxiliary,
            marker->auxiliary_length);
        if (marker_node == NULL || body == NULL ||
            !wsh_add_child(parser, redirection, marker_node) ||
            !wsh_add_child(parser, redirection, body)) {
            return NULL;
        }
        wsh_node_set_end(parser, redirection, marker->end);
        return redirection;
    }
    if (has_equals) {
        return redirection;
    }
    if (!wsh_starts_argument(wsh_current_token(parser))) {
        wsh_expected(parser, "redirection requires an operand");
        return NULL;
    }
    {
        wsh_ast_node *operand = wsh_parse_argument(parser);

        if (operand == NULL ||
            !wsh_add_child(parser, redirection, operand)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            redirection,
            operand->span.end.utf8_byte_offset);
    }
    return redirection;
}

/** Parse one simple command with ordered items. */
static wsh_ast_node *wsh_parse_simple(wsh_parser *parser)
{
    const wsh_token_data *first;
    wsh_ast_node *simple;

    first = wsh_current_token(parser);
    if (first == NULL || wsh_ends_simple(first)) {
        wsh_expected(parser, "expected simple command item");
        return NULL;
    }
    simple = wsh_new_node(
        parser,
        WSH_AST_SIMPLE,
        first->start,
        first->end,
        NULL,
        0U);
    if (simple == NULL) {
        return NULL;
    }
    while (!parser->stopped && parser->failure == WSH_OK &&
        !wsh_ends_simple(wsh_current_token(parser))) {
        const wsh_token_data *token;
        wsh_ast_node *item;
        size_t equals_index;

        token = wsh_current_token(parser);
        equals_index = wsh_assignment_equals(token);
        if (equals_index != SIZE_MAX) {
            item = wsh_parse_assignment(parser, equals_index);
        } else if (wsh_starts_redirection(token)) {
            item = wsh_parse_redirection(parser);
        } else {
            item = wsh_parse_argument(parser);
        }
        if (item == NULL || !wsh_add_child(parser, simple, item)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            simple,
            item->span.end.utf8_byte_offset);
    }
    if (simple->child_count == 0U) {
        wsh_expected(parser, "simple command cannot be empty");
        return NULL;
    }
    return simple;
}

/** Parse a parenthesized command-list condition. */
static wsh_ast_node *wsh_parse_condition(wsh_parser *parser)
{
    const wsh_token_data *left;
    const wsh_token_data *right;
    wsh_ast_node *commands;

    left = wsh_take_kind(parser, WSH_TOKEN_LEFT_PAREN);
    if (left == NULL) {
        wsh_expected(parser, "expected left parenthesis");
        return NULL;
    }
    commands = wsh_parse_command_list(
        parser,
        WSH_TOKEN_RIGHT_PAREN,
        0);
    right = wsh_take_kind(parser, WSH_TOKEN_RIGHT_PAREN);
    if (right == NULL) {
        wsh_expected(parser, "expected right parenthesis");
        return NULL;
    }
    if (commands != NULL) {
        wsh_node_set_end(parser, commands, right->start);
    }
    return commands;
}

/** Parse an if command including an optional if-not arm. */
static wsh_ast_node *wsh_parse_if(wsh_parser *parser)
{
    const wsh_token_data *keyword;
    wsh_ast_node *node;
    wsh_ast_node *condition;
    wsh_ast_node *body;
    size_t saved_position;

    keyword = wsh_current_token(parser);
    parser->position++;
    node = wsh_new_node(
        parser,
        WSH_AST_IF,
        keyword->start,
        keyword->end,
        NULL,
        0U);
    condition = wsh_parse_condition(parser);
    body = wsh_parse_command(parser);
    if (node == NULL || condition == NULL || body == NULL ||
        !wsh_add_child(parser, node, condition) ||
        !wsh_add_child(parser, node, body)) {
        return NULL;
    }
    wsh_node_set_end(parser, node, body->span.end.utf8_byte_offset);
    saved_position = parser->position;
    while (wsh_at_kind(parser, WSH_TOKEN_NEWLINE) ||
        wsh_at_kind(parser, WSH_TOKEN_SEMICOLON) ||
        wsh_at_kind(parser, WSH_TOKEN_AMPERSAND)) {
        parser->position++;
    }
    if (wsh_token_is_word(wsh_current_token(parser), "if") &&
        wsh_token_is_word(wsh_peek_token(parser, 1U), "not")) {
        wsh_ast_node *alternate;

        parser->position += 2U;
        alternate = wsh_parse_command(parser);
        if (alternate == NULL ||
            !wsh_add_child(parser, node, alternate)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            node,
            alternate->span.end.utf8_byte_offset);
    } else {
        parser->position = saved_position;
    }
    return node;
}

/** Parse a while command. */
static wsh_ast_node *wsh_parse_while(wsh_parser *parser)
{
    const wsh_token_data *keyword;
    wsh_ast_node *node;
    wsh_ast_node *condition;
    wsh_ast_node *body;

    keyword = wsh_current_token(parser);
    parser->position++;
    node = wsh_new_node(
        parser,
        WSH_AST_WHILE,
        keyword->start,
        keyword->end,
        NULL,
        0U);
    condition = wsh_parse_condition(parser);
    body = wsh_parse_command(parser);
    if (node == NULL || condition == NULL || body == NULL ||
        !wsh_add_child(parser, node, condition) ||
        !wsh_add_child(parser, node, body)) {
        return NULL;
    }
    wsh_node_set_end(parser, node, body->span.end.utf8_byte_offset);
    return node;
}

/** Parse a for command. */
static wsh_ast_node *wsh_parse_for(wsh_parser *parser)
{
    const wsh_token_data *keyword;
    const wsh_token_data *name;
    const wsh_token_data *right;
    wsh_ast_node *node;
    wsh_ast_node *body;

    keyword = wsh_current_token(parser);
    parser->position++;
    if (wsh_take_kind(parser, WSH_TOKEN_LEFT_PAREN) == NULL) {
        wsh_expected(parser, "for requires a left parenthesis");
        return NULL;
    }
    name = wsh_take_kind(parser, WSH_TOKEN_WORD);
    if (name == NULL || !wsh_is_name(name->text, name->text_length)) {
        wsh_expected(parser, "for requires a valid variable name");
        return NULL;
    }
    node = wsh_new_node(
        parser,
        WSH_AST_FOR,
        keyword->start,
        name->end,
        name->text,
        name->text_length);
    if (node == NULL) {
        return NULL;
    }
    if (wsh_token_is_word(wsh_current_token(parser), "in")) {
        parser->position++;
        while (!parser->stopped && parser->failure == WSH_OK &&
            !wsh_at_kind(parser, WSH_TOKEN_RIGHT_PAREN)) {
            wsh_ast_node *argument;

            if (wsh_at_kind(parser, WSH_TOKEN_EOF)) {
                wsh_expected(parser, "for requires a right parenthesis");
                return NULL;
            }
            argument = wsh_parse_argument(parser);
            if (argument == NULL ||
                !wsh_add_child(parser, node, argument)) {
                return NULL;
            }
        }
    }
    right = wsh_take_kind(parser, WSH_TOKEN_RIGHT_PAREN);
    if (right == NULL) {
        wsh_expected(parser, "for requires in-values or right parenthesis");
        return NULL;
    }
    body = wsh_parse_command(parser);
    if (body == NULL || !wsh_add_child(parser, node, body)) {
        return NULL;
    }
    wsh_node_set_end(parser, node, body->span.end.utf8_byte_offset);
    return node;
}

/** Parse one switch case clause. */
static wsh_ast_node *wsh_parse_case(wsh_parser *parser)
{
    const wsh_token_data *keyword;
    wsh_ast_node *node;

    keyword = wsh_current_token(parser);
    if (!wsh_token_is_word(keyword, "case")) {
        wsh_expected(parser, "switch body requires a case clause");
        return NULL;
    }
    parser->position++;
    node = wsh_new_node(
        parser,
        WSH_AST_CASE,
        keyword->start,
        keyword->end,
        NULL,
        0U);
    while (!parser->stopped && parser->failure == WSH_OK &&
        !wsh_at_kind(parser, WSH_TOKEN_NEWLINE) &&
        !wsh_at_kind(parser, WSH_TOKEN_SEMICOLON) &&
        !wsh_at_kind(parser, WSH_TOKEN_AMPERSAND)) {
        wsh_ast_node *pattern;

        if (wsh_at_kind(parser, WSH_TOKEN_EOF) ||
            wsh_at_kind(parser, WSH_TOKEN_RIGHT_BRACE)) {
            wsh_expected(parser, "case patterns require a list separator");
            return NULL;
        }
        pattern = wsh_parse_argument(parser);
        if (pattern == NULL || !wsh_add_child(parser, node, pattern)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            node,
            pattern->span.end.utf8_byte_offset);
    }
    if (wsh_at_kind(parser, WSH_TOKEN_NEWLINE) ||
        wsh_at_kind(parser, WSH_TOKEN_SEMICOLON) ||
        wsh_at_kind(parser, WSH_TOKEN_AMPERSAND)) {
        parser->position++;
    } else {
        wsh_expected(parser, "case requires a list separator");
        return NULL;
    }
    {
        wsh_ast_node *commands = wsh_parse_command_list(
            parser,
            WSH_TOKEN_RIGHT_BRACE,
            1);

        if (commands != NULL &&
            !wsh_add_child(parser, node, commands)) {
            return NULL;
        }
        if (commands != NULL) {
            wsh_node_set_end(
                parser,
                node,
                commands->span.end.utf8_byte_offset);
        }
    }
    return node;
}

/** Parse a switch command and all top-level case clauses. */
static wsh_ast_node *wsh_parse_switch(wsh_parser *parser)
{
    const wsh_token_data *keyword;
    const wsh_token_data *right;
    wsh_ast_node *node;
    wsh_ast_node *subject;

    keyword = wsh_current_token(parser);
    parser->position++;
    if (wsh_take_kind(parser, WSH_TOKEN_LEFT_PAREN) == NULL) {
        wsh_expected(parser, "switch requires a left parenthesis");
        return NULL;
    }
    subject = wsh_parse_argument(parser);
    if (subject == NULL) {
        return NULL;
    }
    if (wsh_take_kind(parser, WSH_TOKEN_RIGHT_PAREN) == NULL) {
        wsh_expected(parser, "switch requires a right parenthesis");
        return NULL;
    }
    if (wsh_take_kind(parser, WSH_TOKEN_LEFT_BRACE) == NULL) {
        wsh_expected(parser, "switch requires a left brace");
        return NULL;
    }
    node = wsh_new_node(
        parser,
        WSH_AST_SWITCH,
        keyword->start,
        subject->span.end.utf8_byte_offset,
        NULL,
        0U);
    if (node == NULL || !wsh_add_child(parser, node, subject)) {
        return NULL;
    }
    for (;;) {
        while (wsh_at_kind(parser, WSH_TOKEN_NEWLINE) ||
            wsh_at_kind(parser, WSH_TOKEN_SEMICOLON) ||
            wsh_at_kind(parser, WSH_TOKEN_AMPERSAND)) {
            parser->position++;
        }
        if (wsh_at_kind(parser, WSH_TOKEN_RIGHT_BRACE)) {
            break;
        }
        if (wsh_at_kind(parser, WSH_TOKEN_EOF)) {
            wsh_expected(parser, "switch requires a right brace");
            return NULL;
        }
        {
            wsh_ast_node *clause = wsh_parse_case(parser);

            if (clause == NULL || !wsh_add_child(parser, node, clause)) {
                return NULL;
            }
        }
    }
    right = wsh_take_kind(parser, WSH_TOKEN_RIGHT_BRACE);
    if (right == NULL) {
        wsh_expected(parser, "switch requires a right brace");
        return NULL;
    }
    wsh_node_set_end(parser, node, right->end);
    return node;
}

/** Parse a function definition or removal. */
static wsh_ast_node *wsh_parse_function(wsh_parser *parser)
{
    const wsh_token_data *keyword;
    const wsh_token_data *name;
    wsh_ast_node *node;

    keyword = wsh_current_token(parser);
    parser->position++;
    name = wsh_take_kind(parser, WSH_TOKEN_WORD);
    if (name == NULL || !wsh_is_name(name->text, name->text_length)) {
        wsh_expected(parser, "fn requires a valid function name");
        return NULL;
    }
    node = wsh_new_node(
        parser,
        WSH_AST_FUNCTION,
        keyword->start,
        name->end,
        name->text,
        name->text_length);
    if (node == NULL) {
        return NULL;
    }
    if (wsh_at_kind(parser, WSH_TOKEN_LEFT_BRACE)) {
        wsh_ast_node *block = wsh_parse_block(parser);

        if (block == NULL || !wsh_add_child(parser, node, block)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            node,
            block->span.end.utf8_byte_offset);
    }
    return node;
}

/** Parse one command production. */
static wsh_ast_node *wsh_parse_command(wsh_parser *parser)
{
    const wsh_token_data *token;
    wsh_ast_node *node;

    if (!wsh_enter_parse(parser)) {
        return NULL;
    }
    token = wsh_current_token(parser);
    if (token == NULL || token->kind == WSH_TOKEN_EOF) {
        wsh_expected(parser, "expected command");
        wsh_leave_parse(parser);
        return NULL;
    }
    if (token->kind == WSH_TOKEN_LEFT_BRACE) {
        node = wsh_parse_block(parser);
    } else if (wsh_token_is_word(token, "if")) {
        node = wsh_parse_if(parser);
    } else if (wsh_token_is_word(token, "while")) {
        node = wsh_parse_while(parser);
    } else if (wsh_token_is_word(token, "for")) {
        node = wsh_parse_for(parser);
    } else if (wsh_token_is_word(token, "switch")) {
        node = wsh_parse_switch(parser);
    } else if (wsh_token_is_word(token, "fn")) {
        node = wsh_parse_function(parser);
    } else if (wsh_is_reserved_word(token)) {
        wsh_parser_stop(
            parser,
            WSH_SYNTAX_ERROR,
            WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
            "reserved keyword is not valid in this command position",
            token);
        node = NULL;
    } else {
        node = wsh_parse_simple(parser);
    }
    wsh_leave_parse(parser);
    return node;
}

/** Parse zero or more unary prefixes and one command. */
static wsh_ast_node *wsh_parse_unary(wsh_parser *parser)
{
    size_t prefix_start;
    size_t prefix_end;
    wsh_ast_node *operand;

    prefix_start = parser->position;
    while (wsh_at_kind(parser, WSH_TOKEN_BANG) ||
        wsh_at_kind(parser, WSH_TOKEN_AT)) {
        size_t remaining;

        remaining = parser->tree->options.max_parse_depth - parser->depth;
        if (remaining <= 2U ||
            parser->position - prefix_start >= remaining - 2U) {
            parser->failure = WSH_ERR_RESOURCE;
            return NULL;
        }
        parser->position++;
    }
    prefix_end = parser->position;
    operand = wsh_parse_command(parser);
    if (operand == NULL) {
        return NULL;
    }
    while (prefix_end > prefix_start) {
        const wsh_token_data *token;
        wsh_ast_node *node;
        wsh_ast_kind kind;

        token = &parser->stream->tokens[--prefix_end];
        kind = token->kind == WSH_TOKEN_BANG ?
            WSH_AST_NEGATE : WSH_AST_SUBSHELL;
        node = wsh_new_node(
            parser,
            kind,
            token->start,
            operand->span.end.utf8_byte_offset,
            NULL,
            0U);
        if (node == NULL || !wsh_add_child(parser, node, operand)) {
            return NULL;
        }
        operand = node;
    }
    return operand;
}

/** Parse a left-to-right pipeline and its descriptor edges. */
static wsh_ast_node *wsh_parse_pipeline(wsh_parser *parser)
{
    wsh_ast_node *first;
    wsh_ast_node *pipeline;

    first = wsh_parse_unary(parser);
    if (first == NULL || !wsh_at_kind(parser, WSH_TOKEN_PIPE)) {
        return first;
    }
    pipeline = wsh_new_node(
        parser,
        WSH_AST_PIPELINE,
        first->span.start.utf8_byte_offset,
        first->span.end.utf8_byte_offset,
        NULL,
        0U);
    if (pipeline == NULL || !wsh_add_child(parser, pipeline, first)) {
        return NULL;
    }
    while (wsh_at_kind(parser, WSH_TOKEN_PIPE)) {
        const wsh_token_data *operator_token;
        const wsh_token_data *decoration;
        wsh_ast_node *edge;
        wsh_ast_node *stage;
        int ignored_equals;

        operator_token = wsh_current_token(parser);
        parser->position++;
        edge = wsh_new_node(
            parser,
            WSH_AST_PIPE_OPERATOR,
            operator_token->start,
            operator_token->end,
            operator_token->text,
            operator_token->text_length);
        if (edge == NULL) {
            return NULL;
        }
        decoration = wsh_current_token(parser);
        if (decoration != NULL && decoration->kind == WSH_TOKEN_WORD &&
            decoration->start == operator_token->end &&
            decoration->text_length >= 2U && decoration->text[0] == '[') {
            if (!wsh_is_decoration(
                decoration->text,
                decoration->text_length,
                1,
                &ignored_equals)) {
                wsh_parser_stop(
                    parser,
                    WSH_SYNTAX_ERROR,
                    WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
                    "invalid pipeline descriptor decoration",
                    decoration);
                return NULL;
            }
            parser->position++;
            if (wsh_node_set_auxiliary(
                parser,
                edge,
                decoration->text,
                decoration->text_length) != WSH_OK) {
                return NULL;
            }
            wsh_node_set_end(parser, edge, decoration->end);
        }
        stage = wsh_parse_unary(parser);
        if (stage == NULL || !wsh_add_child(parser, pipeline, edge) ||
            !wsh_add_child(parser, pipeline, stage)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            pipeline,
            stage->span.end.utf8_byte_offset);
    }
    return pipeline;
}

/** Parse left-associative conditional-and and conditional-or operators. */
static wsh_ast_node *wsh_parse_and_or(wsh_parser *parser)
{
    wsh_ast_node *left;

    left = wsh_parse_pipeline(parser);
    while (left != NULL && !parser->stopped && parser->failure == WSH_OK &&
        (wsh_at_kind(parser, WSH_TOKEN_AND_IF) ||
            wsh_at_kind(parser, WSH_TOKEN_OR_IF))) {
        const wsh_token_data *operator_token;
        wsh_ast_node *right;
        wsh_ast_node *combined;
        wsh_ast_kind kind;

        operator_token = wsh_current_token(parser);
        parser->position++;
        right = wsh_parse_pipeline(parser);
        if (right == NULL) {
            return NULL;
        }
        kind = operator_token->kind == WSH_TOKEN_AND_IF ?
            WSH_AST_AND : WSH_AST_OR;
        combined = wsh_new_node(
            parser,
            kind,
            left->span.start.utf8_byte_offset,
            right->span.end.utf8_byte_offset,
            NULL,
            0U);
        if (combined == NULL || !wsh_add_child(parser, combined, left) ||
            !wsh_add_child(parser, combined, right)) {
            return NULL;
        }
        left = combined;
    }
    return left;
}

/** Return nonzero when a token is a command-list separator. */
static int wsh_is_list_separator(const wsh_token_data *token)
{
    return token != NULL &&
        (token->kind == WSH_TOKEN_NEWLINE ||
            token->kind == WSH_TOKEN_SEMICOLON ||
            token->kind == WSH_TOKEN_AMPERSAND);
}

/** Parse an ordered command list up to a caller-selected boundary. */
static wsh_ast_node *wsh_parse_command_list(
    wsh_parser *parser,
    wsh_token_kind terminator,
    int stop_at_case)
{
    const wsh_token_data *start_token;
    wsh_ast_node *list;
    size_t start;

    start_token = wsh_current_token(parser);
    start = start_token == NULL ? wsh_source_text(parser->source).length :
        start_token->start;
    list = wsh_new_node(
        parser,
        WSH_AST_COMMAND_LIST,
        start,
        start,
        NULL,
        0U);
    if (list == NULL) {
        return NULL;
    }
    while (wsh_is_list_separator(wsh_current_token(parser))) {
        parser->position++;
    }
    while (!parser->stopped && parser->failure == WSH_OK) {
        const wsh_token_data *current;
        wsh_ast_node *command;
        int background;

        current = wsh_current_token(parser);
        if (current == NULL || current->kind == WSH_TOKEN_EOF ||
            current->kind == terminator ||
            (stop_at_case && wsh_token_is_word(current, "case"))) {
            break;
        }
        command = wsh_parse_and_or(parser);
        if (command == NULL) {
            return NULL;
        }
        background = wsh_at_kind(parser, WSH_TOKEN_AMPERSAND);
        if (background) {
            wsh_ast_node *wrapper = wsh_new_node(
                parser,
                WSH_AST_BACKGROUND,
                command->span.start.utf8_byte_offset,
                wsh_current_token(parser)->end,
                NULL,
                0U);

            if (wrapper == NULL ||
                !wsh_add_child(parser, wrapper, command)) {
                return NULL;
            }
            command = wrapper;
        }
        if (!wsh_add_child(parser, list, command)) {
            return NULL;
        }
        wsh_node_set_end(
            parser,
            list,
            command->span.end.utf8_byte_offset);
        if (!wsh_is_list_separator(wsh_current_token(parser))) {
            current = wsh_current_token(parser);
            if (current != NULL && current->kind != WSH_TOKEN_EOF &&
                current->kind != terminator &&
                !(stop_at_case && wsh_token_is_word(current, "case"))) {
                wsh_parser_stop(
                    parser,
                    WSH_SYNTAX_ERROR,
                    WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
                    "expected command-list separator",
                    current);
                return NULL;
            }
            break;
        }
        while (wsh_is_list_separator(wsh_current_token(parser))) {
            parser->position++;
        }
    }
    return list;
}

/** Append one C string to an immutable-string builder. */
static wsh_result wsh_builder_append_cstr(
    wsh_string_builder *builder,
    const char *text)
{
    if (text == NULL) {
        return WSH_ERR_INVALID;
    }
    return wsh_string_builder_append(
        builder,
        wsh_string_view_from_cstr(text));
}

/** Append one size value using a portable decimal representation. */
static wsh_result wsh_builder_append_size(
    wsh_string_builder *builder,
    size_t value)
{
    char digits[3U * sizeof(size_t) + 1U];
    size_t position;

    position = sizeof(digits);
    do {
        digits[--position] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    return wsh_string_builder_append(
        builder,
        (wsh_string_view){digits + position, sizeof(digits) - position});
}

/** Append escaped node text using only printable ASCII syntax. */
static wsh_result wsh_builder_append_escaped(
    wsh_string_builder *builder,
    const char *text,
    size_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t index;
    wsh_result result;

    result = wsh_builder_append_cstr(builder, "\"");
    for (index = 0U; result == WSH_OK && index < length; index++) {
        unsigned char value = (unsigned char)text[index];
        char escaped[4];
        size_t escaped_length;

        escaped[0] = '\\';
        escaped_length = 2U;
        if (value == (unsigned char)'\"' ||
            value == (unsigned char)'\\') {
            escaped[1] = (char)value;
        } else if (value == (unsigned char)'\n') {
            escaped[1] = 'n';
        } else if (value == (unsigned char)'\r') {
            escaped[1] = 'r';
        } else if (value == (unsigned char)'\t') {
            escaped[1] = 't';
        } else if (value < 0x20U || value == 0x7FU) {
            escaped[1] = 'x';
            escaped[2] = hex[(value >> 4U) & 0x0FU];
            escaped[3] = hex[value & 0x0FU];
            escaped_length = 4U;
        } else if (value >= 0x80U) {
            size_t width;

            if ((value & 0xE0U) == 0xC0U) {
                width = 2U;
            } else if ((value & 0xF0U) == 0xE0U) {
                width = 3U;
            } else {
                width = 4U;
            }
            result = wsh_string_builder_append(
                builder,
                (wsh_string_view){text + index, width});
            index += width - 1U;
            continue;
        } else {
            result = wsh_string_builder_append(
                builder,
                (wsh_string_view){text + index, 1U});
            continue;
        }
        result = wsh_string_builder_append(
            builder,
            (wsh_string_view){escaped, escaped_length});
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_cstr(builder, "\"");
    }
    return result;
}

/** Recursively append one deterministic S-expression node. */
static wsh_result wsh_format_node(
    wsh_string_builder *builder,
    const wsh_ast_node *node)
{
    wsh_result result;
    size_t index;

    if (builder == NULL || node == NULL) {
        return WSH_ERR_INVALID;
    }
    result = wsh_builder_append_cstr(builder, "(");
    if (result == WSH_OK) {
        result = wsh_builder_append_cstr(
            builder,
            wsh_ast_kind_name(node->kind));
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_cstr(builder, "[");
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_size(
            builder,
            node->span.start.utf8_byte_offset);
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_cstr(builder, ":");
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_size(
            builder,
            node->span.end.utf8_byte_offset);
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_cstr(builder, "]");
    }
    if (result == WSH_OK && node->text != NULL) {
        result = wsh_builder_append_cstr(builder, " text=");
        if (result == WSH_OK) {
            result = wsh_builder_append_escaped(
                builder,
                node->text,
                node->text_length);
        }
    }
    if (result == WSH_OK && node->auxiliary != NULL) {
        result = wsh_builder_append_cstr(builder, " aux=");
        if (result == WSH_OK) {
            result = wsh_builder_append_escaped(
                builder,
                node->auxiliary,
                node->auxiliary_length);
        }
    }
    for (index = 0U;
        result == WSH_OK && index < node->child_count;
        index++) {
        result = wsh_builder_append_cstr(builder, " ");
        if (result == WSH_OK) {
            result = wsh_format_node(builder, node->children[index]);
        }
    }
    if (result == WSH_OK) {
        result = wsh_builder_append_cstr(builder, ")");
    }
    return result;
}

/** Copy diagnostics from a lexical owner to a parse owner. */
static wsh_result wsh_copy_stream_diagnostics(
    wsh_parse_tree *tree,
    const wsh_token_stream *stream)
{
    size_t index;

    for (index = 0U; index < stream->diagnostic_count; index++) {
        const wsh_syntax_diagnostic_data *item;
        wsh_result result;

        item = &stream->diagnostics[index];
        result = wsh_tree_add_diagnostic(
            tree,
            item->code,
            item->message,
            &item->span);
        if (result != WSH_OK) {
            return result;
        }
    }
    return WSH_OK;
}

/** @brief Implements wsh_parser_options_init. */
void wsh_parser_options_init(wsh_parser_options *out_options)
{
    if (out_options == NULL) {
        return;
    }
    out_options->allocator = wsh_allocator_default();
    out_options->max_tokens = 262144U;
    out_options->max_ast_nodes = 262144U;
    out_options->max_parse_depth = WSH_PARSER_MAX_PARSE_DEPTH;
    out_options->max_diagnostics = 8U;
}

/** @brief Implements wsh_lex. */
wsh_result wsh_lex(
    const wsh_parser_options *options,
    const wsh_source *source,
    wsh_token_stream **out_stream)
{
    wsh_parser_options applied;
    wsh_token_stream *stream;
    wsh_lexer lexer;
    wsh_result result;

    if (source == NULL || out_stream == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_stream = NULL;
    result = wsh_copy_parser_options(options, &applied);
    if (result != WSH_OK || applied.max_tokens == 0U ||
        applied.max_diagnostics == 0U) {
        return result == WSH_OK ? WSH_ERR_INVALID : result;
    }
    stream = (wsh_token_stream *)wsh_allocate_array(
        &applied.allocator,
        1U,
        sizeof(*stream));
    if (stream == NULL) {
        return WSH_ERR_RESOURCE;
    }
    memset(stream, 0, sizeof(*stream));
    stream->allocator = applied.allocator;
    stream->options = applied;
    stream->status = WSH_SYNTAX_COMPLETE;
    memset(&lexer, 0, sizeof(lexer));
    lexer.source = source;
    lexer.text = wsh_source_text(source);
    lexer.stream = stream;
    result = wsh_run_lexer(&lexer);
    wsh_release(&stream->allocator, lexer.pending_here);
    if (result != WSH_OK) {
        wsh_token_stream_destroy(stream);
        return result;
    }
    *out_stream = stream;
    return WSH_OK;
}

/** @brief Implements wsh_token_stream_destroy. */
void wsh_token_stream_destroy(wsh_token_stream *stream)
{
    size_t index;

    if (stream == NULL) {
        return;
    }
    for (index = 0U; index < stream->token_count; index++) {
        wsh_release(&stream->allocator, stream->tokens[index].text);
        wsh_release(
            &stream->allocator,
            stream->tokens[index].auxiliary);
    }
    for (index = 0U; index < stream->diagnostic_count; index++) {
        wsh_release(
            &stream->allocator,
            stream->diagnostics[index].message);
    }
    wsh_release(&stream->allocator, stream->tokens);
    wsh_release(&stream->allocator, stream->diagnostics);
    wsh_release(&stream->allocator, stream);
}

/** @brief Implements wsh_token_stream_status. */
wsh_syntax_status wsh_token_stream_status(
    const wsh_token_stream *stream)
{
    return stream == NULL ? WSH_SYNTAX_ERROR : stream->status;
}

/** @brief Implements wsh_token_stream_count. */
size_t wsh_token_stream_count(const wsh_token_stream *stream)
{
    return stream == NULL ? 0U : stream->token_count;
}

/** @brief Implements wsh_token_stream_at. */
wsh_result wsh_token_stream_at(
    const wsh_token_stream *stream,
    size_t index,
    wsh_token_view *out_token)
{
    const wsh_token_data *item;

    if (stream == NULL || out_token == NULL ||
        index >= stream->token_count) {
        return WSH_ERR_INVALID;
    }
    item = &stream->tokens[index];
    out_token->kind = item->kind;
    out_token->text.data = item->text;
    out_token->text.length = item->text_length;
    out_token->auxiliary.data = item->auxiliary;
    out_token->auxiliary.length = item->auxiliary_length;
    out_token->span = item->span;
    return WSH_OK;
}

/** @brief Implements wsh_token_stream_diagnostic_count. */
size_t wsh_token_stream_diagnostic_count(
    const wsh_token_stream *stream)
{
    return stream == NULL ? 0U : stream->diagnostic_count;
}

/** @brief Implements wsh_token_stream_diagnostic_at. */
wsh_result wsh_token_stream_diagnostic_at(
    const wsh_token_stream *stream,
    size_t index,
    wsh_syntax_diagnostic_view *out_diagnostic)
{
    const wsh_syntax_diagnostic_data *item;

    if (stream == NULL || out_diagnostic == NULL ||
        index >= stream->diagnostic_count) {
        return WSH_ERR_INVALID;
    }
    item = &stream->diagnostics[index];
    out_diagnostic->code = item->code;
    out_diagnostic->message.data = item->message;
    out_diagnostic->message.length = item->message_length;
    out_diagnostic->span = item->span;
    return WSH_OK;
}

/** @brief Implements wsh_parse. */
wsh_result wsh_parse(
    const wsh_parser_options *options,
    const wsh_source *source,
    wsh_parse_tree **out_tree)
{
    wsh_parser_options applied;
    wsh_token_stream *stream;
    wsh_parse_tree *tree;
    wsh_parser parser;
    wsh_ast_node *list;
    wsh_ast_node *root;
    wsh_result result;

    if (source == NULL || out_tree == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_tree = NULL;
    result = wsh_copy_parser_options(options, &applied);
    if (result != WSH_OK || applied.max_ast_nodes == 0U ||
        applied.max_parse_depth == 0U ||
        applied.max_diagnostics == 0U) {
        return result == WSH_OK ? WSH_ERR_INVALID : result;
    }
    result = wsh_lex(&applied, source, &stream);
    if (result != WSH_OK) {
        return result;
    }
    tree = (wsh_parse_tree *)wsh_allocate_array(
        &applied.allocator,
        1U,
        sizeof(*tree));
    if (tree == NULL) {
        wsh_token_stream_destroy(stream);
        return WSH_ERR_RESOURCE;
    }
    memset(tree, 0, sizeof(*tree));
    tree->allocator = applied.allocator;
    tree->options = applied;
    tree->status = stream->status;
    if (stream->status != WSH_SYNTAX_COMPLETE) {
        result = wsh_copy_stream_diagnostics(tree, stream);
        wsh_token_stream_destroy(stream);
        if (result != WSH_OK) {
            wsh_parse_tree_destroy(tree);
            return result;
        }
        *out_tree = tree;
        return WSH_OK;
    }
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.stream = stream;
    parser.tree = tree;
    parser.failure = WSH_OK;
    list = wsh_parse_command_list(&parser, WSH_TOKEN_EOF, 0);
    if (list != NULL && !parser.stopped &&
        parser.failure == WSH_OK &&
        !wsh_at_kind(&parser, WSH_TOKEN_EOF)) {
        wsh_parser_stop(
            &parser,
            WSH_SYNTAX_ERROR,
            WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED,
            "unexpected trailing token",
            wsh_current_token(&parser));
    }
    root = NULL;
    if (list != NULL && !parser.stopped && parser.failure == WSH_OK) {
        root = wsh_new_node(
            &parser,
            WSH_AST_INPUT,
            0U,
            wsh_source_text(source).length,
            NULL,
            0U);
        if (root != NULL && wsh_add_child(&parser, root, list)) {
            tree->root = root;
            tree->status = WSH_SYNTAX_COMPLETE;
        }
    }
    result = parser.failure;
    wsh_token_stream_destroy(stream);
    if (result != WSH_OK) {
        wsh_parse_tree_destroy(tree);
        return result;
    }
    *out_tree = tree;
    return WSH_OK;
}

/** @brief Implements wsh_parse_tree_destroy. */
void wsh_parse_tree_destroy(wsh_parse_tree *tree)
{
    size_t index;

    if (tree == NULL) {
        return;
    }
    for (index = 0U; index < tree->node_count; index++) {
        wsh_ast_node *node = tree->nodes[index];

        if (node != NULL) {
            wsh_release(&tree->allocator, node->text);
            wsh_release(&tree->allocator, node->auxiliary);
            wsh_release(&tree->allocator, node->children);
            wsh_release(&tree->allocator, node);
        }
    }
    for (index = 0U; index < tree->diagnostic_count; index++) {
        wsh_release(
            &tree->allocator,
            tree->diagnostics[index].message);
    }
    wsh_release(&tree->allocator, tree->nodes);
    wsh_release(&tree->allocator, tree->diagnostics);
    wsh_release(&tree->allocator, tree);
}

/** @brief Implements wsh_parse_tree_status. */
wsh_syntax_status wsh_parse_tree_status(const wsh_parse_tree *tree)
{
    return tree == NULL ? WSH_SYNTAX_ERROR : tree->status;
}

/** @brief Implements wsh_parse_tree_root. */
const wsh_ast_node *wsh_parse_tree_root(const wsh_parse_tree *tree)
{
    return tree == NULL ? NULL : tree->root;
}

/** @brief Implements wsh_parse_tree_diagnostic_count. */
size_t wsh_parse_tree_diagnostic_count(const wsh_parse_tree *tree)
{
    return tree == NULL ? 0U : tree->diagnostic_count;
}

/** @brief Implements wsh_parse_tree_diagnostic_at. */
wsh_result wsh_parse_tree_diagnostic_at(
    const wsh_parse_tree *tree,
    size_t index,
    wsh_syntax_diagnostic_view *out_diagnostic)
{
    const wsh_syntax_diagnostic_data *item;

    if (tree == NULL || out_diagnostic == NULL ||
        index >= tree->diagnostic_count) {
        return WSH_ERR_INVALID;
    }
    item = &tree->diagnostics[index];
    out_diagnostic->code = item->code;
    out_diagnostic->message.data = item->message;
    out_diagnostic->message.length = item->message_length;
    out_diagnostic->span = item->span;
    return WSH_OK;
}

/** @brief Implements wsh_token_kind_name. */
const char *wsh_token_kind_name(wsh_token_kind kind)
{
    static const char *const names[] = {
        "eof", "newline", "word", "quoted-word", "variable", "count",
        "flatten", "semicolon", "ampersand", "and-if", "or-if", "pipe",
        "bang", "at", "caret", "backquote", "left-brace", "right-brace",
        "left-paren", "right-paren", "less", "greater", "append", "here",
        "duplex", "process-read", "process-write", "process-duplex"
    };
    size_t count = sizeof(names) / sizeof(names[0]);

    return (size_t)kind < count ? names[(size_t)kind] : "unknown";
}

/** @brief Implements wsh_ast_kind_name. */
const char *wsh_ast_kind_name(wsh_ast_kind kind)
{
    static const char *const names[] = {
        "input", "command-list", "background", "and", "or", "pipeline",
        "pipe-operator", "negate", "subshell", "simple", "block", "if",
        "while", "for", "switch", "case", "function", "assignment",
        "redirection", "here-body", "word", "quoted-word", "list",
        "variable", "subscript", "count", "flatten",
        "command-substitution", "process-read", "process-write",
        "process-duplex", "concat"
    };
    size_t count = sizeof(names) / sizeof(names[0]);

    return (size_t)kind < count ? names[(size_t)kind] : "unknown";
}

/** @brief Implements wsh_ast_node_kind. */
wsh_ast_kind wsh_ast_node_kind(const wsh_ast_node *node)
{
    return node == NULL ? WSH_AST_INPUT : node->kind;
}

/** @brief Implements wsh_ast_node_span. */
wsh_result wsh_ast_node_span(
    const wsh_ast_node *node,
    wsh_source_span *out_span)
{
    if (node == NULL || out_span == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_span = node->span;
    return WSH_OK;
}

/** @brief Implements wsh_ast_node_text. */
wsh_string_view wsh_ast_node_text(const wsh_ast_node *node)
{
    if (node == NULL || node->text == NULL) {
        return wsh_empty_view();
    }
    return (wsh_string_view){node->text, node->text_length};
}

/** @brief Implements wsh_ast_node_auxiliary. */
wsh_string_view wsh_ast_node_auxiliary(const wsh_ast_node *node)
{
    if (node == NULL || node->auxiliary == NULL) {
        return wsh_empty_view();
    }
    return (wsh_string_view){node->auxiliary, node->auxiliary_length};
}

/** @brief Implements wsh_ast_node_child_count. */
size_t wsh_ast_node_child_count(const wsh_ast_node *node)
{
    return node == NULL ? 0U : node->child_count;
}

/** @brief Implements wsh_ast_node_child_at. */
const wsh_ast_node *wsh_ast_node_child_at(
    const wsh_ast_node *node,
    size_t index)
{
    if (node == NULL || index >= node->child_count) {
        return NULL;
    }
    return node->children[index];
}

/** @brief Implements wsh_parse_tree_format. */
wsh_result wsh_parse_tree_format(
    const wsh_parse_tree *tree,
    wsh_string **out_text)
{
    wsh_string_builder *builder;
    wsh_limits limits;
    wsh_result result;

    if (tree == NULL || out_text == NULL ||
        tree->status != WSH_SYNTAX_COMPLETE || tree->root == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_text = NULL;
    limits = wsh_limits_default();
    result = wsh_string_builder_create(
        &tree->allocator,
        &limits,
        &builder);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_format_node(builder, tree->root);
    if (result == WSH_OK) {
        result = wsh_string_builder_finish(builder, out_text);
    }
    wsh_string_builder_destroy(builder);
    return result;
}
