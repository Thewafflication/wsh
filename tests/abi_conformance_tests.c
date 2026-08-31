/**
 * @file abi_conformance_tests.c
 * @brief M8 embedding ABI 1 conformance and misuse cases.
 *
 * These cases use only the installed public headers and link against the
 * static embedding library, exercising the surface exactly as an external
 * host would. They assert the frozen ABI value, the version accessors, the
 * context/value/diagnostic lifecycle, and that documented misuse returns a
 * defined error instead of crashing.
 */

#include "wsh/core.h"
#include "wsh/evaluator.h"
#include "wsh/parser.h"
#include "wsh/wsh.h"

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
    /** Stable identifier. */
    const char *identifier;
    /** Test implementation returning nonzero only on Pass. */
    int (*function)(void);
} test_case_entry;

/** Build a single-element immutable value from a C string. */
static wsh_result make_single_value(const char *item, wsh_value **out_value)
{
    wsh_value_builder *builder;
    wsh_result result;

    *out_value = NULL;
    result = wsh_value_builder_create(NULL, NULL, &builder);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_value_builder_append(
        builder,
        wsh_string_view_from_cstr(item));
    if (result != WSH_OK) {
        wsh_value_builder_destroy(builder);
        return result;
    }
    return wsh_value_builder_finish(builder, out_value);
}

/** State borrowed by the host-command callback. */
typedef struct host_command_state {
    wsh_evaluator *evaluator;
    const wsh_parse_tree *tree;
    size_t calls;
    wsh_result nested_result;
    wsh_result nested_signal_result;
} host_command_state;

/** Validate structured arguments and return one output item and status. */
static wsh_result host_probe(
    void *user_data,
    wsh_context *context,
    const wsh_value *arguments,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    host_command_state *state = (host_command_state *)user_data;
    wsh_status_list *nested_status = NULL;
    wsh_string_view first;
    wsh_string_view second;

    if (state == NULL || context == NULL || arguments == NULL ||
        output == NULL || status == NULL || wsh_value_count(arguments) != 2U ||
        wsh_value_at(arguments, 0U, &first) != WSH_OK ||
        wsh_value_at(arguments, 1U, &second) != WSH_OK ||
        !wsh_string_view_equal(first, wsh_string_view_from_cstr("alpha")) ||
        !wsh_string_view_equal(second, wsh_string_view_from_cstr("beta"))) {
        return WSH_ERR_INVALID;
    }
    state->calls += 1U;
    state->nested_result = wsh_evaluate(
        state->evaluator, state->tree, &nested_status);
    wsh_status_list_destroy(nested_status);
    nested_status = NULL;
    state->nested_signal_result = wsh_evaluator_invoke_signal(
        state->evaluator,
        wsh_string_view_from_cstr("sigint"),
        130U,
        &nested_status);
    wsh_status_list_destroy(nested_status);
    if (wsh_value_builder_append(
            output, wsh_string_view_from_cstr("host-output")) != WSH_OK) {
        return WSH_ERR_RESOURCE;
    }
    return wsh_status_builder_append(status, 23U);
}

/** Return a defined callback failure without publishing partial builders. */
static wsh_result host_failure(
    void *user_data,
    wsh_context *context,
    const wsh_value *arguments,
    wsh_value_builder *output,
    wsh_status_builder *status)
{
    (void)user_data;
    (void)context;
    (void)arguments;
    (void)output;
    (void)status;
    return WSH_ERR_INTERNAL;
}

/** The frozen ABI value and version accessors are stable and populated. */
static int test_abi_identity(void)
{
    CHECK(WSH_EMBEDDING_ABI == 1u);
    /* The runtime accessor must agree with the compile-time macro. */
    CHECK(wsh_embedding_abi_version() == WSH_EMBEDDING_ABI);

    CHECK(wsh_get_version_string() != NULL);
    CHECK(wsh_get_version_string()[0] != '\0');
    CHECK(wsh_get_runtime_name() != NULL);
    CHECK(strcmp(wsh_get_runtime_name(), "wcrt") == 0);
    CHECK(wsh_get_dependency_summary() != NULL);
    CHECK(wsh_get_dependency_summary()[0] != '\0');

    /* A null stream is documented misuse and must not crash. */
    CHECK(wsh_print_version(NULL) != 0);
    return 1;
}

