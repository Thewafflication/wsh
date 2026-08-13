/**
 * @file parser_tests.c
 * @brief Controlled M3 lexer, parser, and immutable AST verification.
 */

#include "wsh/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Stop one test at the first objective failure. */
#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "CHECK failed at %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #expression); \
            return 0; \
        } \
    } while (0)

/** Allocation state for deterministic failure and leak checks. */
typedef struct tracking_allocator {
    size_t calls; /**< Allocation callbacks observed. */
    size_t fail_at; /**< One-based rejected callback, or zero. */
    size_t outstanding; /**< Successful blocks not released. */
} tracking_allocator;

/** One controlled test table entry. */
typedef struct test_case_entry {
    const char *identifier; /**< Stable TC-NNNN identifier. */
    int (*function)(void); /**< Test function. */
} test_case_entry;

/** Allocate a tracked block unless its ordinal is selected to fail. */
static void *tracking_allocate(void *user_data, size_t size)
{
    tracking_allocator *tracker = (tracking_allocator *)user_data;
    void *pointer;

    tracker->calls++;
    if (tracker->fail_at != 0U && tracker->calls == tracker->fail_at) {
        return NULL;
    }
    pointer = malloc(size == 0U ? 1U : size);
    if (pointer != NULL) {
        tracker->outstanding++;
    }
    return pointer;
}

/** Release a tracked block. */
static void tracking_deallocate(void *user_data, void *pointer)
{
    tracking_allocator *tracker = (tracking_allocator *)user_data;

    if (pointer != NULL) {
        if (tracker->outstanding == 0U) {
            fprintf(stderr, "tracking allocator underflow\n");
            abort();
        }
        tracker->outstanding--;
        free(pointer);
    }
}

/** Bind the portable allocator contract to a tracking state. */
static wsh_allocator tracked_allocator(tracking_allocator *tracker)
{
    wsh_allocator allocator;

    allocator.user_data = tracker;
    allocator.allocate = tracking_allocate;
    allocator.deallocate = tracking_deallocate;
    return allocator;
}

