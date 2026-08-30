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

/** Controlled inventory for the ABI conformance executable. */
static const test_case_entry test_cases[] = {
    {"ABI-IDENTITY", test_abi_identity},
    {"CONTEXT-LIFECYCLE", test_context_lifecycle},
    {"DIAGNOSTICS", test_diagnostics},
    {"MISUSE-DEFINED", test_misuse_is_defined}
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