/** A host can drive the full context and variable lifecycle. */
static int test_context_lifecycle(void)
{
    wsh_context_options options;
    wsh_context *context = NULL;
    wsh_value *value = NULL;
    const wsh_value *borrowed = NULL;
    wsh_string_view element;
    int exported = -1;

    wsh_context_options_init(&options);
    CHECK(wsh_context_create(&options, &context) == WSH_OK);
    CHECK(context != NULL);

    CHECK(make_single_value("world", &value) == WSH_OK);
    CHECK(wsh_context_set_variable(
              context,
              wsh_string_view_from_cstr("hello"),
              value) == WSH_OK);
    CHECK(wsh_context_variable_count(context) == 1u);

    CHECK(wsh_context_get_variable(
              context,
              wsh_string_view_from_cstr("hello"),
              &borrowed) == WSH_OK);
    CHECK(wsh_value_count(borrowed) == 1u);
    CHECK(wsh_value_at(borrowed, 0u, &element) == WSH_OK);
    CHECK(element.length == 5u);
    CHECK(memcmp(element.data, "world", 5u) == 0);

    /* A plain set stays private; an import is exported. */
    CHECK(wsh_context_is_exported(
              context,
              wsh_string_view_from_cstr("hello"),
              &exported) == WSH_OK);
    CHECK(exported == 0);
    CHECK(wsh_context_set_exported(
              context,
              wsh_string_view_from_cstr("hello"),
              1) == WSH_OK);
    CHECK(wsh_context_is_exported(
              context,
              wsh_string_view_from_cstr("hello"),
              &exported) == WSH_OK);
    CHECK(exported == 1);

    wsh_value_destroy(value);
    wsh_context_destroy(context);
    return 1;
}

/** Diagnostics added by a host round-trip through the public accessors. */
static int test_diagnostics(void)
{
    wsh_context *context = NULL;
    wsh_diagnostic_view view;

    CHECK(wsh_context_create(NULL, &context) == WSH_OK);
    CHECK(wsh_context_diagnostic_count(context) == 0u);
    CHECK(wsh_context_add_diagnostic(
              context,
              WSH_DIAGNOSTIC_WARNING,
              WSH_DIAGNOSTIC_LIMIT,
              wsh_string_view_from_cstr("bounded"),
              wsh_string_view_from_cstr("host"),
              NULL) == WSH_OK);
    CHECK(wsh_context_diagnostic_count(context) == 1u);
    CHECK(wsh_context_diagnostic_at(context, 0u, &view) == WSH_OK);
    CHECK(view.severity == WSH_DIAGNOSTIC_WARNING);
    CHECK(view.code == WSH_DIAGNOSTIC_LIMIT);
    CHECK(view.message.length == 7u);
    CHECK(memcmp(view.message.data, "bounded", 7u) == 0);
    CHECK(view.has_span == 0);

    wsh_context_destroy(context);
    return 1;
}

/** Documented misuse returns a defined error instead of crashing. */
static int test_misuse_is_defined(void)
{
    wsh_context_options options;
    wsh_context *context = NULL;
    const wsh_value *borrowed = NULL;
    wsh_value *value = NULL;
    wsh_string_view element;
    uint32_t status_value = 0u;
    wsh_status_list *empty_status = NULL;
    wsh_status_builder *status_builder = NULL;

    /* Null output pointers and null owners are rejected, not dereferenced. */
    wsh_context_options_init(&options);
    CHECK(wsh_context_create(&options, NULL) == WSH_ERR_INVALID);
    CHECK(wsh_context_variable_count(NULL) == 0u);

    /* Null owners on destroy are accepted no-ops. */
    wsh_context_destroy(NULL);
    wsh_value_destroy(NULL);

    CHECK(wsh_context_create(NULL, &context) == WSH_OK);

    /* An absent variable is a defined error, not a crash. */
    CHECK(wsh_context_get_variable(
              context,
              wsh_string_view_from_cstr("absent"),
              &borrowed) == WSH_ERR_INVALID);

    /* An out-of-range element index is a defined error. */
    CHECK(make_single_value("only", &value) == WSH_OK);
    CHECK(wsh_value_at(value, 5u, &element) == WSH_ERR_INVALID);

    /* The last status of an empty list is a defined error. */
    CHECK(wsh_status_builder_create(NULL, NULL, &status_builder) == WSH_OK);
    CHECK(wsh_status_builder_finish(status_builder, &empty_status) == WSH_OK);
    CHECK(wsh_status_list_last(empty_status, &status_value) == WSH_ERR_INVALID);

    wsh_status_list_destroy(empty_status);
    wsh_value_destroy(value);
    wsh_context_destroy(context);
    return 1;
}

