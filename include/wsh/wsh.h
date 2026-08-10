#ifndef WSH_WSH_H
#define WSH_WSH_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WSH_EMBEDDING_ABI 1u

const char *wsh_get_version_string(void);
const char *wsh_get_runtime_name(void);
const char *wsh_get_dependency_summary(void);
int wsh_print_version(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif
