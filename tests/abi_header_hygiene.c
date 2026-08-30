/**
 * @file abi_header_hygiene.c
 * @brief Compile-and-link hygiene check for the public embedding headers.
 *
 * Including only the four public ABI headers, this translation unit must
 * compile standalone under the project's strict flags and link against the
 * static embedding library. It references one entry point from each header so
 * a missing include, an internal-only type, or an unlinked symbol fails the
 * build rather than reaching a shipped host.
 */

#include "wsh/core.h"
#include "wsh/evaluator.h"
#include "wsh/parser.h"
#include "wsh/wsh.h"

int main(void)
{
    int present =
        (wsh_context_options_init != 0) &&
        (wsh_evaluator_options_init != 0) &&
        (wsh_parser_options_init != 0) &&
        (wsh_embedding_abi_version != 0);
    return present ? 0 : 1;
}
