#include "wsh/wsh.h"

#include <stdio.h>

#ifndef WSH_WCRT_VERSION
#define WSH_WCRT_VERSION "unavailable"
#endif

const char *wsh_get_version_string(void)
{
    return "1.0.0";
}

const char *wsh_get_runtime_name(void)
{
    return "wcrt";
}

const char *wsh_get_dependency_summary(void)
{
    return "wcrt " WSH_WCRT_VERSION " (statically linked runtime library)";
}

int wsh_print_version(FILE *stream)
{
    if (stream == NULL) {
        return 1;
    }

    fprintf(stream, "=================================================================\n");
    fprintf(stream, "Waughtal Shell (wsh) Version %s\n", wsh_get_version_string());
    fprintf(stream, "=================================================================\n");
    fprintf(stream, "Dependencies:\n");
    fprintf(stream, "  %s\n", wsh_get_dependency_summary());
    fprintf(stream, "  wsh standard library %s (statically linked)\n", wsh_get_version_string());
    fprintf(stream, "  wsh embedding ABI %u\n", WSH_EMBEDDING_ABI);
    fprintf(stream, "  kernel32 5.0.2195.1 (Windows system library)\n");
    return 0;
}