/** Namespaced host commands are synchronous, structured, and non-reentrant. */
static int test_host_command_registration(void)
{
    wsh_context *context = NULL;
    wsh_evaluator *evaluator = NULL;
    wsh_source *source = NULL;
    wsh_parse_tree *tree = NULL;
    wsh_status_list *status = NULL;
    host_command_state state;
    uint32_t code = 0U;
    size_t diagnostics_before;

    memset(&state, 0, sizeof(state));
    CHECK(wsh_context_create(NULL, &context) == WSH_OK);
    CHECK(wsh_evaluator_create(context, NULL, &evaluator) == WSH_OK);
    CHECK(wsh_source_create(
              NULL,
              NULL,
              (const unsigned char *)"host::probe alpha beta",
              strlen("host::probe alpha beta"),
              &source) == WSH_OK);
    CHECK(wsh_parse(NULL, source, &tree) == WSH_OK);
    CHECK(wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE);
    state.evaluator = evaluator;
    state.tree = tree;

    CHECK(wsh_evaluator_register_host_command(
              evaluator,
              wsh_string_view_from_cstr("plain"),
              host_probe,
              &state) == WSH_ERR_INVALID);
    CHECK(wsh_evaluator_register_host_command(
              evaluator,
              wsh_string_view_from_cstr("host::probe"),
              host_probe,
              &state) == WSH_OK);
    CHECK(wsh_evaluator_register_host_command(
              evaluator,
              wsh_string_view_from_cstr("host::probe"),
              host_probe,
              &state) == WSH_ERR_MISMATCH);
    CHECK(wsh_evaluate(evaluator, tree, &status) == WSH_OK);
    CHECK(state.calls == 1U);
    CHECK(state.nested_result == WSH_ERR_MISMATCH);
    CHECK(state.nested_signal_result == WSH_ERR_MISMATCH);
    CHECK(wsh_status_list_count(status) == 1U);
    CHECK(wsh_status_list_at(status, 0U, &code) == WSH_OK);
    CHECK(code == 23U);
    wsh_status_list_destroy(status);
    status = NULL;

    CHECK(wsh_evaluator_unregister_host_command(
              evaluator,
              wsh_string_view_from_cstr("host::probe")) == WSH_OK);
    CHECK(wsh_evaluator_unregister_host_command(
              evaluator,
              wsh_string_view_from_cstr("host::probe")) == WSH_ERR_INVALID);

    CHECK(wsh_evaluator_register_host_command(
              evaluator,
              wsh_string_view_from_cstr("host::probe"),
              host_failure,
              NULL) == WSH_OK);
    diagnostics_before = wsh_context_diagnostic_count(context);
    CHECK(wsh_evaluate(evaluator, tree, &status) == WSH_ERR_INTERNAL);
    CHECK(status == NULL);
    CHECK(wsh_context_diagnostic_count(context) == diagnostics_before + 1U);

    wsh_parse_tree_destroy(tree);
    wsh_source_destroy(source);
    wsh_evaluator_destroy(evaluator);
    wsh_context_destroy(context);
    return 1;
}

/** Controlled inventory for the ABI conformance executable. */
static const test_case_entry test_cases[] = {
    {"ABI-IDENTITY", test_abi_identity},
    {"CONTEXT-LIFECYCLE", test_context_lifecycle},
    {"DIAGNOSTICS", test_diagnostics},
    {"MISUSE-DEFINED", test_misuse_is_defined},
    {"HOST-COMMAND-REGISTRATION", test_host_command_registration}
};

/**
 * Execute one named case or the entire inventory.
 * @param argc Argument count.
 * @param argv Optional case identifier in argv[1].
 * @return Zero only when every selected case passes.
 */
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
