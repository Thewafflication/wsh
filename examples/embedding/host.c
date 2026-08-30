/**
 * @file host.c
 * @brief Minimal C host embedding the WSH portable core through ABI 1.
 *
 * This example links the static embedding library and includes only the
 * installed public headers. It shows the lifecycle an external host follows:
 * create an isolated context, publish a list-valued variable, read it back,
 * record a diagnostic, and release everything. It deliberately touches no
 * internal type and mutates no process-global state.
 */

#include "wsh/core.h"
#include "wsh/wsh.h"

#include <stdio.h>

/** Publish one immutable list value built from an array of C strings. */
static wsh_result publish_list(
    wsh_context *context,
    const char *name,
    const char *const *items,
    size_t count)
{
    wsh_value_builder *builder;
    wsh_value *value;
    size_t index;
    wsh_result result;

    result = wsh_value_builder_create(NULL, NULL, &builder);
    if (result != WSH_OK) {
        return result;
    }
    for (index = 0U; index < count; ++index) {
        result = wsh_value_builder_append(
            builder,
            wsh_string_view_from_cstr(items[index]));
        if (result != WSH_OK) {
            wsh_value_builder_destroy(builder);
            return result;
        }
    }
    result = wsh_value_builder_finish(builder, &value);
    if (result != WSH_OK) {
        return result;
    }
    result = wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr(name),
        value);
    wsh_value_destroy(value);
    return result;
}

/** Run the host demonstration and return zero only on success. */
int main(void)
{
    static const char *const greeting[] = {"hello", "embedding"};

    wsh_context *context = NULL;
    const wsh_value *borrowed = NULL;
    wsh_string_view element;
    size_t index;

    printf("host: wsh version %s, embedding ABI %u\n",
        wsh_get_version_string(),
        WSH_EMBEDDING_ABI);

    if (wsh_context_create(NULL, &context) != WSH_OK) {
        fprintf(stderr, "host: context creation failed\n");
        return 1;
    }

    if (publish_list(context, "greeting", greeting, 2U) != WSH_OK) {
        fprintf(stderr, "host: variable publish failed\n");
        wsh_context_destroy(context);
        return 1;
    }

    if (wsh_context_get_variable(
            context,
            wsh_string_view_from_cstr("greeting"),
            &borrowed) != WSH_OK) {
        fprintf(stderr, "host: variable lookup failed\n");
        wsh_context_destroy(context);
        return 1;
    }

    printf("host: greeting has %zu elements\n", wsh_value_count(borrowed));
    for (index = 0U; index < wsh_value_count(borrowed); ++index) {
        if (wsh_value_at(borrowed, index, &element) != WSH_OK) {
            fprintf(stderr, "host: element read failed\n");
            wsh_context_destroy(context);
            return 1;
        }
        printf("host: element %zu = %.*s\n",
            index,
            (int)element.length,
            element.data);
    }

    (void)wsh_context_add_diagnostic(
        context,
        WSH_DIAGNOSTIC_NOTE,
        WSH_DIAGNOSTIC_EVALUATION,
        wsh_string_view_from_cstr("host completed"),
        wsh_string_view_from_cstr("example"),
        NULL);
    printf("host: diagnostics recorded = %zu\n",
        wsh_context_diagnostic_count(context));

    wsh_context_destroy(context);
    printf("host: ok\n");
    return 0;
}