/** Create and parse one raw source, then release the decoded source owner. */
static wsh_result parse_bytes(
    const unsigned char *bytes,
    size_t length,
    const wsh_parser_options *options,
    wsh_parse_tree **out_tree)
{
    wsh_source *source;
    wsh_result result;

    if (out_tree == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_tree = NULL;
    result = wsh_source_create(NULL, NULL, bytes, length, &source);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_parse(options, source, out_tree);
    wsh_source_destroy(source);
    return result;
}

/** Parse one C string as strict UTF-8. */
static wsh_result parse_text(
    const char *text,
    const wsh_parser_options *options,
    wsh_parse_tree **out_tree)
{
    return parse_bytes(
        (const unsigned char *)text,
        strlen(text),
        options,
        out_tree);
}

/** Return nonzero when a view equals one C string. */
static int view_equals(wsh_string_view view, const char *text)
{
    return wsh_string_view_equal(
        view,
        wsh_string_view_from_cstr(text));
}

/** Return nonzero when a view contains an ASCII needle. */
static int view_contains(wsh_string_view view, const char *needle)
{
    size_t needle_length;
    size_t position;

    needle_length = strlen(needle);
    if (needle_length == 0U) {
        return 1;
    }
    if (view.data == NULL || needle_length > view.length) {
        return 0;
    }
    for (position = 0U;
        position <= view.length - needle_length;
        position++) {
        if (memcmp(view.data + position, needle, needle_length) == 0) {
            return 1;
        }
    }
    return 0;
}

/** Count one AST kind recursively. */
static size_t count_kind(const wsh_ast_node *node, wsh_ast_kind kind)
{
    size_t count;
    size_t index;

    if (node == NULL) {
        return 0U;
    }
    count = wsh_ast_node_kind(node) == kind ? 1U : 0U;
    for (index = 0U; index < wsh_ast_node_child_count(node); index++) {
        count += count_kind(wsh_ast_node_child_at(node, index), kind);
    }
    return count;
}

/** Find one kind occurrence in depth-first order. */
static const wsh_ast_node *find_kind(
    const wsh_ast_node *node,
    wsh_ast_kind kind,
    size_t *ordinal)
{
    size_t index;

    if (node == NULL || ordinal == NULL) {
        return NULL;
    }
    if (wsh_ast_node_kind(node) == kind) {
        if (*ordinal == 0U) {
            return node;
        }
        (*ordinal)--;
    }
    for (index = 0U; index < wsh_ast_node_child_count(node); index++) {
        const wsh_ast_node *found;

        found = find_kind(
            wsh_ast_node_child_at(node, index),
            kind,
            ordinal);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

/** Parse and require one status and root-publication rule. */
static int has_status(const char *text, wsh_syntax_status status)
{
    wsh_parse_tree *tree;
    wsh_result result;
    int valid;

    result = parse_text(text, NULL, &tree);
    if (result != WSH_OK) {
        return 0;
    }
    valid = wsh_parse_tree_status(tree) == status;
    if (status == WSH_SYNTAX_COMPLETE) {
        valid = valid && wsh_parse_tree_root(tree) != NULL;
    } else {
        valid = valid && wsh_parse_tree_root(tree) == NULL &&
            wsh_parse_tree_diagnostic_count(tree) == 1U;
    }
    wsh_parse_tree_destroy(tree);
    return valid;
}

/** Format a complete tree and copy its bytes to a standard allocation. */
static char *copy_format(const wsh_parse_tree *tree)
{
    wsh_string *formatted;
    wsh_string_view view;
    char *copy;

    if (wsh_parse_tree_format(tree, &formatted) != WSH_OK) {
        return NULL;
    }
    view = wsh_string_bytes(formatted);
    copy = (char *)malloc(view.length + 1U);
    if (copy != NULL) {
        memcpy(copy, view.data, view.length);
        copy[view.length] = '\0';
    }
    wsh_string_destroy(formatted);
    return copy;
}

/** Verify apostrophe quotation and doubled apostrophe decoding. */
static int test_tc_0008(void)
{
    wsh_parse_tree *tree;
    const wsh_ast_node *root;
    const wsh_ast_node *node;
    size_t ordinal;

    CHECK(parse_text("echo '' 'a''b' 'line1\nline2'", NULL, &tree) ==
        WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_QUOTED_WORD) == 3U);
    ordinal = 0U;
    node = find_kind(root, WSH_AST_QUOTED_WORD, &ordinal);
    CHECK(node != NULL && view_equals(wsh_ast_node_text(node), ""));
    ordinal = 1U;
    node = find_kind(root, WSH_AST_QUOTED_WORD, &ordinal);
    CHECK(node != NULL && view_equals(wsh_ast_node_text(node), "a'b"));
    ordinal = 2U;
    node = find_kind(root, WSH_AST_QUOTED_WORD, &ordinal);
    CHECK(node != NULL &&
        view_equals(wsh_ast_node_text(node), "line1\nline2"));
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("echo 'open", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify explicit carets, free carets, and whitespace separation. */
static int test_tc_0009(void)
{
    wsh_parse_tree *tree;
    const wsh_ast_node *root;

    CHECK(parse_text("echo a^'b' a'c' a 'd'", NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_CONCAT) == 2U);
    CHECK(count_kind(root, WSH_AST_QUOTED_WORD) == 3U);
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("echo a^", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify representative lexical and grammar productions are inert. */
static int test_tc_0010(void)
{
    const char *text =
        "# comment\nx=(one two); echo $x $\"x $#x $x(1-2)\n";
    wsh_source *source;
    wsh_token_stream *stream;
    wsh_parse_tree *tree;
    const wsh_ast_node *root;
    size_t index;

    CHECK(wsh_source_create(
        NULL,
        NULL,
        (const unsigned char *)text,
        strlen(text),
        &source) == WSH_OK);
    CHECK(wsh_lex(NULL, source, &stream) == WSH_OK);
    CHECK(wsh_token_stream_status(stream) == WSH_SYNTAX_COMPLETE);
    for (index = 0U; index < wsh_token_stream_count(stream); index++) {
        wsh_token_view token;

        CHECK(wsh_token_stream_at(stream, index, &token) == WSH_OK);
        CHECK(token.kind != WSH_TOKEN_EOF ||
            index + 1U == wsh_token_stream_count(stream));
    }
    CHECK(wsh_parse(NULL, source, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_ASSIGNMENT) == 1U);
    CHECK(count_kind(root, WSH_AST_VARIABLE) == 2U);
    CHECK(count_kind(root, WSH_AST_COUNT) == 1U);
    CHECK(count_kind(root, WSH_AST_FLATTEN) == 1U);
    CHECK(count_kind(root, WSH_AST_SUBSCRIPT) == 1U);
    wsh_parse_tree_destroy(tree);
    wsh_token_stream_destroy(stream);
    wsh_source_destroy(source);
    return 1;
}

/** Verify list, conditional, pipeline, and unary precedence. */
static int test_tc_0014(void)
{
    wsh_parse_tree *tree;
    const wsh_ast_node *root;

    CHECK(parse_text("! a | @ b && c || d &\ne", NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_NEGATE) == 1U);
    CHECK(count_kind(root, WSH_AST_SUBSHELL) == 1U);
    CHECK(count_kind(root, WSH_AST_PIPELINE) == 1U);
    CHECK(count_kind(root, WSH_AST_AND) == 1U);
    CHECK(count_kind(root, WSH_AST_OR) == 1U);
    CHECK(count_kind(root, WSH_AST_BACKGROUND) == 1U);
    wsh_parse_tree_destroy(tree);
    return 1;
}

/** Verify ordered redirections, descriptor forms, and here bodies. */
static int test_tc_0015(void)
{
    const char *text =
        "cmd <in >out >>log >[2=1] <<EOF\nbody $inert\nEOF\n";
    wsh_parse_tree *tree;
    const wsh_ast_node *root;
    const wsh_ast_node *body;
    size_t ordinal;

    CHECK(parse_text(text, NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_REDIRECTION) == 5U);
    CHECK(count_kind(root, WSH_AST_HERE_BODY) == 1U);
    ordinal = 0U;
    body = find_kind(root, WSH_AST_HERE_BODY, &ordinal);
    CHECK(body != NULL &&
        view_equals(wsh_ast_node_text(body), "body $inert\n"));
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("cmd >[2=x]", WSH_SYNTAX_ERROR));
    CHECK(has_status("cmd <<EOF\nbody\n", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify linear pipelines, edge descriptors, and missing stages. */
static int test_tc_0016(void)
{
    wsh_parse_tree *tree;
    const wsh_ast_node *root;
    const wsh_ast_node *edge;
    size_t ordinal;

    CHECK(parse_text("a |[2=3] b | c", NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_PIPELINE) == 1U);
    CHECK(count_kind(root, WSH_AST_PIPE_OPERATOR) == 2U);
    ordinal = 0U;
    edge = find_kind(root, WSH_AST_PIPE_OPERATOR, &ordinal);
    CHECK(edge != NULL &&
        view_equals(wsh_ast_node_auxiliary(edge), "[2=3]"));
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("a |", WSH_SYNTAX_INCOMPLETE));
    CHECK(has_status("a | ;", WSH_SYNTAX_ERROR));
    return 1;
}

/** Verify compound commands, functions, and open forms. */
static int test_tc_0017(void)
{
    const char *text =
        "{ echo hi }; fn f { echo }; fn gone; "
        "if (test) { yes } if not { no }; "
        "while (test) { body }; for (x in a b) { echo $x }; "
        "switch (x) { case ; empty; case a; one; case b c; two }";
    wsh_parse_tree *tree;
    const wsh_ast_node *root;

    CHECK(parse_text(text, NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_BLOCK) >= 5U);
    CHECK(count_kind(root, WSH_AST_FUNCTION) == 2U);
    CHECK(count_kind(root, WSH_AST_IF) == 1U);
    CHECK(count_kind(root, WSH_AST_WHILE) == 1U);
    CHECK(count_kind(root, WSH_AST_FOR) == 1U);
    CHECK(count_kind(root, WSH_AST_SWITCH) == 1U);
    CHECK(count_kind(root, WSH_AST_CASE) == 3U);
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("fn f { echo", WSH_SYNTAX_INCOMPLETE));
    CHECK(has_status("if (test", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify malformed/incomplete partitioning and objective diagnostics. */
static int test_tc_0023(void)
{
    wsh_parse_tree *tree;
    wsh_syntax_diagnostic_view diagnostic;

    CHECK(parse_text("}", NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_ERROR);
    CHECK(wsh_parse_tree_root(tree) == NULL);
    CHECK(wsh_parse_tree_diagnostic_at(tree, 0U, &diagnostic) == WSH_OK);
    CHECK(diagnostic.code == WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED);
    CHECK(diagnostic.span.start.line == 1U);
    CHECK(diagnostic.span.start.scalar_column == 1U);
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("echo '", WSH_SYNTAX_INCOMPLETE));
    CHECK(has_status("echo `{a", WSH_SYNTAX_INCOMPLETE));
    CHECK(has_status("for (x in a", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify Windows path spellings retain ordinary backslashes. */
static int test_tc_0038(void)
{
    const char *paths[] = {
        "C:\\dir\\file", "C:relative", "\\\\server\\share\\",
        ".\\relative\\", "\\\\?\\C:\\long\\",
        "\\\\.\\PIPE\\name"
    };
    const char *text =
        "show C:\\dir\\file C:relative \\\\server\\share\\ "
        ".\\relative\\ \\\\?\\C:\\long\\ \\\\.\\PIPE\\name";
    wsh_parse_tree *tree;
    const wsh_ast_node *root;
    size_t index;

    CHECK(parse_text(text, NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_WORD) == 7U);
    for (index = 0U; index < sizeof(paths) / sizeof(paths[0]); index++) {
        const wsh_ast_node *node;
        size_t ordinal = index + 1U;

        node = find_kind(root, WSH_AST_WORD, &ordinal);
        CHECK(node != NULL &&
            view_equals(wsh_ast_node_text(node), paths[index]));
    }
    wsh_parse_tree_destroy(tree);
    return 1;
}

/** Verify inert process-substitution AST forms. */
static int test_tc_0051(void)
{
    wsh_parse_tree *tree;
    const wsh_ast_node *root;

    CHECK(parse_text("echo <{a} >{b} <>{c}", NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_PROCESS_READ) == 1U);
    CHECK(count_kind(root, WSH_AST_PROCESS_WRITE) == 1U);
    CHECK(count_kind(root, WSH_AST_PROCESS_DUPLEX) == 1U);
    CHECK(count_kind(root, WSH_AST_BLOCK) == 3U);
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("echo <{a", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify inert command substitution, nesting, and concatenation. */
static int test_tc_0052(void)
{
    wsh_parse_tree *tree;
    const wsh_ast_node *root;

    CHECK(parse_text("echo pre`{a `{b}}post", NULL, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    root = wsh_parse_tree_root(tree);
    CHECK(count_kind(root, WSH_AST_COMMAND_SUBSTITUTION) == 2U);
    CHECK(count_kind(root, WSH_AST_CONCAT) >= 1U);
    wsh_parse_tree_destroy(tree);
    CHECK(has_status("echo `{a", WSH_SYNTAX_INCOMPLETE));
    return 1;
}

/** Verify parser allocation failure atomicity and cleanup. */
static int test_tc_0082(void)
{
    const char *text =
        "x=(a b); if (ok) { echo $x | sink }; "
        "echo <{source} `{nested}";
    size_t fail_at;
    int completed;

    completed = 0;
    for (fail_at = 1U; fail_at < 1024U; fail_at++) {
        tracking_allocator tracker;
        wsh_parser_options options;
        wsh_parse_tree *tree;
        wsh_result result;

        memset(&tracker, 0, sizeof(tracker));
        tracker.fail_at = fail_at;
        wsh_parser_options_init(&options);
        options.allocator = tracked_allocator(&tracker);
        result = parse_text(text, &options, &tree);
        if (result == WSH_OK) {
            CHECK(tree != NULL);
            CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
            wsh_parse_tree_destroy(tree);
            completed = 1;
        } else {
            CHECK(result == WSH_ERR_RESOURCE);
            CHECK(tree == NULL);
        }
        CHECK(tracker.outstanding == 0U);
        if (completed) {
            break;
        }
    }
    CHECK(completed);
    return 1;
}

/** Parse and format raw bytes for source-equivalence checks. */
static char *format_bytes(const unsigned char *bytes, size_t length)
{
    wsh_parse_tree *tree;
    char *formatted;

    if (parse_bytes(bytes, length, NULL, &tree) != WSH_OK) {
        return NULL;
    }
    if (wsh_parse_tree_status(tree) != WSH_SYNTAX_COMPLETE) {
        wsh_parse_tree_destroy(tree);
        return NULL;
    }
    formatted = copy_format(tree);
    wsh_parse_tree_destroy(tree);
    return formatted;
}

/** Verify normalized AST equivalence across encodings and line endings. */
static int test_tc_0083(void)
{
    static const unsigned char utf8_lf[] =
        "echo \xF0\x9F\x98\x80\nnext";
    static const unsigned char utf8_bom_crlf[] = {
        0xEFU, 0xBBU, 0xBFU, 'e', 'c', 'h', 'o', ' ',
        0xF0U, 0x9FU, 0x98U, 0x80U, '\r', '\n',
        'n', 'e', 'x', 't'
    };
    static const unsigned char utf16_le_cr[] = {
        0xFFU, 0xFEU, 'e', 0, 'c', 0, 'h', 0, 'o', 0, ' ', 0,
        0x3DU, 0xD8U, 0x00U, 0xDEU, '\r', 0,
        'n', 0, 'e', 0, 'x', 0, 't', 0
    };
    static const unsigned char utf16_be_lf[] = {
        0xFEU, 0xFFU, 0, 'e', 0, 'c', 0, 'h', 0, 'o', 0, ' ',
        0xD8U, 0x3DU, 0xDEU, 0x00U, 0, '\n',
        0, 'n', 0, 'e', 0, 'x', 0, 't'
    };
    char *reference;
    char *variant;

    reference = format_bytes(utf8_lf, sizeof(utf8_lf) - 1U);
    CHECK(reference != NULL);
    variant = format_bytes(utf8_bom_crlf, sizeof(utf8_bom_crlf));
    CHECK(variant != NULL && strcmp(reference, variant) == 0);
    free(variant);
    variant = format_bytes(utf16_le_cr, sizeof(utf16_le_cr));
    CHECK(variant != NULL && strcmp(reference, variant) == 0);
    free(variant);
    variant = format_bytes(utf16_be_lf, sizeof(utf16_be_lf));
    CHECK(variant != NULL && strcmp(reference, variant) == 0);
    free(variant);
    CHECK(strstr(reference, "\xF0\x9F\x98\x80") != NULL);
    free(reference);
    return 1;
}

/** Verify token, node, and parse-depth ceilings. */
static int test_tc_0084(void)
{
    wsh_parser_options options;
    wsh_parse_tree *tree;
    char unary[65];

    wsh_parser_options_init(&options);
    options.max_tokens = 2U;
    options.max_ast_nodes = 4U;
    CHECK(parse_text("a", &options, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    wsh_parse_tree_destroy(tree);
    options.max_tokens = 1U;
    CHECK(parse_text("a", &options, &tree) == WSH_ERR_RESOURCE);
    CHECK(tree == NULL);
    wsh_parser_options_init(&options);
    options.max_ast_nodes = 3U;
    CHECK(parse_text("a", &options, &tree) == WSH_ERR_RESOURCE);
    CHECK(tree == NULL);
    wsh_parser_options_init(&options);
    options.max_parse_depth = 2U;
    CHECK(parse_text("echo `{echo `{x}}", &options, &tree) ==
        WSH_ERR_RESOURCE);
    CHECK(tree == NULL);
    memset(unary, '!', sizeof(unary) - 2U);
    unary[sizeof(unary) - 2U] = 'a';
    unary[sizeof(unary) - 1U] = '\0';
    wsh_parser_options_init(&options);
    options.max_parse_depth = 8U;
    CHECK(parse_text(unary, &options, &tree) == WSH_ERR_RESOURCE);
    CHECK(tree == NULL);
    options.max_parse_depth = WSH_PARSER_MAX_PARSE_DEPTH + 1U;
    CHECK(parse_text("a", &options, &tree) == WSH_ERR_INVALID);
    CHECK(tree == NULL);
    return 1;
}

/** Verify deterministic replay over a generated bounded ASCII corpus. */
static int test_tc_0085(void)
{
    static const char alphabet[] =
        "abc012 ;&|^$`'{}()<>!@#[]=-\\\n";
    unsigned long state;
    size_t corpus_index;

    state = 0xC0DEC0DEUL;
    for (corpus_index = 0U; corpus_index < 4096U; corpus_index++) {
        char input[65];
        size_t length;
        size_t index;
        wsh_parse_tree *left;
        wsh_parse_tree *right;
        wsh_result left_result;
        wsh_result right_result;

        state = state * 1664525UL + 1013904223UL;
        length = (size_t)(state % 65UL);
        for (index = 0U; index < length; index++) {
            state = state * 1664525UL + 1013904223UL;
            input[index] = alphabet[state % (sizeof(alphabet) - 1U)];
        }
        input[length] = '\0';
        left_result = parse_text(input, NULL, &left);
        right_result = parse_text(input, NULL, &right);
        CHECK(left_result == right_result);
        if (left_result == WSH_OK) {
            wsh_syntax_status status = wsh_parse_tree_status(left);

            CHECK(status == wsh_parse_tree_status(right));
            CHECK(wsh_parse_tree_diagnostic_count(left) ==
                wsh_parse_tree_diagnostic_count(right));
            if (status == WSH_SYNTAX_COMPLETE) {
                char *left_format = copy_format(left);
                char *right_format = copy_format(right);

                CHECK(left_format != NULL && right_format != NULL);
                CHECK(strcmp(left_format, right_format) == 0);
                free(left_format);
                free(right_format);
            } else {
                wsh_syntax_diagnostic_view left_diagnostic;
                wsh_syntax_diagnostic_view right_diagnostic;

                CHECK(wsh_parse_tree_root(left) == NULL);
                CHECK(wsh_parse_tree_root(right) == NULL);
                CHECK(wsh_parse_tree_diagnostic_at(
                    left,
                    0U,
                    &left_diagnostic) == WSH_OK);
                CHECK(wsh_parse_tree_diagnostic_at(
                    right,
                    0U,
                    &right_diagnostic) == WSH_OK);
                CHECK(left_diagnostic.code == right_diagnostic.code);
                CHECK(left_diagnostic.span.start.utf8_byte_offset ==
                    right_diagnostic.span.start.utf8_byte_offset);
            }
            wsh_parse_tree_destroy(left);
            wsh_parse_tree_destroy(right);
        } else {
            CHECK(left == NULL && right == NULL);
        }
    }
    return 1;
}

/** Dispatch exactly one controlled M3 test case. */
int main(int argument_count, char **arguments)
{
    static const test_case_entry cases[] = {
        {"TC-0008", test_tc_0008},
        {"TC-0009", test_tc_0009},
        {"TC-0010", test_tc_0010},
        {"TC-0014", test_tc_0014},
        {"TC-0015", test_tc_0015},
        {"TC-0016", test_tc_0016},
        {"TC-0017", test_tc_0017},
        {"TC-0023", test_tc_0023},
        {"TC-0038", test_tc_0038},
        {"TC-0051", test_tc_0051},
        {"TC-0052", test_tc_0052},
        {"TC-0082", test_tc_0082},
        {"TC-0083", test_tc_0083},
        {"TC-0084", test_tc_0084},
        {"TC-0085", test_tc_0085}
    };
    size_t index;

    if (argument_count != 2) {
        fprintf(stderr, "usage: parser-tests TC-NNNN\n");
        return 2;
    }
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        if (strcmp(arguments[1], cases[index].identifier) == 0) {
            if (!cases[index].function()) {
                fprintf(stderr, "%s: FAIL\n", cases[index].identifier);
                return 1;
            }
            printf("%s: PASS\n", cases[index].identifier);
            return 0;
        }
    }
    fprintf(stderr, "unknown test case: %s\n", arguments[1]);
    return 2;
}
