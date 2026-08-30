#ifndef WSH_WSH_H
#define WSH_WSH_H

#include "wsh/api.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WSH_EMBEDDING_ABI 1u

/**
 * Return the embedding ABI version the library was built with.
 * @return The runtime counterpart of the WSH_EMBEDDING_ABI macro, so a host
 *         can negotiate compatibility without recompiling against the header.
 */
WSH_API unsigned wsh_embedding_abi_version(void);
WSH_API const char *wsh_get_version_string(void);
WSH_API const char *wsh_get_runtime_name(void);
WSH_API const char *wsh_get_dependency_summary(void);
WSH_API int wsh_print_version(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif
