/**
 * @file resource_bounds.c
 * @brief M9 resource-exhaustion bound checks for the portable core.
 *
 * Verifies that the configured wsh_limits ceilings actually bound the core:
 * oversized source, strings, lists, variables, tokens, and parse trees are
 * rejected or capped rather than allowed to consume unbounded memory. This is
 * the availability control from WSH-DFS-0001 exercised through the public API.
 */

#include "wsh/core.h"
#include "wsh/parser.h"

#include <stdio.h>
#include <string.h>

/** Print one objective failed expression and leave the case immediately. */
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

/** One test case table entry. */
typedef struct test_case_entry {
    const char *identifier;
    int (*function)(void);
} test_case_entry;

/** Oversized decoded source is rejected by the configured ceiling. */
static int test_source_ceiling(void)
{
    wsh_limits limits = wsh_limits_default();
    unsigned char bytes[64];
    wsh_source *source = NULL;

    limits.max_source_bytes = 8U;
    memset(bytes, (int)'a', sizeof(bytes));
    CHECK(wsh_source_create(NULL, &limits, bytes, sizeof(bytes), &source) ==
          WSH_ERR_RESOURCE);
    CHECK(source == NULL);
    return 1;
}

/** An immutable string beyond the ceiling is rejected. */
static int test_string_ceiling(void)
{
    wsh_limits limits = wsh_limits_default();
    wsh_string *string = NULL;

    limits.max_string_bytes = 4U;
    CHECK(wsh_string_create(
              NULL, &limits,
              wsh_string_view_from_cstr("abcdefgh"),
              &string) == WSH_ERR_RESOURCE);
    CHECK(string == NULL);
    return 1;
}

/** A flat list beyond the item ceiling is rejected at the offending append. */
static int test_list_ceiling(void)
{
    wsh_limits limits = wsh_limits_default();
    wsh_value_builder *builder = NULL;

    limits.max_list_items = 2U;
    CHECK(wsh_value_builder_create(NULL, &limits, &builder) == WSH_OK);
    CHECK(wsh_value_builder_append(builder, wsh_string_view_from_cstr("one")) ==
          WSH_OK);
    CHECK(wsh_value_builder_append(builder, wsh_string_view_from_cstr("two")) ==
          WSH_OK);
    CHECK(wsh_value_builder_append(builder, wsh_string_view_from_cstr("three")) ==
          WSH_ERR_RESOURCE);
    wsh_value_builder_destroy(builder);
    return 1;
}

/** Build a single-element value for variable tests. */
static wsh_result single_value(const char *text, wsh_value **out_value)
{
    wsh_value_builder *builder = NULL;
    wsh_result result;

    *out_value = NULL;
    result = wsh_value_builder_create(NULL, NULL, &builder);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_value_builder_append(builder, wsh_string_view_from_cstr(text));
    if (result != WSH_OK) {
        wsh_value_builder_destroy(builder);
        return result;
    }
    return wsh_value_builder_finish(builder, out_value);
}

/** Variables beyond the ceiling are rejected. */
static int test_variable_ceiling(void)
{
    wsh_context_options options;
    wsh_context *context = NULL;
    wsh_value *value = NULL;

    wsh_context_options_init(&options);
    options.limits = wsh_limits_default();
    options.limits.max_variables = 2U;
    CHECK(wsh_context_create(&options, &context) == WSH_OK);
    CHECK(single_value("v", &value) == WSH_OK);

    CHECK(wsh_context_set_variable(context, wsh_string_view_from_cstr("a"), value) ==
          WSH_OK);
    CHECK(wsh_context_set_variable(context, wsh_string_view_from_cstr("b"), value) ==
          WSH_OK);
    CHECK(wsh_context_set_variable(context, wsh_string_view_from_cstr("c"), value) ==
          WSH_ERR_RESOURCE);
    CHECK(wsh_context_variable_count(context) == 2u);

    wsh_value_destroy(value);
    wsh_context_destroy(context);
    return 1;
}

/** Retained diagnostics never exceed the configured ceiling. */
static int test_diagnostic_ceiling(void)
{
    wsh_context_options options;
    wsh_context *context = NULL;
    int index;

    wsh_context_options_init(&options);
    options.limits = wsh_limits_default();
    options.limits.max_diagnostics = 2U;
    CHECK(wsh_context_create(&options, &context) == WSH_OK);

    for (index = 0; index < 6; ++index) {
        (void)wsh_context_add_diagnostic(
            context,
            WSH_DIAGNOSTIC_WARNING,
            WSH_DIAGNOSTIC_LIMIT,
            wsh_string_view_from_cstr("bounded"),
            wsh_string_view_from_cstr("src"),
            NULL);
    }
    CHECK(wsh_context_diagnostic_count(context) <= 2u);

    wsh_context_destroy(context);
    return 1;
}

