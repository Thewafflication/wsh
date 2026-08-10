#include "wsh/wsh.h"

#include <stdio.h>
#include <string.h>

static void usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s [--help|-h|--version|-V|--print-abi]\n", program_name);
}

int main(int argc, char **argv)
{
    int i;

    if (argc <= 1) {
        usage(argv[0]);
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            return wsh_print_version(stdout);
        }

        if (strcmp(argv[i], "--print-abi") == 0) {
            printf("wsh embedding ABI %u\n", WSH_EMBEDDING_ABI);
            return 0;
        }

        usage(argv[0]);
        return 2;
    }

    return 0;
}
