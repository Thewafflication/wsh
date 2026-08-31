/**
 * @file embedding_equivalence.c
 * @brief Public-API host for executable/static/shared conformance comparison.
 */

#include "wsh/evaluator.h"
#include "wsh/parser.h"
#include "wsh/windows_runtime.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    wsh_windows_runtime *windows = NULL;
    wsh_context_options context_options;
    wsh_context *context = NULL;
    wsh_evaluator *evaluator = NULL;
    wsh_source *source = NULL;
    wsh_parse_tree *tree = NULL;
    wsh_status_list *status = NULL;
    wsh_result result;
    uint32_t code = 1U;
    size_t index;

    if (argc != 2) {
        fprintf(stderr, "usage: embedding-equivalence <source>\n");
        return 2;
    }
    result = wsh_windows_runtime_create(NULL, &windows);
    wsh_context_options_init(&context_options);
    if (result == WSH_OK) {
        context_options.runtime = wsh_windows_runtime_interface(windows);
        result = wsh_context_create(&context_options, &context);
    }
    if (result == WSH_OK) {
        result = wsh_evaluator_create(context, NULL, &evaluator);
    }
    if (result == WSH_OK) {
        result = wsh_source_create(
            NULL,
            NULL,
            (const unsigned char *)argv[1],
            strlen(argv[1]),
            &source);
    }
    if (result == WSH_OK) {
        result = wsh_parse(NULL, source, &tree);
    }
    if (result == WSH_OK &&
        wsh_parse_tree_status(tree) != WSH_SYNTAX_COMPLETE) {
        result = WSH_ERR_INVALID;
    }
    if (result == WSH_OK) {
        result = wsh_evaluate(evaluator, tree, &status);
    }
    code = 0U;
    for (index = 0U; result == WSH_OK &&
         index < wsh_status_list_count(status); ++index) {
        uint32_t item = 0U;
        result = wsh_status_list_at(status, index, &item);
        if (code == 0U && item != 0U) {
            code = item;
        }
    }
    if (result != WSH_OK) {
        fprintf(stderr, "embedding-equivalence: API result %d\n", (int)result);
        code = 125U;
    }
    printf("status=%lu\n", (unsigned long)code);
    wsh_status_list_destroy(status);
    wsh_parse_tree_destroy(tree);
    wsh_source_destroy(source);
    wsh_evaluator_destroy(evaluator);
    wsh_context_destroy(context);
    wsh_windows_runtime_destroy(windows);
    return (int)code;
}