/** Outcome of a bounded lex or parse attempt. */
typedef enum bound_outcome {
    /** The call was rejected with a resource error. */
    BOUND_REJECTED = 0,
    /** The call succeeded and reported a complete result. */
    BOUND_COMPLETE = 1,
    /** The call succeeded but reported a non-complete (bounded) result. */
    BOUND_INCOMPLETE = 2
} bound_outcome;

/** Lex a small ASCII source with the given token ceiling. */
static bound_outcome lex_with_token_limit(size_t max_tokens)
{
    wsh_parser_options options;
    wsh_source *source = NULL;
    wsh_token_stream *stream = NULL;
    const char *text = "a b c d e f g h";
    bound_outcome outcome = BOUND_REJECTED;

    wsh_parser_options_init(&options);
    options.max_tokens = max_tokens;
    if (wsh_source_create(NULL, NULL, (const unsigned char *)text, strlen(text),
            &source) != WSH_OK) {
        return BOUND_REJECTED;
    }
    if (wsh_lex(&options, source, &stream) == WSH_OK) {
        outcome = (wsh_token_stream_status(stream) == WSH_SYNTAX_COMPLETE)
            ? BOUND_COMPLETE : BOUND_INCOMPLETE;
        wsh_token_stream_destroy(stream);
    }
    wsh_source_destroy(source);
    return outcome;
}

/**
 * A token ceiling bounds lexing: a tight limit is rejected or reports a
 * non-complete result, while a generous limit completes.
 */
static int test_token_ceiling(void)
{
    CHECK(lex_with_token_limit(3U) != BOUND_COMPLETE);
    CHECK(lex_with_token_limit(64U) == BOUND_COMPLETE);
    return 1;
}

/** Parse a small ASCII source with the given AST-node ceiling. */
static bound_outcome parse_with_node_limit(size_t max_ast_nodes)
{
    wsh_parser_options options;
    wsh_source *source = NULL;
    wsh_parse_tree *tree = NULL;
    const char *text = "echo a b c d e f";
    bound_outcome outcome = BOUND_REJECTED;

    wsh_parser_options_init(&options);
    options.max_ast_nodes = max_ast_nodes;
    if (wsh_source_create(NULL, NULL, (const unsigned char *)text, strlen(text),
            &source) != WSH_OK) {
        return BOUND_REJECTED;
    }
    if (wsh_parse(&options, source, &tree) == WSH_OK) {
        outcome = (wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE)
            ? BOUND_COMPLETE : BOUND_INCOMPLETE;
        wsh_parse_tree_destroy(tree);
    }
    wsh_source_destroy(source);
    return outcome;
}

/** A parse-node ceiling bounds parsing rather than accepting the whole tree. */
static int test_ast_node_ceiling(void)
{
    CHECK(parse_with_node_limit(2U) != BOUND_COMPLETE);
    CHECK(parse_with_node_limit(256U) == BOUND_COMPLETE);
    return 1;
}

static const test_case_entry test_cases[] = {
    {"SOURCE-CEILING", test_source_ceiling},
    {"STRING-CEILING", test_string_ceiling},
    {"LIST-CEILING", test_list_ceiling},
    {"VARIABLE-CEILING", test_variable_ceiling},
    {"DIAGNOSTIC-CEILING", test_diagnostic_ceiling},
    {"TOKEN-CEILING", test_token_ceiling},
    {"AST-NODE-CEILING", test_ast_node_ceiling}
};

int main(int argc, char **argv)
{
    size_t index;
    int selected = 0;

    for (index = 0U;
            index < sizeof(test_cases) / sizeof(test_cases[0]);
            ++index) {
        if (argc > 1 && strcmp(argv[1], test_cases[index].identifier) != 0) {
            continue;
        }
        selected = 1;
        if (!test_cases[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", test_cases[index].identifier);
            return 1;
        }
        printf("[PASS] %s\n", test_cases[index].identifier);
    }
    if (!selected) {
        fprintf(stderr, "Unknown test case\n");
        return 2;
    }
    return 0;
}
